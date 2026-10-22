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

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QString>

#include <cstdint>
#include <functional>

namespace sam
{
class ImageCache;

struct GameInfo
{
    uint32_t id = 0;
    QString name;
    QString type;
};

class GameListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role
    {
        IdRole = Qt::UserRole + 1,
        TypeRole,
        ImageRole,
        ImageFailedRole,
    };

    using ImageUrlResolver = std::function<QString(uint32_t)>;

    GameListModel(ImageCache *images, ImageUrlResolver resolver, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;

    void setGames(QList<GameInfo> games);
    // Returns false if the game was already present.
    bool addGame(const GameInfo &game);
    void setName(uint32_t id, const QString &name);
    QModelIndex indexOf(uint32_t id) const;
    bool contains(uint32_t id) const { return m_rows.contains(id); }

    // Forget resolved image URLs (e.g. after app data changed).
    void invalidateImage(uint32_t id);

private:
    QString imageUrl(int row) const;
    void onImageReady(const QString &url);

    ImageCache *m_images;
    ImageUrlResolver m_resolver;
    QList<GameInfo> m_games;
    QHash<uint32_t, int> m_rows;
    mutable QHash<uint32_t, QString> m_imageUrls;
    mutable QHash<QString, QSet<uint32_t>> m_urlUsers;
};

class GameFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit GameFilterModel(QObject *parent = nullptr);

    void setSearchText(const QString &text);
    void setTypeVisible(const QString &type, bool visible);
    bool isTypeVisible(const QString &type) const { return !m_hiddenTypes.contains(type); }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    QString m_search;
    QSet<QString> m_hiddenTypes;
};
} // namespace sam
