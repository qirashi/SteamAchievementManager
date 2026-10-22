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

#include "PickerWindow.h"

#include "Application.h"
#include "GameDelegate.h"
#include "Icons.h"
#include "ImageCache.h"
#include "Languages.h"
#include "TitleBar.h"

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QSaveFile>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QSlider>
#include <QStandardPaths>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QWheelEvent>
#include <QXmlStreamReader>

#include <cstring>

namespace sam
{
namespace
{
const QString kGameListUrl = QStringLiteral("https://gib.me/sam/games.xml");
const QString kStoreAssets =
    QStringLiteral("https://shared.cloudflare.steamstatic.com/store_item_assets/steam/apps/%1/%2");
const QString kCommunityAssets =
    QStringLiteral("https://cdn.steamstatic.com/steamcommunity/public/images/apps/%1/%2.jpg");

QString cachedListPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("games.xml"));
}

struct TypeFilter
{
    const char *type;
    const char *label;
    bool defaultVisible;
};

const TypeFilter kTypeFilters[] = {
    {"normal", QT_TRANSLATE_NOOP("sam::PickerWindow", "Show &games"), true},
    {"demo", QT_TRANSLATE_NOOP("sam::PickerWindow", "Show &demos"), false},
    {"mod", QT_TRANSLATE_NOOP("sam::PickerWindow", "Show &mods"), false},
    {"junk", QT_TRANSLATE_NOOP("sam::PickerWindow", "Show &junk"), false},
};
} // namespace

PickerWindow::PickerWindow(steam::Client &client, QWidget *parent)
    : QMainWindow(parent)
    , m_client(client)
    , m_images(new ImageCache(this))
    , m_model(new GameListModel(
          m_images,
          [this](uint32_t id) {
              return resolveImageUrl(id);
          },
          this))
    , m_filter(new GameFilterModel(this))
    , m_delegate(new GameDelegate(this))
    , m_view(new QListView(this))
{
    setWindowTitle(tr("Games"));
    installTitleBar(this);
    m_language = m_client.currentGameLanguage();

    m_filter->setSourceModel(m_model);
    m_filter->sort(0);

    m_view->setModel(m_filter);
    m_view->setItemDelegate(m_delegate);
    m_view->setViewMode(QListView::IconMode);
    m_view->setResizeMode(QListView::Adjust);
    m_view->setMovement(QListView::Static);
    m_view->setUniformItemSizes(true);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_view->verticalScrollBar()->setSingleStep(32);
    // The scroll bar lies over the tiles (see Scrolling.h), so it never changes the layout.
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setMouseTracking(true);
    m_view->viewport()->setAttribute(Qt::WA_Hover);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->viewport()->installEventFilter(this);
    setCentralWidget(m_view);

    connect(m_view, &QListView::activated, this, &PickerWindow::launch);
    connect(m_view, &QListView::customContextMenuRequested, this, &PickerWindow::showContextMenu);
    connect(m_filter, &QAbstractItemModel::rowsInserted, this, &PickerWindow::updateStatus);
    connect(m_filter, &QAbstractItemModel::rowsRemoved, this, &PickerWindow::updateStatus);
    connect(m_filter, &QAbstractItemModel::modelReset, this, &PickerWindow::updateStatus);
    connect(m_filter, &QAbstractItemModel::layoutChanged, this, &PickerWindow::updateStatus);
    connect(m_images, &ImageCache::pendingCountChanged, this, &PickerWindow::updateDownloadStatus);

    createToolBar();
    createStatusBar();
    restoreSettings();

    m_client.registerCallback(steam::kAppDataChangedCallback, [this](const void *payload, int size) {
        steam::AppDataChanged message{};
        std::memcpy(&message, payload, qMin<size_t>(sizeof(message), static_cast<size_t>(qMax(size, 0))));
        onAppDataChanged(message);
    });
    connect(&m_callbackTimer, &QTimer::timeout, this, [this] {
        m_client.runCallbacks();
    });
    m_callbackTimer.start(100);

    m_checkTimer.setInterval(0);
    connect(&m_checkTimer, &QTimer::timeout, this, &PickerWindow::checkOwnershipBatch);

    auto *find = new QShortcut(QKeySequence::Find, this);
    connect(find, &QShortcut::activated, this, [this] {
        m_search->setFocus();
        m_search->selectAll();
    });

    QTimer::singleShot(0, this, &PickerWindow::refreshGames);
}

PickerWindow::~PickerWindow() = default;

void PickerWindow::createToolBar()
{
    auto *toolBar = addToolBar(tr("Main"));
    toolBar->setObjectName(QStringLiteral("mainToolBar"));
    toolBar->setMovable(false);
    toolBar->setFloatable(false);
    toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolBar->setIconSize(QSize(18, 18));
    toolBar->toggleViewAction()->setVisible(false);

    m_search = new QLineEdit(toolBar);
    m_search->setPlaceholderText(tr("Search by name or App ID…"));
    m_search->setClearButtonEnabled(true);
    m_search->addAction(icons::get(QStringLiteral("search")), QLineEdit::LeadingPosition);
    // A fixed width keeps the buttons after it in place when the App ID field
    // unfolds; the spacer after that field takes the rest.
    m_search->setFixedWidth(360);
    m_search->setToolTip(tr("Filter the list by game name or App ID (Ctrl+F)"));
    connect(m_search, &QLineEdit::textChanged, m_filter, &GameFilterModel::setSearchText);
    connect(m_search, &QLineEdit::returnPressed, this, [this] {
        if (m_filter->rowCount() > 0)
        {
            m_view->setFocus();
            m_view->setCurrentIndex(m_filter->index(0, 0));
        }
    });
    toolBar->addWidget(m_search);

    auto *filterMenu = new QMenu(this);
    for (const auto &filter : kTypeFilters)
    {
        auto *action = filterMenu->addAction(tr(filter.label));
        action->setCheckable(true);
        action->setData(QString::fromLatin1(filter.type));
        m_typeActions.append(action);
        connect(action, &QAction::toggled, this, [this, action](bool checked) {
            m_filter->setTypeVisible(action->data().toString(), checked);
        });
    }
    // Steam's store category 22 ("Steam Achievements") says which games have any.
    m_noAchievementsAction = filterMenu->addAction(tr("Show games &without achievements"));
    m_noAchievementsAction->setCheckable(true);
    m_noAchievementsAction->setChecked(true);
    connect(m_noAchievementsAction, &QAction::toggled, m_filter, &GameFilterModel::setShowWithoutAchievements);

    filterMenu->addSection(tr("Sort"));
    auto *sortGroup = new QActionGroup(filterMenu);
    const struct
    {
        GameFilterModel::SortMode mode;
        const char *label;
    } sorts[] = {
        {GameFilterModel::SortMode::Name, QT_TR_NOOP("By &name")},
        {GameFilterModel::SortMode::LastPlayed, QT_TR_NOOP("&Recently played")},
        {GameFilterModel::SortMode::Playtime, QT_TR_NOOP("By &playtime")},
    };
    for (const auto &sort : sorts)
    {
        auto *action = filterMenu->addAction(tr(sort.label));
        action->setCheckable(true);
        action->setData(static_cast<int>(sort.mode));
        sortGroup->addAction(action);
        m_sortActions.append(action);
        connect(action, &QAction::triggered, this, [this, mode = sort.mode] {
            m_filter->setSortMode(mode);
        });
    }
    m_sortActions.first()->setChecked(true);
    auto *filterButton = new QToolButton(toolBar);
    filterButton->setIcon(icons::get(QStringLiteral("filter")));
    filterButton->setToolTip(tr("Choose which apps are listed and how they're sorted"));
    filterButton->setPopupMode(QToolButton::InstantPopup);
    filterButton->setMenu(filterMenu);
    toolBar->addWidget(filterButton);

    m_refreshAction = toolBar->addAction(icons::get(QStringLiteral("refresh")), tr("Refresh"));
    m_refreshAction->setShortcut(QKeySequence::Refresh);
    m_refreshAction->setToolTip(tr("Reload the list of owned games (F5)"));
    connect(m_refreshAction, &QAction::triggered, this, &PickerWindow::refreshGames);

    // "Add game" unfolds an App ID field with an OK button right after it.
    m_addAction = toolBar->addAction(icons::get(QStringLiteral("add")), tr("Add game"));
    m_addAction->setToolTip(tr("Add a game that isn't in the list by its App ID"));
    m_addAction->setCheckable(true);
    connect(m_addAction, &QAction::toggled, this, &PickerWindow::setAddFieldVisible);

    m_addEdit = new QLineEdit(toolBar);
    m_addEdit->setPlaceholderText(tr("App ID"));
    m_addEdit->setValidator(
        new QRegularExpressionValidator(QRegularExpression(QStringLiteral("\\d{1,10}")), m_addEdit));
    m_addEdit->setFixedWidth(100);
    m_addEdit->installEventFilter(this); // Esc folds it again
    connect(m_addEdit, &QLineEdit::returnPressed, this, &PickerWindow::addGameById);
    m_addEditAction = toolBar->addWidget(m_addEdit);

    auto *okButton = new QPushButton(tr("OK"), toolBar);
    okButton->setToolTip(tr("Add the game with this App ID to the list"));
    connect(okButton, &QPushButton::clicked, this, &PickerWindow::addGameById);
    m_addOkAction = toolBar->addWidget(okButton);
    setAddFieldVisible(false);

    auto *spacer = new QWidget(toolBar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolBar->addWidget(spacer);

    auto *settingsButton = new QToolButton(toolBar);
    settingsButton->setIcon(icons::get(QStringLiteral("menu")));
    settingsButton->setToolTip(tr("Settings"));
    settingsButton->setPopupMode(QToolButton::InstantPopup);
    // The only settings menu: the game windows follow it (they watch the
    // settings file), including the ones already open.
    auto *settingsMenu = createSettingsMenu(this);
    QAction *first = settingsMenu->actions().value(0);
    auto *languageMenu = createAchievementLanguageMenu(settingsMenu);
    fillAchievementLanguageMenu(languageMenu, languages::allCodes(), m_language);
    settingsMenu->insertMenu(first, languageMenu);
    // Toggles have no icon: a menu item shows its icon instead of its check box.
    const auto addToggle = [&](const QString &text, const QString &key, bool defaultValue) {
        auto *action = new QAction(text, settingsMenu);
        action->setCheckable(true);
        action->setChecked(settings().value(key, defaultValue).toBool());
        connect(action, &QAction::toggled, this, [key](bool checked) {
            settings().setValue(key, checked);
            settings().sync();
        });
        settingsMenu->insertAction(first, action);
    };
    addToggle(tr("&Group by DLC (steamhunters.com)"), QStringLiteral("game/groupAchievements"), true);
    addToggle(tr("Show the game in &Discord"), QStringLiteral("game/discordPresence"), false);
    settingsMenu->insertSeparator(first);
    settingsButton->setMenu(settingsMenu);
    toolBar->addWidget(settingsButton);
}

void PickerWindow::createStatusBar()
{
    auto *bar = statusBar();
    m_statusLabel = new QLabel(bar);
    bar->addWidget(m_statusLabel, 1);

    m_progress = new QProgressBar(bar);
    m_progress->setMaximumWidth(180);
    m_progress->setMaximumHeight(14);
    m_progress->setTextVisible(false);
    m_progress->hide();
    bar->addPermanentWidget(m_progress);

    m_downloadLabel = new QLabel(bar);
    m_downloadLabel->hide();
    bar->addPermanentWidget(m_downloadLabel);

    auto *zoomIcon = new QLabel(bar);
    zoomIcon->setPixmap(icons::get(QStringLiteral("grid")).pixmap(14, 14));
    zoomIcon->setToolTip(tr("Tile size (Ctrl+mouse wheel)"));
    bar->addPermanentWidget(zoomIcon);

    m_zoom = new QSlider(Qt::Horizontal, bar);
    m_zoom->setRange(60, 160);
    m_zoom->setSingleStep(10);
    m_zoom->setPageStep(20);
    m_zoom->setValue(100);
    m_zoom->setFixedWidth(110);
    m_zoom->setToolTip(zoomIcon->toolTip());
    connect(m_zoom, &QSlider::valueChanged, this, &PickerWindow::setZoom);
    bar->addPermanentWidget(m_zoom);
}

void PickerWindow::restoreSettings()
{
    auto &store = settings();
    store.beginGroup(QStringLiteral("picker"));
    if (!restoreGeometry(store.value(QStringLiteral("geometry")).toByteArray()))
    {
        resize(1000, 680);
    }
    m_zoom->setValue(store.value(QStringLiteral("zoom"), 100).toInt());
    for (auto *action : std::as_const(m_typeActions))
    {
        const QString type = action->data().toString();
        bool defaultVisible = false;
        for (const auto &filter : kTypeFilters)
        {
            if (type == QLatin1StringView(filter.type))
            {
                defaultVisible = filter.defaultVisible;
            }
        }
        const bool visible = store.value(QStringLiteral("show/") + type, defaultVisible).toBool();
        action->setChecked(visible);
        m_filter->setTypeVisible(type, visible);
    }
    m_noAchievementsAction->setChecked(store.value(QStringLiteral("show/noAchievements"), true).toBool());
    const int sort = store.value(QStringLiteral("sort"), 0).toInt();
    for (auto *action : std::as_const(m_sortActions))
    {
        if (action->data().toInt() == sort)
        {
            action->setChecked(true);
            m_filter->setSortMode(static_cast<GameFilterModel::SortMode>(sort));
        }
    }
    store.endGroup();
    setZoom(m_zoom->value());
}

void PickerWindow::closeEvent(QCloseEvent *event)
{
    auto &store = settings();
    store.beginGroup(QStringLiteral("picker"));
    store.setValue(QStringLiteral("geometry"), saveGeometry());
    store.setValue(QStringLiteral("zoom"), m_zoom->value());
    for (auto *action : std::as_const(m_typeActions))
    {
        store.setValue(QStringLiteral("show/") + action->data().toString(), action->isChecked());
    }
    store.setValue(QStringLiteral("show/noAchievements"), m_noAchievementsAction->isChecked());
    for (auto *action : std::as_const(m_sortActions))
    {
        if (action->isChecked())
        {
            store.setValue(QStringLiteral("sort"), action->data().toInt());
        }
    }
    store.endGroup();
    QMainWindow::closeEvent(event);
}

bool PickerWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_addEdit && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape)
    {
        m_addAction->setChecked(false);
        return true;
    }
    if (watched == m_view->viewport() && event->type() == QEvent::Resize)
    {
        updateTileWidth();
    }
    if (watched == m_view->viewport() && event->type() == QEvent::Wheel)
    {
        auto *wheel = static_cast<QWheelEvent *>(event);
        if (wheel->modifiers() & Qt::ControlModifier)
        {
            const int steps = wheel->angleDelta().y() / 120;
            m_zoom->setValue(m_zoom->value() + steps * m_zoom->singleStep());
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void PickerWindow::setZoom(int percent)
{
    m_delegate->setScale(percent / 100.0);
    m_delegate->setTileWidth(0);
    updateTileWidth();
}

void PickerWindow::updateTileWidth()
{
    const int available = m_view->viewport()->width() - 1;
    const int minimum = m_delegate->minimumTileWidth();
    const int columns = qMax(1, available / minimum);
    const int width = qMax(minimum, available / columns);
    if (width != m_delegate->tileWidth())
    {
        m_delegate->setTileWidth(width);
        m_view->doItemsLayout();
    }
}

// ---------------------------------------------------------------------------
// Game list loading
// ---------------------------------------------------------------------------

void PickerWindow::refreshGames()
{
    if (m_listReply)
    {
        m_listReply->abort();
    }
    m_checkTimer.stop();
    m_images->cancelPending();
    m_model->setGames({});
    m_pending.clear();
    m_seen.clear();
    m_checkedCount = 0;
    m_totalToCheck = 0;
    m_listError.clear();
    m_refreshAction->setEnabled(false);
    m_playTimes = loadPlayTimes(m_client.steamId());

    // Start with the list from the previous run (instant), then merge in
    // whatever is new once the download finishes.
    loadCachedList();
    downloadList();
    updateStatus();
}

void PickerWindow::loadCachedList()
{
    QFile file(cachedListPath());
    if (file.open(QIODevice::ReadOnly))
    {
        enqueue(parseList(file.readAll()));
    }
}

void PickerWindow::downloadList()
{
    QNetworkRequest request{QUrl(kGameListUrl)};
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    request.setTransferTimeout(90000);
    // The list is 1.7 MB and changes about once a day: download it only when
    // it differs from the copy on disk (loaded by now; m_seen is what it had).
    if (!m_seen.isEmpty())
    {
        const QByteArray etag = settings().value(QStringLiteral("picker/gameListETag")).toString().toLatin1();
        const QByteArray modified =
            settings().value(QStringLiteral("picker/gameListLastModified")).toString().toLatin1();
        if (!etag.isEmpty())
        {
            request.setRawHeader("If-None-Match", etag);
        }
        if (!modified.isEmpty())
        {
            request.setRawHeader("If-Modified-Since", modified);
        }
    }
    m_listReply = network()->get(request);
    connect(m_listReply, &QNetworkReply::finished, this, &PickerWindow::onListDownloaded);
}

void PickerWindow::onListDownloaded()
{
    QNetworkReply *reply = m_listReply;
    if (reply == nullptr)
    {
        return;
    }
    reply->deleteLater();
    m_listReply = nullptr;

    if (reply->error() == QNetworkReply::OperationCanceledError)
    {
        return;
    }

    if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 304)
    {
        // Not modified: the cached list, already loaded, is current.
    }
    else if (reply->error() == QNetworkReply::NoError)
    {
        const QByteArray body = reply->readAll();
        const auto entries = parseList(body);
        if (!entries.isEmpty())
        {
            QDir().mkpath(QFileInfo(cachedListPath()).absolutePath());
            QSaveFile file(cachedListPath());
            if (file.open(QIODevice::WriteOnly))
            {
                file.write(body);
                if (file.commit())
                {
                    settings().setValue(QStringLiteral("picker/gameListETag"),
                                        QString::fromLatin1(reply->rawHeader("ETag")));
                    settings().setValue(QStringLiteral("picker/gameListLastModified"),
                                        QString::fromLatin1(reply->rawHeader("Last-Modified")));
                }
            }
            enqueue(entries);
        }
        else
        {
            m_listError = tr("the downloaded game list is empty or malformed");
        }
    }
    else
    {
        m_listError = reply->errorString();
    }

    if (!m_listError.isEmpty() && m_seen.isEmpty())
    {
        // Nothing cached either: fall back to Spacewar like the original.
        addGameIfOwned(480, QStringLiteral("normal"));
        showError(this, tr("Failed to download the game list:\n%1").arg(m_listError));
    }

    if (!m_checkTimer.isActive())
    {
        m_refreshAction->setEnabled(true);
    }
    updateStatus();
}

QList<PickerWindow::GameEntry> PickerWindow::parseList(const QByteArray &data)
{
    QList<GameEntry> entries;
    QXmlStreamReader xml(data);
    while (xml.readNextStartElement())
    {
        if (xml.name() != QLatin1StringView("games"))
        {
            xml.skipCurrentElement();
            continue;
        }
        while (xml.readNextStartElement())
        {
            if (xml.name() != QLatin1StringView("game"))
            {
                xml.skipCurrentElement();
                continue;
            }
            QString type = xml.attributes().value(QLatin1StringView("type")).toString();
            if (type.isEmpty())
            {
                type = QStringLiteral("normal");
            }
            bool ok = false;
            const uint id = xml.readElementText().trimmed().toUInt(&ok);
            if (ok)
            {
                entries.append({id, type});
            }
        }
    }
    return xml.hasError() ? QList<GameEntry>() : entries;
}

void PickerWindow::enqueue(const QList<GameEntry> &entries)
{
    for (const auto &entry : entries)
    {
        if (!m_seen.contains(entry.first))
        {
            m_seen.insert(entry.first);
            m_pending.append(entry);
            ++m_totalToCheck;
        }
    }
    if (!m_pending.isEmpty() && !m_checkTimer.isActive())
    {
        m_checkTimer.start();
    }
}

void PickerWindow::checkOwnershipBatch()
{
    // Work in short slices so the window stays responsive.
    QElapsedTimer timer;
    timer.start();
    while (!m_pending.isEmpty() && timer.elapsed() < 30)
    {
        const GameEntry entry = m_pending.takeLast();
        addGameIfOwned(entry.first, entry.second);
        ++m_checkedCount;
    }

    if (m_pending.isEmpty())
    {
        m_checkTimer.stop();
        if (!m_listReply)
        {
            m_refreshAction->setEnabled(true);
        }
    }
    updateStatus();
}

void PickerWindow::addGameIfOwned(uint32_t id, const QString &type)
{
    if (m_model->contains(id) || !m_client.isSubscribedApp(id))
    {
        return;
    }
    GameInfo game;
    game.id = id;
    game.type = type;
    game.name = m_client.appData(id, "name");
    if (game.name.isEmpty())
    {
        game.name = QStringLiteral("App %1").arg(id);
    }
    fillGameInfo(game);
    m_model->addGame(game);
}

void PickerWindow::fillGameInfo(GameInfo &game) const
{
    const PlayTime time = m_playTimes.value(game.id);
    game.playtime = time.minutes;
    game.lastPlayed = time.lastPlayed;
    game.hasAchievements = hasAchievements(game.id);
}

bool PickerWindow::hasAchievements(uint32_t id) const
{
    return m_client.appData(id, "category/category_22") == QLatin1StringView("1");
}

QString PickerWindow::resolveImageUrl(uint32_t id) const
{
    const QString english = QStringLiteral("english");
    QString candidate = m_client.appData(id, QStringLiteral("small_capsule/%1").arg(m_language).toUtf8().constData());
    if (candidate.isEmpty() && m_language != english)
    {
        candidate = m_client.appData(id, "small_capsule/english");
    }
    if (!candidate.isEmpty())
    {
        return kStoreAssets.arg(id).arg(candidate);
    }

    candidate = m_client.appData(id, "logo");
    if (!candidate.isEmpty())
    {
        return kCommunityAssets.arg(id).arg(candidate);
    }

    return kStoreAssets.arg(id).arg(QStringLiteral("capsule_231x87.jpg"));
}

void PickerWindow::onAppDataChanged(const steam::AppDataChanged &message)
{
    if (!message.result || !m_model->contains(message.appId))
    {
        return;
    }
    m_model->setName(message.appId, m_client.appData(message.appId, "name"));
    m_model->setHasAchievements(message.appId, hasAchievements(message.appId));
    m_model->invalidateImage(message.appId);
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void PickerWindow::addGameById()
{
    bool ok = false;
    const uint id = m_addEdit->text().trimmed().toUInt(&ok);
    if (!ok || id == 0)
    {
        showError(this, tr("Please enter a valid game ID."));
        return;
    }

    if (!m_client.isSubscribedApp(id))
    {
        showError(this, tr("You don't own that game."));
        return;
    }

    if (!m_model->contains(id))
    {
        GameInfo game;
        game.id = id;
        game.type = QStringLiteral("normal");
        game.name = m_client.appData(id, "name");
        if (game.name.isEmpty())
        {
            game.name = QStringLiteral("App %1").arg(id);
        }
        fillGameInfo(game);
        m_model->addGame(game);
    }

    // Make sure it is visible and select it.
    const QString type = m_model->indexOf(id).data(GameListModel::TypeRole).toString();
    for (auto *action : std::as_const(m_typeActions))
    {
        if (action->data().toString() == type)
        {
            action->setChecked(true);
        }
    }
    m_search->clear();
    m_addAction->setChecked(false);
    const QModelIndex index = m_filter->mapFromSource(m_model->indexOf(id));
    if (index.isValid())
    {
        m_view->setCurrentIndex(index);
        m_view->scrollTo(index, QAbstractItemView::PositionAtCenter);
        m_view->setFocus();
    }
}

void PickerWindow::setAddFieldVisible(bool visible)
{
    m_addEditAction->setVisible(visible);
    m_addOkAction->setVisible(visible);
    if (visible)
    {
        m_addEdit->setFocus();
    }
    else
    {
        m_addEdit->clear();
    }
}

void PickerWindow::launch(const QModelIndex &index)
{
    if (!index.isValid())
    {
        return;
    }
    const uint32_t id = index.data(GameListModel::IdRole).toUInt();
    const QString program = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("SAM.Game.exe"));
    if (!QProcess::startDetached(program, {QString::number(id)}))
    {
        showError(this, tr("Failed to start SAM.Game.exe."));
        return;
    }
    statusBar()->showMessage(tr("Opening %1…").arg(index.data(Qt::DisplayRole).toString()), 4000);
}

void PickerWindow::showContextMenu(const QPoint &position)
{
    const QModelIndex index = m_view->indexAt(position);
    if (!index.isValid())
    {
        return;
    }
    m_view->setCurrentIndex(index);
    const uint32_t id = index.data(GameListModel::IdRole).toUInt();

    QMenu menu(this);
    auto *open = menu.addAction(icons::get(QStringLiteral("trophy")), tr("&Manage achievements"));
    menu.setDefaultAction(open);
    connect(open, &QAction::triggered, this, [this, index] {
        launch(index);
    });
    menu.addSeparator();
    menu.addAction(icons::get(QStringLiteral("external")), tr("Open &store page"), this, [id] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://store.steampowered.com/app/%1/").arg(id)));
    });
    menu.addAction(icons::get(QStringLiteral("external")), tr("Show in Steam &library"), this, [id] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("steam://nav/games/details/%1").arg(id)));
    });
    menu.addSeparator();
    menu.addAction(icons::get(QStringLiteral("copy")), tr("Copy App &ID"), this, [id] {
        QGuiApplication::clipboard()->setText(QString::number(id));
    });
    const QString name = index.data(Qt::DisplayRole).toString();
    menu.addAction(icons::get(QStringLiteral("copy")), tr("Copy &name"), this, [name] {
        QGuiApplication::clipboard()->setText(name);
    });
    menu.exec(m_view->viewport()->mapToGlobal(position));
}

void PickerWindow::updateStatus()
{
    const bool checking = m_checkTimer.isActive();
    const bool downloading = !m_listReply.isNull();

    QString text;
    if (checking)
    {
        text = tr("Checking game ownership… %1 of %2").arg(m_checkedCount).arg(m_totalToCheck);
    }
    else if (downloading && m_model->rowCount() == 0)
    {
        text = tr("Downloading game list…");
    }
    else
    {
        text = tr("Showing %1 of %n game(s).", nullptr, m_model->rowCount()).arg(m_filter->rowCount());
        if (downloading)
        {
            text += u' ' + tr("Updating game list…");
        }
        else if (!m_listError.isEmpty())
        {
            text += u' ' + tr("(Game list download failed: %1)").arg(m_listError);
        }
    }
    m_statusLabel->setText(text);

    m_progress->setVisible(checking || downloading);
    if (checking)
    {
        m_progress->setRange(0, qMax(1, m_totalToCheck));
        m_progress->setValue(m_checkedCount);
    }
    else
    {
        m_progress->setRange(0, 0);
    }
}

void PickerWindow::updateDownloadStatus(int pending)
{
    m_downloadLabel->setVisible(pending > 0);
    m_downloadLabel->setText(tr("Loading %n image(s)…", nullptr, pending));
}
} // namespace sam
