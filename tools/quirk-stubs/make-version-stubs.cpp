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

// Generates a tiny x86-64 Windows executable for every DAW in `daw_quirks`.
// These only contain a version resource with the DAW's version information.
// The Wine plugin host presents itself as these files to plugins that identify
// the DAW through the process they're loaded into. The PE file is written
// directly so building yabridge doesn't need a MinGW toolchain. The entry
// point just returns.
//
// Usage: make-version-stubs <output_directory>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "../../src/common/daw-quirks.h"

namespace {

constexpr uint32_t file_alignment = 0x200;
constexpr uint32_t section_alignment = 0x1000;
constexpr uint64_t image_base = 0x140000000;
constexpr uint32_t text_rva = 0x1000;
constexpr uint32_t rsrc_rva = 0x2000;

constexpr uint16_t rt_version = 16;
constexpr uint16_t lang_en_us = 0x0409;
constexpr uint16_t codepage_unicode = 1200;

using Bytes = std::vector<uint8_t>;

uint32_t align(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

void put16(Bytes& out, uint16_t value) {
    out.push_back(value & 0xFF);
    out.push_back(value >> 8);
}

void put32(Bytes& out, uint32_t value) {
    put16(out, value & 0xFFFF);
    put16(out, value >> 16);
}

void put64(Bytes& out, uint64_t value) {
    put32(out, value & 0xFFFFFFFF);
    put32(out, value >> 32);
}

void pad_to(Bytes& out, size_t alignment) {
    out.resize((out.size() + alignment - 1) / alignment * alignment, 0);
}

/**
 * Encode an ASCII string as null terminated UTF-16LE. The quirk metadata only
 * contains ASCII.
 */
Bytes utf16z(std::string_view value) {
    Bytes out;
    for (const char c : value) {
        put16(out, static_cast<uint8_t>(c));
    }
    put16(out, 0);

    return out;
}

/**
 * One of the nested structures in a `VS_VERSIONINFO` resource: `wLength`,
 * `wValueLength`, `wType`, the key, padding, the value, padding, and the
 * children, all aligned to 32-bit boundaries relative to the start of the
 * resource. `value_length` is in bytes for binary values and in characters
 * for text values.
 */
Bytes version_block(std::string_view key,
                    const Bytes& value,
                    uint16_t value_length,
                    bool is_text,
                    const std::vector<Bytes>& children) {
    Bytes out;
    put16(out, 0);
    put16(out, value_length);
    put16(out, is_text ? 1 : 0);
    const Bytes encoded_key = utf16z(key);
    out.insert(out.end(), encoded_key.begin(), encoded_key.end());
    pad_to(out, 4);
    out.insert(out.end(), value.begin(), value.end());
    for (const Bytes& child : children) {
        pad_to(out, 4);
        out.insert(out.end(), child.begin(), child.end());
    }

    out[0] = out.size() & 0xFF;
    out[1] = out.size() >> 8;

    return out;
}

Bytes version_resource(const DawQuirk& quirk) {
    // The binary version fields are 16-bit, so components like a build number
    // of 108761 are clamped. The string version keeps the full number.
    uint32_t parts[4] = {0, 0, 0, 0};
    size_t part = 0;
    for (const char c : quirk.version) {
        if (c == '.') {
            part++;
        } else if (part < 4) {
            parts[part] = std::min<uint32_t>(parts[part] * 10 + (c - '0'),
                                             0xFFFF);
        }
    }
    const uint32_t version_ms = (parts[0] << 16) | parts[1];
    const uint32_t version_ls = (parts[2] << 16) | parts[3];

    Bytes fixed_file_info;
    put32(fixed_file_info, 0xFEEF04BD);  // dwSignature
    put32(fixed_file_info, 0x00010000);  // dwStrucVersion
    put32(fixed_file_info, version_ms);  // dwFileVersionMS
    put32(fixed_file_info, version_ls);  // dwFileVersionLS
    put32(fixed_file_info, version_ms);  // dwProductVersionMS
    put32(fixed_file_info, version_ls);  // dwProductVersionLS
    put32(fixed_file_info, 0x3F);        // dwFileFlagsMask
    put32(fixed_file_info, 0);           // dwFileFlags
    put32(fixed_file_info, 0x00040004);  // dwFileOS: VOS_NT_WINDOWS32
    put32(fixed_file_info, 1);           // dwFileType: VFT_APP
    put32(fixed_file_info, 0);           // dwFileSubtype
    put32(fixed_file_info, 0);           // dwFileDateMS
    put32(fixed_file_info, 0);           // dwFileDateLS

    const std::pair<std::string_view, std::string_view> strings[] = {
        {"CompanyName", quirk.company_name},
        {"FileDescription", quirk.file_description},
        {"FileVersion", quirk.version},
        {"InternalName", quirk.product_name},
        {"OriginalFilename", quirk.windows_executable},
        {"ProductName", quirk.product_name},
        {"ProductVersion", quirk.version},
    };
    std::vector<Bytes> string_blocks;
    for (const auto& [name, value] : strings) {
        string_blocks.push_back(
            version_block(name, utf16z(value),
                          static_cast<uint16_t>(value.size() + 1), true, {}));
    }

    // "040904b0", English (US) with the Unicode code page
    const Bytes string_table =
        version_block("040904b0", {}, 0, true, string_blocks);
    const Bytes string_file_info =
        version_block("StringFileInfo", {}, 0, true, {string_table});

    Bytes translation_value;
    put16(translation_value, lang_en_us);
    put16(translation_value, codepage_unicode);
    const Bytes translation =
        version_block("Translation", translation_value, 4, false, {});
    const Bytes var_file_info =
        version_block("VarFileInfo", {}, 0, true, {translation});

    return version_block(
        "VS_VERSION_INFO", fixed_file_info,
        static_cast<uint16_t>(fixed_file_info.size()), false,
        {string_file_info, var_file_info});
}

/**
 * A `.rsrc` section with a single RT_VERSION/1/en-US resource. The directory
 * offsets are relative to the start of the section, and the data entry points
 * to an RVA.
 */
Bytes resource_section(const Bytes& version_info) {
    constexpr uint32_t name_directory_offset = 24;
    constexpr uint32_t language_directory_offset = 48;
    constexpr uint32_t data_entry_offset = 72;
    constexpr uint32_t data_offset = 88;

    Bytes out;
    const auto directory = [&](uint32_t id, uint32_t offset,
                               bool is_subdirectory) {
        put32(out, 0);  // Characteristics
        put32(out, 0);  // TimeDateStamp
        put16(out, 0);  // MajorVersion
        put16(out, 0);  // MinorVersion
        put16(out, 0);  // NumberOfNamedEntries
        put16(out, 1);  // NumberOfIdEntries
        put32(out, id);
        put32(out, offset | (is_subdirectory ? 0x80000000 : 0));
    };
    directory(rt_version, name_directory_offset, true);
    directory(1, language_directory_offset, true);
    directory(lang_en_us, data_entry_offset, false);

    put32(out, rsrc_rva + data_offset);
    put32(out, static_cast<uint32_t>(version_info.size()));
    put32(out, 0);  // CodePage
    put32(out, 0);  // Reserved
    out.insert(out.end(), version_info.begin(), version_info.end());

    return out;
}

Bytes make_executable(const DawQuirk& quirk) {
    // `xor eax, eax; ret`
    const Bytes text = {0x31, 0xC0, 0xC3};
    const Bytes rsrc = resource_section(version_resource(quirk));

    const uint32_t headers_size = file_alignment;
    const uint32_t text_raw_size =
        align(static_cast<uint32_t>(text.size()), file_alignment);
    const uint32_t rsrc_raw_size =
        align(static_cast<uint32_t>(rsrc.size()), file_alignment);
    const uint32_t text_offset = headers_size;
    const uint32_t rsrc_offset = text_offset + text_raw_size;
    const uint32_t image_size = align(
        rsrc_rva + static_cast<uint32_t>(rsrc.size()), section_alignment);

    Bytes out;

    // DOS header, only `e_magic` and `e_lfanew`
    put16(out, 0x5A4D);
    out.resize(0x3C, 0);
    put32(out, 0x40);

    // PE signature and COFF header
    put32(out, 0x00004550);
    put16(out, 0x8664);  // Machine: AMD64
    put16(out, 2);       // NumberOfSections
    put32(out, 0);       // TimeDateStamp
    put32(out, 0);       // PointerToSymbolTable
    put32(out, 0);       // NumberOfSymbols
    put16(out, 240);     // SizeOfOptionalHeader
    put16(out, 0x0022);  // Characteristics: executable, large address aware

    // PE32+ optional header
    const size_t optional_header_start = out.size();
    put16(out, 0x20B);          // Magic
    out.push_back(14);          // MajorLinkerVersion
    out.push_back(0);           // MinorLinkerVersion
    put32(out, text_raw_size);  // SizeOfCode
    put32(out, rsrc_raw_size);  // SizeOfInitializedData
    put32(out, 0);              // SizeOfUninitializedData
    put32(out, text_rva);       // AddressOfEntryPoint
    put32(out, text_rva);       // BaseOfCode
    put64(out, image_base);
    put32(out, section_alignment);
    put32(out, file_alignment);
    put16(out, 6);  // MajorOperatingSystemVersion
    put16(out, 0);  // MinorOperatingSystemVersion
    put16(out, 0);  // MajorImageVersion
    put16(out, 0);  // MinorImageVersion
    put16(out, 6);  // MajorSubsystemVersion
    put16(out, 0);  // MinorSubsystemVersion
    put32(out, 0);  // Win32VersionValue
    put32(out, image_size);
    put32(out, headers_size);
    put32(out, 0);          // CheckSum
    put16(out, 2);          // Subsystem: Windows GUI
    put16(out, 0x0100);     // DllCharacteristics: NX compatible
    put64(out, 0x100000);   // SizeOfStackReserve
    put64(out, 0x1000);     // SizeOfStackCommit
    put64(out, 0x100000);   // SizeOfHeapReserve
    put64(out, 0x1000);     // SizeOfHeapCommit
    put32(out, 0);          // LoaderFlags
    put32(out, 16);         // NumberOfRvaAndSizes
    for (int directory = 0; directory < 16; directory++) {
        // Only the resource table is used
        const bool is_resource_table = directory == 2;
        put32(out, is_resource_table ? rsrc_rva : 0);
        put32(out,
              is_resource_table ? static_cast<uint32_t>(rsrc.size()) : 0);
    }
    if (out.size() - optional_header_start != 240) {
        throw std::logic_error("Unexpected optional header size");
    }

    const auto section_header = [&](std::string_view name,
                                    uint32_t virtual_size, uint32_t rva,
                                    uint32_t raw_size, uint32_t raw_offset,
                                    uint32_t characteristics) {
        std::string padded_name(name);
        padded_name.resize(8, '\0');
        out.insert(out.end(), padded_name.begin(), padded_name.end());
        put32(out, virtual_size);
        put32(out, rva);
        put32(out, raw_size);
        put32(out, raw_offset);
        put32(out, 0);  // PointerToRelocations
        put32(out, 0);  // PointerToLinenumbers
        put16(out, 0);  // NumberOfRelocations
        put16(out, 0);  // NumberOfLinenumbers
        put32(out, characteristics);
    };
    section_header(".text", static_cast<uint32_t>(text.size()), text_rva,
                   text_raw_size, text_offset, 0x60000020);
    section_header(".rsrc", static_cast<uint32_t>(rsrc.size()), rsrc_rva,
                   rsrc_raw_size, rsrc_offset, 0x40000040);

    out.resize(headers_size, 0);
    out.insert(out.end(), text.begin(), text.end());
    out.resize(text_offset + text_raw_size, 0);
    out.insert(out.end(), rsrc.begin(), rsrc.end());
    out.resize(rsrc_offset + rsrc_raw_size, 0);

    return out;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <output_directory>" << std::endl;
        return 1;
    }

    const std::filesystem::path output_directory(argv[1]);
    std::filesystem::create_directories(output_directory);
    for (const DawQuirk& quirk : daw_quirks) {
        // Some DAWs are only listed for `wayland_only`
        if (!quirk.presents_as_windows_executable()) {
            continue;
        }

        const Bytes executable = make_executable(quirk);
        std::ofstream file(output_directory / quirk.windows_executable,
                           std::ios::binary);
        file.write(reinterpret_cast<const char*>(executable.data()),
                   static_cast<std::streamsize>(executable.size()));
        if (!file) {
            std::cerr << "Could not write the stub for '"
                      << quirk.windows_executable << "'" << std::endl;
            return 1;
        }
    }

    return 0;
}
