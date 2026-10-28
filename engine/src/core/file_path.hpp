// file_path.hpp -- the engine's paths are UTF-8 (the C ABI says so).
//
// On POSIX a std::string is already what the filesystem takes, so this is the
// identity and the Apple build is unchanged. On Windows a narrow std::string
// handed to std::ifstream is read in the ANSI code page, which cannot name an
// install folder under a non-ASCII user name; going through char8_t makes it
// a wide path, which libstdc++ (MinGW) and MSVC both open with _wfopen.
#pragma once
#include <string>
#ifdef _WIN32
#include <filesystem>
#endif

namespace spk {

#ifdef _WIN32
inline std::filesystem::path file_path(const std::string& utf8) {
    // Verbatim Windows paths (\\\\?\\) do not accept forward slashes, while
    // resource callers append components with '/'. Normalize at the boundary.
    auto path = std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
    return path.make_preferred();
}
#else
inline const std::string& file_path(const std::string& utf8) { return utf8; }
#endif

}  // namespace spk
