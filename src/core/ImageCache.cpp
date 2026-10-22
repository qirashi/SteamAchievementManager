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

#include "ImageCache.h"

#include "Application.h"

#include <QImageReader>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace sam
{
ImageCache::ImageCache(QObject *parent) : QObject(parent)
{
    // Cost is in KiB.
    m_images.setMaxCost(192 * 1024);
}

QImage ImageCache::image(const QString &url)
{
    if (url.isEmpty())
    {
        return {};
    }

    if (const QImage *image = m_images.object(url))
    {
        return *image;
    }

    if (m_failed.contains(url))
    {
        return {};
    }

    if (m_requested.contains(url))
    {
        // Already queued: move it to the front so visible items load first.
        if (const auto index = m_queue.indexOf(url); index >= 0 && index != m_queue.size() - 1)
        {
            m_queue.move(index, m_queue.size() - 1);
        }
        return {};
    }

    m_requested.insert(url);
    m_queue.append(url);
    emit pendingCountChanged(pendingCount());
    startNext();
    return {};
}

void ImageCache::cancelPending()
{
    for (const auto &url : std::as_const(m_queue))
    {
        m_requested.remove(url);
    }
    m_queue.clear();
    emit pendingCountChanged(pendingCount());
}

void ImageCache::startNext()
{
    while (m_active < m_maxConcurrent && !m_queue.isEmpty())
    {
        const QString url = m_queue.takeLast();
        QNetworkRequest request{QUrl(url)};
        request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setTransferTimeout(30000);
        QNetworkReply *reply = network()->get(request);
        ++m_active;
        connect(reply, &QNetworkReply::finished, this, [this, reply, url] {
            onFinished(reply, url);
        });
    }
}

void ImageCache::onFinished(QNetworkReply *reply, const QString &url)
{
    reply->deleteLater();
    --m_active;
    m_requested.remove(url);

    QImage image;
    if (reply->error() == QNetworkReply::NoError)
    {
        QImageReader reader(reply);
        reader.setAutoTransform(true);
        image = reader.read();
    }

    if (image.isNull())
    {
        m_failed.insert(url);
    }
    else
    {
        if (m_maximumSize.isValid() &&
            (image.width() > m_maximumSize.width() || image.height() > m_maximumSize.height()))
        {
            image = image.scaled(m_maximumSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        if (image.format() != QImage::Format_ARGB32_Premultiplied && image.hasAlphaChannel())
        {
            image.convertTo(QImage::Format_ARGB32_Premultiplied);
        }
        const auto cost = static_cast<int>(qMax<qsizetype>(1, image.sizeInBytes() / 1024));
        m_images.insert(url, new QImage(std::move(image)), cost);
    }

    emit imageReady(url);
    emit pendingCountChanged(pendingCount());
    startNext();
}
} // namespace sam
