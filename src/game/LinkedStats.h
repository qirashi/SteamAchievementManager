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

#pragma once

#include <QHash>
#include <QList>
#include <QString>

namespace sam
{
struct Achievement;
class StatsModel;

// Keeps the stats that unlock achievements in step with them: while an
// achievement is to be unlocked, its stat goes up to the unlocking value (the
// highest one when several achievements share the stat); when none is any
// more, the stat goes back. `raised` remembers what was set, so a value
// changed by hand since is left alone. With `enabled` off, everything raised
// is put back. Stats StatsModel::canRaise() refuses are not touched.
void syncLinkedStats(const QList<Achievement> &achievements,
                     StatsModel &stats,
                     QHash<QString, double> &raised,
                     bool enabled);
} // namespace sam
