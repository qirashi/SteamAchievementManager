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
#include <QString>

#include <cstdint>
#include <functional>

class QByteArray;
class QObject;

namespace sam
{
// How many players have each achievement of `appId`, in percent, keyed by
// achievement ID: Steam's public GetGlobalAchievementPercentagesForApp. Steam
// says how long the answer stays valid (a few days), so it mostly comes from
// the disk cache; offline, a stale copy is used. An empty hash means no data.
void requestAchievementRarity(uint32_t appId, QObject *context, std::function<void(QHash<QString, double>)> done);

QHash<QString, double> parseAchievementRarity(const QByteArray &json);
} // namespace sam
