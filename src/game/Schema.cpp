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

#include "Schema.h"

#include "KeyValue.h"
#include "SteamClient.h"

#include <QDir>
#include <QFileInfo>

#include <limits>

namespace sam
{
namespace
{
// Mirrors SAM.API.Types.UserStatType.
enum class StatType
{
    Invalid = 0,
    Integer = 1,
    Float = 2,
    AverageRate = 3,
    Achievements = 4,
    GroupAchievements = 5,
};

StatType parseTypeName(const QString &name)
{
    const QString lower = name.trimmed().toLower();
    if (lower == QLatin1StringView("int") || lower == QLatin1StringView("integer"))
    {
        return StatType::Integer;
    }
    if (lower == QLatin1StringView("float"))
    {
        return StatType::Float;
    }
    if (lower == QLatin1StringView("avgrate") || lower == QLatin1StringView("averagerate"))
    {
        return StatType::AverageRate;
    }
    if (lower == QLatin1StringView("achievements"))
    {
        return StatType::Achievements;
    }
    if (lower == QLatin1StringView("groupachievements"))
    {
        return StatType::GroupAchievements;
    }
    bool ok = false;
    const int value = lower.toInt(&ok);
    return ok ? static_cast<StatType>(value) : StatType::Invalid;
}

StatType statType(const KeyValue &stat)
{
    // Schema in the new format?
    const KeyValue &typeNode = stat["type"];
    StatType type = StatType::Invalid;
    if (typeNode.valid && typeNode.type == KeyValue::Type::String)
    {
        type = parseTypeName(typeNode.asString());
    }

    // Schema in the old format?
    if (type == StatType::Invalid)
    {
        const KeyValue &typeIntNode = stat["type_int"];
        type = static_cast<StatType>(typeIntNode.valid ? typeIntNode.asInteger(0) : typeNode.asInteger(0));
    }
    return type;
}

void addLanguages(QStringList &languages, const LocalizedString &text)
{
    text.collectLanguages(languages);
}
} // namespace

QString LocalizedString::get(const QString &language, const QString &fallback) const
{
    QString value = values.value(language.toLower());
    if (!value.isEmpty())
    {
        return value;
    }
    value = values.value(QStringLiteral("english"));
    if (!value.isEmpty())
    {
        return value;
    }
    if (!plain.isEmpty())
    {
        return plain;
    }
    return fallback;
}

void LocalizedString::collectLanguages(QStringList &languages) const
{
    for (auto it = values.cbegin(); it != values.cend(); ++it)
    {
        if (!it.value().isEmpty() && !languages.contains(it.key()))
        {
            languages.append(it.key());
        }
    }
}

LocalizedString LocalizedString::from(const KeyValue &node)
{
    LocalizedString result;
    if (!node.valid)
    {
        return result;
    }
    if (node.type != KeyValue::Type::None)
    {
        result.plain = node.asString();
        return result;
    }
    for (const auto &child : node.children)
    {
        // "token" is the localization key, not a language.
        if (!child.valid || child.name.compare(QLatin1StringView("token"), Qt::CaseInsensitive) == 0)
        {
            continue;
        }
        const QString text = child.asString();
        if (!text.isEmpty())
        {
            result.values.insert(child.name.toLower(), text);
        }
    }
    return result;
}

std::optional<Schema> Schema::load(uint32_t appId)
{
    const QString steamPath = steam::installPath();
    if (steamPath.isEmpty())
    {
        return std::nullopt;
    }
    const QString path =
        QDir(steamPath).filePath(QStringLiteral("appcache/stats/UserGameStatsSchema_%1.bin").arg(appId));
    if (!QFileInfo::exists(path))
    {
        return std::nullopt;
    }

    const auto root = KeyValue::loadBinary(path);
    if (!root)
    {
        return std::nullopt;
    }

    const KeyValue &game = (*root)[QString::number(appId)];
    const KeyValue &stats = game["stats"];
    if (!stats.valid)
    {
        return std::nullopt;
    }

    Schema schema;
    schema.gameName = game["gamename"].asString();

    for (const auto &stat : stats.children)
    {
        if (!stat.valid)
        {
            continue;
        }

        switch (const StatType type = statType(stat))
        {
            case StatType::Invalid: break;

            case StatType::Integer:
            case StatType::Float:
            case StatType::AverageRate: {
                StatDefinition definition;
                definition.id = stat["name"].asString();
                definition.displayName = LocalizedString::from(stat["display"]["name"]);
                definition.incrementOnly = stat["incrementonly"].asBoolean(false);
                definition.permission = stat["permission"].asInteger(0);
                if (type == StatType::Integer)
                {
                    definition.kind = StatDefinition::Kind::Integer;
                    definition.minValue = stat["min"].asInteger(std::numeric_limits<int>::min());
                    definition.maxValue = stat["max"].asInteger(std::numeric_limits<int>::max());
                    definition.maxChange = stat["maxchange"].asInteger(0);
                    definition.defaultValue = stat["default"].asInteger(0);
                    definition.setByTrustedGameServer = stat["bSetByTrustedGS"].asBoolean(false);
                }
                else
                {
                    definition.kind =
                        type == StatType::Float ? StatDefinition::Kind::Float : StatDefinition::Kind::AverageRate;
                    definition.minValue = stat["min"].asFloat(std::numeric_limits<float>::lowest());
                    definition.maxValue = stat["max"].asFloat(std::numeric_limits<float>::max());
                    definition.maxChange = stat["maxchange"].asFloat(0.0f);
                    definition.defaultValue = stat["default"].asFloat(0.0f);
                }
                addLanguages(schema.languages, definition.displayName);
                schema.stats.append(std::move(definition));
                break;
            }

            case StatType::Achievements:
            case StatType::GroupAchievements: {
                for (const auto &bits : stat.children)
                {
                    if (bits.name.compare(QLatin1StringView("bits"), Qt::CaseInsensitive) != 0 || !bits.valid)
                    {
                        continue;
                    }
                    for (const auto &bit : bits.children)
                    {
                        const KeyValue &display = bit["display"];
                        AchievementDefinition definition;
                        definition.id = bit["name"].asString();
                        definition.name = LocalizedString::from(display["name"]);
                        definition.description = LocalizedString::from(display["desc"]);
                        definition.iconNormal = display["icon"].asString();
                        definition.iconLocked = display["icon_gray"].asString();
                        definition.isHidden = display["hidden"].asBoolean(false);
                        definition.permission = bit["permission"].asInteger(0);
                        // "progress": {"min_val", "max_val", "value": {"operation": "statvalue",
                        // "operand1": <stat>}}; other operations aren't a single stat.
                        const KeyValue &progress = bit["progress"];
                        const KeyValue &value = progress["value"];
                        if (progress.valid && value["operation"].asString().compare(QLatin1StringView("statvalue"),
                                                                                    Qt::CaseInsensitive) == 0)
                        {
                            definition.progressStat = value["operand1"].asString();
                            definition.progressMin = progress["min_val"].asFloat(0.0f);
                            definition.progressMax = progress["max_val"].asFloat(0.0f);
                            if (definition.progressMax <= definition.progressMin)
                            {
                                definition.progressStat.clear();
                            }
                        }
                        addLanguages(schema.languages, definition.name);
                        addLanguages(schema.languages, definition.description);
                        schema.achievements.append(std::move(definition));
                    }
                }
                break;
            }

            default:
                // Unknown stat type; skip it rather than failing the whole schema.
                break;
        }
    }

    return schema;
}
} // namespace sam
