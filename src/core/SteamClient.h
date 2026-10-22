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

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <functional>
#include <vector>

namespace sam::steam
{
// Steam callback ids we care about.
constexpr int kAppDataChangedCallback = 1001;
constexpr int kUserStatsReceivedCallback = 1101;

// k_EResultOK
constexpr int kResultOK = 1;

// Mirrors of the Steam callback structures (Windows uses 8-byte packing).
struct AppDataChanged
{
    uint32_t appId;
    bool result;
};

struct UserStatsReceived
{
    uint64_t gameId;
    int32_t result;
    uint64_t steamIdUser;
};

// Returns the Steam installation directory, or an empty string.
QString installPath();

class Client
{
public:
    enum class InitError
    {
        None,
        GetInstallPath,
        Load,
        CreateSteamClient,
        CreateSteamPipe,
        ConnectToGlobalUser,
        AppIdMismatch,
    };

    Client() = default;
    ~Client();
    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;

    // Connects to the running Steam client. When appId is non-zero, the
    // connection pretends to be that game (this is what makes the stats
    // accessible).
    InitError initialize(uint32_t appId);
    static QString describe(InitError error);

    using CallbackHandler = std::function<void(const void *data, int size)>;
    void registerCallback(int id, CallbackHandler handler);
    void runCallbacks();

    // ISteamUser
    uint64_t steamId() const;

    // ISteamUserStats
    bool getStat(const QString &name, int32_t *value) const;
    bool getStat(const QString &name, float *value) const;
    bool setStat(const QString &name, int32_t value) const;
    bool setStat(const QString &name, float value) const;
    bool getAchievementAndUnlockTime(const QString &name, bool *achieved, uint32_t *unlockTime) const;
    bool setAchievement(const QString &name, bool achieved) const;
    bool storeStats() const;
    uint64_t requestUserStats(uint64_t steamId) const;
    bool resetAllStats(bool achievementsToo) const;

    // ISteamUtils
    uint32_t appId() const;
    QString ipCountry() const;

    // ISteamApps
    QString appData(uint32_t appId, const char *key) const;
    bool isSubscribedApp(uint32_t appId) const;
    QString currentGameLanguage() const;

private:
    struct Callback
    {
        int id;
        CallbackHandler handler;
    };

    void *m_client = nullptr;
    void *m_user = nullptr;
    void *m_userStats = nullptr;
    void *m_utils = nullptr;
    void *m_apps001 = nullptr;
    void *m_apps008 = nullptr;
    int32_t m_pipe = 0;
    int32_t m_userHandle = 0;
    bool m_runningCallbacks = false;
    std::vector<Callback> m_callbacks;
};
} // namespace sam::steam
