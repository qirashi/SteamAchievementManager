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

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTextLayout>
#include <QTreeView>

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
const QColor kBlue(0x3b, 0x82, 0xf6);
const QColor kPurple(0x8b, 0x5c, 0xf6);

// A section and its achievements are painted as one rounded card, inset from
// the sides of the list; a gap above each section separates the cards.
constexpr int kCardInset = 6;
constexpr int kCardRadius = 8;
constexpr int kSectionGap = 8;
// The hover and selection blocks lie inside the card.
constexpr int kSelectionInset = 3;

// `rect` with rounded corners on the top and / or on the bottom only.
QPainterPath roundedRect(const QRectF &rect, qreal radius, bool top, bool bottom)
{
    QPainterPath path;
    path.addRoundedRect(rect.adjusted(0, top ? 0 : -radius, 0, bottom ? 0 : radius), radius, radius);
    if (top && bottom)
    {
        return path;
    }
    QPainterPath bounds;
    bounds.addRect(rect);
    return path.intersected(bounds);
}

// The row across all columns, inset from the sides of the list.
QRectF rowRect(const QStyleOptionViewItem &option)
{
    const auto *view = qobject_cast<const QAbstractItemView *>(option.widget);
    const int width = view != nullptr ? view->viewport()->width() : option.rect.right() + 1;
    return QRectF(kCardInset, option.rect.top(), width - 2 * kCardInset, option.rect.height());
}

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
    QString icon; // optional, before the text
};

qreal badgeWidth(const QFont &font, const Badge &badge)
{
    const QFontMetricsF metrics(font);
    const qreal icon = badge.icon.isEmpty() ? 0 : metrics.height() + 2;
    return metrics.horizontalAdvance(badge.text) + icon + 12;
}

qreal drawBadge(QPainter *painter, const QFont &font, const Badge &badge, qreal x, qreal centerY, bool darkBase)
{
    const QFontMetricsF metrics(font);
    const qreal width = badgeWidth(font, badge);
    const qreal height = metrics.height() + 2;
    const QRectF rect(x, centerY - height / 2, width, height);

    QColor fill = badge.color;
    fill.setAlphaF(darkBase ? 0.28f : 0.16f);
    painter->setPen(Qt::NoPen);
    painter->setBrush(fill);
    painter->drawRoundedRect(rect, height / 2, height / 2);

    const QColor text = darkBase ? badge.color.lighter(150) : badge.color.darker(120);
    QRectF textRect = rect;
    if (!badge.icon.isEmpty())
    {
        const qreal size = metrics.height() - 2;
        const QRectF iconRect(rect.left() + 6, centerY - size / 2, size, size);
        icons::get(badge.icon, text).paint(painter, iconRect.toAlignedRect());
        textRect.setLeft(iconRect.right() + 2);
        textRect.setRight(rect.right() - 6);
    }
    painter->setPen(text);
    painter->setFont(font);
    painter->drawText(textRect, Qt::AlignCenter, badge.text);
    return width;
}
} // namespace

AchievementDelegate::AchievementDelegate(QObject *parent) : QStyledItemDelegate(parent)
{
}

QSize AchievementDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    if (index.data(AchievementModel::SectionRole).toBool())
    {
        // Keep the unlock time column sized by the achievements only.
        const int width = index.column() == AchievementModel::NameColumn ? 360 : 0;
        return QSize(width, option.fontMetrics.height() * 3 / 2 + 2 * kPadding + kSectionGap);
    }

    QSize size = QStyledItemDelegate::sizeHint(option, index);
    const int textHeight = option.fontMetrics.height() * 3 + 6;
    size.setHeight(qMax(kIconSize, textHeight) + 2 * kPadding);
    if (index.column() == AchievementModel::NameColumn)
    {
        size.setWidth(qMax(size.width(), 360));
    }
    else
    {
        size.rwidth() += kCardInset;
    }
    return size;
}

QRect AchievementDelegate::checkArea(const QStyleOptionViewItem &option) const
{
    return QRect(option.rect.left() + kCardInset, option.rect.top(), kCheckAreaWidth, option.rect.height());
}

void AchievementDelegate::paintBackground(QPainter *painter,
                                          const QStyleOptionViewItem &option,
                                          const QModelIndex &index) const
{
    const QPalette &palette = option.palette;
    const bool darkBase = palette.color(QPalette::Base).lightness() < 128;
    const bool section = index.data(AchievementModel::SectionRole).toBool();
    QRectF row = rowRect(option);
    if (section)
    {
        row.setTop(row.top() + kSectionGap);
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    // Every column paints its part of the row.
    painter->setClipRect(option.rect);
    painter->setPen(Qt::NoPen);

    QColor card = palette.color(QPalette::Text);
    if (section)
    {
        const QModelIndex first = index.siblingAtColumn(0);
        const auto *view = qobject_cast<const QTreeView *>(option.widget);
        const bool open = view != nullptr && view->isExpanded(first) && index.model()->rowCount(first) > 0;
        card.setAlphaF(darkBase ? 0.075f : 0.05f);
        painter->setBrush(card);
        painter->drawPath(roundedRect(row, kCardRadius, true, !open));
    }
    else if (index.parent().isValid())
    {
        const bool last = index.row() == index.model()->rowCount(index.parent()) - 1;
        card.setAlphaF(darkBase ? 0.03f : 0.022f);
        painter->setBrush(card);
        painter->drawPath(roundedRect(row, kCardRadius, false, last));
    }

    const bool selected = option.state & QStyle::State_Selected;
    if (selected || option.state & QStyle::State_MouseOver)
    {
        QColor block = palette.color(QPalette::Text);
        if (selected)
        {
            const QPalette::ColorGroup group = !(option.state & QStyle::State_Enabled) ? QPalette::Disabled
                                               : option.state & QStyle::State_Active   ? QPalette::Active
                                                                                       : QPalette::Inactive;
            block = palette.color(group, QPalette::Highlight);
        }
        else
        {
            block.setAlphaF(darkBase ? 0.07f : 0.05f);
        }
        painter->setBrush(block);
        constexpr qreal radius = kCardRadius - kSelectionInset;
        painter->drawRoundedRect(row.adjusted(kSelectionInset, 2, -kSelectionInset, -2), radius, radius);
    }
    painter->restore();
}

void AchievementDelegate::paintSection(QPainter *painter,
                                       const QStyleOptionViewItem &option,
                                       const QModelIndex &index) const
{
    paintBackground(painter, option, index);
    if (index.column() != AchievementModel::NameColumn)
    {
        return;
    }

    const QPalette &palette = option.palette;
    const bool darkBase = palette.color(QPalette::Base).lightness() < 128;
    const QRect rect = option.rect.adjusted(kCardInset, kSectionGap, 0, 0);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    const auto *view = qobject_cast<const QTreeView *>(option.widget);
    const bool expanded = view != nullptr && view->isExpanded(index);
    const int checked = index.data(AchievementModel::SectionCheckedRole).toInt();
    const int total = index.data(AchievementModel::SectionTotalRole).toInt();
    const bool modified = index.data(AchievementModel::ModifiedRole).toBool();
    const bool complete = total > 0 && checked == total;
    const QColor textColor = palette.color(QPalette::Text);
    QColor secondary = textColor;
    secondary.setAlphaF(0.68f);

    // Chevron, centered where the achievement check boxes are.
    const QPointF center(rect.left() + kCheckAreaWidth / 2.0, rect.center().y() + 0.5);
    QPainterPath chevron;
    if (expanded)
    {
        chevron.moveTo(center + QPointF(-4.5, -2));
        chevron.lineTo(center + QPointF(0, 2.5));
        chevron.lineTo(center + QPointF(4.5, -2));
    }
    else
    {
        chevron.moveTo(center + QPointF(-2, -4.5));
        chevron.lineTo(center + QPointF(2.5, 0));
        chevron.lineTo(center + QPointF(-2, 4.5));
    }
    painter->setPen(QPen(secondary, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(chevron);

    // Progress on the right: "3 / 5" and a thin bar.
    constexpr int kBarWidth = 120;
    QFont countFont = option.font;
    const QString count = QStringLiteral("%1 / %2").arg(checked).arg(total);
    const QFontMetricsF countMetrics(countFont);
    const qreal countWidth = countMetrics.horizontalAdvance(count);
    const qreal barLeft = rect.right() - kPadding - kBarWidth;
    const QRectF barRect(barLeft, rect.center().y() - 2, kBarWidth, 5);
    QColor trough = textColor;
    trough.setAlphaF(0.12f);
    painter->setPen(Qt::NoPen);
    painter->setBrush(trough);
    painter->drawRoundedRect(barRect, 2.5, 2.5);
    if (checked > 0)
    {
        painter->setBrush(complete ? kGreen : kBlue);
        painter->drawRoundedRect(
            QRectF(barRect.topLeft(), QSizeF(barRect.width() * checked / total, barRect.height())), 2.5, 2.5);
    }
    painter->setFont(countFont);
    painter->setPen(complete ? (darkBase ? kGreen.lighter(140) : kGreen.darker(110)) : secondary);
    const QRectF countRect(barLeft - 10 - countWidth, rect.top(), countWidth + 1, rect.height());
    painter->drawText(countRect, Qt::AlignRight | Qt::AlignVCenter, count);

    // Name and badges.
    QFont nameFont = option.font;
    nameFont.setBold(true);
    const QFontMetricsF nameMetrics(nameFont);
    QFont badgeFont = option.font;
    badgeFont.setPointSizeF(option.font.pointSizeF() * 0.82);

    QList<Badge> badges;
    if (index.data(AchievementModel::SectionDlcRole).toUInt() != 0)
    {
        badges.append({QStringLiteral("DLC"), kBlue});
    }
    if (modified)
    {
        badges.append({tr("changed"), kOrange});
    }
    qreal badgesWidth = 0;
    for (const auto &badge : badges)
    {
        badgesWidth += badgeWidth(badgeFont, badge) + 6;
    }

    const qreal textLeft = rect.left() + kCheckAreaWidth;
    const qreal available = countRect.left() - 16 - textLeft;
    const QString name = index.data(Qt::DisplayRole).toString();
    const qreal nameWidth = qMin(nameMetrics.horizontalAdvance(name), qMax(40.0, available - badgesWidth));
    painter->setFont(nameFont);
    painter->setPen(textColor);
    const QRectF nameRect(textLeft, rect.top(), nameWidth + 1, rect.height());
    painter->drawText(
        nameRect, Qt::AlignLeft | Qt::AlignVCenter, nameMetrics.elidedText(name, Qt::ElideRight, nameWidth + 1));

    qreal badgeX = nameRect.right() + 8;
    for (const auto &badge : badges)
    {
        if (badgeX > countRect.left() - 30)
        {
            break;
        }
        badgeX += drawBadge(painter, badgeFont, badge, badgeX, nameRect.center().y(), darkBase) + 6;
    }

    painter->restore();
}

void AchievementDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    if (index.data(AchievementModel::SectionRole).toBool())
    {
        paintSection(painter, option, index);
        return;
    }
    paintBackground(painter, option, index);
    const bool selected = option.state & QStyle::State_Selected;
    if (index.column() != AchievementModel::NameColumn)
    {
        // Only the text: the background is the card's.
        QStyleOptionViewItem opt = option;
        opt.state &= ~(QStyle::State_Selected | QStyle::State_MouseOver | QStyle::State_HasFocus);
        opt.rect.adjust(0, 0, -kCardInset, 0);
        if (selected)
        {
            opt.palette.setColor(QPalette::Text, option.palette.color(QPalette::HighlightedText));
        }
        QStyledItemDelegate::paint(painter, opt, index);
        return;
    }

    const QPalette &palette = option.palette;
    const bool darkBase = palette.color(QPalette::Base).lightness() < 128;

    painter->save();

    const QRect rect = option.rect.adjusted(kCardInset, 0, 0, 0);
    const bool checked = index.data(Qt::CheckStateRole).value<Qt::CheckState>() == Qt::Checked;
    const bool modified = index.data(AchievementModel::ModifiedRole).toBool();
    const bool isProtected = index.data(AchievementModel::ProtectedRole).toBool();
    const bool hidden = index.data(AchievementModel::HiddenRole).toBool();

    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);

    // Pending change marker.
    if (modified)
    {
        painter->setPen(Qt::NoPen);
        painter->setBrush(checked ? kGreen : kOrange);
        painter->drawRoundedRect(
            QRectF(rect.left() + kSelectionInset + 1, rect.top() + 8, 3, rect.height() - 16), 1.5, 1.5);
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
        badges.append({tr("hidden"), kPurple, QStringLiteral("hidden")});
    }
    qreal badgesWidth = 0;
    for (const auto &badge : badges)
    {
        badgesWidth += badgeWidth(badgeFont, badge) + 6;
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
