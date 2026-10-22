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

#include <QCache>
#include <QHash>
#include <QImage>
#include <QList>
#include <QObject>
#include <QSet>
#include <QSize>
#include <QString>

class QNetworkReply;

namespace sam
{
// Downloads remote images in parallel, newest request first (so whatever is
// currently on screen wins), and keeps decoded copies in memory. Responses
// are also kept in the shared on-disk HTTP cache, so restarts are fast.
class ImageCache : public QObject
{
    Q_OBJECT

public:
    explicit ImageCache(QObject *parent = nullptr);

    // Images are scaled down to fit this size (in device pixels) once loaded.
    void setMaximumSize(const QSize &size) { m_maximumSize = size; }
    void setMaxConcurrent(int count) { m_maxConcurrent = qMax(1, count); }

    // Returns the image if it is available; otherwise schedules a download
    // (unless it already failed) and returns a null image.
    QImage image(const QString &url);
    bool hasFailed(const QString &url) const { return m_failed.contains(url); }

    // Drops everything still waiting in the queue (not what's in flight).
    void cancelPending();
    int pendingCount() const { return static_cast<int>(m_queue.size()) + m_active; }

signals:
    void imageReady(const QString &url);
    void pendingCountChanged(int count);

private:
    void startNext();
    void onFinished(QNetworkReply *reply, const QString &url);

    QCache<QString, QImage> m_images;
    QSet<QString> m_failed;
    QSet<QString> m_requested;
    QList<QString> m_queue;
    QSize m_maximumSize;
    int m_maxConcurrent = 6;
    int m_active = 0;
};
} // namespace sam
