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

namespace sam
{
struct PlayTime
{
    int minutes = 0;       // total time in the game
    qint64 lastPlayed = 0; // seconds since the epoch, 0 if never
};

// What Steam keeps about the user's games in
// <Steam>/userdata/<account id>/config/localconfig.vdf: the playtime and the
// last time each game was played. Steam updates the file while it runs, so the
// numbers can be a little behind those on the Steam servers.
QHash<uint32_t, PlayTime> loadPlayTimes(uint64_t steamId);

// "12 h 5 min", "3 h", "40 min".
QString formatPlaytime(int minutes);
} // namespace sam
