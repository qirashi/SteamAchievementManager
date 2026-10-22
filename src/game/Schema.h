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

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>

namespace sam
{
class KeyValue;

// A string from the schema, available in several languages.
struct LocalizedString
{
    QHash<QString, QString> values; // keyed by lower-case Steam language
    QString plain;                  // used when the node isn't localized

    // Falls back to English, then to the unlocalized value, then `fallback`.
    QString get(const QString &language, const QString &fallback = {}) const;
    void collectLanguages(QStringList &languages) const;
    static LocalizedString from(const KeyValue &node);
};

struct AchievementDefinition
{
    QString id;
    LocalizedString name;
    LocalizedString description;
    QString iconNormal;
    QString iconLocked;
    bool isHidden = false;
    int permission = 0;
};

struct StatDefinition
{
    enum class Kind
    {
        Integer,
        Float,
        AverageRate,
    };

    Kind kind = Kind::Integer;
    QString id;
    LocalizedString displayName;
    double minValue = 0;
    double maxValue = 0;
    double maxChange = 0;
    double defaultValue = 0;
    bool incrementOnly = false;
    bool setByTrustedGameServer = false;
    int permission = 0;
};

struct Schema
{
    QString gameName;
    QList<AchievementDefinition> achievements;
    QList<StatDefinition> stats;
    QStringList languages; // every language the schema has text for

    // Loads <Steam>/appcache/stats/UserGameStatsSchema_<appId>.bin.
    static std::optional<Schema> load(uint32_t appId);
};
} // namespace sam
