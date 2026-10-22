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
#include "PickerWindow.h"
#include "SteamClient.h"

#include <QApplication>
#include <QCoreApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    sam::initializeApplication(app, QStringLiteral("picker"));

    if (sam::isRunningFromSteamDirectory())
    {
        sam::showError(
            nullptr,
            QCoreApplication::translate("Application", "This tool declines to being run from the Steam directory."));
        return 1;
    }

    sam::steam::Client client;
    const auto error = client.initialize(0);
    if (error != sam::steam::Client::InitError::None)
    {
        sam::showError(nullptr, sam::steamInitErrorMessage(error, false));
        return 1;
    }

    sam::PickerWindow window(client);
    window.show();
    return QApplication::exec();
}
