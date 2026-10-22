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

#include <QCollator>
#include <QLocale>

#include <algorithm>

namespace sam
{
// Model index layout: top-level items have internal id 0; children of
// section N have internal id N + 1.

AchievementModel::AchievementModel(uint32_t appId, ImageCache *images, QObject *parent)
    : QAbstractItemModel(parent)
    , m_appId(appId)
    , m_images(images)
{
    connect(m_images, &ImageCache::imageReady, this, &AchievementModel::onImageReady);
}

QModelIndex AchievementModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
    {
        return {};
    }
    return createIndex(row, column, parent.isValid() ? static_cast<quintptr>(parent.row()) + 1 : 0);
}

QModelIndex AchievementModel::parent(const QModelIndex &child) const
{
    if (!child.isValid() || child.internalId() == 0)
    {
        return {};
    }
    return createIndex(static_cast<int>(child.internalId() - 1), 0, quintptr(0));
}

int AchievementModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid())
    {
        return static_cast<int>(isGrouped() ? m_sections.size() : m_achievements.size());
    }
    if (isSection(parent) && parent.column() == 0)
    {
        return static_cast<int>(m_sections.at(parent.row()).members.size());
    }
    return 0;
}

int AchievementModel::columnCount(const QModelIndex &) const
{
    return ColumnCount;
}

bool AchievementModel::isSection(const QModelIndex &index) const
{
    return index.isValid() && isGrouped() && index.internalId() == 0;
}

int AchievementModel::achievementIndex(const QModelIndex &index) const
{
    if (!index.isValid() || index.model() != this)
    {
        return -1;
    }
    if (!isGrouped())
    {
        return index.row() < m_achievements.size() ? index.row() : -1;
    }
    if (index.internalId() == 0)
    {
        return -1;
    }
    const int section = static_cast<int>(index.internalId() - 1);
    if (section >= m_sections.size())
    {
        return -1;
    }
    return m_sections.at(section).members.value(index.row(), -1);
}

QModelIndex AchievementModel::indexOf(int achievement, int column) const
{
    if (!isGrouped())
    {
        return createIndex(achievement, column, quintptr(0));
    }
    const Location &location = m_locations.at(achievement);
    return createIndex(location.row, column, static_cast<quintptr>(location.section) + 1);
}

QList<int> AchievementModel::sectionMembers(int section) const
{
    return m_sections.value(section).members;
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

QVariant AchievementModel::sectionData(const Section &section, int column, int role) const
{
    switch (role)
    {
        case Qt::DisplayRole: return column == NameColumn ? section.name : QVariant();
        case Qt::ToolTipRole: {
            QString tip = QStringLiteral("<b>%1</b>").arg(section.name.toHtmlEscaped());
            if (section.dlcAppId != 0)
            {
                tip += QStringLiteral("<br><small>DLC, App ID %1</small>").arg(section.dlcAppId);
            }
            return tip;
        }
        case SectionRole: return true;
        case SectionDlcRole: return section.dlcAppId;
        case SectionCheckedRole:
            return static_cast<int>(std::count_if(section.members.cbegin(), section.members.cend(), [this](int i) {
                return m_achievements.at(i).checked;
            }));
        case SectionTotalRole: return static_cast<int>(section.members.size());
        case ModifiedRole:
            return std::any_of(section.members.cbegin(), section.members.cend(), [this](int i) {
                return m_achievements.at(i).isModified();
            });
        default: return {};
    }
}

QVariant AchievementModel::data(const QModelIndex &index, int role) const
{
    if (isSection(index))
    {
        return index.row() < m_sections.size() ? sectionData(m_sections.at(index.row()), index.column(), role)
                                               : QVariant();
    }
    const int position = achievementIndex(index);
    if (position < 0)
    {
        return {};
    }

    const Achievement &achievement = m_achievements.at(position);
    switch (role)
    {
        case Qt::DisplayRole:
            if (index.column() == NameColumn)
            {
                return displayName(achievement);
            }
            if (index.column() == RarityColumn)
            {
                const auto it = m_rarity.constFind(achievement.definition.id);
                return it != m_rarity.cend() ? QStringLiteral("%1 %").arg(QLocale().toString(*it, 'f', 1)) : QVariant();
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
            if (const auto it = m_rarity.constFind(achievement.definition.id); it != m_rarity.cend())
            {
                tip += QStringLiteral("<br>%1").arg(
                    tr("%1 % of players have this achievement.").arg(QLocale().toString(*it, 'f', 1)));
            }
            if (const auto it = m_statValues.constFind(achievement.definition.progressStat);
                !achievement.definition.progressStat.isEmpty() && it != m_statValues.cend())
            {
                tip += QStringLiteral("<br>%1").arg(
                    tr("Progress: %1 of %2 (stat %3)")
                        .arg(QLocale().toString(*it, 'g', 10),
                             QLocale().toString(achievement.definition.progressMax, 'g', 10),
                             achievement.definition.progressStat.toHtmlEscaped()));
                const bool reached = *it >= achievement.definition.progressMax;
                const QString blocker = m_raiseBlockers.value(achievement.definition.id);
                if (!achievement.achieved && achievement.checked && !reached && !blocker.isEmpty())
                {
                    tip += QStringLiteral("<br><small>%1</small>")
                               .arg(tr("Unlocking it leaves the stat where it is, and the program can't raise "
                                       "the stat: %1.")
                                        .arg(blocker.toHtmlEscaped()));
                }
                else if (!achievement.achieved && achievement.checked && !reached)
                {
                    tip += QStringLiteral("<br><small>%1</small>")
                               .arg(tr("Unlocking it leaves the stat where it is. To raise the stat too, turn on "
                                       "\"Raise the linked stat\" in the ☰ menu of the game list."));
                }
                else if (!achievement.achieved && !achievement.checked && reached)
                {
                    tip += QStringLiteral("<br><small>%1</small>")
                               .arg(tr("The stat has reached the value: Steam unlocks the achievement by itself "
                                       "when the changes are saved."));
                }
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
            if (index.column() == RarityColumn)
            {
                return QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
            }
            return {};
        case IdRole: return achievement.definition.id;
        case DescriptionRole: return description(achievement);
        case IconRole: return m_images->image(iconUrl(achievement));
        case AchievedRole: return achievement.achieved;
        case ProtectedRole: return achievement.isProtected();
        case HiddenRole: return achievement.definition.isHidden;
        case ModifiedRole: return achievement.isModified();
        case RarityRole:
            return m_rarity.value(achievement.definition.id, -1.0) >= 0
                       ? QVariant(m_rarity.value(achievement.definition.id))
                       : QVariant();
        case UnlockTimeRole: return achievement.achieved ? achievement.unlockTime : QDateTime();
        case ProgressValueRole: {
            const QString &stat = achievement.definition.progressStat;
            const auto it = m_statValues.constFind(stat);
            return stat.isEmpty() || it == m_statValues.cend() ? QVariant() : QVariant(*it);
        }
        case ProgressMaxRole: return achievement.definition.progressMax;
        case ProgressStatRole: return achievement.definition.progressStat;
        case ProgressBlockerRole: return m_raiseBlockers.value(achievement.definition.id);
        case SectionRole: return false;
        default: return {};
    }
}

QVariant AchievementModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::ToolTipRole && section == RarityColumn)
    {
        return tr("Share of Steam players who have the achievement. Click a column to sort by it.");
    }
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
    {
        return {};
    }
    switch (section)
    {
        case NameColumn: return tr("Achievement");
        case RarityColumn: return tr("Rarity");
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
    if (isSection(index))
    {
        return Qt::ItemIsEnabled;
    }
    Qt::ItemFlags result = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemNeverHasChildren;
    if (index.column() == NameColumn)
    {
        result |= Qt::ItemIsUserCheckable;
    }
    return result;
}

bool AchievementModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    const int position = achievementIndex(index);
    if (position < 0 || role != Qt::CheckStateRole || index.column() != NameColumn)
    {
        return false;
    }
    Achievement &achievement = m_achievements[position];
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
    emitRowChanged(position);
    emit changesUpdated();
    return true;
}

void AchievementModel::setAchievements(QList<Achievement> achievements)
{
    beginResetModel();
    m_achievements = std::move(achievements);
    rebuildSections();
    endResetModel();
    emit changesUpdated();
}

void AchievementModel::setGroups(QList<AchievementGroup> groups)
{
    beginResetModel();
    m_groups = std::move(groups);
    rebuildSections();
    endResetModel();
}

void AchievementModel::rebuildSections()
{
    m_sections.clear();
    m_locations.clear();
    if (m_groups.isEmpty() || m_achievements.isEmpty())
    {
        return;
    }

    QHash<QString, int> byId;
    for (int i = 0; i < m_achievements.size(); ++i)
    {
        byId.insert(m_achievements.at(i).definition.id, i);
    }

    // Section 0 is the base game: everything no group claims.
    QList<Section> sections;
    sections.append({tr("Base game"), 0, {}});
    QList<int> sectionOf(m_achievements.size(), 0);
    for (const auto &group : std::as_const(m_groups))
    {
        const int section = static_cast<int>(sections.size());
        sections.append({group.name, group.dlcAppId, {}});
        for (const auto &id : group.achievementIds)
        {
            const auto it = byId.constFind(id);
            if (it != byId.cend() && sectionOf[*it] == 0)
            {
                sectionOf[*it] = section;
            }
        }
    }
    for (int i = 0; i < m_achievements.size(); ++i)
    {
        sections[sectionOf[i]].members.append(i);
    }
    sections.removeIf([](const Section &section) {
        return section.members.isEmpty();
    });
    // A single section adds nothing over the flat list.
    if (sections.size() < 2)
    {
        return;
    }

    m_sections = std::move(sections);
    m_locations.resize(m_achievements.size());
    for (int s = 0; s < m_sections.size(); ++s)
    {
        const auto &members = m_sections.at(s).members;
        for (int row = 0; row < members.size(); ++row)
        {
            m_locations[members.at(row)] = {s, row};
        }
    }
}

void AchievementModel::setLanguage(const QString &language)
{
    if (language == m_language)
    {
        return;
    }
    m_language = language;
    // Section names don't depend on the achievement language.
    emitAllChanged();
}

void AchievementModel::setRarity(QHash<QString, double> rarity)
{
    m_rarity = std::move(rarity);
    emitAllChanged();
}

void AchievementModel::setStatValues(QHash<QString, double> values, QHash<QString, QString> blockers)
{
    m_statValues = std::move(values);
    m_raiseBlockers = std::move(blockers);
    emitAllChanged();
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

int AchievementModel::revert(const QList<int> &rows)
{
    bool changed = false;
    for (const int row : rows)
    {
        Achievement &achievement = m_achievements[row];
        if (achievement.isModified())
        {
            achievement.checked = achievement.achieved;
            emitRowChanged(row);
            changed = true;
        }
    }
    if (changed)
    {
        emit changesUpdated();
    }
    return 0; // protected achievements are never modified
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
    for (int i = 0; i < m_achievements.size(); ++i)
    {
        if (iconUrl(m_achievements.at(i)) == url)
        {
            const QModelIndex changed = indexOf(i, NameColumn);
            emit dataChanged(changed, changed, {IconRole});
        }
    }
}

void AchievementModel::emitRowChanged(int achievement)
{
    emit dataChanged(indexOf(achievement, 0), indexOf(achievement, ColumnCount - 1));
    if (isGrouped())
    {
        const int section = m_locations.at(achievement).section;
        emit dataChanged(index(section, 0), index(section, ColumnCount - 1));
    }
}

void AchievementModel::emitAllChanged()
{
    if (!isGrouped())
    {
        if (!m_achievements.isEmpty())
        {
            emit dataChanged(index(0, 0), index(rowCount() - 1, ColumnCount - 1));
        }
        return;
    }
    for (int s = 0; s < m_sections.size(); ++s)
    {
        const QModelIndex parent = index(s, 0);
        emit dataChanged(index(0, 0, parent), index(rowCount(parent) - 1, ColumnCount - 1, parent));
    }
}

AchievementFilterModel::AchievementFilterModel(QObject *parent) : QSortFilterProxyModel(parent)
{
    // A section is shown when any of its achievements is.
    setRecursiveFilteringEnabled(true);
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
    if (index.data(AchievementModel::SectionRole).toBool())
    {
        return false;
    }

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
bool AchievementFilterModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    // The view reverses the result for a descending sort; sections undo that.
    if (left.data(AchievementModel::SectionRole).toBool())
    {
        return (left.row() < right.row()) == (sortOrder() == Qt::AscendingOrder);
    }

    switch (left.column())
    {
        case AchievementModel::NameColumn: {
            static const QCollator collator = [] {
                QCollator result;
                result.setCaseSensitivity(Qt::CaseInsensitive);
                result.setNumericMode(true);
                return result;
            }();
            const int order =
                collator.compare(left.data(Qt::DisplayRole).toString(), right.data(Qt::DisplayRole).toString());
            if (order != 0)
            {
                return order < 0;
            }
            break;
        }
        case AchievementModel::RarityColumn: {
            // Achievements without data go after the others.
            const QVariant a = left.data(AchievementModel::RarityRole);
            const QVariant b = right.data(AchievementModel::RarityRole);
            const double x = a.isValid() ? a.toDouble() : 1000.0;
            const double y = b.isValid() ? b.toDouble() : 1000.0;
            if (x != y)
            {
                return x < y;
            }
            break;
        }
        case AchievementModel::UnlockTimeColumn: {
            const QDateTime a = left.data(AchievementModel::UnlockTimeRole).toDateTime();
            const QDateTime b = right.data(AchievementModel::UnlockTimeRole).toDateTime();
            if (a != b)
            {
                // Locked achievements (no time) go after the unlocked ones.
                return a.isValid() && (!b.isValid() || a < b);
            }
            break;
        }
        default: break;
    }
    return left.row() < right.row();
}
} // namespace sam
