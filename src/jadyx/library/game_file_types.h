// SPDX-FileCopyrightText: Copyright 2026 Jadyx Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace Jadyx {

/// The game files the library lists, by lower-case extension (with the dot): Switch 1 and
/// Switch 2 (NX2) dumps the core loader opens directly. .nup is the Switch 2 package format
/// (core/loader/nup.cpp); .nsz and .xcz are compressed .nsp and .xci.
[[nodiscard]] inline bool IsGameFileExtension(std::string_view ext) {
    return ext == ".nsp" || ext == ".xci" || ext == ".nro" || ext == ".nsz" || ext == ".xcz" ||
           ext == ".nup";
}

/// Lower-case copy of a name or extension.
[[nodiscard]] inline std::string LowerAscii(std::string_view text) {
    std::string result{text};
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

/// An extracted ("raw") game is a folder whose exefs holds main and main.npdm (and a romfs
/// folder beside it). The library finds it by main.npdm and launches the main file next to it,
/// which the loader opens as a deconstructed ROM directory (Loader::FileType::DeconstructedRomDirectory).
inline constexpr std::string_view RawGameMarker = "main.npdm";
inline constexpr std::string_view RawGameLaunchFile = "main";

/// The title folder of a raw game, given the folder holding its main file: the folder that holds
/// exefs, or that folder itself when it is not called exefs.
template <typename Path>
[[nodiscard]] Path RawGameTitleFolder(const Path& exefs_dir) {
    const auto name = LowerAscii(exefs_dir.filename().string());
    if (name == "exefs" && exefs_dir.has_parent_path() && !exefs_dir.parent_path().empty()) {
        return exefs_dir.parent_path().filename();
    }
    return exefs_dir.filename();
}

} // namespace Jadyx
