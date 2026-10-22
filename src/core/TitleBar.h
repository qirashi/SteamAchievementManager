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

class QMainWindow;

namespace sam
{
// Replaces the system caption of the window with a title bar of its own: the
// window icon on the left, the title in the middle and the window buttons on
// the right. The system frame stays around the other sides, so the window
// still resizes, snaps and has its shadow and rounded corners; the bar's free
// space drags it.
//
// Call it before the saved geometry is restored: the caption has to be gone by
// then, or the window would shift by its height on every launch.
void installTitleBar(QMainWindow *window);
} // namespace sam
