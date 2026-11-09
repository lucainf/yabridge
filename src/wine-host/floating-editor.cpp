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

#include "floating-editor.h"

#include <string>

namespace {

constexpr wchar_t floating_editor_class_name[] = L"yabridge floating editor";

// Resizing is driven by the plugin through `IPlugFrame::resizeView()`, so the
// user can't resize or maximize the window directly
constexpr DWORD floating_editor_style =
    WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX;
constexpr DWORD floating_editor_ex_style = WS_EX_APPWINDOW;

LRESULT CALLBACK floating_editor_proc(HWND handle,
                                      UINT message,
                                      WPARAM wParam,
                                      LPARAM lParam) {
    // The host owns the editor's lifetime, so closing the window only hides it.
    // Reopening the plugin's editor in the host shows it again.
    if (message == WM_CLOSE) {
        ShowWindow(handle, SW_HIDE);
        return 0;
    }

    return DefWindowProcW(handle, message, wParam, lParam);
}

ATOM register_floating_editor_class() {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(WNDCLASSEXW);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = floating_editor_proc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.hCursor =
        LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    window_class.lpszClassName = floating_editor_class_name;

    return RegisterClassExW(&window_class);
}

/**
 * Convert a client area size to the outer window size for our window style.
 */
SIZE outer_window_size(int width, int height) {
    RECT rect{0, 0, width, height};
    AdjustWindowRectEx(&rect, floating_editor_style, false,
                       floating_editor_ex_style);

    return SIZE{rect.right - rect.left, rect.bottom - rect.top};
}

}  // namespace

FloatingEditor::FloatingEditor(const std::string& title,
                               std::optional<Size> initial_size) {
    static const ATOM window_class = register_floating_editor_class();
    (void)window_class;

    const Size size = initial_size.value_or(Size(640, 480));
    const SIZE outer_size = outer_window_size(size.width, size.height);

    // NOTE: Winelib code is built with `-fshort-wchar` while libstdc++'s
    //       `std::wstring` uses a 4-byte `wchar_t`, so we can't use that here.
    //       See `to_u16string()` in `utils.cpp`.
    static_assert(sizeof(WCHAR) == sizeof(char16_t));
    std::u16string wide_title;
    if (const int length = MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1,
                                               nullptr, 0);
        length > 0) {
        wide_title.resize(length);
        MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1,
                            reinterpret_cast<WCHAR*>(wide_title.data()),
                            length);
        wide_title.resize(length - 1);
    }

    window_ = CreateWindowExW(floating_editor_ex_style,
                              floating_editor_class_name,
                              reinterpret_cast<const WCHAR*>(wide_title.c_str()),
                              floating_editor_style, CW_USEDEFAULT,
                              CW_USEDEFAULT, outer_size.cx, outer_size.cy,
                              nullptr, nullptr, GetModuleHandleW(nullptr),
                              nullptr);
}

FloatingEditor::~FloatingEditor() noexcept {
    if (window_) {
        DestroyWindow(window_);
    }
}

void FloatingEditor::resize(uint16_t width, uint16_t height) {
    const SIZE outer_size = outer_window_size(width, height);
    SetWindowPos(window_, nullptr, 0, 0, outer_size.cx, outer_size.cy,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void FloatingEditor::show() noexcept {
    ShowWindow(window_, SW_SHOWNORMAL);
    SetForegroundWindow(window_);
}
