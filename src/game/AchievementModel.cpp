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

#include "AchievementModel.h"

#include "ImageCache.h"

#include <QLocale>

#include <algorithm>

namespace sam
{
AchievementModel::AchievementModel(uint32_t appId, ImageCache *images, QObject *parent)
    : QAbstractTableModel(parent)
    , m_appId(appId)
    , m_images(images)
{
    connect(m_images, &ImageCache::imageReady, this, &AchievementModel::onImageReady);
}

int AchievementModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_achievements.size());
}

int AchievementModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QString AchievementModel::displayName(const Achievement &achievement) const
{
    const QString name = achievement.definition.name.get(m_language, achievement.definition.id);
    // Unresolved localization tokens look like "#ACH_NAME"; show the id instead.
    return name.startsWith(u'#') ? achievement.definition.id : name;
}

QString AchievementModel::description(const Achievement &achievement) const
{
    const QString name = achievement.definition.name.get(m_language, achievement.definition.id);
    if (name.startsWith(u'#'))
    {
        return {};
    }
    return achievement.definition.description.get(m_language);
}

QString AchievementModel::iconUrl(const Achievement &achievement) const
{
    const QString &normal = achievement.definition.iconNormal;
    const QString &locked = achievement.definition.iconLocked.isEmpty() ? normal : achievement.definition.iconLocked;
    const QString &icon = achievement.checked ? normal : locked;
    if (icon.isEmpty())
    {
        return {};
    }
    return QStringLiteral("https://cdn.steamstatic.com/steamcommunity/public/images/apps/%1/%2").arg(m_appId).arg(icon);
}

QVariant AchievementModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_achievements.size())
    {
        return {};
    }

    const Achievement &achievement = m_achievements.at(index.row());
    switch (role)
    {
        case Qt::DisplayRole:
            if (index.column() == NameColumn)
            {
                return displayName(achievement);
            }
            if (index.column() == UnlockTimeColumn && achievement.achieved && achievement.unlockTime.isValid())
            {
                return QLocale().toString(achievement.unlockTime, QLocale::ShortFormat);
            }
            return {};
        case Qt::CheckStateRole:
            if (index.column() == NameColumn)
            {
                return achievement.checked ? Qt::Checked : Qt::Unchecked;
            }
            return {};
        case Qt::ToolTipRole: {
            QString tip = QStringLiteral("<b>%1</b>").arg(displayName(achievement).toHtmlEscaped());
            const QString text = description(achievement);
            if (!text.isEmpty())
            {
                tip += QStringLiteral("<br>%1").arg(text.toHtmlEscaped());
            }
            tip += QStringLiteral("<br><small>ID: %1</small>").arg(achievement.definition.id.toHtmlEscaped());
            if (achievement.isProtected())
            {
                tip += QStringLiteral("<br><small>%1</small>")
                           .arg(tr("Protected: this achievement can't be changed with Steam Achievement Manager."));
            }
            return tip;
        }
        case Qt::TextAlignmentRole:
            if (index.column() == UnlockTimeColumn)
            {
                return QVariant::fromValue(Qt::AlignLeft | Qt::AlignVCenter);
            }
            return {};
        case IdRole: return achievement.definition.id;
        case DescriptionRole: return description(achievement);
        case IconRole: return m_images->image(iconUrl(achievement));
        case AchievedRole: return achievement.achieved;
        case ProtectedRole: return achievement.isProtected();
        case HiddenRole: return achievement.definition.isHidden;
        case ModifiedRole: return achievement.isModified();
        default: return {};
    }
}

QVariant AchievementModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
    {
        return {};
    }
    switch (section)
    {
        case NameColumn: return tr("Achievement");
        case UnlockTimeColumn: return tr("Unlocked on");
        default: return {};
    }
}

Qt::ItemFlags AchievementModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
    {
        return Qt::NoItemFlags;
    }
    Qt::ItemFlags result = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == NameColumn)
    {
        result |= Qt::ItemIsUserCheckable;
    }
    return result;
}

bool AchievementModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || role != Qt::CheckStateRole || index.column() != NameColumn)
    {
        return false;
    }
    Achievement &achievement = m_achievements[index.row()];
    const bool checked = value.value<Qt::CheckState>() == Qt::Checked;
    if (checked == achievement.checked)
    {
        return true;
    }
    if (achievement.isProtected())
    {
        emit protectedChangeRejected(displayName(achievement));
        return false;
    }
    achievement.checked = checked;
    emitRowChanged(index.row());
    emit changesUpdated();
    return true;
}

void AchievementModel::setAchievements(QList<Achievement> achievements)
{
    beginResetModel();
    m_achievements = std::move(achievements);
    endResetModel();
    emit changesUpdated();
}

void AchievementModel::setLanguage(const QString &language)
{
    if (language == m_language)
    {
        return;
    }
    m_language = language;
    if (!m_achievements.isEmpty())
    {
        emit dataChanged(index(0, 0), index(rowCount() - 1, ColumnCount - 1));
    }
}

int AchievementModel::setChecked(const QList<int> &rows, bool checked)
{
    int skipped = 0;
    bool changed = false;
    for (const int row : rows)
    {
        Achievement &achievement = m_achievements[row];
        if (achievement.checked == checked)
        {
            continue;
        }
        if (achievement.isProtected())
        {
            ++skipped;
            continue;
        }
        achievement.checked = checked;
        emitRowChanged(row);
        changed = true;
    }
    if (changed)
    {
        emit changesUpdated();
    }
    return skipped;
}

int AchievementModel::invert(const QList<int> &rows)
{
    int skipped = 0;
    bool changed = false;
    for (const int row : rows)
    {
        Achievement &achievement = m_achievements[row];
        if (achievement.isProtected())
        {
            ++skipped;
            continue;
        }
        achievement.checked = !achievement.checked;
        emitRowChanged(row);
        changed = true;
    }
    if (changed)
    {
        emit changesUpdated();
    }
    return skipped;
}

int AchievementModel::modifiedCount() const
{
    return static_cast<int>(std::count_if(m_achievements.cbegin(), m_achievements.cend(), [](const auto &a) {
        return a.isModified();
    }));
}

int AchievementModel::checkedCount() const
{
    return static_cast<int>(std::count_if(m_achievements.cbegin(), m_achievements.cend(), [](const auto &a) {
        return a.checked;
    }));
}

void AchievementModel::onImageReady(const QString &url)
{
    for (int row = 0; row < m_achievements.size(); ++row)
    {
        if (iconUrl(m_achievements.at(row)) == url)
        {
            const QModelIndex changed = index(row, NameColumn);
            emit dataChanged(changed, changed, {IconRole});
        }
    }
}

void AchievementModel::emitRowChanged(int row)
{
    emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
}

AchievementFilterModel::AchievementFilterModel(QObject *parent) : QSortFilterProxyModel(parent)
{
}

void AchievementFilterModel::setSearchText(const QString &text)
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

void AchievementFilterModel::setState(State state)
{
    if (state == m_state)
    {
        return;
    }
    beginFilterChange();
    m_state = state;
    endFilterChange(Direction::Rows);
}

bool AchievementFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QModelIndex index = sourceModel()->index(sourceRow, AchievementModel::NameColumn, sourceParent);

    switch (m_state)
    {
        case State::All: break;
        case State::Locked:
            if (index.data(AchievementModel::AchievedRole).toBool())
            {
                return false;
            }
            break;
        case State::Unlocked:
            if (!index.data(AchievementModel::AchievedRole).toBool())
            {
                return false;
            }
            break;
        case State::Modified:
            if (!index.data(AchievementModel::ModifiedRole).toBool())
            {
                return false;
            }
            break;
    }

    if (m_search.isEmpty())
    {
        return true;
    }
    return index.data(Qt::DisplayRole).toString().contains(m_search, Qt::CaseInsensitive) ||
           index.data(AchievementModel::DescriptionRole).toString().contains(m_search, Qt::CaseInsensitive) ||
           index.data(AchievementModel::IdRole).toString().contains(m_search, Qt::CaseInsensitive);
}
} // namespace sam
