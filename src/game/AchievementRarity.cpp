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

#include "AchievementRarity.h"

#include "Application.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QUrl>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace sam
{
namespace
{
void get(uint32_t appId,
         QNetworkRequest::CacheLoadControl cacheControl,
         QObject *context,
         std::function<void(QHash<QString, double>)> done)
{
    QNetworkRequest request(
        QUrl(u"https://api.steampowered.com/ISteamUserStats/GetGlobalAchievementPercentagesForApp/v2/?gameid=%1"_s.arg(
            appId)));
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, cacheControl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(15000);

    QNetworkReply *reply = network()->get(request);
    QObject::connect(reply,
                     &QNetworkReply::finished,
                     context,
                     [reply, appId, cacheControl, context = QPointer(context), done = std::move(done)] {
                         reply->deleteLater();
                         if (context.isNull())
                         {
                             return;
                         }
                         if (reply->error() == QNetworkReply::NoError)
                         {
                             done(parseAchievementRarity(reply->readAll()));
                         }
                         else if (cacheControl != QNetworkRequest::AlwaysCache)
                         {
                             get(appId, QNetworkRequest::AlwaysCache, context, done);
                         }
                         else
                         {
                             done({});
                         }
                     });
}
} // namespace

void requestAchievementRarity(uint32_t appId, QObject *context, std::function<void(QHash<QString, double>)> done)
{
    get(appId, QNetworkRequest::PreferNetwork, context, std::move(done));
}

QHash<QString, double> parseAchievementRarity(const QByteArray &json)
{
    QHash<QString, double> result;
    const QJsonArray achievements = QJsonDocument::fromJson(json)
                                        .object()
                                        .value("achievementpercentages"_L1)
                                        .toObject()
                                        .value("achievements"_L1)
                                        .toArray();
    for (const auto &value : achievements)
    {
        const QJsonObject object = value.toObject();
        // The percentage comes as a string ("51.7") or, in older answers, a number.
        const QJsonValue percent = object.value("percent"_L1);
        bool ok = percent.isDouble();
        const double number = ok ? percent.toDouble() : percent.toString().toDouble(&ok);
        const QString id = object.value("name"_L1).toString();
        if (ok && !id.isEmpty())
        {
            result.insert(id, std::clamp(number, 0.0, 100.0));
        }
    }
    return result;
}
} // namespace sam
