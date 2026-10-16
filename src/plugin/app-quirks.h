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

#include <ghc/filesystem.hpp>

#include "../common/logging/common.h"
#include "utils.h"

/**
 * Workarounds for specific plugins and hosts. These are only built with the
 * `app_quirks` build option.
 */
namespace app_quirks {

/**
 * Some plugins identify the DAW they run in through the process they are
 * loaded into. On Windows that's the DAW itself, but with yabridge it's the
 * Wine plugin host. Celemony's Tonalic for instance only enables its ARA
 * integration for a list of known DAWs, and otherwise shows an "ARA DAW
 * Incompatibility" message.
 *
 * For those plugins this returns what the Wine plugin host should present
 * itself as. It's passed to the host through `YABRIDGE_HOST_PROCESS_NAME`, and
 * the host then rewrites its image path before loading the plugin (see
 * `apply_host_process_name_override()` on the Wine side). Plugins may also read
 * the version information from that executable, so if there's a stub for the
 * DAW in a `quirks` directory next to the Wine plugin host binary (generated
 * in `tools/quirk-stubs`), this is the absolute path to that stub. Otherwise
 * it's just the DAW's executable name with an `.exe` extension (e.g.
 * `Studio One.exe`). Returns `std::nullopt` for all other plugins.
 *
 * @param plugin_info Information about the plugin being bridged.
 * @param host_path The path to the Wine plugin host's `.exe` launcher.
 * @param logger Used to log when the quirk is applied.
 */
std::optional<std::string> host_process_name(
    const PluginInfo& plugin_info,
    const ghc::filesystem::path& host_path,
    Logger& logger);

}  // namespace app_quirks
