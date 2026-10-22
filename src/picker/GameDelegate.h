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

#include <QStyledItemDelegate>

namespace sam
{
// Draws a game as a tile: capsule artwork with the name underneath.
class GameDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit GameDelegate(QObject *parent = nullptr);

    // The scale sets the minimum tile width; the window then stretches tiles
    // so that the columns fill the whole row.
    void setScale(qreal scale);
    qreal scale() const { return m_scale; }
    int minimumTileWidth() const;
    void setTileWidth(int width) { m_tileWidth = width; }
    int tileWidth() const { return m_tileWidth; }
    QSize imageSize() const;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    qreal m_scale = 1.0;
    int m_tileWidth = 0;
};
} // namespace sam
