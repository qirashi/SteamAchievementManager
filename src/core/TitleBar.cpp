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

#include "TitleBar.h"

#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QToolButton>
#include <QWindow>
#include <QtGui/qpa/qplatformwindow_p.h>

#include <windows.h>
#include <windowsx.h>

#include <algorithm>

namespace sam
{
namespace
{
constexpr int kBarHeight = 30;
constexpr int kButtonWidth = 46;
constexpr int kIconSize = 18;
constexpr int kCaptionHeight = 22;
constexpr int kCaptionGap = 16;

// Segoe Fluent Icons / Segoe MDL2 Assets glyphs.
constexpr char16_t kMinimizeGlyph = 0xE921;
constexpr char16_t kMaximizeGlyph = 0xE922;
constexpr char16_t kRestoreGlyph = 0xE923;
constexpr char16_t kCloseGlyph = 0xE8BB;

// Half-transparent gray reads on both themes. The colors of the caption come
// from the application style sheet (Style.cpp): the one of a widget would win
// over it whatever the selector.
const auto kStyleSheet =
    QStringLiteral("#titleBarCaption { border-radius: 4px; padding: 0px 7px; }\n"
                   "#titleBarButton, #titleBarCloseButton { font-size: 10px; border: none; border-radius: 0px;"
                   " background: transparent; }\n"
                   "#titleBarButton:hover { background: rgba(128, 128, 128, 50); }\n"
                   "#titleBarButton:pressed { background: rgba(128, 128, 128, 80); }\n"
                   "#titleBarCloseButton:hover { background: #c42b1c; color: white; }\n"
                   "#titleBarCloseButton:pressed { background: #b22a1b; color: white; }\n");

class TitleBar : public QObject, public QAbstractNativeEventFilter
{
public:
    explicit TitleBar(QMainWindow *window) : QObject(window), m_window(window)
    {
        m_bar = new QWidget(window);
        m_bar->setObjectName(QStringLiteral("titleBar"));
        m_bar->setFixedHeight(kBarHeight);
        m_bar->setAutoFillBackground(true);
        m_bar->setBackgroundRole(QPalette::Window);
        m_bar->setStyleSheet(kStyleSheet);

        auto *layout = new QHBoxLayout(m_bar);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        m_icon = new QLabel(m_bar);
        m_icon->setContentsMargins(10, 0, 6, 0);
        updateIcon();
        layout->addWidget(m_icon);
        layout->addStretch();

        m_buttons = new QWidget(m_bar);
        auto *buttonsLayout = new QHBoxLayout(m_buttons);
        buttonsLayout->setContentsMargins(0, 0, 0, 0);
        buttonsLayout->setSpacing(0);
        QToolButton *minimize = createButton(kMinimizeGlyph, "titleBarButton");
        m_maximize = createButton(kMaximizeGlyph, "titleBarButton");
        QToolButton *close = createButton(kCloseGlyph, "titleBarCloseButton");
        buttonsLayout->addWidget(minimize);
        buttonsLayout->addWidget(m_maximize);
        buttonsLayout->addWidget(close);
        layout->addWidget(m_buttons);

        connect(minimize, &QToolButton::clicked, window, &QWidget::showMinimized);
        connect(m_maximize, &QToolButton::clicked, window, [this] {
            m_window->isMaximized() ? m_window->showNormal() : m_window->showMaximized();
        });
        connect(close, &QToolButton::clicked, window, &QWidget::close);

        // Not in the layout: it's centered over the whole bar (see placeCaption).
        m_caption = new QLabel(m_bar);
        m_caption->setObjectName(QStringLiteral("titleBarCaption"));
        m_caption->setTextFormat(Qt::RichText);
        updateCaption();

        window->setMenuWidget(m_bar);

        // The native window is created right away, so that it has lost its
        // caption before the saved geometry is restored.
        bindNativeWindow();

        m_bar->installEventFilter(this);
        window->installEventFilter(this);
        qApp->installNativeEventFilter(this);
    }

    ~TitleBar() override { qApp->removeNativeEventFilter(this); }

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        const QEvent::Type type = event->type();
        if (object == m_window)
        {
            switch (type)
            {
                case QEvent::WinIdChange:
                    if (m_window->internalWinId() != 0)
                    {
                        bindNativeWindow();
                    }
                    break;
                case QEvent::WindowIconChange:
                case QEvent::ApplicationWindowIconChange: updateIcon(); break;
                case QEvent::WindowTitleChange: updateCaption(); break;
                case QEvent::WindowStateChange:
                    m_maximize->setText(QChar(m_window->isMaximized() ? kRestoreGlyph : kMaximizeGlyph));
                    updateHiddenTop();
                    break;
                case QEvent::Resize:
                case QEvent::Show: updateHiddenTop(); break;
                default: break;
            }
        }
        else if (object == m_bar && (type == QEvent::Resize || type == QEvent::Show))
        {
            placeCaption();
        }
        return QObject::eventFilter(object, event);
    }

public:
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override
    {
        if (eventType != "windows_generic_MSG")
        {
            return false;
        }
        const MSG *msg = static_cast<const MSG *>(message);
        if (msg->message != WM_NCHITTEST || msg->hwnd != m_hwnd)
        {
            return false;
        }

        // The side and bottom frames are still there, invisible, and resize
        // the window as usual.
        const LRESULT hit = ::DefWindowProcW(msg->hwnd, msg->message, msg->wParam, msg->lParam);
        if (hit != HTCLIENT)
        {
            *result = hit;
            return true;
        }

        POINT point = {GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam)};
        ::ScreenToClient(m_hwnd, &point);

        // The top frame went away with the caption, so its strip is cut out
        // of the client area.
        if (!::IsZoomed(m_hwnd) && !m_window->isFullScreen())
        {
            const int border = ::GetSystemMetricsForDpi(SM_CYSIZEFRAME, ::GetDpiForWindow(m_hwnd));
            if (point.y < border)
            {
                RECT client = {};
                ::GetClientRect(m_hwnd, &client);
                const int corner = 2 * border;
                *result = point.x < corner ? HTTOPLEFT : (point.x >= client.right - corner ? HTTOPRIGHT : HTTOP);
                return true;
            }
        }

        const qreal ratio = m_window->devicePixelRatioF();
        const QPoint position = m_bar->mapFrom(m_window, QPoint(qRound(point.x / ratio), qRound(point.y / ratio)));
        if (!m_bar->isVisible() || !m_bar->rect().contains(position))
        {
            return false;
        }
        QWidget *child = m_bar->childAt(position);
        if (child == m_icon)
        {
            *result = HTSYSMENU;
            return true;
        }
        if (child == nullptr || child == m_caption)
        {
            *result = HTCAPTION;
            return true;
        }
        return false; // the window buttons
    }

private:
    QToolButton *createButton(char16_t glyph, const char *objectName)
    {
        auto *button = new QToolButton(m_buttons);
        button->setObjectName(QString::fromLatin1(objectName));
        QFont font = button->font();
        font.setFamilies({QStringLiteral("Segoe Fluent Icons"), QStringLiteral("Segoe MDL2 Assets")});
        button->setFont(font);
        button->setText(QChar(glyph));
        button->setFixedSize(kButtonWidth, kBarHeight);
        button->setFocusPolicy(Qt::NoFocus);
        return button;
    }

    void updateIcon() { m_icon->setPixmap(m_window->windowIcon().pixmap(kIconSize, kIconSize)); }

    void updateCaption()
    {
        const QString title = m_window->windowTitle();
        const QString application = QGuiApplication::applicationDisplayName();
        QString text = title.toHtmlEscaped();
        if (!application.isEmpty() && application != title)
        {
            text +=
                QStringLiteral("&nbsp;&nbsp;<span style=\"color: #8c8c8c\">%1</span>").arg(application.toHtmlEscaped());
        }
        m_caption->setText(text);
        placeCaption();
    }

    // In the middle of the bar, or as close to it as the icon and the buttons
    // let it be.
    void placeCaption()
    {
        const QSize size(m_caption->sizeHint().width(), kCaptionHeight);
        const int left = std::max((m_bar->width() - size.width()) / 2, m_icon->geometry().right() + kCaptionGap);
        m_caption->setGeometry(QRect(QPoint(left, (m_bar->height() - size.height()) / 2), size));
        m_caption->setVisible(left + size.width() + kCaptionGap <= m_buttons->geometry().left());
    }

    void bindNativeWindow()
    {
        m_hwnd = reinterpret_cast<HWND>(m_window->winId());
        m_normalStyle = static_cast<DWORD>(::GetWindowLongPtrW(m_hwnd, GWL_STYLE));
        m_normalExStyle = static_cast<DWORD>(::GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE));
        disconnect(m_screenChanged);
        m_screenChanged = connect(m_window->windowHandle(), &QWindow::screenChanged, this, [this] {
            updateFrame();
        });
        updateFrame();
    }

    // Custom margins of the Qt window take the caption and the top frame out
    // of the non-client area. They're given to Qt rather than applied in
    // WM_NCCALCSIZE, since Qt adds them to the frame margins it maps the window
    // geometry with.
    void updateFrame()
    {
        auto *native = m_window->windowHandle()->nativeInterface<QNativeInterface::Private::QWindowsWindow>();
        if (native == nullptr)
        {
            return;
        }
        RECT frame = {};
        ::AdjustWindowRectExForDpi(&frame, m_normalStyle, FALSE, m_normalExStyle, ::GetDpiForWindow(m_hwnd));
        native->setCustomMargins(QMargins(0, frame.top, 0, 0));
        updateHiddenTop();
    }

    // A maximized window hangs over the edges of the screen by its frame, so
    // the top of the client area may be out of sight.
    void updateHiddenTop()
    {
        if (m_hwnd == nullptr)
        {
            return;
        }
        POINT origin = {0, 0};
        ::ClientToScreen(m_hwnd, &origin);
        LONG visibleTop = origin.y;
        if (::IsZoomed(m_hwnd))
        {
            MONITORINFO monitor = {};
            monitor.cbSize = sizeof(monitor);
            ::GetMonitorInfoW(::MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &monitor);
            visibleTop = monitor.rcWork.top;
        }
        else
        {
            RECT rect = {};
            ::GetWindowRect(m_hwnd, &rect);
            visibleTop = rect.top;
        }
        const int hidden = std::max(0, static_cast<int>(visibleTop - origin.y));
        m_window->setContentsMargins(0, qRound(hidden / m_window->devicePixelRatioF()), 0, 0);
    }

    QMainWindow *m_window;
    QWidget *m_bar = nullptr;
    QLabel *m_icon = nullptr;
    QLabel *m_caption = nullptr;
    QWidget *m_buttons = nullptr;
    QToolButton *m_maximize = nullptr;

    HWND m_hwnd = nullptr;
    DWORD m_normalStyle = 0;
    DWORD m_normalExStyle = 0;
    QMetaObject::Connection m_screenChanged;
};
} // namespace

void installTitleBar(QMainWindow *window)
{
    new TitleBar(window);
}
} // namespace sam
