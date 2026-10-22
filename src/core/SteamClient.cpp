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

#include "SteamClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

#include <windows.h>

#include <cstring>

namespace sam::steam
{
namespace
{
#if defined(_M_IX86)
#define SAM_THISCALL __thiscall
constexpr const wchar_t *kClientLibrary = L"steamclient.dll";
#else
#define SAM_THISCALL
constexpr const wchar_t *kClientLibrary = L"steamclient64.dll";
#endif

// Calls the virtual function at `index` in the vtable of the Steam interface
// `self`. Overloaded functions are listed under distinct names below, in the
// order MSVC lays them out in the vtable.
template <typename R, typename... Args> R vcall(void *self, int index, Args... args)
{
    using Fn = R(SAM_THISCALL *)(void *, Args...);
    auto vtable = *reinterpret_cast<void ***>(self);
    return reinterpret_cast<Fn>(vtable[index])(self, args...);
}

namespace ISteamClient018
{
enum
{
    CreateSteamPipe = 0,
    ReleaseSteamPipe = 1,
    ConnectToGlobalUser = 2,
    ReleaseUser = 4,
    GetISteamUser = 5,
    GetISteamUtils = 9,
    GetISteamUserStats = 13,
    GetISteamApps = 15,
};
} // namespace ISteamClient018

namespace ISteamUser012
{
enum
{
    GetSteamID = 2,
};
} // namespace ISteamUser012

namespace ISteamUserStats013
{
enum
{
    GetStatFloat = 0,
    GetStatInteger = 1,
    SetStatFloat = 2,
    SetStatInteger = 3,
    SetAchievement = 6,
    ClearAchievement = 7,
    GetAchievementAndUnlockTime = 8,
    StoreStats = 9,
    RequestUserStats = 15,
    ResetAllStats = 20,
};
} // namespace ISteamUserStats013

namespace ISteamUtils005
{
enum
{
    GetIPCountry = 4,
    GetAppID = 9,
};
} // namespace ISteamUtils005

namespace ISteamApps001
{
enum
{
    GetAppData = 0,
};
} // namespace ISteamApps001

namespace ISteamApps008
{
enum
{
    GetCurrentGameLanguage = 4,
    IsSubscribedApp = 6,
};
} // namespace ISteamApps008

struct CallbackMessage
{
    int32_t user;
    int32_t id;
    const uint8_t *param;
    int32_t paramSize;
};

using CreateInterfaceFn = void *(__cdecl *)(const char *version, int *returnCode);
using GetCallbackFn = bool(__cdecl *)(int32_t pipe, CallbackMessage *message, int32_t *call);
using FreeLastCallbackFn = bool(__cdecl *)(int32_t pipe);

struct Library
{
    HMODULE module = nullptr;
    CreateInterfaceFn createInterface = nullptr;
    GetCallbackFn getCallback = nullptr;
    FreeLastCallbackFn freeLastCallback = nullptr;
};

Library g_library;

bool loadLibrary()
{
    if (g_library.module != nullptr)
    {
        return true;
    }

    const QString path = installPath();
    if (path.isEmpty())
    {
        return false;
    }

    // steamclient pulls in other DLLs from the Steam directory.
    const QString nativePath = QDir::toNativeSeparators(path);
    const QString binPath = QDir::toNativeSeparators(QDir(path).filePath(QStringLiteral("bin")));
    SetDllDirectoryW(reinterpret_cast<const wchar_t *>(nativePath.utf16()));
    const QByteArray envPath = qgetenv("PATH");
    qputenv("PATH", (nativePath + u';' + binPath + u';').toLocal8Bit() + envPath);

    const QString libraryPath = nativePath + u'\\' + QString::fromWCharArray(kClientLibrary);
    HMODULE module =
        LoadLibraryExW(reinterpret_cast<const wchar_t *>(libraryPath.utf16()), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (module == nullptr)
    {
        return false;
    }

    Library library;
    library.module = module;
    library.createInterface = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(module, "CreateInterface"));
    library.getCallback = reinterpret_cast<GetCallbackFn>(GetProcAddress(module, "Steam_BGetCallback"));
    library.freeLastCallback = reinterpret_cast<FreeLastCallbackFn>(GetProcAddress(module, "Steam_FreeLastCallback"));
    if (library.createInterface == nullptr || library.getCallback == nullptr || library.freeLastCallback == nullptr)
    {
        FreeLibrary(module);
        return false;
    }

    g_library = library;
    return true;
}

QString fromUtf8(const char *value)
{
    return value != nullptr ? QString::fromUtf8(value) : QString();
}
} // namespace

QString installPath()
{
    // Steam is a 32-bit application, so its machine-wide key lives in the
    // 32-bit registry view.
    QSettings machine(QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\Valve\\Steam"), QSettings::Registry32Format);
    QString path = machine.value(QStringLiteral("InstallPath")).toString();
    if (path.isEmpty())
    {
        QSettings user(QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam"), QSettings::NativeFormat);
        path = user.value(QStringLiteral("SteamPath")).toString();
    }
    if (path.isEmpty())
    {
        return {};
    }
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

Client::~Client()
{
    if (m_client != nullptr && m_pipe != 0)
    {
        if (m_userHandle != 0)
        {
            vcall<void>(m_client, ISteamClient018::ReleaseUser, m_pipe, m_userHandle);
            m_userHandle = 0;
        }
        vcall<bool>(m_client, ISteamClient018::ReleaseSteamPipe, m_pipe);
        m_pipe = 0;
    }
}

Client::InitError Client::initialize(uint32_t appId)
{
    if (installPath().isEmpty())
    {
        return InitError::GetInstallPath;
    }

    if (appId != 0)
    {
        qputenv("SteamAppId", QByteArray::number(appId));
    }

    if (!loadLibrary())
    {
        return InitError::Load;
    }

    m_client = g_library.createInterface("SteamClient018", nullptr);
    if (m_client == nullptr)
    {
        return InitError::CreateSteamClient;
    }

    m_pipe = vcall<int32_t>(m_client, ISteamClient018::CreateSteamPipe);
    if (m_pipe == 0)
    {
        return InitError::CreateSteamPipe;
    }

    m_userHandle = vcall<int32_t>(m_client, ISteamClient018::ConnectToGlobalUser, m_pipe);
    if (m_userHandle == 0)
    {
        return InitError::ConnectToGlobalUser;
    }

    m_utils = vcall<void *>(m_client, ISteamClient018::GetISteamUtils, m_pipe, "SteamUtils005");
    if (m_utils == nullptr || (appId != 0 && this->appId() != appId))
    {
        return InitError::AppIdMismatch;
    }

    m_user = vcall<void *>(m_client, ISteamClient018::GetISteamUser, m_userHandle, m_pipe, "SteamUser012");
    m_userStats = vcall<void *>(
        m_client, ISteamClient018::GetISteamUserStats, m_userHandle, m_pipe, "STEAMUSERSTATS_INTERFACE_VERSION013");
    m_apps001 =
        vcall<void *>(m_client, ISteamClient018::GetISteamApps, m_userHandle, m_pipe, "STEAMAPPS_INTERFACE_VERSION001");
    m_apps008 =
        vcall<void *>(m_client, ISteamClient018::GetISteamApps, m_userHandle, m_pipe, "STEAMAPPS_INTERFACE_VERSION008");
    if (m_user == nullptr || m_userStats == nullptr || m_apps001 == nullptr || m_apps008 == nullptr)
    {
        return InitError::CreateSteamClient;
    }
    return InitError::None;
}

QString Client::describe(InitError error)
{
    switch (error)
    {
        case InitError::None: return {};
        case InitError::GetInstallPath: return QStringLiteral("failed to get Steam install path");
        case InitError::Load: return QStringLiteral("failed to load SteamClient");
        case InitError::CreateSteamClient: return QStringLiteral("failed to create ISteamClient018");
        case InitError::CreateSteamPipe: return QStringLiteral("failed to create pipe");
        case InitError::ConnectToGlobalUser: return QStringLiteral("failed to connect to global user");
        case InitError::AppIdMismatch: return QStringLiteral("appID mismatch");
    }
    return {};
}

void Client::registerCallback(int id, CallbackHandler handler)
{
    m_callbacks.push_back({id, std::move(handler)});
}

void Client::runCallbacks()
{
    if (m_runningCallbacks || m_pipe == 0)
    {
        return;
    }
    m_runningCallbacks = true;

    CallbackMessage message{};
    int32_t call = 0;
    while (g_library.getCallback(m_pipe, &message, &call))
    {
        for (const auto &callback : m_callbacks)
        {
            if (callback.id == message.id)
            {
                callback.handler(message.param, message.paramSize);
            }
        }
        g_library.freeLastCallback(m_pipe);
    }

    m_runningCallbacks = false;
}

uint64_t Client::steamId() const
{
    // CSteamID is returned through a hidden pointer.
    uint64_t steamId = 0;
    vcall<uint64_t *>(m_user, ISteamUser012::GetSteamID, &steamId);
    return steamId;
}

bool Client::getStat(const QString &name, int32_t *value) const
{
    return vcall<bool>(m_userStats, ISteamUserStats013::GetStatInteger, name.toUtf8().constData(), value);
}

bool Client::getStat(const QString &name, float *value) const
{
    return vcall<bool>(m_userStats, ISteamUserStats013::GetStatFloat, name.toUtf8().constData(), value);
}

bool Client::setStat(const QString &name, int32_t value) const
{
    return vcall<bool>(m_userStats, ISteamUserStats013::SetStatInteger, name.toUtf8().constData(), value);
}

bool Client::setStat(const QString &name, float value) const
{
    return vcall<bool>(m_userStats, ISteamUserStats013::SetStatFloat, name.toUtf8().constData(), value);
}

bool Client::getAchievementAndUnlockTime(const QString &name, bool *achieved, uint32_t *unlockTime) const
{
    return vcall<bool>(
        m_userStats, ISteamUserStats013::GetAchievementAndUnlockTime, name.toUtf8().constData(), achieved, unlockTime);
}

bool Client::setAchievement(const QString &name, bool achieved) const
{
    return vcall<bool>(m_userStats,
                       achieved ? ISteamUserStats013::SetAchievement : ISteamUserStats013::ClearAchievement,
                       name.toUtf8().constData());
}

bool Client::storeStats() const
{
    return vcall<bool>(m_userStats, ISteamUserStats013::StoreStats);
}

uint64_t Client::requestUserStats(uint64_t steamId) const
{
    return vcall<uint64_t>(m_userStats, ISteamUserStats013::RequestUserStats, steamId);
}

bool Client::resetAllStats(bool achievementsToo) const
{
    return vcall<bool>(m_userStats, ISteamUserStats013::ResetAllStats, achievementsToo);
}

uint32_t Client::appId() const
{
    return vcall<uint32_t>(m_utils, ISteamUtils005::GetAppID);
}

QString Client::ipCountry() const
{
    return fromUtf8(vcall<const char *>(m_utils, ISteamUtils005::GetIPCountry));
}

QString Client::appData(uint32_t appId, const char *key) const
{
    char buffer[4096];
    const int length =
        vcall<int>(m_apps001, ISteamApps001::GetAppData, appId, key, buffer, static_cast<int>(sizeof(buffer)));
    if (length <= 0)
    {
        return {};
    }
    buffer[sizeof(buffer) - 1] = '\0';
    return QString::fromUtf8(buffer, static_cast<qsizetype>(strnlen(buffer, sizeof(buffer))));
}

bool Client::isSubscribedApp(uint32_t appId) const
{
    return vcall<bool>(m_apps008, ISteamApps008::IsSubscribedApp, appId);
}

QString Client::currentGameLanguage() const
{
    return fromUtf8(vcall<const char *>(m_apps008, ISteamApps008::GetCurrentGameLanguage));
}
} // namespace sam::steam
