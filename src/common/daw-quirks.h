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

#include <array>
#include <optional>
#include <string_view>

/**
 * A DAW that needs special handling. This is the single source of truth for
 * detecting these DAWs on the plugin side, and for `tools/quirk-stubs`, which
 * generates executable stubs with this version information at build time. The
 * Wine plugin host presents itself as those stubs to some plugins when
 * building with the `app_quirks` option.
 */
struct DawQuirk {
    /**
     * The file name of the DAW's Linux executable, used to detect the DAW
     * through `/proc/self/exe`.
     */
    std::string_view linux_executable;
    /**
     * The file name of the DAW's executable on Windows. This is the name of
     * the generated stub, and the name the Wine plugin host presents itself
     * as. Leave this and the version information empty for DAWs that only
     * need `wayland_only`, see `presents_as_windows_executable()`.
     */
    std::string_view windows_executable = {};

    // Version information for the stub's version resource
    std::string_view company_name = {};
    std::string_view product_name = {};
    std::string_view file_description = {};
    /**
     * The version in `major.minor.patch.build` form.
     */
    std::string_view version = {};

    /**
     * Whether the DAW only supports Wayland for embedding plugin editors. The
     * editor is then shown in a separate window on the Wine side instead, see
     * `FloatingEditor`. This does not depend on the `app_quirks` option.
     */
    bool wayland_only = false;

    /**
     * Whether the Wine plugin host can present itself as this DAW's Windows
     * executable to plugins in `plugins_needing_daw_process_name`, and whether
     * `tools/quirk-stubs` generates a stub for it.
     */
    constexpr bool presents_as_windows_executable() const {
        return !windows_executable.empty() && !version.empty();
    }
};

/**
 * The DAWs that need special handling. Studio One on Linux only supports
 * Wayland, and Tonalic's ARA integration is available in Studio One Pro 7.2.3
 * and later.
 */
inline constexpr std::array daw_quirks{
    DawQuirk{
        .linux_executable = "Studio One",
        .windows_executable = "Studio One.exe",
        .company_name = "PreSonus Software Ltd",
        .product_name = "Studio One",
        .file_description = "Studio One",
        .version = "7.2.3.108761",
        .wayland_only = true,
    },
};

/**
 * Plugins that need to run in a process that looks like the DAW, matched on
 * the Windows plugin file name without its extension.
 */
inline constexpr std::array<std::string_view, 1>
    plugins_needing_daw_process_name{
        // Tonalic's ARA integration is only enabled for a list of known DAWs
        "Tonalic",
    };

/**
 * Find the quirk entry for a DAW by its Linux executable's file name.
 */
inline std::optional<DawQuirk> find_daw_quirk(
    std::string_view linux_executable) {
    for (const auto& quirk : daw_quirks) {
        if (quirk.linux_executable == linux_executable) {
            return quirk;
        }
    }

    return std::nullopt;
}
