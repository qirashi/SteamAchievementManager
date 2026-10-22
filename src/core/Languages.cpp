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

#include "Languages.h"

#include <QCollator>
#include <QHash>
#include <QLocale>

#include <algorithm>

namespace sam::languages
{
namespace
{
struct Language
{
    const char *code;
    const char *name;
    QLocale::Language locale;
    QLocale::Territory territory;
};

// https://partner.steamgames.com/doc/store/localization/languages
const Language kLanguages[] = {
    {"arabic", "العربية", QLocale::Arabic, QLocale::AnyTerritory},
    {"bulgarian", "Български", QLocale::Bulgarian, QLocale::AnyTerritory},
    {"schinese", "简体中文", QLocale::Chinese, QLocale::China},
    {"tchinese", "繁體中文", QLocale::Chinese, QLocale::Taiwan},
    {"czech", "Čeština", QLocale::Czech, QLocale::AnyTerritory},
    {"danish", "Dansk", QLocale::Danish, QLocale::AnyTerritory},
    {"dutch", "Nederlands", QLocale::Dutch, QLocale::AnyTerritory},
    {"english", "English", QLocale::English, QLocale::AnyTerritory},
    {"finnish", "Suomi", QLocale::Finnish, QLocale::AnyTerritory},
    {"french", "Français", QLocale::French, QLocale::AnyTerritory},
    {"german", "Deutsch", QLocale::German, QLocale::AnyTerritory},
    {"greek", "Ελληνικά", QLocale::Greek, QLocale::AnyTerritory},
    {"hungarian", "Magyar", QLocale::Hungarian, QLocale::AnyTerritory},
    {"indonesian", "Bahasa Indonesia", QLocale::Indonesian, QLocale::AnyTerritory},
    {"italian", "Italiano", QLocale::Italian, QLocale::AnyTerritory},
    {"japanese", "日本語", QLocale::Japanese, QLocale::AnyTerritory},
    {"koreana", "한국어", QLocale::Korean, QLocale::AnyTerritory},
    {"korean", "한국어", QLocale::Korean, QLocale::AnyTerritory},
    {"norwegian", "Norsk", QLocale::NorwegianBokmal, QLocale::AnyTerritory},
    {"polish", "Polski", QLocale::Polish, QLocale::AnyTerritory},
    {"portuguese", "Português", QLocale::Portuguese, QLocale::Portugal},
    {"brazilian", "Português (Brasil)", QLocale::Portuguese, QLocale::Brazil},
    {"romanian", "Română", QLocale::Romanian, QLocale::AnyTerritory},
    {"russian", "Русский", QLocale::Russian, QLocale::AnyTerritory},
    {"spanish", "Español (España)", QLocale::Spanish, QLocale::Spain},
    {"latam", "Español (Latinoamérica)", QLocale::Spanish, QLocale::AnyTerritory},
    {"swedish", "Svenska", QLocale::Swedish, QLocale::AnyTerritory},
    {"thai", "ไทย", QLocale::Thai, QLocale::AnyTerritory},
    {"turkish", "Türkçe", QLocale::Turkish, QLocale::AnyTerritory},
    {"ukrainian", "Українська", QLocale::Ukrainian, QLocale::AnyTerritory},
    {"vietnamese", "Tiếng Việt", QLocale::Vietnamese, QLocale::AnyTerritory},
};

const Language *find(const QString &code)
{
    for (const auto &language : kLanguages)
    {
        if (code.compare(QLatin1StringView(language.code), Qt::CaseInsensitive) == 0)
        {
            return &language;
        }
    }
    return nullptr;
}
} // namespace

QString displayName(const QString &code)
{
    if (const auto *language = find(code))
    {
        return QString::fromUtf8(language->name);
    }
    if (code.isEmpty())
    {
        return code;
    }
    return code.left(1).toUpper() + code.mid(1);
}

QStringList allCodes()
{
    QStringList codes;
    for (const auto &language : kLanguages)
    {
        // "korean" is a legacy alias of "koreana".
        if (qstrcmp(language.code, "korean") != 0)
        {
            codes.append(QString::fromLatin1(language.code));
        }
    }
    return codes;
}

QStringList sorted(QStringList codes)
{
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(codes.begin(), codes.end(), [&](const QString &a, const QString &b) {
        const bool aEnglish = a.compare(QLatin1StringView("english"), Qt::CaseInsensitive) == 0;
        const bool bEnglish = b.compare(QLatin1StringView("english"), Qt::CaseInsensitive) == 0;
        if (aEnglish != bEnglish)
        {
            return aEnglish;
        }
        return collator.compare(displayName(a), displayName(b)) < 0;
    });
    return codes;
}

QString fromSystemLocale()
{
    const QLocale locale = QLocale::system();
    const Language *fallback = nullptr;
    for (const auto &language : kLanguages)
    {
        if (language.locale != locale.language())
        {
            continue;
        }
        if (language.territory == locale.territory())
        {
            return QString::fromLatin1(language.code);
        }
        if (fallback == nullptr)
        {
            fallback = &language;
        }
    }
    return fallback != nullptr ? QString::fromLatin1(fallback->code) : QStringLiteral("english");
}
} // namespace sam::languages
