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

#include <QAbstractItemModel>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QSortFilterProxyModel>

#include <cstdint>

namespace sam
{
class ImageCache;

namespace steam
{
class Client;
}

struct Achievement
{
    AchievementDefinition definition;
    bool achieved = false; // state reported by Steam
    bool checked = false;  // state the user wants
    QDateTime unlockTime;

    bool isProtected() const { return (definition.permission & 3) != 0; }
    bool isModified() const { return achieved != checked; }
};

// Lists achievements either flat or, when groups are set, as a two-level tree:
// sections (base game, each DLC/update) with their achievements as children.
//
// Achievements are addressed by their position in achievements(); use
// achievementIndex() to get it from a model index.
class AchievementModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum Column
    {
        NameColumn,
        UnlockTimeColumn,
        ColumnCount,
    };

    enum Role
    {
        IdRole = Qt::UserRole + 1,
        DescriptionRole,
        IconRole,
        AchievedRole,
        ProtectedRole,
        HiddenRole,
        ModifiedRole,
        // Section rows only.
        SectionRole,
        SectionDlcRole,
        SectionCheckedRole,
        SectionTotalRole,
    };

    AchievementModel(uint32_t appId, ImageCache *images, QObject *parent = nullptr);

    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;

    void setAchievements(QList<Achievement> achievements);
    // Groups achievements into sections; an empty list shows a flat list.
    void setGroups(QList<AchievementGroup> groups);
    void setLanguage(const QString &language);
    const QList<Achievement> &achievements() const { return m_achievements; }

    bool isGrouped() const { return !m_sections.isEmpty(); }
    bool isSection(const QModelIndex &index) const;
    // Position in achievements(), or -1 for sections and invalid indexes.
    int achievementIndex(const QModelIndex &index) const;
    QList<int> sectionMembers(int section) const;

    // Sets/toggles the state of the given achievements; protected ones are
    // skipped. Returns how many protected achievements were skipped.
    int setChecked(const QList<int> &rows, bool checked);
    int invert(const QList<int> &rows);

    int achievementCount() const { return static_cast<int>(m_achievements.size()); }
    int modifiedCount() const;
    int checkedCount() const;

signals:
    void changesUpdated();
    void protectedChangeRejected(const QString &name);

private:
    struct Section
    {
        QString name;
        uint32_t dlcAppId = 0;
        QList<int> members;
    };

    struct Location
    {
        int section = -1;
        int row = -1;
    };

    QString displayName(const Achievement &achievement) const;
    QString description(const Achievement &achievement) const;
    QString iconUrl(const Achievement &achievement) const;
    QVariant sectionData(const Section &section, int column, int role) const;
    QModelIndex indexOf(int achievement, int column) const;
    void rebuildSections();
    void onImageReady(const QString &url);
    void emitRowChanged(int achievement);
    void emitAllChanged();

    uint32_t m_appId;
    ImageCache *m_images;
    QString m_language = QStringLiteral("english");
    QList<Achievement> m_achievements;
    QList<AchievementGroup> m_groups;
    QList<Section> m_sections;
    QList<Location> m_locations; // per achievement, when grouped
};

class AchievementFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    enum class State
    {
        All,
        Locked,
        Unlocked,
        Modified,
    };

    explicit AchievementFilterModel(QObject *parent = nullptr);

    void setSearchText(const QString &text);
    void setState(State state);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    QString m_search;
    State m_state = State::All;
};
} // namespace sam
