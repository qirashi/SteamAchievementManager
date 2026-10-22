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

#include "GameListModel.h"
#include "SteamClient.h"

#include <QList>
#include <QMainWindow>
#include <QPair>
#include <QPointer>
#include <QSet>
#include <QTimer>

class QLabel;
class QLineEdit;
class QListView;
class QNetworkReply;
class QProgressBar;
class QSlider;
class QToolButton;

namespace sam
{
class GameDelegate;
class ImageCache;

class PickerWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit PickerWindow(steam::Client &client, QWidget *parent = nullptr);
    ~PickerWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    using GameEntry = QPair<uint32_t, QString>;

    void createToolBar();
    void createStatusBar();
    void restoreSettings();

    void refreshGames();
    void loadCachedList();
    void downloadList();
    void onListDownloaded();
    static QList<GameEntry> parseList(const QByteArray &data);
    void enqueue(const QList<GameEntry> &entries);
    void checkOwnershipBatch();
    void addGameIfOwned(uint32_t id, const QString &type);

    QString resolveImageUrl(uint32_t id) const;
    void onAppDataChanged(const steam::AppDataChanged &message);

    void addGameById();
    void setAddFieldVisible(bool visible);
    void launch(const QModelIndex &index);
    void showContextMenu(const QPoint &position);
    void setZoom(int percent);
    void updateTileWidth();
    void updateStatus();
    void updateDownloadStatus(int pending);

    steam::Client &m_client;
    ImageCache *m_images;
    GameListModel *m_model;
    GameFilterModel *m_filter;
    GameDelegate *m_delegate;
    QListView *m_view;

    QLineEdit *m_search = nullptr;
    QLineEdit *m_addEdit = nullptr;
    QAction *m_addAction = nullptr;     // toggles the App ID field
    QAction *m_addEditAction = nullptr; // the field in the tool bar
    QAction *m_addOkAction = nullptr;
    QAction *m_refreshAction = nullptr;
    QList<QAction *> m_typeActions;

    QLabel *m_statusLabel = nullptr;
    QLabel *m_downloadLabel = nullptr;
    QProgressBar *m_progress = nullptr;
    QSlider *m_zoom = nullptr;

    QTimer m_callbackTimer;
    QTimer m_checkTimer;
    QPointer<QNetworkReply> m_listReply;
    QList<GameEntry> m_pending;
    QSet<uint32_t> m_seen;
    int m_checkedCount = 0;
    int m_totalToCheck = 0;
    QString m_listError;
    QString m_language;
};
} // namespace sam
