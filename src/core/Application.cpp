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

#include "Icons.h"
#include "Languages.h"
#include "Style.h"
#include "Version.h"

#include <QActionGroup>
#include <QApplication>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTimer>
#include <QTranslator>

namespace sam
{
namespace
{
QString g_component;
Theme g_appliedTheme = Theme::System;

void applyTheme(Theme theme)
{
    // The color scheme also reaches native parts (dialogs); the palette and
    // style sheet follow it (see initializeApplication).
    g_appliedTheme = theme;
    auto *hints = QGuiApplication::styleHints();
    switch (theme)
    {
        case Theme::System: hints->unsetColorScheme(); break;
        case Theme::Light: hints->setColorScheme(Qt::ColorScheme::Light); break;
        case Theme::Dark: hints->setColorScheme(Qt::ColorScheme::Dark); break;
    }
    applyColorScheme(hints->colorScheme() == Qt::ColorScheme::Dark, theme == Theme::System);
}

void installTranslations(QApplication &app)
{
    QString language = interfaceLanguage();
    QLocale locale = language.isEmpty() ? QLocale::system() : QLocale(language);
    QLocale::setDefault(locale);

    // Ours (embedded); English is the source language and needs no file.
    auto *translator = new QTranslator(&app);
    if (translator->load(locale, QStringLiteral("sam"), QStringLiteral("_"), QStringLiteral(":/i18n")))
    {
        QCoreApplication::installTranslator(translator);
    }
    else
    {
        delete translator;
        QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
        return;
    }

    // Qt's own strings (standard dialog buttons, context menus...).
    const QStringList directories = {
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("translations")),
        QLibraryInfo::path(QLibraryInfo::TranslationsPath),
    };
    // windeployqt merges Qt's catalogs into qt_<lang>.qm; a Qt install has qtbase_<lang>.qm.
    for (const auto &directory : directories)
    {
        for (const auto &name : {QStringLiteral("qt"), QStringLiteral("qtbase")})
        {
            auto *qtTranslator = new QTranslator(&app);
            if (qtTranslator->load(locale, name, QStringLiteral("_"), directory))
            {
                QCoreApplication::installTranslator(qtTranslator);
                return;
            }
            delete qtTranslator;
        }
    }
}

void restartApplication()
{
    QStringList arguments = QCoreApplication::arguments();
    arguments.removeFirst();
    if (QProcess::startDetached(QCoreApplication::applicationFilePath(), arguments))
    {
        QCoreApplication::quit();
    }
}

void showAbout(QWidget *parent)
{
    QMessageBox box(parent);
    box.setWindowTitle(QCoreApplication::translate("Application", "About Steam Achievement Manager"));
    box.setIconPixmap(QApplication::windowIcon().pixmap(64, 64));
    box.setTextFormat(Qt::RichText);
    box.setText(QStringLiteral("<h3>Steam Achievement Manager %1</h3>").arg(QStringLiteral(SAM_VERSION_STRING)));
    const auto link = [](const char *url) {
        const QString target = QString::fromLatin1(url);
        return QStringLiteral("<a href=\"%1\">%2</a>").arg(target, QString(target).remove(QStringLiteral("https://")));
    };
    box.setInformativeText(
        QCoreApplication::translate("Application",
                                    "A lightweight tool to manage achievements and statistics of your Steam games.") +
        QStringLiteral("<br><br>") +
        QCoreApplication::translate("Application",
                                    "Qt %1 port, based on the original Steam Achievement Manager %2 (commit %3) "
                                    "by Rick (gibbed), released under the zlib license.")
            .arg(QString::fromLatin1(qVersion()),
                 QStringLiteral(SAM_UPSTREAM_VERSION_STRING),
                 QStringLiteral(SAM_UPSTREAM_COMMIT_STRING)) +
        QStringLiteral("<br><br>") +
        QCoreApplication::translate("Application", "This version: %1<br>Original: %2")
            .arg(link(SAM_REPOSITORY_URL), link(SAM_UPSTREAM_URL)) +
        QStringLiteral("<br><br>") +
        QCoreApplication::translate("Application",
                                    "Qt is used under the GNU LGPL version 3; the licenses of Qt "
                                    "and its components are in THIRD-PARTY-NOTICES.txt and the "
                                    "\"licenses\" folder next to the program."));
    box.exec();
}
} // namespace

void initializeApplication(QApplication &app, const QString &component)
{
    g_component = component;
    QCoreApplication::setOrganizationName(QStringLiteral("SAM"));
    QCoreApplication::setApplicationName(QStringLiteral("SteamAchievementManager"));
    QCoreApplication::setApplicationVersion(QStringLiteral(SAM_VERSION_STRING));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Steam Achievement Manager"));

    installStyle(app);
    applyTheme(theme());
    // "Follow system": Windows switching between light and dark. Qt resets the
    // palette on such a change, so ours goes back on afterwards.
    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, &app, [](Qt::ColorScheme scheme) {
        QTimer::singleShot(0, qApp, [scheme] {
            applyColorScheme(scheme == Qt::ColorScheme::Dark, g_appliedTheme == Theme::System);
        });
    });
    installTranslations(app);

    QApplication::setWindowIcon(QIcon(QStringLiteral(":/sam/SAM.ico")));
}

QSettings &settings()
{
    static QSettings instance(
        QSettings::IniFormat, QSettings::UserScope, QStringLiteral("SAM"), QStringLiteral("SteamAchievementManager"));
    return instance;
}

QNetworkAccessManager *network()
{
    static QNetworkAccessManager *manager = [] {
        auto *result = new QNetworkAccessManager(qApp);
        auto *cache = new QNetworkDiskCache(result);
        // One cache per executable: QNetworkDiskCache isn't multi-process safe.
        const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        cache->setCacheDirectory(QDir(base).filePath(QStringLiteral("http-") + g_component));
        cache->setMaximumCacheSize(512LL * 1024 * 1024);
        result->setCache(cache);
        return result;
    }();
    return manager;
}

Theme theme()
{
    const QString value = settings().value(QStringLiteral("ui/theme")).toString();
    if (value == QLatin1StringView("light"))
    {
        return Theme::Light;
    }
    if (value == QLatin1StringView("dark"))
    {
        return Theme::Dark;
    }
    return Theme::System;
}

void setTheme(Theme theme)
{
    const char *value = theme == Theme::Light ? "light" : theme == Theme::Dark ? "dark" : "system";
    settings().setValue(QStringLiteral("ui/theme"), QString::fromLatin1(value));
    settings().sync(); // open game windows follow it
    applyTheme(theme);
}

void applySavedTheme()
{
    const Theme saved = theme();
    if (saved != g_appliedTheme)
    {
        applyTheme(saved);
    }
}

QString interfaceLanguage()
{
    return settings().value(QStringLiteral("ui/language")).toString();
}

QMenu *createSettingsMenu(QWidget *parent)
{
    auto *menu = new QMenu(parent);

    auto *themeMenu =
        menu->addMenu(icons::get(QStringLiteral("theme")), QCoreApplication::translate("Application", "&Theme"));
    auto *themeGroup = new QActionGroup(themeMenu);
    const struct
    {
        Theme value;
        const char *label;
    } themes[] = {
        {Theme::System, QT_TRANSLATE_NOOP("Application", "Follow &system")},
        {Theme::Light, QT_TRANSLATE_NOOP("Application", "&Light")},
        {Theme::Dark, QT_TRANSLATE_NOOP("Application", "&Dark")},
    };
    for (const auto &entry : themes)
    {
        auto *action = themeMenu->addAction(QCoreApplication::translate("Application", entry.label));
        action->setCheckable(true);
        action->setChecked(theme() == entry.value);
        themeGroup->addAction(action);
        const Theme value = entry.value;
        QObject::connect(action, &QAction::triggered, menu, [value] {
            setTheme(value);
        });
    }

    auto *languageMenu = menu->addMenu(icons::get(QStringLiteral("language")),
                                       QCoreApplication::translate("Application", "Interface &language"));
    auto *languageGroup = new QActionGroup(languageMenu);
    const struct
    {
        const char *code;
        QString label;
    } languages[] = {
        {"", QCoreApplication::translate("Application", "Follow &system")},
        {"en", QStringLiteral("English")},
        {"ru", QStringLiteral("Русский")},
    };
    for (const auto &entry : languages)
    {
        auto *action = languageMenu->addAction(entry.label);
        action->setCheckable(true);
        const QString code = QString::fromLatin1(entry.code);
        action->setChecked(interfaceLanguage() == code);
        languageGroup->addAction(action);
        QObject::connect(action, &QAction::triggered, menu, [code, parent] {
            if (interfaceLanguage() == code)
            {
                return;
            }
            settings().setValue(QStringLiteral("ui/language"), code);
            settings().sync();
            const auto answer = QMessageBox::question(
                parent,
                QCoreApplication::translate("Application", "Interface language"),
                QCoreApplication::translate("Application",
                                            "The interface language will change after a restart. Restart now?"));
            if (answer == QMessageBox::Yes)
            {
                restartApplication();
            }
        });
    }

    menu->addSeparator();
    auto *about =
        menu->addAction(icons::get(QStringLiteral("info")), QCoreApplication::translate("Application", "&About..."));
    QObject::connect(about, &QAction::triggered, menu, [parent] {
        showAbout(parent);
    });
    menu->addAction(QCoreApplication::translate("Application", "About &Qt..."), qApp, &QApplication::aboutQt);
    return menu;
}

QString achievementLanguage()
{
    return settings().value(QStringLiteral("game/achievementLanguage")).toString();
}

QMenu *createAchievementLanguageMenu(QWidget *parent)
{
    auto *menu = new QMenu(QCoreApplication::translate("Application", "&Achievement language"), parent);
    menu->setIcon(icons::get(QStringLiteral("trophy")));
    return menu;
}

void fillAchievementLanguageMenu(QMenu *menu,
                                 const QStringList &codes,
                                 const QString &steamLanguage,
                                 const std::function<void()> &changed)
{
    menu->clear();
    const QList<QActionGroup *> groups = menu->findChildren<QActionGroup *>(Qt::FindDirectChildrenOnly);
    qDeleteAll(groups);
    auto *group = new QActionGroup(menu);

    const QString current = achievementLanguage();
    const QStringList sortedCodes = languages::sorted(codes);
    const bool currentAvailable = sortedCodes.contains(current, Qt::CaseInsensitive);

    const auto add = [&](const QString &label, const QString &code) {
        auto *action = menu->addAction(label);
        action->setCheckable(true);
        action->setChecked(currentAvailable ? code.compare(current, Qt::CaseInsensitive) == 0 : code.isEmpty());
        group->addAction(action);
        QObject::connect(action, &QAction::triggered, menu, [code, changed] {
            settings().setValue(QStringLiteral("game/achievementLanguage"), code);
            settings().sync(); // open game windows follow it
            if (changed)
            {
                changed();
            }
        });
    };

    add(QCoreApplication::translate("Application", "Steam language (%1)").arg(languages::displayName(steamLanguage)),
        QString());
    menu->addSeparator();
    for (const auto &code : sortedCodes)
    {
        add(languages::displayName(code), code.toLower());
    }
}

bool isRunningFromSteamDirectory()
{
    const QString steam = steam::installPath();
    if (steam.isEmpty())
    {
        return false;
    }
    const QString self = QDir::cleanPath(QCoreApplication::applicationDirPath());
    return self.compare(steam, Qt::CaseInsensitive) == 0;
}

QString steamInitErrorMessage(steam::Client::InitError error, bool forGame)
{
    QString message = QCoreApplication::translate("Application",
                                                  "Steam is not running. Please start Steam then run this tool again.");
    if (forGame && error == steam::Client::InitError::ConnectToGlobalUser)
    {
        message += QStringLiteral("\n\n");
        message +=
            QCoreApplication::translate("Application",
                                        "If you have the game through Family Share, the game may be locked due to "
                                        "the Family Share account actively playing a game.");
    }
    const QString details = steam::Client::describe(error);
    if (!details.isEmpty())
    {
        message += QStringLiteral("\n\n(%1)").arg(details);
    }
    return message;
}

void showError(QWidget *parent, const QString &text)
{
    QMessageBox::critical(parent, QCoreApplication::translate("Application", "Error"), text);
}
} // namespace sam
