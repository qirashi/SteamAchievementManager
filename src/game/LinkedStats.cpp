/* Copyright (c) 2026 Qirashi
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
 */

#include "LinkedStats.h"

#include "AchievementModel.h"
#include "StatsModel.h"

namespace sam
{
namespace
{
double valueOf(const Stat &stat)
{
    return stat.isInteger() ? stat.intValue : stat.floatValue;
}
} // namespace

void syncLinkedStats(const QList<Achievement> &achievements,
                     StatsModel &stats,
                     QHash<QString, double> &raised,
                     bool enabled)
{
    // The value each stat should reach.
    QHash<QString, double> targets;
    if (enabled)
    {
        for (const auto &achievement : achievements)
        {
            const AchievementDefinition &definition = achievement.definition;
            if (achievement.checked && !achievement.achieved && !definition.progressStat.isEmpty())
            {
                double &target = targets[definition.progressStat];
                target = qMax(target, definition.progressMax);
            }
        }
    }

    for (int row = 0; row < stats.stats().size(); ++row)
    {
        // Copies: raising or reverting changes the list.
        const QString id = stats.stats().at(row).definition.id;
        const double current = valueOf(stats.stats().at(row));

        auto previous = raised.find(id);
        if (previous != raised.end() && *previous != current)
        {
            raised.erase(previous); // changed by hand since: it's the user's now
            previous = raised.end();
        }

        const auto target = targets.constFind(id);
        const bool wanted = target != targets.cend();
        if (previous != raised.end() && (!wanted || *target != *previous))
        {
            stats.revert(row);
            raised.remove(id);
        }
        if (wanted && stats.canRaise(row, *target))
        {
            stats.raise(row, *target);
            raised.insert(id, valueOf(stats.stats().at(row)));
        }
    }
}
} // namespace sam
