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

#pragma once

#include "AchievementGroups.h"
#include "Schema.h"
#include "SteamClient.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QMainWindow>
#include <QSet>
#include <QTimer>

#include <cstdint>
#include <optional>

class QAbstractButton;
class QButtonGroup;
class QCheckBox;
class QMenu;
class QLabel;
class QLineEdit;
class QProgressBar;
class QTabWidget;
class QTableView;
class QToolButton;
class QTreeView;

namespace sam
{
class AchievementFilterModel;
class AchievementModel;
class DiscordPresence;
class ImageCache;
class StatsModel;

class ManagerWindow : public QMainWindow
{
    Q_OBJECT

public:
    ManagerWindow(uint32_t appId, steam::Client &client, QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *createHeader();
    QWidget *createAchievementsPage();
    QWidget *createStatisticsPage();
    void createActions();
    QToolButton *createActionButton(QAction *action, QWidget *parent);
    void createStatusBar();
    void restoreSettings();
    void onSettingsChanged(const QString &path);

    void refreshStats();
    void onUserStatsReceived(const steam::UserStatsReceived &message);
    void loadAchievements();
    void loadStatistics();
    QString effectiveLanguage() const;
    void applyLanguage();
    bool isGroupingEnabled() const;
    bool isDiscordPresenceEnabled() const;
    void applyGroups();
    void restoreSectionExpansion();

    bool hasChanges() const;
    bool confirmDiscardChanges(const QString &question);
    bool store();
    bool storeAchievements(int *count);
    bool storeStatistics(int *count);
    void resetAllStats();

    QList<int> visibleAchievementRows() const;
    QList<int> selectedAchievementRows() const;
    QList<int> visibleSectionRows(const QModelIndex &section) const;
    void applyToAchievements(const QList<int> &rows, int mode);
    void showAchievementContextMenu(const QPoint &position);
    void showSectionContextMenu(const QModelIndex &section, const QPoint &position);
    void showStatContextMenu(const QPoint &position);

    void setBusy(bool busy, const QString &message = {});
    void updateSummary();
    void updatePlayTime();
    void updateHeaderImage();
    void updateDownloadStatus(int pending);
    void showStatusMessage(const QString &message, bool warning = false);

    uint32_t m_appId;
    steam::Client &m_client;
    QString m_gameName;
    QString m_steamLanguage;
    QString m_headerUrl;
    std::optional<Schema> m_schema;
    std::optional<QList<AchievementGroup>> m_groups; // set once downloaded
    bool m_groupsRequested = false;
    bool m_groupingApplied = false;
    QFileSystemWatcher m_settingsWatcher;
    QSet<QString> m_collapsedSections;
    bool m_busy = false;

    ImageCache *m_images;
    AchievementModel *m_achievements;
    AchievementFilterModel *m_achievementFilter;
    StatsModel *m_stats;
    DiscordPresence *m_discord;

    // Header
    QLabel *m_headerImage = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QLabel *m_playTimeLabel = nullptr;
    QProgressBar *m_summaryProgress = nullptr;

    // Actions (buttons on the pages)
    QAction *m_storeAction = nullptr;
    QAction *m_refreshAction = nullptr;
    QAction *m_resetAction = nullptr;

    // Pages
    QTabWidget *m_tabs = nullptr;
    QLineEdit *m_achievementSearch = nullptr;
    QButtonGroup *m_stateGroup = nullptr;
    QTreeView *m_achievementView = nullptr;
    QCheckBox *m_editStatsCheck = nullptr;
    QTableView *m_statsView = nullptr;

    // Status bar
    QLabel *m_statusLabel = nullptr;
    QLabel *m_downloadLabel = nullptr;
    QLabel *m_discordLabel = nullptr;
    QProgressBar *m_busyIndicator = nullptr;

    QTimer m_callbackTimer;
    // Steam counts the game as running from the start of SAM.Game on.
    QElapsedTimer m_playTime;
    QDateTime m_playStart;
    QTimer m_playTimeTimer;
};
} // namespace sam
