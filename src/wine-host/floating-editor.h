// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2026 Robbert van der Helm
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <optional>
#include <string>

#include <windows.h>

// For `Size`
#include "editor.h"

/**
 * The VST3 platform type string for Wayland hosts. This is not yet part of the
 * VST3 SDK version we build against.
 */
constexpr char kPlatformTypeWaylandSurfaceID[] = "WaylandSurfaceID";

/**
 * A plugin editor shown in its own top-level Wine window instead of being
 * embedded into the host's window.
 *
 * Wayland does not allow embedding another client's window, and Wine's windows
 * are X11 windows (through XWayland) that cannot be parented to the
 * `wl_surface` a Wayland-only host like Studio One passes to
 * `IPlugView::attached()`. So when a host only offers `WaylandSurfaceID`, the
 * editor floats in a separate window instead. The host's own view area stays
 * empty, but the plugin's GUI is usable.
 */
class FloatingEditor {
   public:
    /**
     * Create the (still hidden) window.
     *
     * @param title The window title, usually the plugin's name.
     * @param initial_size The initial client area size, if the plugin
     *   reported a valid one.
     */
    FloatingEditor(const std::string& title,
                   std::optional<Size> initial_size);

    ~FloatingEditor() noexcept;

    FloatingEditor(const FloatingEditor&) = delete;
    FloatingEditor& operator=(const FloatingEditor&) = delete;
    FloatingEditor(FloatingEditor&&) = delete;
    FloatingEditor& operator=(FloatingEditor&&) = delete;

    /**
     * Resize the window so its client area matches the plugin's view size.
     */
    void resize(uint16_t width, uint16_t height);

    /**
     * Show the window and bring it to the front.
     */
    void show() noexcept;

    /**
     * The window the plugin should attach its view to.
     */
    HWND win32_handle() const noexcept { return window_; }

   private:
    HWND window_;
};
