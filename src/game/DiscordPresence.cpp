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

#include "DiscordPresence.h"

#include "Application.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>

#include <memory>
#include <optional>

using namespace Qt::StringLiterals;

namespace sam
{
namespace
{
// Discord's list of the games it detects, 13 MB of JSON. Only the Steam App
// ID → executable part of it is kept, and refreshed once a week.
const auto kDetectableUrl = u"https://discord.com/api/v9/applications/detectable"_s;
constexpr qint64 kListMaxAge = 7 * 24 * 60 * 60;

QString discordDirectory()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(u"discord"_s);
}

QString listPath()
{
    return QDir(discordDirectory()).filePath(u"games.json"_s);
}

// Turns a name from Discord's list ("win64/cs2.exe", ">hl2.exe") into a
// relative path that can be created on Windows, or an empty string.
QString sanitizeExecutable(QString name)
{
    name = name.trimmed();
    if (name.startsWith(u'>')) // ">" marks names that have to match exactly
    {
        name.remove(0, 1);
    }
    name.replace(u'\\', u'/');
    const QStringList parts = name.split(u'/');
    for (const auto &part : parts)
    {
        if (part.isEmpty() || part == "."_L1 || part == ".."_L1 || part.endsWith(u'.') || part.endsWith(u' '))
        {
            return {};
        }
        for (const QChar c : part)
        {
            if (c.unicode() < 32 || u"<>:\"|?*"_s.contains(c))
            {
                return {};
            }
        }
    }
    if (!parts.last().endsWith(".exe"_L1, Qt::CaseInsensitive))
    {
        return {};
    }
    return parts.join(u'/');
}

// Builds {"fetched": <seconds>, "games": {"<steam app id>": ["<exe>", "<arguments>"]}}.
QByteArray buildList(const QByteArray &detectable)
{
    QJsonObject games;
    const QJsonArray apps = QJsonDocument::fromJson(detectable).array();
    for (const auto &value : apps)
    {
        const QJsonObject app = value.toObject();
        QJsonArray executable;
        for (const auto &entry : app.value("executables"_L1).toArray())
        {
            const QJsonObject object = entry.toObject();
            if (object.value("os"_L1).toString() != "win32"_L1 || object.value("is_launcher"_L1).toBool())
            {
                continue;
            }
            const QString path = sanitizeExecutable(object.value("name"_L1).toString());
            if (!path.isEmpty())
            {
                executable = {path, object.value("arguments"_L1).toString()};
                break;
            }
        }
        if (executable.isEmpty())
        {
            continue;
        }
        for (const auto &sku : app.value("third_party_skus"_L1).toArray())
        {
            const QJsonObject object = sku.toObject();
            const QString id = object.value("id"_L1).toVariant().toString();
            if (object.value("distributor"_L1).toString() == "steam"_L1 && !id.isEmpty() && !games.contains(id))
            {
                games.insert(id, executable);
            }
        }
    }
    if (games.isEmpty())
    {
        return {};
    }
    const QJsonObject root{{"fetched"_L1, QDateTime::currentSecsSinceEpoch()}, {"games"_L1, games}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

std::optional<QJsonObject> readList(qint64 *age)
{
    QFile file(listPath());
    if (!file.open(QIODevice::ReadOnly))
    {
        return std::nullopt;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (!root.value("games"_L1).isObject())
    {
        return std::nullopt;
    }
    *age = QDateTime::currentSecsSinceEpoch() - root.value("fetched"_L1).toInteger();
    return root.value("games"_L1).toObject();
}
} // namespace

DiscordPresence::DiscordPresence(uint32_t appId, QObject *parent) : QObject(parent), m_appId(appId)
{
}

DiscordPresence::~DiscordPresence()
{
    stopStub();
}

void DiscordPresence::setEnabled(bool enabled)
{
    if (enabled == m_enabled)
    {
        return;
    }
    m_enabled = enabled;
    if (!enabled)
    {
        stopStub();
        return;
    }
    if (!m_busy)
    {
        lookUp();
    }
}

void DiscordPresence::lookUp()
{
    qint64 age = 0;
    std::optional<QJsonObject> games = readList(&age);
    if (games && age >= 0 && age < kListMaxAge)
    {
        resolve(*games);
        return;
    }

    // Missing or stale: download a fresh list, falling back to the stale one.
    m_busy = true;
    QNetworkRequest request{QUrl(kDetectableUrl)};
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false); // too big for the image cache
    request.setTransferTimeout(30000);
    QNetworkReply *reply = network()->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, games] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
        {
            m_busy = false;
            if (games)
            {
                resolve(*games);
            }
            else if (m_enabled)
            {
                emit statusChanged(tr("Couldn't download Discord's game list: %1").arg(reply->errorString()), true);
            }
            return;
        }

        // Parsing 13 MB of JSON takes a moment; keep the window responsive.
        auto list = std::make_shared<QByteArray>();
        QThread *thread = QThread::create([list, body = reply->readAll()] {
            *list = buildList(body);
        });
        connect(thread, &QThread::finished, this, [this, thread, list, games] {
            thread->deleteLater();
            m_busy = false;
            if (list->isEmpty())
            {
                if (games)
                {
                    resolve(*games);
                }
                else if (m_enabled)
                {
                    emit statusChanged(tr("Couldn't read Discord's game list."), true);
                }
                return;
            }
            QDir().mkpath(discordDirectory());
            QSaveFile file(listPath());
            if (file.open(QIODevice::WriteOnly))
            {
                file.write(*list);
                file.commit();
            }
            resolve(QJsonDocument::fromJson(*list).object().value("games"_L1).toObject());
        });
        thread->start();
    });
}

void DiscordPresence::resolve(const QJsonObject &games)
{
    if (!m_enabled || m_stub != nullptr)
    {
        return;
    }
    const QJsonArray entry = games.value(QString::number(m_appId)).toArray();
    if (entry.isEmpty())
    {
        emit statusChanged(tr("Discord doesn't detect this game, so it can't be shown there."), true);
        return;
    }
    startStub({entry.at(0).toString(), entry.at(1).toString()});
}

void DiscordPresence::startStub(const Executable &executable)
{
    const QString source = QDir(QCoreApplication::applicationDirPath()).filePath(u"SAM.Stub.exe"_s);
    if (!QFileInfo::exists(source))
    {
        emit statusChanged(tr("SAM.Stub.exe is missing next to SAM.Game.exe."), true);
        return;
    }

    // The path ends the way Discord expects ("…/win64/cs2.exe").
    const QString target = QDir(discordDirectory()).filePath(QString::number(m_appId) + u'/' + executable.path);
    QDir().mkpath(QFileInfo(target).absolutePath());
    // Refresh the copy; it's in use (and kept) while another window of this game runs.
    QFile::remove(target);
    QFile::copy(source, target);
    if (!QFileInfo::exists(target))
    {
        emit statusChanged(tr("Couldn't create %1.").arg(QDir::toNativeSeparators(target)), true);
        return;
    }

    m_stub = new QProcess(this);
    m_stub->setProgram(target);
    m_stub->setNativeArguments(executable.arguments);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(u"SAM_PARENT_PID"_s, QString::number(QCoreApplication::applicationPid()));
    if (!m_title.isEmpty())
    {
        environment.insert(u"SAM_WINDOW_TITLE"_s, m_title);
    }
    m_stub->setProcessEnvironment(environment);
    // Closing the stub's window ends it.
    connect(m_stub, &QProcess::finished, this, [this] {
        emit statusChanged(tr("Discord no longer sees this game: its window was closed."), true);
    });
    connect(m_stub, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
        {
            emit statusChanged(tr("Couldn't start the process for Discord: %1").arg(m_stub->errorString()), true);
        }
    });
    m_stub->start();
    emit statusChanged(tr("Discord sees this game as running (%1).").arg(QFileInfo(executable.path).fileName()), false);
}

void DiscordPresence::stopStub()
{
    if (m_stub == nullptr)
    {
        return;
    }
    m_stub->disconnect(this);
    m_stub->kill();
    m_stub->waitForFinished(2000);
    delete m_stub;
    m_stub = nullptr;
}
} // namespace sam
