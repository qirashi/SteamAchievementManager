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

#include "Style.h"

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QEasingCurve>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QProxyStyle>
#include <QScrollBar>
#include <QStyleFactory>
#include <QStyleOptionMenuItem>
#include <QStyleOptionSlider>
#include <QTimer>
#include <QVariantAnimation>
#include <QWheelEvent>

#include <algorithm>

namespace sam
{
namespace
{
// Menus: the radius of their corners, and the size and radius of the check
// box of an item (the one the style picks is meant for regular-height items).
constexpr int kMenuRadius = 6;
constexpr int kMenuIndicatorSize = 14;
constexpr int kMenuIndicatorRadius = 3;

QString colorName(const QColor &color)
{
    return color.name(QColor::HexRgb);
}

// The outline of a check box, the way Fusion takes it from the window color
// (QFusionStylePrivate::outline).
QColor checkBoxBorderColor(const QColor &window, bool dark)
{
    const QColor outline = window.darker(140);
    return dark ? outline.lighter(240) : outline.lighter(110);
}

const char *const kOverlayProperty = "samOverlayScrollBar";
const char *const kSmoothProperty = "samSmoothScroll";

// The bar takes a strip this wide over the contents and is drawn inset from all its edges.
constexpr int kOverlayExtent = 10;
constexpr int kOverlayInset = 3;
constexpr int kOverlayMinLength = 20;
constexpr int kFadeDuration = 150;
constexpr int kHideDelay = 1000;
constexpr int kSmoothDuration = 220;

// The vertical/horizontal bar of a scroll area (which keeps them in containers
// of its own). Scroll bars elsewhere keep their regular look.
QAbstractScrollArea *scrollAreaOf(const QWidget *bar)
{
    if (bar == nullptr || bar->parentWidget() == nullptr)
    {
        return nullptr;
    }
    auto *area = qobject_cast<QAbstractScrollArea *>(bar->parentWidget()->parentWidget());
    if (area == nullptr || (area->verticalScrollBar() != bar && area->horizontalScrollBar() != bar))
    {
        return nullptr;
    }
    return area;
}

bool isOverlayScrollBar(const QWidget *widget)
{
    return scrollAreaOf(widget) != nullptr;
}

void animateTo(QVariantAnimation &animation, qreal target)
{
    if (animation.endValue().isValid() && animation.endValue().toReal() == target)
    {
        return;
    }
    const qreal current = animation.currentValue().toReal();
    animation.stop();
    animation.setStartValue(current);
    animation.setEndValue(target);
    animation.start();
}

// Fade state of an overlay scroll bar: it shows up while its area is scrolled
// or entered by the cursor and fades away a second later, unless the cursor
// stays on it or drags it.
class OverlayScrollBar : public QObject
{
public:
    static void attach(QScrollBar *bar)
    {
        if (isOverlayScrollBar(bar) && !bar->property(kOverlayProperty).isValid())
        {
            new OverlayScrollBar(bar);
        }
    }

    static OverlayScrollBar *of(const QWidget *widget)
    {
        return widget != nullptr
                   ? static_cast<OverlayScrollBar *>(qvariant_cast<QObject *>(widget->property(kOverlayProperty)))
                   : nullptr;
    }

    void flash()
    {
        animateTo(m_opacity, 1.0);
        if (!isHeld())
        {
            m_hideTimer.start();
        }
    }

    void hide()
    {
        if (!isHeld())
        {
            m_hideTimer.stop();
            animateTo(m_opacity, 0.0);
        }
    }

    qreal opacity() const { return m_opacity.currentValue().toReal(); }
    qreal hover() const { return m_hover.currentValue().toReal(); }

protected:
    // A dragged bar has the mouse grabbed, so it's left only once released.
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() == QEvent::Enter)
        {
            m_hideTimer.stop();
            animateTo(m_opacity, 1.0);
            animateTo(m_hover, 1.0);
        }
        else if (event->type() == QEvent::Leave)
        {
            animateTo(m_hover, 0.0);
            if (!m_bar->isSliderDown())
            {
                m_hideTimer.start();
            }
        }
        return QObject::eventFilter(object, event);
    }

private:
    explicit OverlayScrollBar(QScrollBar *bar) : QObject(bar), m_bar(bar)
    {
        m_bar->setProperty(kOverlayProperty, QVariant::fromValue<QObject *>(this));
        m_bar->installEventFilter(this);

        m_hideTimer.setSingleShot(true);
        m_hideTimer.setInterval(kHideDelay);
        connect(&m_hideTimer, &QTimer::timeout, this, [this] {
            animateTo(m_opacity, 0.0);
        });
        for (QVariantAnimation *animation : {&m_opacity, &m_hover})
        {
            animation->setDuration(kFadeDuration);
            connect(animation, &QVariantAnimation::valueChanged, m_bar, qOverload<>(&QWidget::update));
        }
        connect(m_bar, &QAbstractSlider::valueChanged, this, &OverlayScrollBar::flash);
        connect(m_bar, &QAbstractSlider::sliderReleased, this, [this] {
            if (!m_bar->underMouse())
            {
                m_hideTimer.start();
            }
        });
    }

    bool isHeld() const { return m_bar->underMouse() || m_bar->isSliderDown(); }

    QScrollBar *m_bar;
    QTimer m_hideTimer;
    QVariantAnimation m_opacity;
    QVariantAnimation m_hover;
};

// Eases a scroll bar towards a target value; wheel notches arriving while it
// runs extend the target instead of restarting from the current position.
class SmoothScroll : public QObject
{
public:
    static SmoothScroll *of(QScrollBar *bar)
    {
        auto *existing = static_cast<SmoothScroll *>(qvariant_cast<QObject *>(bar->property(kSmoothProperty)));
        return existing != nullptr ? existing : new SmoothScroll(bar);
    }

    void scrollBy(int pixels)
    {
        const bool running = m_animation.state() == QAbstractAnimation::Running;
        const int from = running ? m_target : m_bar->value();
        m_target = std::clamp(from + pixels, m_bar->minimum(), m_bar->maximum());
        m_animation.stop();
        if (m_target == m_bar->value())
        {
            return;
        }
        m_animation.setStartValue(static_cast<qreal>(m_bar->value()));
        m_animation.setEndValue(static_cast<qreal>(m_target));
        m_animation.start();
    }

private:
    explicit SmoothScroll(QScrollBar *bar) : QObject(bar), m_bar(bar)
    {
        m_bar->setProperty(kSmoothProperty, QVariant::fromValue<QObject *>(this));
        m_animation.setDuration(kSmoothDuration);
        m_animation.setEasingCurve(QEasingCurve::OutCubic);
        connect(&m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            m_animating = true;
            m_bar->setValue(qRound(value.toReal()));
            m_animating = false;
        });
        // Anything else moving the bar (dragging, keys, scrollTo...) wins.
        connect(m_bar, &QAbstractSlider::valueChanged, this, [this] {
            if (!m_animating)
            {
                m_animation.stop();
            }
        });
    }

    QScrollBar *m_bar;
    QVariantAnimation m_animation;
    int m_target = 0;
    bool m_animating = false;
};

class ApplicationStyle : public QProxyStyle
{
public:
    ApplicationStyle() : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))) {}

    // A menu whose items are styled draws them itself and asks the style for
    // a bare check mark of a checked item only, which loses the check box the
    // style draws everywhere else. This answers with the very same check box
    // (or radio button), its corners rounded like the empty box of an
    // unchecked item (see menuIndicatorStyle).
    void drawPrimitive(PrimitiveElement element,
                       const QStyleOption *option,
                       QPainter *painter,
                       const QWidget *widget) const override
    {
        const auto *menuItem = qstyleoption_cast<const QStyleOptionMenuItem *>(option);
        if (element != PE_IndicatorMenuCheckMark || menuItem == nullptr)
        {
            QProxyStyle::drawPrimitive(element, option, painter, widget);
            return;
        }

        QStyleOptionButton indicator;
        indicator.QStyleOption::operator=(*option);
        // The item keeps its check state in a field of its own, and the states
        // of the item itself must not reach the indicator drawn inside it.
        indicator.state.setFlag(State_On, menuItem->checked);
        indicator.state.setFlag(State_Off, !menuItem->checked);
        indicator.state &= ~(State_Selected | State_MouseOver | State_Sunken | State_HasFocus);
        if (menuItem->checkType == QStyleOptionMenuItem::Exclusive)
        {
            QProxyStyle::drawPrimitive(PE_IndicatorRadioButton, &indicator, painter, widget);
            return;
        }

        // Fusion draws the box with square corners: draw it aside, cut its
        // corners off and draw the outline over them once again.
        const qreal scale = painter->device()->devicePixelRatioF();
        QPixmap box(option->rect.size() * scale);
        box.setDevicePixelRatio(scale);
        box.fill(Qt::transparent);
        indicator.rect = QRect(QPoint(0, 0), option->rect.size());
        QPainter boxPainter(&box);
        QProxyStyle::drawPrimitive(PE_IndicatorCheckBox, &indicator, &boxPainter, widget);
        boxPainter.end();

        // Not the palette of the option: the style sheet makes the background
        // of a menu item transparent, and with it the window color there.
        const QColor window = QApplication::palette().color(QPalette::Window);
        const QRectF rect(QPointF(0, 0), QSizeF(indicator.rect.size()));
        QPainterPath corners;
        corners.addRoundedRect(rect, kMenuIndicatorRadius, kMenuIndicatorRadius);
        QPainterPath outline;
        outline.addRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), kMenuIndicatorRadius, kMenuIndicatorRadius);
        QPainter cornerPainter(&box);
        cornerPainter.setRenderHint(QPainter::Antialiasing);
        cornerPainter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        cornerPainter.fillPath(corners, Qt::white);
        cornerPainter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        cornerPainter.setPen(checkBoxBorderColor(window, window.lightness() < 128));
        cornerPainter.drawPath(outline);
        cornerPainter.end();

        painter->drawPixmap(option->rect.topLeft(), box);
    }

    void drawComplexControl(ComplexControl control,
                            const QStyleOptionComplex *option,
                            QPainter *painter,
                            const QWidget *widget) const override
    {
        const auto *scrollBar = qstyleoption_cast<const QStyleOptionSlider *>(option);
        if (control == CC_ScrollBar && scrollBar != nullptr && isOverlayScrollBar(widget))
        {
            drawOverlayScrollBar(scrollBar, painter, widget);
            return;
        }
        QProxyStyle::drawComplexControl(control, option, painter, widget);
    }

    // An overlay scroll bar keeps off the ends of its strip as well as off its sides.
    QRect subControlRect(ComplexControl control,
                         const QStyleOptionComplex *option,
                         SubControl subControl,
                         const QWidget *widget) const override
    {
        const auto *scrollBar = qstyleoption_cast<const QStyleOptionSlider *>(option);
        if (control != CC_ScrollBar || scrollBar == nullptr || !isOverlayScrollBar(widget))
        {
            return QProxyStyle::subControlRect(control, option, subControl, widget);
        }
        const QPoint inset =
            scrollBar->orientation == Qt::Horizontal ? QPoint(kOverlayInset, 0) : QPoint(0, kOverlayInset);
        QStyleOptionSlider inner(*scrollBar);
        inner.rect.setSize(inner.rect.size() - 2 * QSize(inset.x(), inset.y()));
        return QProxyStyle::subControlRect(control, &inner, subControl, widget).translated(inset);
    }

    // Qt lays transient scroll bars over the contents of their area
    // (QAbstractScrollAreaPrivate::layoutChildren). A click on the narrow
    // groove moves the bar there and drags it.
    int styleHint(StyleHint hint,
                  const QStyleOption *option,
                  const QWidget *widget,
                  QStyleHintReturn *returnData) const override
    {
        if ((hint == SH_ScrollBar_Transient || hint == SH_ScrollBar_LeftClickAbsolutePosition) &&
            isOverlayScrollBar(widget))
        {
            return 1;
        }
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }

    // Any overlap lays the scroll bar over the contents; the whole strip is taken.
    int pixelMetric(PixelMetric metric, const QStyleOption *option, const QWidget *widget) const override
    {
        if ((metric == PM_ScrollBarExtent || metric == PM_ScrollView_ScrollBarOverlap) && isOverlayScrollBar(widget))
        {
            return kOverlayExtent;
        }
        if (metric == PM_ScrollBarSliderMin && isOverlayScrollBar(widget))
        {
            return kOverlayMinLength;
        }
        return QProxyStyle::pixelMetric(metric, option, widget);
    }

private:
    // Telegram's alphas, over the text color instead of black: the groove grows
    // stronger while the cursor is on the strip, the bar while it's dragged.
    void drawOverlayScrollBar(const QStyleOptionSlider *option, QPainter *painter, const QWidget *widget) const
    {
        const OverlayScrollBar *overlay = OverlayScrollBar::of(widget);
        const qreal opacity = overlay != nullptr ? overlay->opacity() : 1.0;
        if (opacity <= 0.0)
        {
            return;
        }

        const qreal hover = overlay != nullptr ? overlay->hover() : 0.0;
        const bool sliderActive = (option->activeSubControls & SC_ScrollBarSlider) != 0;
        const QMargins across = option->orientation == Qt::Horizontal ? QMargins(0, kOverlayInset, 0, kOverlayInset)
                                                                      : QMargins(kOverlayInset, 0, kOverlayInset, 0);
        const QRectF groove(
            proxy()->subControlRect(CC_ScrollBar, option, SC_ScrollBarGroove, widget).marginsRemoved(across));
        const QRectF slider(
            proxy()->subControlRect(CC_ScrollBar, option, SC_ScrollBarSlider, widget).marginsRemoved(across));
        const qreal radius = std::min(groove.width(), groove.height()) / 2.0;

        QColor grooveColor = option->palette.color(QPalette::WindowText);
        QColor sliderColor = grooveColor;
        grooveColor.setAlpha(qRound(opacity * (0x1A + (0x2C - 0x1A) * hover)));
        sliderColor.setAlpha(qRound(opacity * (sliderActive ? 0x7A : 0x53)));

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(grooveColor);
        painter->drawRoundedRect(groove, radius, radius);
        painter->setBrush(sliderColor);
        painter->drawRoundedRect(slider, radius, radius);
        painter->restore();
    }
};

class StyleFilter : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        switch (event->type())
        {
            case QEvent::Polish:
                if (auto *bar = qobject_cast<QScrollBar *>(object))
                {
                    OverlayScrollBar::attach(bar);
                }
                // A style sheet radius rounds only what's painted inside the
                // rectangular window of a menu, so the window is made
                // transparent around it. On Windows that takes a frameless
                // window (or the corners come out black), and the rectangular
                // system shadow would show in the corners. Both are asked for
                // before the native window exists: a menu is polished while it
                // computes its size, before its first show.
                if (auto *menu = qobject_cast<QMenu *>(object); menu != nullptr && menu->isWindow())
                {
                    menu->setAttribute(Qt::WA_TranslucentBackground);
                    menu->overrideWindowFlags(menu->windowFlags() | Qt::FramelessWindowHint |
                                              Qt::NoDropShadowWindowHint);
                }
                break;
            case QEvent::Enter:
            case QEvent::Leave:
                if (auto *area = qobject_cast<QAbstractScrollArea *>(object))
                {
                    for (QScrollBar *bar : {area->horizontalScrollBar(), area->verticalScrollBar()})
                    {
                        if (OverlayScrollBar *overlay = OverlayScrollBar::of(bar))
                        {
                            event->type() == QEvent::Enter ? overlay->flash() : overlay->hide();
                        }
                    }
                }
                break;
            case QEvent::Wheel: return smoothWheel(object, static_cast<QWheelEvent *>(event));
            default: break;
        }
        return QObject::eventFilter(object, event);
    }

private:
    // Plain mouse wheel notches over a scroll area's viewport or vertical bar.
    // Touchpads already scroll smoothly (pixel deltas); modified wheels (zoom,
    // horizontal scrolling) keep Qt's handling.
    static bool smoothWheel(QObject *object, QWheelEvent *wheel)
    {
        if (wheel->modifiers() != Qt::NoModifier || !wheel->pixelDelta().isNull() ||
            wheel->phase() != Qt::NoScrollPhase || wheel->angleDelta().x() != 0 || wheel->angleDelta().y() == 0)
        {
            return false;
        }

        auto *widget = qobject_cast<QWidget *>(object);
        if (widget == nullptr)
        {
            return false;
        }
        QAbstractScrollArea *area = qobject_cast<QAbstractScrollArea *>(widget->parentWidget());
        if (area == nullptr || area->viewport() != widget)
        {
            area = scrollAreaOf(widget);
            if (area == nullptr || area->verticalScrollBar() != widget)
            {
                return false;
            }
        }
        // Views scrolling item by item would only animate between whole rows.
        if (auto *view = qobject_cast<QAbstractItemView *>(area);
            view != nullptr && view->verticalScrollMode() != QAbstractItemView::ScrollPerPixel)
        {
            return false;
        }

        QScrollBar *bar = area->verticalScrollBar();
        if (!bar->isEnabled() || bar->minimum() >= bar->maximum())
        {
            return false;
        }
        const int step = QApplication::wheelScrollLines() * bar->singleStep();
        SmoothScroll::of(bar)->scrollBy(-wheel->angleDelta().y() * step / 120);
        wheel->accept();
        return true;
    }
};
// The palettes of the dark ("Dark Modern") and the light theme.
QPalette themePalette(const QColor &window, bool dark)
{
    QPalette palette(window);
    const QColor text = dark ? QColor(0xF2, 0xF2, 0xF2) : QColor(Qt::black);
    const QColor disabledText = dark ? QColor(0x75, 0x75, 0x75) : QColor(0, 0, 0, 0x50);

    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, dark ? text : QColor(0x25, 0x25, 0x25));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
    palette.setColor(QPalette::Dark, window);
    palette.setColor(QPalette::Midlight, dark ? window.lighter(130) : window.darker(130));
    palette.setColor(QPalette::Mid, dark ? QColor(0x4C, 0x4C, 0x4C) : QColor(0xD3, 0xD3, 0xD3));
    palette.setColor(QPalette::Light, window);
    palette.setColor(QPalette::Disabled, QPalette::Light, window);

    palette.setColor(QPalette::Base, dark ? window.darker(130) : QColor(Qt::white));
    if (!dark)
    {
        palette.setColor(QPalette::Disabled, QPalette::Base, window);
    }
    palette.setColor(QPalette::AlternateBase, window);
    palette.setColor(QPalette::ToolTipBase, window);
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::PlaceholderText, disabledText);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Disabled, QPalette::Text, disabledText);

    palette.setColor(QPalette::Button, window);
    palette.setColor(QPalette::ButtonText, dark ? text : text.lighter(130));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
    palette.setColor(QPalette::BrightText, Qt::red);
    palette.setColor(QPalette::Link, dark ? QColor(0x2A, 0x82, 0xDA) : QColor(Qt::blue));

    palette.setColor(QPalette::Highlight, dark ? QColor(0x37, 0x63, 0xAD) : QColor(0x88, 0xBB, 0xFF));
    palette.setColor(QPalette::Inactive, QPalette::Highlight, dark ? disabledText : window.darker(120));
    palette.setColor(QPalette::Disabled, QPalette::Highlight, dark ? disabledText : window.darker(120));
    palette.setColor(QPalette::HighlightedText, dark ? QColor(Qt::white) : text);
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, dark ? QColor(0xC0, 0xC0, 0xC0) : disabledText);
    return palette;
}

// Menus in the manner of Visual Studio: the selection is a rounded block inset
// from the edges of the menu. Styling the box model of an item makes the menu
// draw the items itself (see ApplicationStyle::drawPrimitive).
QString menuStyle(const QColor &background, const QColor &selected, const QColor &separator, const QColor &border)
{
    return QStringLiteral("QMenu { background: %1; border: 1px solid %4; border-radius: %5px; padding: 3px 0px; }\n"
                          "QMenu::item { padding: 4px 24px 4px 12px; margin: 0px 3px; border-radius: 4px;"
                          " background: transparent; }\n"
                          "QMenu::item:selected { background: %2; }\n"
                          "QMenu::icon { left: 6px; }\n"
                          "QMenu::separator { height: 1px; margin: 2px 0px; background: %3; }\n")
        .arg(colorName(background), colorName(selected), colorName(separator), colorName(border))
        .arg(kMenuRadius);
}

// The style is asked for the check box of a checked item only (see
// ApplicationStyle::drawPrimitive), so the empty one is painted here, repeating
// what Fusion paints around a check mark: a gradient over the top of the box
// and the outlines of the check box and the radio button.
QString menuIndicatorStyle(const QColor &window, bool dark)
{
    const QColor base = dark ? window.darker(130) : QColor(Qt::white);
    const QString fill = QStringLiteral("qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,"
                                        " stop: 0 %1, stop: 0.15 %2, stop: 1 %2)")
                             .arg(colorName(base.darker(115)), colorName(base));
    // A size given to a subcontrol excludes its border, hence the 2 px less.
    return QStringLiteral("QMenu::indicator { left: 6px; width: %1px; height: %1px; }\n"
                          "QMenu::indicator:non-exclusive:unchecked { width: %2px; height: %2px;"
                          " border: 1px solid %3; border-radius: %4px; background: %5; }\n"
                          "QMenu::indicator:exclusive:unchecked { width: %2px; height: %2px;"
                          " border: 1px solid %6; border-radius: %7px; background: %8; }\n")
        .arg(kMenuIndicatorSize)
        .arg(kMenuIndicatorSize - 2)
        .arg(colorName(checkBoxBorderColor(window, dark)))
        .arg(kMenuIndicatorRadius)
        .arg(fill, colorName(window.darker(150)))
        .arg(kMenuIndicatorSize / 2)
        .arg(colorName(base));
}

// Rounded blocks: every tool bar of a window (the way Visual Studio has them),
// the header of the game window, the lists of achievements and statistics and
// the caption in the title bar.
QString blockStyle(const QColor &window, bool dark)
{
    const QColor toolBar = dark ? window.darker(102) : window.lighter(103);
    const QColor border = dark ? window.darker(139) : window.darker(118);
    const QColor caption = dark ? window.darker(105) : window.darker(103);
    // The text of the window, faded.
    const auto textAlpha = [dark](int alpha) {
        return dark ? QStringLiteral("rgba(242, 242, 242, %1)").arg(alpha)
                    : QStringLiteral("rgba(37, 37, 37, %1)").arg(alpha);
    };
    return QStringLiteral("QMainWindow > QToolBar, QToolBar#mainToolBar { background: %1; border: 1px solid %2;"
                          " border-radius: 4px; padding: 1px; spacing: 1px; }\n"
                          "QMainWindow > QToolBar { margin: 1px 4px; }\n"
                          "#gameHeader { background: palette(base); border: 1px solid %2; border-radius: 6px; }\n"
                          // Like the progress of the sections of the achievement list.
                          "#gameHeader QProgressBar { background: rgba(128, 128, 128, 50); border: none;"
                          " border-radius: 4px; }\n"
                          "#gameHeader QProgressBar::chunk { background: #3b82f6; border-radius: 4px; }\n"
                          "#titleBarCaption { background: %3; border: 1px solid %2; }\n"
                          "#listBlock { background: palette(base); border: 1px solid %2; border-radius: 8px; }\n"
                          "#listBlock QAbstractItemView { background: transparent; border: none; }\n"
                          "#listBlock QHeaderView { background: transparent; border: none; }\n"
                          "#listBlock QHeaderView::section { background: transparent; color: %4; border: none;"
                          " border-bottom: 1px solid %5; padding: 5px 10px; }\n")
        .arg(colorName(toolBar), colorName(border), colorName(caption), textAlpha(150), textAlpha(dark ? 26 : 30));
}
} // namespace

void installStyle(QApplication &app)
{
    QApplication::setStyle(new ApplicationStyle);
    app.installEventFilter(new StyleFilter(&app));
}

void applyColorScheme(bool dark)
{
    const QColor window = dark ? QColor(0x2B, 0x2B, 0x2B) : QColor(0xF0, 0xF0, 0xF0);
    const QColor menu = dark ? window.darker(115) : window.lighter(104);
    const QColor menuBorder = dark ? window.lighter(140) : window.darker(125);
    const QColor menuSelected = dark ? window.lighter(125) : window.darker(108);
    const QColor menuSeparator = dark ? window.lighter(130) : window.darker(112);

    QString sheet = menuStyle(menu, menuSelected, menuSeparator, menuBorder) + menuIndicatorStyle(window, dark) +
                    blockStyle(window, dark);
    if (dark)
    {
        sheet += QStringLiteral("QToolTip { color: #e0e0e0; background-color: #373737; border: 1px solid #4c4c4c; }\n");
    }

    QApplication::setPalette(themePalette(window, dark));
    qApp->setStyleSheet(sheet);
}
} // namespace sam
