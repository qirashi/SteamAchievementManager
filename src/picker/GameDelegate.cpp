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

#include "GameDelegate.h"

#include "GameListModel.h"
#include "Icons.h"
#include "PlayTimes.h"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>

namespace sam
{
namespace
{
// Steam's small capsule is 231x87.
constexpr int kImageWidth = 231;
constexpr int kImageHeight = 87;
constexpr int kPadding = 6;
constexpr int kMargin = 4;
constexpr qreal kRadius = 6.0;

QColor placeholderColor(uint32_t id, const QPalette &palette)
{
    const bool dark = palette.color(QPalette::Base).lightness() < 128;
    const int hue = static_cast<int>((id * 2654435761u) % 360u);
    return QColor::fromHsl(hue, dark ? 60 : 70, dark ? 60 : 200);
}

QString typeLabel(const QString &type)
{
    if (type == QLatin1StringView("demo"))
    {
        return QCoreApplication::translate("GameDelegate", "Demo");
    }
    if (type == QLatin1StringView("mod"))
    {
        return QCoreApplication::translate("GameDelegate", "Mod");
    }
    if (type == QLatin1StringView("junk"))
    {
        return QCoreApplication::translate("GameDelegate", "Junk");
    }
    return {};
}
} // namespace

GameDelegate::GameDelegate(QObject *parent) : QStyledItemDelegate(parent)
{
}

void GameDelegate::setScale(qreal scale)
{
    m_scale = qBound(0.6, scale, 1.6);
}

int GameDelegate::minimumTileWidth() const
{
    return qRound(kImageWidth * m_scale) + 2 * (kPadding + kMargin);
}

QSize GameDelegate::imageSize() const
{
    const int tileWidth = qMax(m_tileWidth, minimumTileWidth());
    const int width = tileWidth - 2 * (kPadding + kMargin);
    return QSize(width, qRound(width * kImageHeight / static_cast<qreal>(kImageWidth)));
}

QSize GameDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const
{
    const QSize image = imageSize();
    const int textHeight = option.fontMetrics.height() + 4;
    return QSize(image.width() + 2 * (kPadding + kMargin), image.height() + textHeight + 2 * (kPadding + kMargin));
}

void GameDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);

    const QPalette &palette = option.palette;
    const QRectF tile = QRectF(option.rect).adjusted(kMargin, kMargin, -kMargin, -kMargin);
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;
    const bool active = option.state & QStyle::State_Active;

    // Tile background.
    QColor background = palette.color(QPalette::Base);
    QColor highlight = palette.color(active ? QPalette::Active : QPalette::Inactive, QPalette::Highlight);
    if (selected)
    {
        background = highlight;
        background.setAlphaF(0.28f);
    }
    else if (hovered)
    {
        background = highlight;
        background.setAlphaF(0.12f);
    }
    painter->setPen(Qt::NoPen);
    painter->setBrush(background);
    painter->drawRoundedRect(tile, kRadius + 2, kRadius + 2);
    if (selected)
    {
        painter->setPen(QPen(highlight, 2.0));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(tile.adjusted(1, 1, -1, -1), kRadius + 1, kRadius + 1);
    }

    // Artwork.
    const QSize imageSize = this->imageSize();
    const QRectF imageRect(tile.left() + kPadding, tile.top() + kPadding, imageSize.width(), imageSize.height());
    QPainterPath clip;
    clip.addRoundedRect(imageRect, kRadius, kRadius);
    painter->setClipPath(clip);

    const QImage image = index.data(GameListModel::ImageRole).value<QImage>();
    const QString name = index.data(Qt::DisplayRole).toString();
    if (!image.isNull())
    {
        const QSizeF scaled = QSizeF(image.size()).scaled(imageRect.size(), Qt::KeepAspectRatioByExpanding);
        const QRectF target(imageRect.center().x() - scaled.width() / 2,
                            imageRect.center().y() - scaled.height() / 2,
                            scaled.width(),
                            scaled.height());
        painter->drawImage(target, image);
    }
    else
    {
        const uint32_t id = index.data(GameListModel::IdRole).toUInt();
        const QColor base = placeholderColor(id, palette);
        QLinearGradient gradient(imageRect.topLeft(), imageRect.bottomRight());
        gradient.setColorAt(0.0, base.lighter(115));
        gradient.setColorAt(1.0, base.darker(125));
        painter->fillRect(imageRect, gradient);

        QFont font = option.font;
        font.setPointSizeF(font.pointSizeF() * 1.15 * m_scale);
        font.setBold(true);
        painter->setFont(font);
        QColor text = base.lightness() < 128 ? QColor(255, 255, 255, 220) : QColor(0, 0, 0, 190);
        painter->setPen(text);
        const QRectF textRect = imageRect.adjusted(10, 6, -10, -6);
        const QString elided = QFontMetricsF(font).elidedText(name, Qt::ElideRight, textRect.width() * 2.0);
        painter->drawText(textRect, Qt::AlignCenter | Qt::TextWordWrap, elided);
    }
    painter->setClipping(false);

    // Type badge (demo / mod / junk).
    const QString badge = typeLabel(index.data(GameListModel::TypeRole).toString());
    if (!badge.isEmpty())
    {
        QFont font = option.font;
        font.setPointSizeF(font.pointSizeF() * 0.8);
        font.setBold(true);
        const QFontMetricsF metrics(font);
        const QRectF badgeRect(imageRect.right() - metrics.horizontalAdvance(badge) - 14,
                               imageRect.top() + 5,
                               metrics.horizontalAdvance(badge) + 9,
                               metrics.height() + 2);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0, 0, 0, 170));
        painter->drawRoundedRect(badgeRect, 4, 4);
        painter->setPen(Qt::white);
        painter->setFont(font);
        painter->drawText(badgeRect, Qt::AlignCenter, badge);
    }

    // Playtime, at the bottom left of the artwork.
    if (const int minutes = index.data(GameListModel::PlaytimeRole).toInt(); minutes > 0)
    {
        QFont font = option.font;
        font.setPointSizeF(font.pointSizeF() * 0.8);
        font.setBold(true);
        const QFontMetricsF metrics(font);
        const QString text = formatPlaytime(minutes);
        const qreal iconSize = metrics.height() - 3;
        const QRectF pill(imageRect.left() + 5,
                          imageRect.bottom() - metrics.height() - 7,
                          metrics.horizontalAdvance(text) + iconSize + 14,
                          metrics.height() + 2);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0, 0, 0, 170));
        painter->drawRoundedRect(pill, 4, 4);
        const QRectF iconRect(pill.left() + 5, pill.center().y() - iconSize / 2, iconSize, iconSize);
        icons::get(QStringLiteral("play"), QColor(0x5f, 0xd3, 0x76)).paint(painter, iconRect.toAlignedRect());
        painter->setPen(Qt::white);
        painter->setFont(font);
        painter->drawText(pill.adjusted(iconSize + 8, 0, -5, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
    }

    // Name.
    const QRectF textRect(imageRect.left(), imageRect.bottom() + 4, imageRect.width(), option.fontMetrics.height());
    painter->setFont(option.font);
    painter->setPen(palette.color(QPalette::Text));
    painter->drawText(textRect,
                      Qt::AlignHCenter | Qt::AlignVCenter,
                      option.fontMetrics.elidedText(name, Qt::ElideRight, qRound(textRect.width())));

    painter->restore();
}
} // namespace sam
