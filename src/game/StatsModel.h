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

#include "Schema.h"

#include <QAbstractTableModel>
#include <QList>

namespace sam
{
struct Stat
{
    StatDefinition definition;
    int32_t intValue = 0;
    int32_t intOriginal = 0;
    float floatValue = 0;
    float floatOriginal = 0;

    bool isInteger() const { return definition.kind == StatDefinition::Kind::Integer; }
    bool isProtected() const { return (definition.permission & 2) != 0; }
    bool isModified() const { return isInteger() ? intValue != intOriginal : floatValue != floatOriginal; }
};

class StatsModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column
    {
        NameColumn,
        ValueColumn,
        FlagsColumn,
        IdColumn,
        ColumnCount,
    };

    enum Role
    {
        ModifiedRole = Qt::UserRole + 1,
        ProtectedRole,
    };

    explicit StatsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;

    void setStats(QList<Stat> stats);
    const QList<Stat> &stats() const { return m_stats; }
    void setLanguage(const QString &language);
    void setEditingEnabled(bool enabled);
    bool isEditingEnabled() const { return m_editingEnabled; }

    int modifiedCount() const;
    void revert(int row);

    // Whether the stat in `row` may be raised to `value` by the program: a
    // plain integer or float stat with no permission flags (protected, set by a
    // game server), `value` above the current one, within the stat's range and
    // its largest change per store.
    bool canRaise(int row, double value) const;
    // Sets the value without the "I understand" check of the statistics page;
    // only after canRaise().
    void raise(int row, double value);

signals:
    void changesUpdated();
    void editRejected(const QString &message);

private:
    QString valueText(const Stat &stat) const;
    QString flagsText(const Stat &stat) const;

    QList<Stat> m_stats;
    QString m_language = QStringLiteral("english");
    bool m_editingEnabled = false;
};
} // namespace sam
