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

#include "Application.h"
#include "ManagerWindow.h"
#include "SteamClient.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QProcess>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    sam::initializeApplication(app, QStringLiteral("game"));

    const QStringList arguments = QCoreApplication::arguments();
    if (arguments.size() < 2)
    {
        // Started without a game: open the game list instead.
        QProcess::startDetached(
            QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("SAM.Picker.exe")));
        return 0;
    }

    bool ok = false;
    const uint appId = arguments.at(1).toUInt(&ok);
    if (!ok || appId == 0)
    {
        sam::showError(
            nullptr,
            QCoreApplication::translate("Application", "Could not parse application ID from command line argument."));
        return 1;
    }

    if (sam::isRunningFromSteamDirectory())
    {
        sam::showError(
            nullptr,
            QCoreApplication::translate("Application", "This tool declines to being run from the Steam directory."));
        return 1;
    }

    sam::steam::Client client;
    const auto error = client.initialize(appId);
    if (error != sam::steam::Client::InitError::None)
    {
        sam::showError(nullptr, sam::steamInitErrorMessage(error, true));
        return 1;
    }

    sam::ManagerWindow window(appId, client);
    window.show();
    return QApplication::exec();
}
