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

#include "Icons.h"

#include <QApplication>
#include <QHash>
#include <QIconEngine>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPixmapCache>
#include <QSvgRenderer>

namespace sam::icons
{
namespace
{
// 24x24 stroke icons. `%1` is replaced with the stroke color.
const QHash<QString, const char *> &shapes()
{
    static const QHash<QString, const char *> table = {
        {QStringLiteral("save"),
         R"(<path d="M5 3h11l3 3v13a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z"/><path d="M7 3v5h8V3"/><path d="M7 21v-7h10v7"/>)"},
        {QStringLiteral("refresh"),
         R"(<path d="M20 11A8 8 0 0 0 5.3 6.7L3 9"/><path d="M3 4v5h5"/><path d="M4 13a8 8 0 0 0 14.7 4.3L21 15"/><path d="M21 20v-5h-5"/>)"},
        {QStringLiteral("reset"),
         R"(<path d="M4 7h16"/><path d="M10 11v6"/><path d="M14 11v6"/><path d="M6 7l1 13a1 1 0 0 0 1 1h8a1 1 0 0 0 1-1l1-13"/><path d="M9 7V4h6v3"/>)"},
        {QStringLiteral("lock"),
         R"(<rect x="5" y="11" width="14" height="10" rx="2"/><path d="M8 11V7a4 4 0 0 1 8 0v4"/>)"},
        {QStringLiteral("unlock"),
         R"(<rect x="5" y="11" width="14" height="10" rx="2"/><path d="M8 11V7a4 4 0 0 1 7.75-1.4"/>)"},
        {QStringLiteral("invert"),
         R"(<path d="M7 3L3 7l4 4"/><path d="M3 7h14"/><path d="M17 13l4 4-4 4"/><path d="M21 17H7"/>)"},
        {QStringLiteral("search"), R"(<circle cx="11" cy="11" r="7"/><path d="M20 20l-4-4"/>)"},
        {QStringLiteral("filter"), R"(<path d="M3 4h18l-7 8.5V19l-4 2v-8.5z"/>)"},
        {QStringLiteral("download"), R"(<path d="M12 4v11"/><path d="M7 10l5 5 5-5"/><path d="M5 20h14"/>)"},
        {QStringLiteral("language"),
         R"(<circle cx="12" cy="12" r="9"/><path d="M3 12h18"/><path d="M12 3c2.5 2.5 3.8 5.5 3.8 9s-1.3 6.5-3.8 9c-2.5-2.5-3.8-5.5-3.8-9S9.5 5.5 12 3z"/>)"},
        {QStringLiteral("add"), R"(<path d="M12 5v14"/><path d="M5 12h14"/>)"},
        {QStringLiteral("menu"), R"(<path d="M4 6h16"/><path d="M4 12h16"/><path d="M4 18h16"/>)"},
        {QStringLiteral("external"),
         R"(<path d="M14 4h6v6"/><path d="M20 4l-9 9"/><path d="M18 14v5a1 1 0 0 1-1 1H5a1 1 0 0 1-1-1V7a1 1 0 0 1 1-1h5"/>)"},
        {QStringLiteral("copy"),
         R"(<rect x="8" y="8" width="12" height="12" rx="2"/><path d="M16 8V5a1 1 0 0 0-1-1H5a1 1 0 0 0-1 1v10a1 1 0 0 0 1 1h3"/>)"},
        {QStringLiteral("play"), R"(<path d="M7 4l12 8-12 8z"/>)"},
        {QStringLiteral("trophy"),
         R"(<path d="M8 4h8v6a4 4 0 0 1-8 0z"/><path d="M8 6H5.5a2.5 2.5 0 0 0 2.6 4.8"/><path d="M16 6h2.5a2.5 2.5 0 0 1-2.6 4.8"/><path d="M12 14v4"/><path d="M8 21h8"/><path d="M9 18h6"/>)"},
        {QStringLiteral("stats"),
         R"(<path d="M3 21h18"/><path d="M6 17v-5"/><path d="M11 17V6"/><path d="M16 17v-8"/><path d="M21 17V4"/>)"},
        {QStringLiteral("shield"), R"(<path d="M12 3l7 3v5c0 5-3 8.5-7 10-4-1.5-7-5-7-10V6z"/>)"},
        {QStringLiteral("hidden"),
         R"(<path d="M3 3l18 18"/><path d="M10.6 6.1A9.4 9.4 0 0 1 12 6c5 0 9 6 9 6a15.6 15.6 0 0 1-2.4 3"/><path d="M6.6 7.6C4.3 9.2 3 12 3 12s4 6 9 6a8.6 8.6 0 0 0 4.4-1.2"/><path d="M9.9 9.9a3 3 0 0 0 4.2 4.2"/>)"},
        {QStringLiteral("warning"), R"(<path d="M12 3l10 18H2z"/><path d="M12 10v5"/><path d="M12 18h.01"/>)"},
        {QStringLiteral("theme"), R"(<circle cx="12" cy="12" r="9"/><path d="M12 3a9 9 0 0 0 0 18z" fill="%1"/>)"},
        {QStringLiteral("info"), R"(<circle cx="12" cy="12" r="9"/><path d="M12 11v6"/><path d="M12 7.5h.01"/>)"},
        {QStringLiteral("clear"), R"(<path d="M6 6l12 12"/><path d="M18 6L6 18"/>)"},
        {QStringLiteral("check"), R"(<path d="M4 12.5l5 5L20 6.5"/>)"},
        {QStringLiteral("grid"),
         R"(<rect x="4" y="4" width="7" height="7" rx="1"/><rect x="13" y="4" width="7" height="7" rx="1"/><rect x="4" y="13" width="7" height="7" rx="1"/><rect x="13" y="13" width="7" height="7" rx="1"/>)"},
    };
    return table;
}

QByteArray svgFor(const QString &name, const QColor &color)
{
    const char *shape = shapes().value(name, shapes().value(QStringLiteral("info")));
    const QString colorName = color.name(QColor::HexRgb);
    const QString body = QString::fromUtf8(shape).replace(QLatin1StringView("%1"), colorName);
    return QStringLiteral(R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="%1" )"
                          R"(opacity="%2" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round">%3</svg>)")
        .arg(colorName, QString::number(color.alphaF()), body)
        .toUtf8();
}

class SvgIconEngine : public QIconEngine
{
public:
    SvgIconEngine(const QString &name, const QColor &color) : m_name(name), m_color(color) {}

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) override
    {
        const qreal dpr = painter->device() != nullptr ? painter->device()->devicePixelRatioF() : 1.0;
        painter->drawPixmap(rect, render(rect.size(), dpr, mode, state));
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        return render(size, 1.0, mode, state);
    }

    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        return render(size, scale, mode, state);
    }

    QIconEngine *clone() const override { return new SvgIconEngine(m_name, m_color); }

    QString key() const override { return QStringLiteral("sam-svg"); }

    bool isNull() override { return false; }

private:
    QColor colorFor(QIcon::Mode mode) const
    {
        const QPalette palette = QApplication::palette();
        QColor color = m_color.isValid() ? m_color : palette.color(QPalette::Active, QPalette::ButtonText);
        if (mode == QIcon::Disabled)
        {
            color = m_color.isValid() ? m_color : palette.color(QPalette::Disabled, QPalette::ButtonText);
            color.setAlphaF(color.alphaF() * 0.45f);
        }
        else if (mode == QIcon::Selected && !m_color.isValid())
        {
            color = palette.color(QPalette::Active, QPalette::HighlightedText);
        }
        return color;
    }

    QPixmap render(const QSize &size, qreal dpr, QIcon::Mode mode, QIcon::State) const
    {
        const QColor color = colorFor(mode);
        const QString cacheKey = QStringLiteral("sam-icon:%1:%2:%3x%4@%5")
                                     .arg(m_name, color.name(QColor::HexArgb))
                                     .arg(size.width())
                                     .arg(size.height())
                                     .arg(dpr);
        QPixmap pixmap;
        if (QPixmapCache::find(cacheKey, &pixmap))
        {
            return pixmap;
        }

        pixmap = QPixmap(size * dpr);
        pixmap.setDevicePixelRatio(dpr);
        pixmap.fill(Qt::transparent);
        QSvgRenderer renderer(svgFor(m_name, color));
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        const int side = qMin(size.width(), size.height());
        const QRectF target((size.width() - side) / 2.0, (size.height() - side) / 2.0, side, side);
        renderer.render(&painter, target);
        painter.end();

        QPixmapCache::insert(cacheKey, pixmap);
        return pixmap;
    }

    QString m_name;
    QColor m_color;
};
} // namespace

QIcon get(const QString &name)
{
    return QIcon(new SvgIconEngine(name, QColor()));
}

QIcon get(const QString &name, const QColor &color)
{
    return QIcon(new SvgIconEngine(name, color));
}
} // namespace sam::icons
