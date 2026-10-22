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

#include <QList>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>

class QObject;

namespace sam
{
// A set of achievements added by a DLC or a game update. Steam itself doesn't
// record this, so the data comes from SteamHunters (steamhunters.com).
struct AchievementGroup
{
    QString name;
    uint32_t dlcAppId = 0; // 0 for updates and other non-DLC groups
    QStringList achievementIds;
};

// Downloads the groups of `appId`. Falls back to the disk cache when offline.
// `done` is called on the context's thread; an empty list means no data.
void requestAchievementGroups(uint32_t appId,
                              const QString &gameName,
                              QObject *context,
                              std::function<void(QList<AchievementGroup>)> done);

// Parses a GetAchievementGroups/v1 response. `gameName` is stripped from DLC
// names ("Game - Expansion" becomes "Expansion").
QList<AchievementGroup> parseAchievementGroups(const QByteArray &json, const QString &gameName);
} // namespace sam
