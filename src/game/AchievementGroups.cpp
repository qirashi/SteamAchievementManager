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

#include "AchievementGroups.h"

#include "Application.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace sam
{
namespace
{
QString stripGameName(const QString &name, const QString &gameName)
{
    if (gameName.isEmpty() || !name.startsWith(gameName, Qt::CaseInsensitive))
    {
        return name;
    }
    const QString rest = name.mid(gameName.size());
    for (const auto separator : {u" - "_s, u" – "_s, u" — "_s, u": "_s})
    {
        if (rest.startsWith(separator))
        {
            const QString stripped = rest.mid(separator.size()).trimmed();
            return stripped.isEmpty() ? name : stripped;
        }
    }
    return name;
}

void get(uint32_t appId,
         QNetworkRequest::CacheLoadControl cacheControl,
         const QString &gameName,
         QObject *context,
         std::function<void(QList<AchievementGroup>)> done)
{
    QNetworkRequest request(QUrl(u"https://steamhunters.com/api/GetAchievementGroups/v1?appid=%1"_s.arg(appId)));
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, cacheControl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(15000);

    QNetworkReply *reply = network()->get(request);
    QObject::connect(reply,
                     &QNetworkReply::finished,
                     context,
                     [reply, appId, cacheControl, gameName, context = QPointer(context), done = std::move(done)] {
                         reply->deleteLater();
                         if (context.isNull())
                         {
                             return;
                         }
                         if (reply->error() == QNetworkReply::NoError)
                         {
                             done(parseAchievementGroups(reply->readAll(), gameName));
                         }
                         else if (cacheControl != QNetworkRequest::AlwaysCache)
                         {
                             // Offline: use whatever the last successful request left in the cache.
                             get(appId, QNetworkRequest::AlwaysCache, gameName, context, done);
                         }
                         else
                         {
                             done({});
                         }
                     });
}
} // namespace

void requestAchievementGroups(uint32_t appId,
                              const QString &gameName,
                              QObject *context,
                              std::function<void(QList<AchievementGroup>)> done)
{
    get(appId, QNetworkRequest::PreferNetwork, gameName, context, std::move(done));
}

QList<AchievementGroup> parseAchievementGroups(const QByteArray &json, const QString &gameName)
{
    QList<AchievementGroup> result;
    const QJsonArray groups = QJsonDocument::fromJson(json).object().value("groups"_L1).toArray();
    for (const auto &value : groups)
    {
        const QJsonObject object = value.toObject();
        AchievementGroup group;
        group.dlcAppId = static_cast<uint32_t>(object.value("dlcAppId"_L1).toInteger());
        const QString dlcName = stripGameName(object.value("dlcAppName"_L1).toString().trimmed(), gameName);
        const QString name = object.value("name"_L1).toString().trimmed();
        if (!dlcName.isEmpty() && !name.isEmpty())
        {
            group.name = dlcName + u" — "_s + name;
        }
        else
        {
            group.name = !dlcName.isEmpty() ? dlcName : name;
        }
        for (const auto &id : object.value("achievementApiNames"_L1).toArray())
        {
            group.achievementIds.append(id.toString());
        }
        if (!group.achievementIds.isEmpty())
        {
            if (group.name.isEmpty())
            {
                group.name = group.dlcAppId != 0 ? u"DLC %1"_s.arg(group.dlcAppId) : u"?"_s;
            }
            result.append(std::move(group));
        }
    }
    return result;
}
} // namespace sam
