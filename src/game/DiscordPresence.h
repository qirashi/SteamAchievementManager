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

#include <QObject>
#include <QString>

#include <cstdint>

class QJsonObject;
class QProcess;

namespace sam
{
// Makes Discord show the game as being played. Discord detects games by the
// executables that run (its list maps Steam App IDs to executable names), so
// while enabled a copy of SAM.Stub.exe runs under the game's executable name
// and exits together with this process.
class DiscordPresence : public QObject
{
    Q_OBJECT

public:
    DiscordPresence(uint32_t appId, QObject *parent = nullptr);
    ~DiscordPresence() override;

    void setEnabled(bool enabled);
    // Title of the stub's window (the game's name).
    void setTitle(const QString &title) { m_title = title; }

signals:
    void statusChanged(const QString &message, bool warning);

private:
    struct Executable
    {
        QString path; // relative, '/'-separated, e.g. "win64/cs2.exe"
        QString arguments;
    };

    void lookUp();
    void resolve(const QJsonObject &games);
    void startStub(const Executable &executable);
    void stopStub();

    uint32_t m_appId;
    QString m_title;
    bool m_enabled = false;
    bool m_busy = false;
    QProcess *m_stub = nullptr;
};
} // namespace sam
