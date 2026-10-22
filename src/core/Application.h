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

#include "SteamClient.h"

#include <QString>
#include <QStringList>

#include <functional>

class QApplication;
class QMenu;
class QNetworkAccessManager;
class QSettings;
class QWidget;

namespace sam
{
enum class Theme
{
    System,
    Light,
    Dark,
};

// Sets up Fusion style, color scheme, translations and the shared network
// stack. `component` names the executable ("picker" or "game").
void initializeApplication(QApplication &app, const QString &component);

QSettings &settings();
QNetworkAccessManager *network();

Theme theme();
void setTheme(Theme theme);
// Applies the theme in the settings if another process changed it.
void applySavedTheme();

// Empty string means "follow the system language".
QString interfaceLanguage();

// A menu with theme / interface language selection and an about box.
QMenu *createSettingsMenu(QWidget *parent);

// Preferred language for achievement names and descriptions, shared by both
// executables. Empty means "use the Steam client language".
QString achievementLanguage();

// Builds the "Achievement language" submenu: "Steam language (...)" followed
// by `codes`. The current preference is checked; picking an entry stores it
// and calls `changed`. Calling it again repopulates the menu.
void fillAchievementLanguageMenu(QMenu *menu,
                                 const QStringList &codes,
                                 const QString &steamLanguage,
                                 const std::function<void()> &changed = {});
QMenu *createAchievementLanguageMenu(QWidget *parent);

bool isRunningFromSteamDirectory();
QString steamInitErrorMessage(steam::Client::InitError error, bool forGame);

void showError(QWidget *parent, const QString &text);
} // namespace sam
