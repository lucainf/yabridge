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

#include "utils.h"

#include <winternl.h>

#include <iostream>

#include "bridges/common.h"

using namespace std::literals::chrono_literals;

uint32_t WINAPI
win32_thread_trampoline(fu2::unique_function<void()>* entry_point) {
    (*entry_point)();
    delete entry_point;

    return 0;
}

Win32Thread::Win32Thread() noexcept : handle_(nullptr, nullptr) {}

Win32Thread::~Win32Thread() noexcept {
    if (handle_) {
        WaitForSingleObject(handle_.get(), INFINITE);
    }
}

Win32Thread::Win32Thread(Win32Thread&& o) noexcept
    : handle_(std::move(o.handle_)) {
    o.handle_.reset();
}

Win32Thread& Win32Thread::operator=(Win32Thread&& o) noexcept {
    handle_ = std::move(o.handle_);
    o.handle_.reset();

    return *this;
}

Win32Timer::Win32Timer() noexcept {}

Win32Timer::Win32Timer(HWND window_handle,
                       size_t timer_id,
                       unsigned int interval_ms) noexcept
    : window_handle_(window_handle), timer_id_(timer_id) {
    SetTimer(window_handle, timer_id, interval_ms, nullptr);
}

Win32Timer::~Win32Timer() noexcept {
    if (timer_id_) {
        KillTimer(window_handle_, *timer_id_);
    }
}

Win32Timer::Win32Timer(Win32Timer&& o) noexcept
    : window_handle_(o.window_handle_), timer_id_(std::move(o.timer_id_)) {
    o.timer_id_.reset();
}

Win32Timer& Win32Timer::operator=(Win32Timer&& o) noexcept {
    window_handle_ = o.window_handle_;
    timer_id_ = std::move(o.timer_id_);
    o.timer_id_.reset();

    return *this;
}

MainContext::MainContext()
    : context_(),
      events_timer_(context_),
      watchdog_context_(),
      watchdog_timer_(watchdog_context_) {}

void MainContext::run() {
    // We need to know which thread is the GUI thread because mutual recursion
    // in VST3 plugins needs to be handled differently depending on whether the
    // potentially mutually recursive function was called from an audio thread
    // or a GUI thread
    gui_thread_id_ = GetCurrentThreadId();

    // NOTE: We allow disabling the watchdog timer to allow the Wine process to
    //       be run from a separate namespace. This is not something you'd
    //       normally want to enable.
    if (is_watchdog_timer_disabled()) {
        std::cerr << "WARNING: Watchdog timer disabled. Not protecting"
                  << std::endl;
        std::cerr << "         against dangling processes." << std::endl;
    } else {
        // To account for hosts terminating before the bridged plugin has
        // initialized, we'll do the first watchdog check five seconds. After
        // this we'll run the timer on a 30 second interval.
        async_handle_watchdog_timer(5s);

        watchdog_handler_ = Win32Thread([&]() {
            pthread_setname_np(pthread_self(), "watchdog");

            watchdog_context_.run();
        });
    }

    context_.run();

    // We only need to check if the host is still running while the main context
    // is also running. If a stop was requested, the entire application is
    // supposed to shut down. Otherwise `watchdog_handler` would just block on
    // the join as the watchdog timer is still active.
    watchdog_context_.stop();
}

void MainContext::stop() noexcept {
    context_.stop();
}

void MainContext::update_timer_interval(
    std::chrono::steady_clock::duration new_interval) noexcept {
    timer_interval_ = new_interval;
}

MainContext::WatchdogGuard::WatchdogGuard(
    HostBridge& bridge,
    std::unordered_set<HostBridge*>& watched_bridges,
    std::mutex& watched_bridges_mutex)
    : bridge_(&bridge),
      watched_bridges_(watched_bridges),
      watched_bridges_mutex_(watched_bridges_mutex) {
    std::lock_guard lock(watched_bridges_mutex);
    watched_bridges.insert(&bridge);
}

MainContext::WatchdogGuard::~WatchdogGuard() noexcept {
    if (is_active_) {
        std::lock_guard lock(watched_bridges_mutex_.get());
        watched_bridges_.get().erase(bridge_);
    }
}

MainContext::WatchdogGuard::WatchdogGuard(WatchdogGuard&& o) noexcept
    : bridge_(std::move(o.bridge_)),
      watched_bridges_(std::move(o.watched_bridges_)),
      watched_bridges_mutex_(std::move(o.watched_bridges_mutex_)) {
    o.is_active_ = false;
}

MainContext::WatchdogGuard& MainContext::WatchdogGuard::operator=(
    WatchdogGuard&& o) noexcept {
    bridge_ = std::move(o.bridge_);
    watched_bridges_ = std::move(o.watched_bridges_);
    watched_bridges_mutex_ = std::move(o.watched_bridges_mutex_);
    o.is_active_ = false;

    return *this;
}

MainContext::WatchdogGuard MainContext::register_watchdog(HostBridge& bridge) {
    // The guard's constructor and destructor will handle actually registering
    // and unregistering the bridge from `watched_bridges`
    return WatchdogGuard(bridge, watched_bridges_, watched_bridges_mutex_);
}

void MainContext::async_handle_watchdog_timer(
    std::chrono::steady_clock::duration interval) {
    watchdog_timer_.expires_at(std::chrono::steady_clock::now() + interval);
    watchdog_timer_.async_wait([&](const std::error_code& error) {
        if (error) {
            return;
        }

        // When the `WatchdogGuard` field on `HostBridge` gets destroyed, that
        // bridge instance will be removed from `watched_bridges`. So if our
        // call to `HostBridge::shutdown_if_dangling()` shuts the plugin down,
        // the instance will be removed after this lambda exits.
        std::lock_guard lock(watched_bridges_mutex_);
        for (auto& bridge : watched_bridges_) {
            bridge->shutdown_if_dangling();
        }

        async_handle_watchdog_timer(30s);
    });
}

// Exported by Wine's kernel32, converts a Unix path to a DOS path allocated on
// the process heap
extern "C" WCHAR* CDECL wine_get_dos_file_name(LPCSTR unix_path);

namespace {

// NOTE: Winelib code is built with `-fshort-wchar`, but libstdc++'s
//       `std::wstring` is compiled with a 4-byte `wchar_t`, so using it here
//       corrupts memory. `std::u16string` has the same layout as a `WCHAR`
//       string without that problem.
static_assert(sizeof(WCHAR) == sizeof(char16_t));

std::u16string to_u16string(const UNICODE_STRING& string) {
    return std::u16string(reinterpret_cast<const char16_t*>(string.Buffer),
                          string.Length / sizeof(WCHAR));
}

std::string to_utf8(const std::u16string& string) {
    std::string result(string.size() * 3, '\0');
    result.resize(WideCharToMultiByte(
        CP_UTF8, 0, reinterpret_cast<const WCHAR*>(string.data()),
        static_cast<int>(string.size()), result.data(),
        static_cast<int>(result.size()), nullptr, nullptr));

    return result;
}

/**
 * Allocate a `UNICODE_STRING` for the PEB that lives for the rest of the
 * process' lifetime.
 */
UNICODE_STRING make_persistent_unicode_string(const std::u16string& value) {
    const size_t bytes = (value.size() + 1) * sizeof(WCHAR);
    auto* buffer = static_cast<WCHAR*>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes));
    std::copy(value.begin(), value.end(), reinterpret_cast<char16_t*>(buffer));

    UNICODE_STRING result{};
    result.Length = static_cast<USHORT>(value.size() * sizeof(WCHAR));
    result.MaximumLength = static_cast<USHORT>(bytes);
    result.Buffer = buffer;

    return result;
}

/**
 * The part of a Windows path after the last separator.
 */
std::u16string file_name(const std::u16string& path) {
    const size_t separator = path.find_last_of(u"\\/");
    return separator == std::u16string::npos ? path
                                             : path.substr(separator + 1);
}

}  // namespace

std::optional<std::string> apply_host_process_name_override() {
    const auto* variable =
        reinterpret_cast<const WCHAR*>(u"YABRIDGE_HOST_PROCESS_NAME");
    WCHAR value_buffer[MAX_PATH];
    const DWORD value_length =
        GetEnvironmentVariableW(variable, value_buffer, MAX_PATH);
    if (value_length == 0 || value_length >= MAX_PATH) {
        return std::nullopt;
    }
    SetEnvironmentVariableW(variable, nullptr);
    const std::u16string value(reinterpret_cast<const char16_t*>(value_buffer),
                               value_length);

    PEB* peb = NtCurrentTeb()->Peb;
    RTL_USER_PROCESS_PARAMETERS* parameters = peb->ProcessParameters;

    // Either use an existing executable as the image path, or keep the current
    // directory and only replace the file name
    std::u16string image_path;
    if (value.starts_with(u"/")) {
        WCHAR* dos_path = wine_get_dos_file_name(to_utf8(value).c_str());
        if (!dos_path) {
            return std::nullopt;
        }
        image_path = reinterpret_cast<const char16_t*>(dos_path);
        HeapFree(GetProcessHeap(), 0, dos_path);
    } else {
        const std::u16string current = to_u16string(parameters->ImagePathName);
        const size_t separator = current.find_last_of(u"\\/");
        image_path = separator == std::u16string::npos
                         ? value
                         : current.substr(0, separator + 1) + value;
    }
    parameters->ImagePathName = make_persistent_unicode_string(image_path);

    // The command line starts with the (possibly quoted) image path
    const std::u16string command_line = to_u16string(parameters->CommandLine);
    size_t arguments_start;
    if (!command_line.empty() && command_line[0] == u'"') {
        const size_t closing_quote = command_line.find(u'"', 1);
        arguments_start = closing_quote == std::u16string::npos
                              ? command_line.size()
                              : closing_quote + 1;
    } else {
        arguments_start =
            std::min(command_line.find(u' '), command_line.size());
    }
    parameters->CommandLine = make_persistent_unicode_string(
        u"\"" + image_path + u"\"" + command_line.substr(arguments_start));

    // The first entry in the load order module list is the main executable
    LIST_ENTRY* main_module_link = peb->LdrData->InLoadOrderModuleList.Flink;
    if (main_module_link != &peb->LdrData->InLoadOrderModuleList) {
        auto* main_module = CONTAINING_RECORD(
            main_module_link, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        main_module->FullDllName = make_persistent_unicode_string(image_path);
        main_module->BaseDllName =
            make_persistent_unicode_string(file_name(image_path));
    }

    return to_utf8(image_path);
}
