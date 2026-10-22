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

#include "StatsModel.h"

#include <QBrush>
#include <QFont>
#include <QLocale>

#include <algorithm>
#include <cmath>
#include <limits>

namespace sam
{
namespace
{
QString formatFloat(float value)
{
    return QLocale().toString(static_cast<double>(value), 'g', std::numeric_limits<float>::max_digits10);
}

bool parseFloat(const QString &text, float *value)
{
    bool ok = false;
    float result = QLocale().toFloat(text.trimmed(), &ok);
    if (!ok)
    {
        result = QLocale::c().toFloat(text.trimmed(), &ok);
    }
    if (ok && std::isfinite(result))
    {
        *value = result;
        return true;
    }
    return false;
}

bool parseInt(const QString &text, int32_t *value)
{
    QString trimmed = text.trimmed();
    trimmed.remove(QLocale().groupSeparator());
    bool ok = false;
    const int result = QLocale().toInt(trimmed, &ok);
    if (!ok)
    {
        return false;
    }
    *value = result;
    return true;
}
} // namespace

StatsModel::StatsModel(QObject *parent) : QAbstractTableModel(parent)
{
}

int StatsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_stats.size());
}

int StatsModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QString StatsModel::valueText(const Stat &stat) const
{
    return stat.isInteger() ? QLocale().toString(stat.intValue) : formatFloat(stat.floatValue);
}

QString StatsModel::flagsText(const Stat &stat) const
{
    QStringList flags;
    if (stat.definition.incrementOnly)
    {
        flags << tr("increment only");
    }
    if (stat.isProtected())
    {
        flags << tr("protected");
    }
    if ((stat.definition.permission & ~2) != 0)
    {
        flags << tr("unknown permission");
    }
    if (stat.definition.kind == StatDefinition::Kind::AverageRate)
    {
        flags << tr("average rate");
    }
    return flags.join(QStringLiteral(", "));
}

QVariant StatsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_stats.size())
    {
        return {};
    }

    const Stat &stat = m_stats.at(index.row());
    switch (role)
    {
        case Qt::DisplayRole:
            switch (index.column())
            {
                case NameColumn: return stat.definition.displayName.get(m_language, stat.definition.id);
                case ValueColumn: return valueText(stat);
                case FlagsColumn: return flagsText(stat);
                case IdColumn: return stat.definition.id;
                default: return {};
            }
        case Qt::EditRole:
            if (index.column() == ValueColumn)
            {
                return stat.isInteger() ? QVariant(stat.intValue) : QVariant(formatFloat(stat.floatValue));
            }
            return data(index, Qt::DisplayRole);
        case Qt::ToolTipRole: {
            QString tip = QStringLiteral("<b>%1</b><br><small>ID: %2</small>")
                              .arg(stat.definition.displayName.get(m_language, stat.definition.id).toHtmlEscaped(),
                                   stat.definition.id.toHtmlEscaped());
            if (stat.isModified())
            {
                const QString original =
                    stat.isInteger() ? QLocale().toString(stat.intOriginal) : formatFloat(stat.floatOriginal);
                tip += QStringLiteral("<br>") + tr("Original value: %1").arg(original);
            }
            const bool hasMin = stat.isInteger() ? stat.definition.minValue > std::numeric_limits<int>::min()
                                                 : stat.definition.minValue > std::numeric_limits<float>::lowest();
            const bool hasMax = stat.isInteger() ? stat.definition.maxValue < std::numeric_limits<int>::max()
                                                 : stat.definition.maxValue < std::numeric_limits<float>::max();
            if (hasMin || hasMax)
            {
                tip += QStringLiteral("<br>") +
                       tr("Allowed range: %1 … %2")
                           .arg(hasMin ? QLocale().toString(stat.definition.minValue) : QStringLiteral("−∞"),
                                hasMax ? QLocale().toString(stat.definition.maxValue) : QStringLiteral("∞"));
            }
            return tip;
        }
        case Qt::FontRole:
            if (index.column() == ValueColumn && stat.isModified())
            {
                QFont font;
                font.setBold(true);
                return font;
            }
            return {};
        case Qt::TextAlignmentRole: return QVariant::fromValue(Qt::AlignLeft | Qt::AlignVCenter);
        case ModifiedRole: return stat.isModified();
        case ProtectedRole: return stat.isProtected();
        default: return {};
    }
}

QVariant StatsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
    {
        return {};
    }
    switch (section)
    {
        case NameColumn: return tr("Name");
        case ValueColumn: return tr("Value");
        case FlagsColumn: return tr("Flags");
        case IdColumn: return tr("ID");
        default: return {};
    }
}

Qt::ItemFlags StatsModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
    {
        return Qt::NoItemFlags;
    }
    Qt::ItemFlags result = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == ValueColumn && m_editingEnabled && !m_stats.at(index.row()).isProtected())
    {
        result |= Qt::ItemIsEditable;
    }
    return result;
}

bool StatsModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || role != Qt::EditRole || index.column() != ValueColumn || !m_editingEnabled)
    {
        return false;
    }

    Stat &stat = m_stats[index.row()];
    const QString name = stat.definition.displayName.get(m_language, stat.definition.id);
    if (stat.isProtected())
    {
        emit editRejected(tr("Stat \"%1\" is protected — you can't modify it.").arg(name));
        return false;
    }

    double newValue = 0;
    if (stat.isInteger())
    {
        int32_t parsed = 0;
        if (value.typeId() == QMetaType::Int)
        {
            parsed = value.toInt();
        }
        else if (!parseInt(value.toString(), &parsed))
        {
            emit editRejected(tr("\"%1\" is not a valid integer.").arg(value.toString()));
            return false;
        }
        newValue = parsed;
        if (newValue < stat.definition.minValue || newValue > stat.definition.maxValue)
        {
            emit editRejected(tr("Value for \"%1\" must be between %2 and %3.")
                                  .arg(name,
                                       QLocale().toString(stat.definition.minValue),
                                       QLocale().toString(stat.definition.maxValue)));
            return false;
        }
        stat.intValue = parsed;
    }
    else
    {
        float parsed = 0;
        if (!parseFloat(value.toString(), &parsed))
        {
            emit editRejected(tr("\"%1\" is not a valid number.").arg(value.toString()));
            return false;
        }
        newValue = parsed;
        if (newValue < stat.definition.minValue || newValue > stat.definition.maxValue)
        {
            emit editRejected(tr("Value for \"%1\" must be between %2 and %3.")
                                  .arg(name,
                                       QLocale().toString(stat.definition.minValue),
                                       QLocale().toString(stat.definition.maxValue)));
            return false;
        }
        stat.floatValue = parsed;
    }

    emit dataChanged(index.siblingAtColumn(0), index.siblingAtColumn(ColumnCount - 1));
    emit changesUpdated();
    return true;
}

void StatsModel::setStats(QList<Stat> stats)
{
    beginResetModel();
    m_stats = std::move(stats);
    endResetModel();
    emit changesUpdated();
}

void StatsModel::setLanguage(const QString &language)
{
    if (language == m_language)
    {
        return;
    }
    m_language = language;
    if (!m_stats.isEmpty())
    {
        emit dataChanged(index(0, NameColumn), index(rowCount() - 1, NameColumn));
    }
}

void StatsModel::setEditingEnabled(bool enabled)
{
    if (enabled == m_editingEnabled)
    {
        return;
    }
    m_editingEnabled = enabled;
    if (!m_stats.isEmpty())
    {
        emit dataChanged(index(0, ValueColumn), index(rowCount() - 1, ValueColumn));
    }
}

int StatsModel::modifiedCount() const
{
    return static_cast<int>(std::count_if(m_stats.cbegin(), m_stats.cend(), [](const Stat &stat) {
        return stat.isModified();
    }));
}

void StatsModel::revert(int row)
{
    if (row < 0 || row >= m_stats.size())
    {
        return;
    }
    Stat &stat = m_stats[row];
    stat.intValue = stat.intOriginal;
    stat.floatValue = stat.floatOriginal;
    emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
    emit changesUpdated();
}
} // namespace sam
