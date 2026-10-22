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

#include <QList>
#include <QString>
#include <QStringView>
#include <QVariant>

#include <cstdint>
#include <optional>

namespace sam
{
// Valve's binary KeyValues format, as used by the cached stat schemas in
// <Steam>/appcache/stats/UserGameStatsSchema_<appid>.bin.
class KeyValue
{
public:
    enum class Type : uint8_t
    {
        None = 0,
        String = 1,
        Int32 = 2,
        Float32 = 3,
        Pointer = 4,
        WideString = 5,
        Color = 6,
        UInt64 = 7,
        End = 8,
    };

    QString name = QStringLiteral("<root>");
    Type type = Type::None;
    QVariant value;
    bool valid = false;
    QList<KeyValue> children;

    // Case-insensitive child lookup; returns an invalid node when missing.
    const KeyValue &operator[](QStringView key) const;
    const KeyValue &operator[](const char *key) const { return (*this)[QString::fromLatin1(key)]; }

    QString asString(const QString &defaultValue = {}) const;
    int asInteger(int defaultValue) const;
    float asFloat(float defaultValue) const;
    bool asBoolean(bool defaultValue) const;

    static std::optional<KeyValue> loadBinary(const QString &path);
    static std::optional<KeyValue> parseBinary(const QByteArray &data);
};
} // namespace sam
