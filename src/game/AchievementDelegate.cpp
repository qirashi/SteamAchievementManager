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

#include "AchievementDelegate.h"

#include "AchievementModel.h"
#include "Icons.h"

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTextLayout>

namespace sam
{
namespace
{
constexpr int kIconSize = 48;
constexpr int kPadding = 8;
constexpr int kCheckSize = 18;
constexpr int kCheckAreaWidth = kCheckSize + 2 * 10;

const QColor kGreen(0x2e, 0xa0, 0x43);
const QColor kOrange(0xd2, 0x8a, 0x22);
const QColor kRed(0xd7, 0x3a, 0x49);

// Lays out `text` in at most `maxLines` lines, eliding the last one.
QStringList wrapLines(const QString &text, const QFont &font, qreal width, int maxLines)
{
    QStringList lines;
    if (text.isEmpty() || width <= 0)
    {
        return lines;
    }

    const QFontMetricsF metrics(font);
    QTextLayout layout(text, font);
    QTextOption textOption;
    textOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(textOption);
    layout.beginLayout();
    while (true)
    {
        QTextLine line = layout.createLine();
        if (!line.isValid())
        {
            break;
        }
        line.setLineWidth(width);
        if (lines.size() == maxLines - 1)
        {
            const QString rest = text.mid(line.textStart()).simplified();
            lines.append(metrics.elidedText(rest, Qt::ElideRight, width));
            break;
        }
        lines.append(text.mid(line.textStart(), line.textLength()).trimmed());
    }
    layout.endLayout();
    return lines;
}

struct Badge
{
    QString text;
    QColor color;
};

qreal drawBadge(QPainter *painter, const QFont &font, const Badge &badge, qreal x, qreal centerY, bool darkBase)
{
    const QFontMetricsF metrics(font);
    const qreal width = metrics.horizontalAdvance(badge.text) + 12;
    const qreal height = metrics.height() + 2;
    const QRectF rect(x, centerY - height / 2, width, height);

    QColor fill = badge.color;
    fill.setAlphaF(darkBase ? 0.28f : 0.16f);
    painter->setPen(Qt::NoPen);
    painter->setBrush(fill);
    painter->drawRoundedRect(rect, height / 2, height / 2);

    QColor text = darkBase ? badge.color.lighter(150) : badge.color.darker(120);
    painter->setPen(text);
    painter->setFont(font);
    painter->drawText(rect, Qt::AlignCenter, badge.text);
    return width;
}
} // namespace

AchievementDelegate::AchievementDelegate(QObject *parent) : QStyledItemDelegate(parent)
{
}

QSize AchievementDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    const int textHeight = option.fontMetrics.height() * 3 + 6;
    size.setHeight(qMax(kIconSize, textHeight) + 2 * kPadding);
    if (index.column() == AchievementModel::NameColumn)
    {
        size.setWidth(qMax(size.width(), 360));
    }
    return size;
}

QRect AchievementDelegate::checkArea(const QStyleOptionViewItem &option) const
{
    return QRect(option.rect.left(), option.rect.top(), kCheckAreaWidth, option.rect.height());
}

void AchievementDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    if (index.column() != AchievementModel::NameColumn)
    {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    const QWidget *widget = opt.widget;
    QStyle *style = widget != nullptr ? widget->style() : QApplication::style();
    const QPalette &palette = opt.palette;
    const bool darkBase = palette.color(QPalette::Base).lightness() < 128;
    const bool selected = opt.state & QStyle::State_Selected;

    painter->save();

    // Background (selection / hover) as the style draws it.
    opt.text.clear();
    opt.icon = QIcon();
    opt.features &= ~QStyleOptionViewItem::HasCheckIndicator;
    style->drawPrimitive(QStyle::PE_PanelItemViewItem, &opt, painter, widget);

    const QRect rect = option.rect;
    const bool checked = index.data(Qt::CheckStateRole).value<Qt::CheckState>() == Qt::Checked;
    const bool modified = index.data(AchievementModel::ModifiedRole).toBool();
    const bool isProtected = index.data(AchievementModel::ProtectedRole).toBool();
    const bool hidden = index.data(AchievementModel::HiddenRole).toBool();

    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);

    // Pending change marker.
    if (modified)
    {
        painter->fillRect(QRect(rect.left(), rect.top() + 4, 3, rect.height() - 8), checked ? kGreen : kOrange);
    }

    // Check box.
    const QRectF box(rect.left() + (kCheckAreaWidth - kCheckSize) / 2.0 + 0.5,
                     rect.top() + (rect.height() - kCheckSize) / 2.0 + 0.5,
                     kCheckSize - 1,
                     kCheckSize - 1);
    painter->save();
    if (isProtected)
    {
        painter->setOpacity(0.4);
    }
    if (checked)
    {
        painter->setPen(Qt::NoPen);
        painter->setBrush(kGreen);
        painter->drawRoundedRect(box, 4, 4);
        QPen tick(Qt::white, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter->setPen(tick);
        painter->setBrush(Qt::NoBrush);
        QPainterPath mark;
        mark.moveTo(box.left() + box.width() * 0.24, box.top() + box.height() * 0.52);
        mark.lineTo(box.left() + box.width() * 0.43, box.top() + box.height() * 0.71);
        mark.lineTo(box.left() + box.width() * 0.77, box.top() + box.height() * 0.31);
        painter->drawPath(mark);
    }
    else
    {
        QColor border = palette.color(QPalette::Text);
        border.setAlphaF(0.5f);
        painter->setPen(QPen(border, 1.4));
        painter->setBrush(palette.color(QPalette::Base));
        painter->drawRoundedRect(box, 4, 4);
    }
    painter->restore();

    // Icon.
    const QRect iconRect(
        rect.left() + kCheckAreaWidth, rect.top() + (rect.height() - kIconSize) / 2, kIconSize, kIconSize);
    QPainterPath clip;
    clip.addRoundedRect(iconRect, 6, 6);
    const QImage icon = index.data(AchievementModel::IconRole).value<QImage>();
    painter->save();
    painter->setClipPath(clip);
    if (!icon.isNull())
    {
        painter->drawImage(iconRect, icon);
    }
    else
    {
        QColor placeholder = palette.color(QPalette::Text);
        placeholder.setAlphaF(0.08f);
        painter->fillRect(iconRect, placeholder);
        icons::get(QStringLiteral("trophy"))
            .paint(painter, iconRect.adjusted(12, 12, -12, -12), Qt::AlignCenter, QIcon::Disabled);
    }
    painter->restore();

    // Text.
    const int textLeft = iconRect.right() + 1 + 12;
    const int textWidth = rect.right() - textLeft - kPadding;
    const QColor textColor = palette.color(selected ? QPalette::HighlightedText : QPalette::Text);
    QColor secondary = textColor;
    secondary.setAlphaF(0.68f);

    QFont nameFont = option.font;
    nameFont.setBold(true);
    const QFontMetricsF nameMetrics(nameFont);
    QFont badgeFont = option.font;
    badgeFont.setPointSizeF(option.font.pointSizeF() * 0.82);

    const QString description = index.data(AchievementModel::DescriptionRole).toString();
    const QStringList descriptionLines = wrapLines(description, option.font, textWidth, 2);
    const qreal lineHeight = option.fontMetrics.height();
    const qreal blockHeight = nameMetrics.height() + 2 + descriptionLines.size() * lineHeight;
    qreal y = rect.top() + (rect.height() - blockHeight) / 2;

    QList<Badge> badges;
    if (modified)
    {
        badges.append({checked ? tr("will unlock") : tr("will lock"), checked ? kGreen : kOrange});
    }
    if (isProtected)
    {
        badges.append({tr("protected"), kRed});
    }
    if (hidden)
    {
        badges.append({tr("hidden"), palette.color(QPalette::Text)});
    }
    qreal badgesWidth = 0;
    for (const auto &badge : badges)
    {
        badgesWidth += QFontMetricsF(badgeFont).horizontalAdvance(badge.text) + 12 + 6;
    }

    const QString name = index.data(Qt::DisplayRole).toString();
    const qreal nameWidth = qMin(nameMetrics.horizontalAdvance(name), qMax(40.0, textWidth - badgesWidth));
    const QString elidedName = nameMetrics.elidedText(name, Qt::ElideRight, nameWidth + 1);
    painter->setFont(nameFont);
    painter->setPen(textColor);
    const QRectF nameRect(textLeft, y, nameWidth + 1, nameMetrics.height());
    painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, elidedName);

    qreal badgeX = nameRect.right() + 8;
    for (const auto &badge : badges)
    {
        if (badgeX > rect.right() - 20)
        {
            break;
        }
        badgeX += drawBadge(painter, badgeFont, badge, badgeX, nameRect.center().y(), darkBase) + 6;
    }

    y += nameMetrics.height() + 2;
    painter->setFont(option.font);
    painter->setPen(secondary);
    for (const auto &line : descriptionLines)
    {
        painter->drawText(QRectF(textLeft, y, textWidth, lineHeight), Qt::AlignLeft | Qt::AlignVCenter, line);
        y += lineHeight;
    }

    painter->restore();
}

bool AchievementDelegate::editorEvent(QEvent *event,
                                      QAbstractItemModel *model,
                                      const QStyleOptionViewItem &option,
                                      const QModelIndex &index)
{
    const QModelIndex nameIndex = index.siblingAtColumn(AchievementModel::NameColumn);
    if (!(nameIndex.flags() & Qt::ItemIsUserCheckable))
    {
        return false;
    }

    const auto toggle = [&] {
        const bool checked = nameIndex.data(Qt::CheckStateRole).value<Qt::CheckState>() == Qt::Checked;
        model->setData(nameIndex, checked ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
    };
    const auto inCheckArea = [&](QEvent *e) {
        auto *mouse = static_cast<QMouseEvent *>(e);
        return mouse->button() == Qt::LeftButton && index.column() == AchievementModel::NameColumn &&
               checkArea(option).contains(mouse->position().toPoint());
    };

    switch (event->type())
    {
        case QEvent::MouseButtonPress:
            // Swallow presses on the check box so they don't change the selection.
            return inCheckArea(event);
        case QEvent::MouseButtonDblClick:
            // Double-clicking anywhere on the row toggles it.
            if (static_cast<QMouseEvent *>(event)->button() != Qt::LeftButton)
            {
                return false;
            }
            toggle();
            m_skipNextRelease = true;
            return true;
        case QEvent::MouseButtonRelease:
            if (m_skipNextRelease)
            {
                m_skipNextRelease = false;
                return true;
            }
            if (!inCheckArea(event))
            {
                return false;
            }
            toggle();
            return true;
        default:
            // Keyboard toggling is handled by the window so it can apply to
            // the whole selection.
            return false;
    }
}
} // namespace sam
