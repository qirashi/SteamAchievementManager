/* Copyright (c) 2024 Rick (rick 'at' gibbed 'dot' us)
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would
 *    be appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and must not
 *    be misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source
 *    distribution.
 *
 * Altered source version: Qt 6 port of Steam Achievement Manager.
 */

#include "ManagerWindow.h"

#include "AchievementDelegate.h"
#include "AchievementModel.h"
#include "Application.h"
#include "DiscordPresence.h"
#include "Icons.h"
#include "ImageCache.h"
#include "Languages.h"
#include "StatsModel.h"
#include "TitleBar.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableView>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cstring>
#include <iterator>

namespace sam
{
namespace
{
enum BulkMode
{
    LockMode,
    UnlockMode,
    RevertMode, // back to the state reported by Steam
};

constexpr QSize kHeaderImageSize(184, 69);

QString translateError(int result)
{
    if (result == 2)
    {
        return QCoreApplication::translate("sam::ManagerWindow",
                                           "generic error — this usually means you don't own the game");
    }
    return QString::number(result);
}

QPixmap roundedPixmap(const QImage &image, const QSize &size, qreal dpr)
{
    QPixmap pixmap(size * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(QPointF(0, 0), QSizeF(size)), 6, 6);
    painter.setClipPath(clip);
    const QSizeF scaled = QSizeF(image.size()).scaled(QSizeF(size), Qt::KeepAspectRatioByExpanding);
    painter.drawImage(QRectF((size.width() - scaled.width()) / 2,
                             (size.height() - scaled.height()) / 2,
                             scaled.width(),
                             scaled.height()),
                      image);
    return pixmap;
}

// Puts a list into a rounded block (see Style.cpp); the list itself is
// transparent so the corners of the block stay visible.
QWidget *createListBlock(QAbstractItemView *view, QWidget *parent)
{
    auto *block = new QFrame(parent);
    block->setObjectName(QStringLiteral("listBlock"));
    auto *layout = new QVBoxLayout(block);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    view->setFrameShape(QFrame::NoFrame);
    layout->addWidget(view);
    return block;
}
} // namespace

ManagerWindow::ManagerWindow(uint32_t appId, steam::Client &client, QWidget *parent)
    : QMainWindow(parent)
    , m_appId(appId)
    , m_client(client)
    , m_images(new ImageCache(this))
    , m_achievements(new AchievementModel(appId, m_images, this))
    , m_achievementFilter(new AchievementFilterModel(this))
    , m_stats(new StatsModel(this))
    , m_discord(new DiscordPresence(appId, this))
{
    m_gameName = m_client.appData(m_appId, "name");
    setWindowTitle(m_gameName.isEmpty() ? QStringLiteral("App %1").arg(m_appId) : m_gameName);
    installTitleBar(this);
    m_steamLanguage = m_client.currentGameLanguage();
    if (m_steamLanguage.isEmpty())
    {
        m_steamLanguage = QStringLiteral("english");
    }
    m_achievementFilter->setSourceModel(m_achievements);
    createActions();

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(10, 8, 10, 4);
    layout->setSpacing(8);
    layout->addWidget(createHeader());

    m_tabs = new QTabWidget(central);
    m_tabs->setDocumentMode(true);
    m_tabs->addTab(createAchievementsPage(), icons::get(QStringLiteral("trophy")), tr("Achievements"));
    m_tabs->addTab(createStatisticsPage(), icons::get(QStringLiteral("stats")), tr("Statistics"));
    layout->addWidget(m_tabs, 1);
    setCentralWidget(central);

    createStatusBar();
    restoreSettings();

    connect(m_achievements, &AchievementModel::changesUpdated, this, &ManagerWindow::updateSummary);
    connect(m_achievements, &AchievementModel::protectedChangeRejected, this, [this](const QString &name) {
        showStatusMessage(tr("“%1” is protected and cannot be changed with Steam Achievement Manager.").arg(name),
                          true);
        QApplication::beep();
    });
    connect(m_stats, &StatsModel::changesUpdated, this, &ManagerWindow::updateSummary);
    connect(m_stats, &StatsModel::editRejected, this, [this](const QString &message) {
        showStatusMessage(message, true);
        QApplication::beep();
    });
    connect(m_images, &ImageCache::pendingCountChanged, this, &ManagerWindow::updateDownloadStatus);
    connect(m_images, &ImageCache::imageReady, this, &ManagerWindow::updateHeaderImage);

    m_client.registerCallback(steam::kUserStatsReceivedCallback, [this](const void *payload, int size) {
        steam::UserStatsReceived message{};
        std::memcpy(&message, payload, qMin<size_t>(sizeof(message), static_cast<size_t>(qMax(size, 0))));
        onUserStatsReceived(message);
    });
    connect(&m_callbackTimer, &QTimer::timeout, this, [this] {
        m_client.runCallbacks();
    });
    m_callbackTimer.start(100);

    updateHeaderImage();
    updateSummary();
    applyGroups(); // downloads in parallel with the stats request
    connect(m_discord, &DiscordPresence::statusChanged, this, [this](const QString &message, bool warning) {
        // Success stays visible next to the other permanent status bar items.
        m_discordLabel->setVisible(!warning);
        m_discordLabel->setToolTip(message);
        if (warning)
        {
            showStatusMessage(message, true);
        }
    });
    m_discord->setTitle(m_gameName);
    m_discord->setEnabled(isDiscordPresenceEnabled());

    // The settings menu lives in the game list; follow what's changed there.
    m_settingsWatcher.addPath(settings().fileName());
    connect(&m_settingsWatcher, &QFileSystemWatcher::fileChanged, this, &ManagerWindow::onSettingsChanged);

    QTimer::singleShot(0, this, &ManagerWindow::refreshStats);
}

void ManagerWindow::onSettingsChanged(const QString &path)
{
    // QSettings replaces the file when it saves, which drops it from the watcher.
    if (!m_settingsWatcher.files().contains(path))
    {
        m_settingsWatcher.addPath(path);
    }
    // The game list saves other things too (its zoom, its geometry), so only
    // what actually changed is applied: regrouping rebuilds the list.
    settings().sync();
    applySavedTheme();
    applyLanguage();
    if (isGroupingEnabled() != m_groupingApplied)
    {
        applyGroups();
    }
    const bool discord = isDiscordPresenceEnabled();
    m_discord->setEnabled(discord);
    if (!discord)
    {
        m_discordLabel->hide();
    }
}

// ---------------------------------------------------------------------------
// UI construction
// ---------------------------------------------------------------------------

QWidget *ManagerWindow::createHeader()
{
    auto *header = new QFrame(this);
    header->setObjectName(QStringLiteral("gameHeader")); // a rounded block, see Style.cpp
    auto *layout = new QHBoxLayout(header);
    layout->setContentsMargins(10, 10, 14, 10);
    layout->setSpacing(14);

    m_headerImage = new QLabel(header);
    m_headerImage->setFixedSize(kHeaderImageSize);
    layout->addWidget(m_headerImage);

    auto *info = new QVBoxLayout;
    info->setSpacing(4);
    m_titleLabel = new QLabel(header);
    QFont titleFont = m_titleLabel->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.45);
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);
    m_titleLabel->setTextFormat(Qt::PlainText); // a name from Steam, never markup
    m_titleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_titleLabel->setText(m_gameName.isEmpty() ? QStringLiteral("App %1").arg(m_appId) : m_gameName);
    info->addWidget(m_titleLabel);

    auto *idLabel = new QLabel(QStringLiteral("App ID %1").arg(m_appId), header);
    idLabel->setForegroundRole(QPalette::PlaceholderText);
    idLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    info->addWidget(idLabel);

    m_summaryLabel = new QLabel(header);
    info->addWidget(m_summaryLabel);

    m_summaryProgress = new QProgressBar(header);
    m_summaryProgress->setTextVisible(false);
    m_summaryProgress->setFixedHeight(8);
    m_summaryProgress->setFixedWidth(420);
    info->addWidget(m_summaryProgress, 0, Qt::AlignLeft);
    info->addStretch();
    layout->addLayout(info, 1);

    auto *pageButton = new QToolButton(header);
    pageButton->setIcon(icons::get(QStringLiteral("external")));
    pageButton->setToolTip(tr("Open the achievements page in the browser"));
    pageButton->setAutoRaise(true);
    connect(pageButton, &QToolButton::clicked, this, [this] {
        QDesktopServices::openUrl(
            QUrl(QStringLiteral("https://steamcommunity.com/stats/%1/achievements").arg(m_appId)));
    });
    layout->addWidget(pageButton, 0, Qt::AlignTop | Qt::AlignRight);

    return header;
}

QWidget *ManagerWindow::createAchievementsPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setSpacing(6);

    auto *controls = new QHBoxLayout;
    controls->setSpacing(6);

    m_achievementSearch = new QLineEdit(page);
    m_achievementSearch->setPlaceholderText(tr("Search achievements…"));
    m_achievementSearch->setClearButtonEnabled(true);
    m_achievementSearch->addAction(icons::get(QStringLiteral("search")), QLineEdit::LeadingPosition);
    m_achievementSearch->setMinimumWidth(160);
    m_achievementSearch->setToolTip(tr("Matches name, description or ID (Ctrl+F)"));
    connect(m_achievementSearch, &QLineEdit::textChanged, m_achievementFilter, &AchievementFilterModel::setSearchText);
    controls->addWidget(m_achievementSearch);
    controls->addWidget(createActionButton(m_storeAction, page));
    controls->addWidget(createActionButton(m_refreshAction, page));

    // Segmented state filter.
    m_stateGroup = new QButtonGroup(page);
    m_stateGroup->setExclusive(true);
    const struct
    {
        AchievementFilterModel::State state;
        QString label;
        QString tip;
    } states[] = {
        {AchievementFilterModel::State::All, tr("All"), tr("Show all achievements")},
        {AchievementFilterModel::State::Locked, tr("Locked"), tr("Show only achievements you haven't unlocked yet")},
        {AchievementFilterModel::State::Unlocked, tr("Unlocked"), tr("Show only achievements you have unlocked")},
        {AchievementFilterModel::State::Modified, tr("Changed"), tr("Show only achievements with unsaved changes")},
    };
    auto *segmentWidget = new QWidget(page);
    segmentWidget->setStyleSheet(QStringLiteral(
        "QToolButton { border: 1px solid palette(mid); background: palette(button); padding: 3px 14px; }"
        "QToolButton:hover { border-color: palette(highlight); }"
        "QToolButton:checked { background: palette(highlight); color: palette(highlighted-text);"
        "                      border-color: palette(highlight); }"
        "QToolButton[segment=\"first\"] { border-top-left-radius: 4px; border-bottom-left-radius: 4px; }"
        "QToolButton[segment=\"last\"] { border-top-right-radius: 4px; border-bottom-right-radius: 4px; }"
        "QToolButton[segment=\"middle\"], QToolButton[segment=\"last\"] { margin-left: -1px; }"));
    auto *segment = new QHBoxLayout(segmentWidget);
    segment->setContentsMargins(0, 0, 0, 0);
    segment->setSpacing(0);
    const int stateCount = static_cast<int>(std::size(states));
    for (const auto &entry : states)
    {
        auto *button = new QToolButton(segmentWidget);
        const int position = static_cast<int>(&entry - states);
        button->setProperty("segment",
                            position == 0                ? QStringLiteral("first")
                            : position == stateCount - 1 ? QStringLiteral("last")
                                                         : QStringLiteral("middle"));
        button->setText(entry.label);
        button->setToolTip(entry.tip);
        button->setCheckable(true);
        button->setMinimumWidth(72);
        button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        m_stateGroup->addButton(button, static_cast<int>(entry.state));
        segment->addWidget(button);
    }
    m_stateGroup->button(0)->setChecked(true);
    connect(m_stateGroup, &QButtonGroup::idClicked, this, [this](int id) {
        m_achievementFilter->setState(static_cast<AchievementFilterModel::State>(id));
    });
    controls->addWidget(segmentWidget);
    controls->addStretch();

    const struct
    {
        const char *icon;
        QString label;
        QString tip;
        int mode;
    } bulk[] = {
        {"lock", tr("Lock all"), tr("Lock all shown achievements"), LockMode},
        {"unlock", tr("Unlock all"), tr("Unlock all shown achievements"), UnlockMode},
    };
    for (const auto &entry : bulk)
    {
        auto *button = new QToolButton(page);
        button->setIcon(icons::get(QString::fromLatin1(entry.icon)));
        button->setText(entry.label);
        button->setToolTip(entry.tip);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setAutoRaise(true);
        button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        const int mode = entry.mode;
        connect(button, &QToolButton::clicked, this, [this, mode] {
            applyToAchievements(visibleAchievementRows(), mode);
        });
        controls->addWidget(button);
    }

    // Kept apart from the save button so it isn't hit by accident.
    auto *separator = new QFrame(page);
    separator->setFrameShape(QFrame::VLine);
    separator->setFrameShadow(QFrame::Sunken);
    controls->addSpacing(6);
    controls->addWidget(separator);
    controls->addSpacing(6);
    m_resetAction = new QAction(icons::get(QStringLiteral("reset")), tr("Reset…"), this);
    m_resetAction->setToolTip(tr("Reset statistics and optionally achievements for this game"));
    connect(m_resetAction, &QAction::triggered, this, &ManagerWindow::resetAllStats);
    auto *resetButton = new QToolButton(page);
    resetButton->setDefaultAction(m_resetAction);
    resetButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    resetButton->setAutoRaise(true);
    resetButton->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    controls->addWidget(resetButton);
    layout->addLayout(controls);

    m_achievementView = new QTreeView(page);
    m_achievementView->setModel(m_achievementFilter);
    m_achievementView->setItemDelegate(new AchievementDelegate(m_achievementView));
    // Sections draw their own chevron; achievements aren't indented under them.
    m_achievementView->setRootIsDecorated(false);
    m_achievementView->setIndentation(0);
    m_achievementView->setExpandsOnDoubleClick(false);
    m_achievementView->setAllColumnsShowFocus(true);
    m_achievementView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_achievementView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_achievementView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_achievementView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_achievementView->setMouseTracking(true);
    m_achievementView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_achievementView->header()->setStretchLastSection(false);
    m_achievementView->header()->setSectionResizeMode(AchievementModel::NameColumn, QHeaderView::Stretch);
    m_achievementView->header()->setSectionResizeMode(AchievementModel::UnlockTimeColumn,
                                                      QHeaderView::ResizeToContents);
    m_achievementView->installEventFilter(this);
    connect(
        m_achievementView, &QTreeView::customContextMenuRequested, this, &ManagerWindow::showAchievementContextMenu);
    layout->addWidget(createListBlock(m_achievementView, page), 1);

    // Sections toggle on a single click and stay collapsed across filtering and reloads.
    connect(m_achievementView, &QTreeView::clicked, this, [this](const QModelIndex &index) {
        if (index.data(AchievementModel::SectionRole).toBool())
        {
            const QModelIndex section = index.siblingAtColumn(0);
            m_achievementView->setExpanded(section, !m_achievementView->isExpanded(section));
        }
    });
    connect(m_achievementView, &QTreeView::expanded, this, [this](const QModelIndex &index) {
        m_collapsedSections.remove(index.data().toString());
    });
    connect(m_achievementView, &QTreeView::collapsed, this, [this](const QModelIndex &index) {
        m_collapsedSections.insert(index.data().toString());
    });
    connect(m_achievementFilter, &QAbstractItemModel::modelReset, this, [this] {
        restoreSectionExpansion();
        m_achievementView->scrollToTop();
    });
    connect(m_achievementFilter, &QAbstractItemModel::layoutChanged, this, &ManagerWindow::restoreSectionExpansion);
    connect(m_achievementFilter, &QAbstractItemModel::rowsInserted, this, [this](const QModelIndex &parent) {
        if (!parent.isValid())
        {
            restoreSectionExpansion();
        }
    });

    auto *find = new QShortcut(QKeySequence::Find, page);
    connect(find, &QShortcut::activated, this, [this] {
        m_tabs->setCurrentIndex(0);
        m_achievementSearch->setFocus();
        m_achievementSearch->selectAll();
    });

    return page;
}

QWidget *ManagerWindow::createStatisticsPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setSpacing(6);

    auto *warning = new QFrame(page);
    warning->setFrameShape(QFrame::StyledPanel);
    auto *warningLayout = new QHBoxLayout(warning);
    warningLayout->setContentsMargins(10, 6, 10, 6);
    auto *warningIcon = new QLabel(warning);
    warningIcon->setPixmap(icons::get(QStringLiteral("warning"), QColor(0xd2, 0x8a, 0x22)).pixmap(20, 20));
    warningLayout->addWidget(warningIcon);
    m_editStatsCheck = new QCheckBox(
        tr("I understand by modifying the values of stats, I may screw things up and can't blame anyone but myself."),
        warning);
    warningLayout->addWidget(m_editStatsCheck, 1);
    // Saving and reloading cover the statistics too.
    warningLayout->addWidget(createActionButton(m_storeAction, warning));
    warningLayout->addWidget(createActionButton(m_refreshAction, warning));
    layout->addWidget(warning);

    m_statsView = new QTableView(page);
    m_statsView->setModel(m_stats);
    m_statsView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_statsView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_statsView->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed |
                                 QAbstractItemView::AnyKeyPressed);
    m_statsView->setAlternatingRowColors(true);
    m_statsView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_statsView->setShowGrid(false);
    m_statsView->setWordWrap(false);
    m_statsView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_statsView->verticalHeader()->hide();
    m_statsView->verticalHeader()->setDefaultSectionSize(m_statsView->fontMetrics().height() + 12);
    auto *header = m_statsView->horizontalHeader();
    header->setSectionResizeMode(StatsModel::NameColumn, QHeaderView::Stretch);
    header->setSectionResizeMode(StatsModel::ValueColumn, QHeaderView::Interactive);
    header->setSectionResizeMode(StatsModel::FlagsColumn, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(StatsModel::IdColumn, QHeaderView::ResizeToContents);
    header->resizeSection(StatsModel::ValueColumn, 140);
    header->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    connect(m_statsView, &QTableView::customContextMenuRequested, this, &ManagerWindow::showStatContextMenu);
    layout->addWidget(createListBlock(m_statsView, page), 1);

    connect(m_editStatsCheck, &QCheckBox::toggled, m_stats, &StatsModel::setEditingEnabled);
    return page;
}

void ManagerWindow::createActions()
{
    // Added to the window so the shortcuts work whatever tab is shown; the
    // buttons sit next to the search field and on the statistics page.
    m_storeAction = new QAction(icons::get(QStringLiteral("save")), tr("Save changes"), this);
    addAction(m_storeAction);
    m_storeAction->setShortcut(QKeySequence::Save);
    m_storeAction->setToolTip(tr("Store achievements and statistics for this game (Ctrl+S)"));
    connect(m_storeAction, &QAction::triggered, this, [this] {
        store();
    });

    m_refreshAction = new QAction(icons::get(QStringLiteral("refresh")), tr("Refresh"), this);
    addAction(m_refreshAction);
    m_refreshAction->setShortcut(QKeySequence::Refresh);
    m_refreshAction->setToolTip(tr("Reload achievements and statistics from Steam (F5)"));
    connect(m_refreshAction, &QAction::triggered, this, [this] {
        if (confirmDiscardChanges(tr("Reload and discard your unsaved changes?")))
        {
            refreshStats();
        }
    });
}

QToolButton *ManagerWindow::createActionButton(QAction *action, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setDefaultAction(action);
    button->setAutoRaise(true);
    button->setIconSize(QSize(18, 18));
    return button;
}

void ManagerWindow::createStatusBar()
{
    auto *bar = statusBar();
    m_statusLabel = new QLabel(bar);
    m_statusLabel->setTextFormat(Qt::PlainText);
    bar->addWidget(m_statusLabel, 1);

    m_busyIndicator = new QProgressBar(bar);
    m_busyIndicator->setRange(0, 0);
    m_busyIndicator->setMaximumWidth(120);
    m_busyIndicator->setMaximumHeight(14);
    m_busyIndicator->setTextVisible(false);
    m_busyIndicator->hide();
    bar->addPermanentWidget(m_busyIndicator);

    m_downloadLabel = new QLabel(bar);
    m_downloadLabel->hide();
    bar->addPermanentWidget(m_downloadLabel);

    m_discordLabel = new QLabel(tr("Discord: playing"), bar);
    m_discordLabel->hide();
    bar->addPermanentWidget(m_discordLabel);
}

void ManagerWindow::restoreSettings()
{
    auto &store = settings();
    if (!restoreGeometry(store.value(QStringLiteral("game/geometry")).toByteArray()))
    {
        resize(900, 680);
    }
    m_tabs->setCurrentIndex(store.value(QStringLiteral("game/tab"), 0).toInt());
}

void ManagerWindow::closeEvent(QCloseEvent *event)
{
    if (hasChanges())
    {
        QMessageBox box(QMessageBox::Warning,
                        tr("Unsaved changes"),
                        tr("You have unsaved changes. Save them to Steam before closing?"),
                        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                        this);
        box.setDefaultButton(QMessageBox::Save);
        const int answer = box.exec();
        if (answer == QMessageBox::Cancel || (answer == QMessageBox::Save && !store()))
        {
            event->ignore();
            return;
        }
    }
    settings().setValue(QStringLiteral("game/geometry"), saveGeometry());
    settings().setValue(QStringLiteral("game/tab"), m_tabs->currentIndex());
    QMainWindow::closeEvent(event);
}

bool ManagerWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_achievementView && event->type() == QEvent::KeyPress)
    {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Space && key->modifiers() == Qt::NoModifier)
        {
            const QList<int> rows = selectedAchievementRows();
            if (!rows.isEmpty())
            {
                const bool allChecked = std::all_of(rows.cbegin(), rows.cend(), [this](int row) {
                    return m_achievements->achievements().at(row).checked;
                });
                applyToAchievements(rows, allChecked ? LockMode : UnlockMode);
            }
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

void ManagerWindow::refreshStats()
{
    if (m_busy)
    {
        return;
    }

    m_achievements->setAchievements({});
    m_stats->setStats({});

    const uint64_t steamId = m_client.steamId();
    // This still triggers the UserStatsReceived callback, in addition to the
    // call result, so there is no need to implement call results.
    if (m_client.requestUserStats(steamId) == 0)
    {
        showError(this, tr("Failed to request user stats from Steam."));
        return;
    }

    setBusy(true, tr("Retrieving stat information…"));
}

void ManagerWindow::onUserStatsReceived(const steam::UserStatsReceived &message)
{
    if (!m_busy || (message.gameId & 0xFFFFFFu) != m_appId)
    {
        return;
    }

    if (message.result != steam::kResultOK)
    {
        setBusy(false);
        showStatusMessage(tr("Error while retrieving stats: %1").arg(translateError(message.result)), true);
        return;
    }

    m_schema = Schema::load(m_appId);
    if (!m_schema)
    {
        setBusy(false);
        showStatusMessage(tr("Failed to load schema."), true);
        return;
    }

    if (m_gameName.isEmpty() && !m_schema->gameName.isEmpty())
    {
        m_gameName = m_schema->gameName;
        m_titleLabel->setText(m_gameName);
    }
    setWindowTitle(m_gameName.isEmpty() ? QStringLiteral("App %1").arg(m_appId) : m_gameName);

    applyLanguage();
    loadAchievements();
    loadStatistics();
    setBusy(false);

    showStatusMessage(tr("Retrieved %1 and %2.")
                          .arg(tr("%n achievement(s)", nullptr, m_achievements->achievementCount()),
                               tr("%n statistic(s)", nullptr, m_stats->rowCount())));
}

void ManagerWindow::loadAchievements()
{
    QList<Achievement> achievements;
    for (const auto &definition : std::as_const(m_schema->achievements))
    {
        if (definition.id.isEmpty())
        {
            continue;
        }
        bool achieved = false;
        uint32_t unlockTime = 0;
        if (!m_client.getAchievementAndUnlockTime(definition.id, &achieved, &unlockTime))
        {
            continue;
        }
        Achievement achievement;
        achievement.definition = definition;
        achievement.achieved = achieved;
        achievement.checked = achieved;
        if (achieved && unlockTime > 0)
        {
            achievement.unlockTime = QDateTime::fromSecsSinceEpoch(unlockTime);
        }
        achievements.append(std::move(achievement));
    }
    m_achievements->setAchievements(std::move(achievements));
}

void ManagerWindow::loadStatistics()
{
    QList<Stat> stats;
    for (const auto &definition : std::as_const(m_schema->stats))
    {
        if (definition.id.isEmpty())
        {
            continue;
        }
        Stat stat;
        stat.definition = definition;
        if (stat.isInteger())
        {
            if (!m_client.getStat(definition.id, &stat.intValue))
            {
                continue;
            }
            stat.intOriginal = stat.intValue;
        }
        else
        {
            if (!m_client.getStat(definition.id, &stat.floatValue))
            {
                continue;
            }
            stat.floatOriginal = stat.floatValue;
        }
        stats.append(std::move(stat));
    }
    m_stats->setStats(std::move(stats));
}

QString ManagerWindow::effectiveLanguage() const
{
    // The preference only applies when the game actually has that language.
    const QString preferred = achievementLanguage();
    if (!preferred.isEmpty() && m_schema && m_schema->languages.contains(preferred, Qt::CaseInsensitive))
    {
        return preferred;
    }
    return m_steamLanguage;
}

void ManagerWindow::applyLanguage()
{
    const QString language = effectiveLanguage();
    m_achievements->setLanguage(language);
    m_stats->setLanguage(language);
}

bool ManagerWindow::isGroupingEnabled() const
{
    return settings().value(QStringLiteral("game/groupAchievements"), true).toBool();
}

bool ManagerWindow::isDiscordPresenceEnabled() const
{
    return settings().value(QStringLiteral("game/discordPresence"), false).toBool();
}

void ManagerWindow::applyGroups()
{
    m_groupingApplied = isGroupingEnabled();
    if (!m_groupingApplied)
    {
        m_achievements->setGroups({});
        return;
    }
    if (m_groups)
    {
        m_achievements->setGroups(*m_groups);
        return;
    }
    if (m_groupsRequested)
    {
        return;
    }
    // Steam doesn't say which DLC an achievement belongs to; SteamHunters does.
    m_groupsRequested = true;
    requestAchievementGroups(m_appId, m_gameName, this, [this](QList<AchievementGroup> groups) {
        m_groups = std::move(groups);
        if (isGroupingEnabled())
        {
            m_achievements->setGroups(*m_groups);
        }
    });
}

void ManagerWindow::restoreSectionExpansion()
{
    for (int row = 0; row < m_achievementFilter->rowCount(); ++row)
    {
        const QModelIndex index = m_achievementFilter->index(row, 0);
        if (index.data(AchievementModel::SectionRole).toBool())
        {
            m_achievementView->setExpanded(index, !m_collapsedSections.contains(index.data().toString()));
        }
    }
}

// ---------------------------------------------------------------------------
// Storing
// ---------------------------------------------------------------------------

bool ManagerWindow::hasChanges() const
{
    return m_achievements->modifiedCount() > 0 || m_stats->modifiedCount() > 0;
}

bool ManagerWindow::confirmDiscardChanges(const QString &question)
{
    if (!hasChanges())
    {
        return true;
    }
    return QMessageBox::question(
               this, tr("Unsaved changes"), question, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) ==
           QMessageBox::Yes;
}

bool ManagerWindow::store()
{
    if (m_busy)
    {
        return false;
    }

    int toUnlock = 0;
    int toLock = 0;
    for (const auto &achievement : m_achievements->achievements())
    {
        if (achievement.isModified())
        {
            (achievement.checked ? toUnlock : toLock) += 1;
        }
    }
    const int statChanges = m_stats->modifiedCount();
    if (toUnlock + toLock + statChanges == 0)
    {
        showStatusMessage(tr("There are no changes to save."));
        return true;
    }

    QStringList lines;
    if (toUnlock > 0)
    {
        lines << tr("• unlock %n achievement(s)", nullptr, toUnlock);
    }
    if (toLock > 0)
    {
        lines << tr("• lock %n achievement(s)", nullptr, toLock);
    }
    if (statChanges > 0)
    {
        lines << tr("• change %n statistic(s)", nullptr, statChanges);
    }
    QMessageBox confirm(QMessageBox::Question,
                        tr("Save changes"),
                        tr("The following changes will be stored on Steam:"),
                        QMessageBox::Save | QMessageBox::Cancel,
                        this);
    confirm.setInformativeText(lines.join(u'\n'));
    confirm.setDefaultButton(QMessageBox::Save);
    if (confirm.exec() != QMessageBox::Save)
    {
        return false;
    }

    int achievements = 0;
    int stats = 0;
    if (!storeAchievements(&achievements) || !storeStatistics(&stats))
    {
        refreshStats();
        return false;
    }

    if (!m_client.storeStats())
    {
        showError(this, tr("An error occurred while storing, aborting."));
        refreshStats();
        return false;
    }

    const QString summary =
        tr("Stored %1 and %2.")
            .arg(tr("%n achievement(s)", nullptr, achievements), tr("%n statistic(s)", nullptr, stats));
    QMessageBox::information(this, tr("Changes saved"), summary);
    refreshStats();
    showStatusMessage(summary);
    return true;
}

bool ManagerWindow::storeAchievements(int *count)
{
    *count = 0;
    for (const auto &achievement : m_achievements->achievements())
    {
        if (!achievement.isModified())
        {
            continue;
        }
        if (!m_client.setAchievement(achievement.definition.id, achievement.checked))
        {
            showError(
                this,
                tr("An error occurred while setting the state for %1, aborting store.").arg(achievement.definition.id));
            return false;
        }
        ++*count;
    }
    return true;
}

bool ManagerWindow::storeStatistics(int *count)
{
    *count = 0;
    for (const auto &stat : m_stats->stats())
    {
        if (!stat.isModified())
        {
            continue;
        }
        const bool ok = stat.isInteger() ? m_client.setStat(stat.definition.id, stat.intValue)
                                         : m_client.setStat(stat.definition.id, stat.floatValue);
        if (!ok)
        {
            showError(this,
                      tr("An error occurred while setting the value for %1, aborting store.").arg(stat.definition.id));
            return false;
        }
        ++*count;
    }
    return true;
}

void ManagerWindow::resetAllStats()
{
    if (m_busy)
    {
        return;
    }

    QMessageBox first(QMessageBox::Warning,
                      tr("Reset statistics"),
                      tr("Are you absolutely sure you want to reset stats?"),
                      QMessageBox::Yes | QMessageBox::No,
                      this);
    first.setInformativeText(tr("This resets every statistic of this game on Steam. It cannot be undone."));
    first.setDefaultButton(QMessageBox::No);
    auto *achievementsToo = new QCheckBox(tr("Reset achievements too"), &first);
    first.setCheckBox(achievementsToo);
    if (first.exec() != QMessageBox::Yes)
    {
        return;
    }

    const QString what = achievementsToo->isChecked() ? tr("all statistics and achievements") : tr("all statistics");
    if (QMessageBox::warning(this,
                             tr("Reset statistics"),
                             tr("Really really sure? This will reset %1 for %2.").arg(what, m_gameName),
                             QMessageBox::Yes | QMessageBox::No,
                             QMessageBox::No) != QMessageBox::Yes)
    {
        return;
    }

    if (!m_client.resetAllStats(achievementsToo->isChecked()))
    {
        showError(this, tr("Failed to reset statistics."));
        return;
    }
    refreshStats();
}

// ---------------------------------------------------------------------------
// Achievement list actions
// ---------------------------------------------------------------------------

QList<int> ManagerWindow::visibleAchievementRows() const
{
    // Achievements in collapsed sections aren't shown, so they're left alone.
    QList<int> rows;
    for (int row = 0; row < m_achievementFilter->rowCount(); ++row)
    {
        const QModelIndex index = m_achievementFilter->index(row, 0);
        if (!index.data(AchievementModel::SectionRole).toBool())
        {
            rows.append(m_achievements->achievementIndex(m_achievementFilter->mapToSource(index)));
        }
        else if (m_achievementView->isExpanded(index))
        {
            rows.append(visibleSectionRows(index));
        }
    }
    return rows;
}

QList<int> ManagerWindow::visibleSectionRows(const QModelIndex &section) const
{
    QList<int> rows;
    for (int row = 0; row < m_achievementFilter->rowCount(section); ++row)
    {
        rows.append(m_achievements->achievementIndex(
            m_achievementFilter->mapToSource(m_achievementFilter->index(row, 0, section))));
    }
    return rows;
}

QList<int> ManagerWindow::selectedAchievementRows() const
{
    QList<int> rows;
    const auto selected = m_achievementView->selectionModel()->selectedRows();
    for (const auto &index : selected)
    {
        const int row = m_achievements->achievementIndex(m_achievementFilter->mapToSource(index));
        if (row >= 0)
        {
            rows.append(row);
        }
    }
    std::sort(rows.begin(), rows.end());
    return rows;
}

void ManagerWindow::applyToAchievements(const QList<int> &rows, int mode)
{
    if (m_busy || rows.isEmpty())
    {
        return;
    }
    const int skipped =
        mode == RevertMode ? m_achievements->revert(rows) : m_achievements->setChecked(rows, mode == UnlockMode);
    if (skipped > 0)
    {
        showStatusMessage(tr("%n protected achievement(s) skipped — they can't be changed.", nullptr, skipped), true);
    }
}

void ManagerWindow::showAchievementContextMenu(const QPoint &position)
{
    const QModelIndex index = m_achievementView->indexAt(position);
    if (!index.isValid())
    {
        return;
    }
    if (index.data(AchievementModel::SectionRole).toBool())
    {
        showSectionContextMenu(index.siblingAtColumn(0), position);
        return;
    }
    if (!m_achievementView->selectionModel()->isRowSelected(index.row(), index.parent()))
    {
        m_achievementView->setCurrentIndex(index);
    }
    const QList<int> rows = selectedAchievementRows();
    const QModelIndex source = m_achievementFilter->mapToSource(index.siblingAtColumn(AchievementModel::NameColumn));

    QMenu menu(this);
    menu.addAction(icons::get(QStringLiteral("unlock")), tr("&Unlock selected"), this, [this, rows] {
        applyToAchievements(rows, UnlockMode);
    });
    menu.addAction(icons::get(QStringLiteral("lock")), tr("&Lock selected"), this, [this, rows] {
        applyToAchievements(rows, LockMode);
    });

    QList<int> modified;
    for (const int row : rows)
    {
        if (m_achievements->achievements().at(row).isModified())
        {
            modified.append(row);
        }
    }
    auto *revert = menu.addAction(icons::get(QStringLiteral("refresh")), tr("&Revert changes"), this, [this, modified] {
        applyToAchievements(modified, RevertMode);
    });
    revert->setEnabled(!modified.isEmpty());

    menu.addSeparator();
    const QString name = source.data(Qt::DisplayRole).toString();
    const QString id = source.data(AchievementModel::IdRole).toString();
    menu.addAction(icons::get(QStringLiteral("copy")), tr("Copy &name"), this, [name] {
        QGuiApplication::clipboard()->setText(name);
    });
    menu.addAction(icons::get(QStringLiteral("copy")), tr("Copy &ID"), this, [id] {
        QGuiApplication::clipboard()->setText(id);
    });
    menu.exec(m_achievementView->viewport()->mapToGlobal(position));
}

void ManagerWindow::showSectionContextMenu(const QModelIndex &section, const QPoint &position)
{
    // Applies to the section's achievements that pass the current filter.
    const QList<int> rows = visibleSectionRows(section);

    QMenu menu(this);
    menu.addAction(icons::get(QStringLiteral("unlock")), tr("&Unlock all in this section"), this, [this, rows] {
        applyToAchievements(rows, UnlockMode);
    });
    menu.addAction(icons::get(QStringLiteral("lock")), tr("&Lock all in this section"), this, [this, rows] {
        applyToAchievements(rows, LockMode);
    });
    menu.addSeparator();
    menu.addAction(tr("&Expand all"), this, [this] {
        m_collapsedSections.clear();
        restoreSectionExpansion();
    });
    menu.addAction(tr("&Collapse all"), this, [this] {
        for (int row = 0; row < m_achievementFilter->rowCount(); ++row)
        {
            const QModelIndex index = m_achievementFilter->index(row, 0);
            if (index.data(AchievementModel::SectionRole).toBool())
            {
                m_achievementView->collapse(index);
            }
        }
    });

    const uint32_t dlcAppId = section.data(AchievementModel::SectionDlcRole).toUInt();
    if (dlcAppId != 0)
    {
        menu.addSeparator();
        menu.addAction(icons::get(QStringLiteral("external")), tr("Open DLC &store page"), this, [dlcAppId] {
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://store.steampowered.com/app/%1/").arg(dlcAppId)));
        });
    }
    menu.exec(m_achievementView->viewport()->mapToGlobal(position));
}

void ManagerWindow::showStatContextMenu(const QPoint &position)
{
    const QModelIndex index = m_statsView->indexAt(position);
    if (!index.isValid())
    {
        return;
    }
    m_statsView->selectRow(index.row());
    const int row = index.row();

    QMenu menu(this);
    auto *edit = menu.addAction(tr("&Edit value"), this, [this, index] {
        m_statsView->edit(index.siblingAtColumn(StatsModel::ValueColumn));
    });
    edit->setEnabled(m_stats->flags(index.siblingAtColumn(StatsModel::ValueColumn)) & Qt::ItemIsEditable);
    auto *revert = menu.addAction(icons::get(QStringLiteral("refresh")), tr("&Revert value"), this, [this, row] {
        m_stats->revert(row);
    });
    revert->setEnabled(index.data(StatsModel::ModifiedRole).toBool());
    menu.addSeparator();
    const QString id = index.siblingAtColumn(StatsModel::IdColumn).data().toString();
    menu.addAction(icons::get(QStringLiteral("copy")), tr("Copy &ID"), this, [id] {
        QGuiApplication::clipboard()->setText(id);
    });
    menu.exec(m_statsView->viewport()->mapToGlobal(position));
}

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------

void ManagerWindow::setBusy(bool busy, const QString &message)
{
    m_busy = busy;
    m_busyIndicator->setVisible(busy);
    m_refreshAction->setEnabled(!busy);
    m_resetAction->setEnabled(!busy);
    if (!message.isEmpty())
    {
        showStatusMessage(message);
    }
    updateSummary();
}

void ManagerWindow::updateSummary()
{
    const int total = m_achievements->achievementCount();
    const int unlocked = m_achievements->checkedCount();
    const int achievementChanges = m_achievements->modifiedCount();
    const int statChanges = m_stats->modifiedCount();
    const int changes = achievementChanges + statChanges;

    if (m_busy && total == 0)
    {
        m_summaryLabel->setText(tr("Loading…"));
    }
    else if (total == 0)
    {
        m_summaryLabel->setText(tr("This game has no achievements."));
    }
    else
    {
        const int percent = qRound(100.0 * unlocked / total);
        QString text = tr("%1 of %2 achievements unlocked (%3%)").arg(unlocked).arg(total).arg(percent);
        if (changes > 0)
        {
            text += QStringLiteral("  ·  ") + tr("%n unsaved change(s)", nullptr, changes);
        }
        m_summaryLabel->setText(text);
    }
    m_summaryProgress->setRange(0, qMax(1, total));
    m_summaryProgress->setValue(unlocked);

    m_storeAction->setText(changes > 0 ? tr("Save changes (%1)").arg(changes) : tr("Save changes"));
    // The button shows only its icon, so the tool tip carries the count.
    m_storeAction->setToolTip(changes > 0 ? tr("Save %n change(s) to Steam (Ctrl+S)", nullptr, changes)
                                          : tr("Store achievements and statistics for this game (Ctrl+S)"));
    m_storeAction->setEnabled(!m_busy && changes > 0);

    m_tabs->setTabText(
        0, achievementChanges > 0 ? tr("Achievements (%1) •").arg(total) : tr("Achievements (%1)").arg(total));
    m_tabs->setTabText(1,
                       statChanges > 0 ? tr("Statistics (%1) •").arg(m_stats->rowCount())
                                       : tr("Statistics (%1)").arg(m_stats->rowCount()));
}

void ManagerWindow::updateHeaderImage()
{
    if (m_headerUrl.isEmpty())
    {
        QString candidate =
            m_client.appData(m_appId, QStringLiteral("small_capsule/%1").arg(m_steamLanguage).toUtf8().constData());
        if (candidate.isEmpty())
        {
            candidate = m_client.appData(m_appId, "small_capsule/english");
        }
        if (candidate.isEmpty())
        {
            candidate = QStringLiteral("capsule_231x87.jpg");
        }
        m_headerUrl = QStringLiteral("https://shared.cloudflare.steamstatic.com/store_item_assets/steam/apps/%1/%2")
                          .arg(m_appId)
                          .arg(candidate);
    }
    const QString &url = m_headerUrl;
    const QImage image = m_images->image(url);
    if (!image.isNull())
    {
        m_headerImage->setPixmap(roundedPixmap(image, kHeaderImageSize, devicePixelRatioF()));
    }
    else if (m_headerImage->pixmap().isNull())
    {
        QImage placeholder(kHeaderImageSize, QImage::Format_ARGB32_Premultiplied);
        placeholder.fill(palette().color(QPalette::Mid));
        m_headerImage->setPixmap(roundedPixmap(placeholder, kHeaderImageSize, devicePixelRatioF()));
    }
}

void ManagerWindow::updateDownloadStatus(int pending)
{
    m_downloadLabel->setVisible(pending > 0);
    m_downloadLabel->setText(tr("Downloading %n icon(s)…", nullptr, pending));
}

void ManagerWindow::showStatusMessage(const QString &message, bool warning)
{
    m_statusLabel->setText(message);
    QPalette palette;
    if (warning)
    {
        palette.setColor(QPalette::WindowText, QColor(0xd7, 0x3a, 0x49));
    }
    m_statusLabel->setPalette(palette);
}
} // namespace sam
