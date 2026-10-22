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

#include "GameListModel.h"

#include "ImageCache.h"

#include <QCollator>

namespace sam
{
GameListModel::GameListModel(ImageCache *images, ImageUrlResolver resolver, QObject *parent)
    : QAbstractListModel(parent)
    , m_images(images)
    , m_resolver(std::move(resolver))
{
    connect(m_images, &ImageCache::imageReady, this, &GameListModel::onImageReady);
}

int GameListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_games.size());
}

QVariant GameListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_games.size())
    {
        return {};
    }

    const GameInfo &game = m_games.at(index.row());
    switch (role)
    {
        case Qt::DisplayRole:
        case Qt::EditRole: return game.name;
        case Qt::ToolTipRole:
            return QStringLiteral("<b>%1</b><br>App ID: %2").arg(game.name.toHtmlEscaped()).arg(game.id);
        case IdRole: return game.id;
        case TypeRole: return game.type;
        case ImageRole: return m_images->image(imageUrl(index.row()));
        case ImageFailedRole: {
            const QString url = imageUrl(index.row());
            return url.isEmpty() || m_images->hasFailed(url);
        }
        default: return {};
    }
}

void GameListModel::setGames(QList<GameInfo> games)
{
    beginResetModel();
    m_games = std::move(games);
    m_rows.clear();
    for (int i = 0; i < m_games.size(); ++i)
    {
        m_rows.insert(m_games.at(i).id, i);
    }
    endResetModel();
}

bool GameListModel::addGame(const GameInfo &game)
{
    if (m_rows.contains(game.id))
    {
        return false;
    }
    const int row = static_cast<int>(m_games.size());
    beginInsertRows({}, row, row);
    m_games.append(game);
    m_rows.insert(game.id, row);
    endInsertRows();
    return true;
}

void GameListModel::setName(uint32_t id, const QString &name)
{
    const auto it = m_rows.constFind(id);
    if (it == m_rows.cend() || name.isEmpty())
    {
        return;
    }
    m_games[*it].name = name;
    const QModelIndex changed = index(*it);
    emit dataChanged(changed, changed, {Qt::DisplayRole, Qt::EditRole, Qt::ToolTipRole});
}

QModelIndex GameListModel::indexOf(uint32_t id) const
{
    const auto it = m_rows.constFind(id);
    return it == m_rows.cend() ? QModelIndex() : index(*it);
}

void GameListModel::invalidateImage(uint32_t id)
{
    const auto it = m_imageUrls.constFind(id);
    if (it != m_imageUrls.cend())
    {
        m_urlUsers[*it].remove(id);
        m_imageUrls.erase(it);
    }
    const QModelIndex changed = indexOf(id);
    if (changed.isValid())
    {
        emit dataChanged(changed, changed, {ImageRole, ImageFailedRole});
    }
}

QString GameListModel::imageUrl(int row) const
{
    const uint32_t id = m_games.at(row).id;
    auto it = m_imageUrls.constFind(id);
    if (it == m_imageUrls.cend())
    {
        const QString url = m_resolver(id);
        it = m_imageUrls.insert(id, url);
        if (!url.isEmpty())
        {
            m_urlUsers[url].insert(id);
        }
    }
    return *it;
}

void GameListModel::onImageReady(const QString &url)
{
    const auto it = m_urlUsers.constFind(url);
    if (it == m_urlUsers.cend())
    {
        return;
    }
    for (const uint32_t id : *it)
    {
        const QModelIndex changed = indexOf(id);
        if (changed.isValid())
        {
            emit dataChanged(changed, changed, {ImageRole, ImageFailedRole});
        }
    }
}

GameFilterModel::GameFilterModel(QObject *parent) : QSortFilterProxyModel(parent)
{
    setSortCaseSensitivity(Qt::CaseInsensitive);
    setDynamicSortFilter(true);
}

void GameFilterModel::setSearchText(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed == m_search)
    {
        return;
    }
    beginFilterChange();
    m_search = trimmed;
    endFilterChange(Direction::Rows);
}

void GameFilterModel::setTypeVisible(const QString &type, bool visible)
{
    if (visible == !m_hiddenTypes.contains(type))
    {
        return;
    }
    beginFilterChange();
    if (visible)
    {
        m_hiddenTypes.remove(type);
    }
    else
    {
        m_hiddenTypes.insert(type);
    }
    endFilterChange(Direction::Rows);
}

bool GameFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);

    const QString type = index.data(GameListModel::TypeRole).toString();
    if (m_hiddenTypes.contains(type))
    {
        return false;
    }

    if (m_search.isEmpty())
    {
        return true;
    }

    if (index.data(Qt::DisplayRole).toString().contains(m_search, Qt::CaseInsensitive))
    {
        return true;
    }

    // Typing an App ID also finds the game.
    bool isNumber = false;
    const uint id = m_search.toUInt(&isNumber);
    return isNumber && index.data(GameListModel::IdRole).toUInt() == id;
}

bool GameFilterModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    static const QCollator collator = [] {
        QCollator result;
        result.setCaseSensitivity(Qt::CaseInsensitive);
        result.setNumericMode(true);
        return result;
    }();
    const int order = collator.compare(left.data(Qt::DisplayRole).toString(), right.data(Qt::DisplayRole).toString());
    if (order != 0)
    {
        return order < 0;
    }
    return left.data(GameListModel::IdRole).toUInt() < right.data(GameListModel::IdRole).toUInt();
}
} // namespace sam
