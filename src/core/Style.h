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

#pragma once

class QApplication;
class QColor;

namespace sam
{
// Sets up the look of the application:
// - Fusion with Telegram-style scroll bars: the bars of a scroll area lie over
//   its contents instead of taking room beside them, show up while the area is
//   scrolled or hovered and fade away a second later;
// - animated mouse wheel scrolling in every scroll area that scrolls per pixel;
// - rounded, translucent menus with check boxes drawn like everywhere else.
void installStyle(QApplication &app);

// Applies the palette and style sheet of the dark ("Dark Modern") or the light
// theme. `systemAccent` takes the selection and accent colors from Windows
// (and follows them when they change) instead of our blue.
void applyColorScheme(bool dark, bool systemAccent);

// The color of progress bars and similar marks in the current theme.
QColor accentColor();
} // namespace sam
