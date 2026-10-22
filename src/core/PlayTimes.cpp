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

#include "PlayTimes.h"

#include "SteamClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLocale>
#include <QStringList>

namespace sam
{
namespace
{
// Reads the tokens of a text KeyValues (VDF) file: quoted strings, unquoted
// words and braces; "//" starts a comment.
class Tokenizer
{
public:
    explicit Tokenizer(const QByteArray &data) : m_data(data) {}

    enum class Kind
    {
        String,
        Open,
        Close,
        End,
    };

    Kind next(QByteArray *text)
    {
        while (m_position < m_data.size())
        {
            const char c = m_data.at(m_position);
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            {
                ++m_position;
            }
            else if (c == '/' && m_position + 1 < m_data.size() && m_data.at(m_position + 1) == '/')
            {
                const qsizetype end = m_data.indexOf('\n', m_position);
                m_position = end < 0 ? m_data.size() : end;
            }
            else if (c == '{' || c == '}')
            {
                ++m_position;
                return c == '{' ? Kind::Open : Kind::Close;
            }
            else if (c == '"')
            {
                text->clear();
                ++m_position;
                while (m_position < m_data.size() && m_data.at(m_position) != '"')
                {
                    char ch = m_data.at(m_position++);
                    if (ch == '\\' && m_position < m_data.size())
                    {
                        ch = m_data.at(m_position++);
                    }
                    text->append(ch);
                }
                ++m_position; // the closing quote
                return Kind::String;
            }
            else
            {
                const qsizetype start = m_position;
                while (m_position < m_data.size() && !QByteArrayView(" \t\r\n{}\"").contains(m_data.at(m_position)))
                {
                    ++m_position;
                }
                *text = m_data.mid(start, m_position - start);
                return Kind::String;
            }
        }
        return Kind::End;
    }

private:
    const QByteArray &m_data;
    qsizetype m_position = 0;
};

bool sameKey(const QByteArray &key, const char *name)
{
    return qstricmp(key.constData(), name) == 0;
}
} // namespace

QHash<uint32_t, PlayTime> loadPlayTimes(uint64_t steamId)
{
    QHash<uint32_t, PlayTime> result;
    const QString steamPath = steam::installPath();
    const uint32_t accountId = static_cast<uint32_t>(steamId & 0xFFFFFFFFu);
    if (steamPath.isEmpty() || accountId == 0)
    {
        return result;
    }
    QFile file(QDir(steamPath).filePath(QStringLiteral("userdata/%1/config/localconfig.vdf").arg(accountId)));
    if (!file.open(QIODevice::ReadOnly))
    {
        return result;
    }
    const QByteArray data = file.readAll();

    // The games are the children of .../Software/Valve/Steam/apps.
    Tokenizer tokenizer(data);
    QList<QByteArray> path;
    QByteArray key;
    QByteArray text;
    bool haveKey = false;
    while (true)
    {
        const Tokenizer::Kind kind = tokenizer.next(&text);
        if (kind == Tokenizer::Kind::End)
        {
            break;
        }
        if (kind == Tokenizer::Kind::Open)
        {
            path.append(haveKey ? key : QByteArray());
            haveKey = false;
            continue;
        }
        if (kind == Tokenizer::Kind::Close)
        {
            if (!path.isEmpty())
            {
                path.removeLast();
            }
            haveKey = false;
            continue;
        }
        if (!haveKey)
        {
            key = text;
            haveKey = true;
            continue;
        }

        // A value: is it a field of a game?
        haveKey = false;
        const qsizetype depth = path.size();
        if (depth < 3 || !sameKey(path.at(depth - 2), "apps") || !sameKey(path.at(depth - 3), "Steam"))
        {
            continue;
        }
        bool ok = false;
        const uint32_t appId = path.at(depth - 1).toUInt(&ok);
        if (!ok || appId == 0)
        {
            continue;
        }
        if (sameKey(key, "Playtime"))
        {
            result[appId].minutes = text.toInt();
        }
        else if (sameKey(key, "LastPlayed"))
        {
            result[appId].lastPlayed = text.toLongLong();
        }
    }
    return result;
}
QString formatPlaytime(int minutes)
{
    if (minutes < 60)
    {
        return QCoreApplication::translate("PlayTime", "%1 min").arg(minutes);
    }
    const QString hours = QLocale().toString(minutes / 60);
    if (minutes % 60 == 0)
    {
        return QCoreApplication::translate("PlayTime", "%1 h").arg(hours);
    }
    return QCoreApplication::translate("PlayTime", "%1 h %2 min").arg(hours).arg(minutes % 60);
}
} // namespace sam
