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

#include <QString>
#include <QStringList>

namespace sam::languages
{
// Human readable (native) name for a Steam API language code such as
// "russian" or "schinese". Unknown codes are returned capitalized.
QString displayName(const QString &code);

// Every Steam language code we know about.
QStringList allCodes();

// Sorts language codes by display name, keeping English first.
QStringList sorted(QStringList codes);

// Best guess at the Steam language matching the system locale.
QString fromSystemLocale();
} // namespace sam::languages
