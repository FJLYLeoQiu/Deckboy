// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The filesystem library, spelled dbfs:: everywhere in Deckboy.
//
// On a Mac, std::filesystem is not compiled into the app. It lives in the C++
// runtime that ships WITH macOS, and Apple only added it in 10.15 (Catalina).
// The Intel Mac build runs on 10.13 High Sierra, where using it means the app
// will not even launch, so there dbfs is ghc::filesystem: a header-only copy
// of the same API (native/third_party/ghc, MIT, v1.5.16). Every other build,
// the Apple Silicon one included, gets the standard library's own.
//
// DECKBOY_GHC_FILESYSTEM forces the copy on any platform, to compile-test it
// without a Mac.
#if defined(DECKBOY_GHC_FILESYSTEM) || \
    (defined(__APPLE__) && defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__) && \
     __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 101500)
#include "../third_party/ghc/filesystem.hpp"
namespace dbfs = ghc::filesystem;
#else
#include <filesystem>
namespace dbfs = std::filesystem;
#endif

#include <string>
#include <string_view>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace deckboy {

/// Converts a UTF-8 path string to a dbfs::path portably.
/// On Windows, converts UTF-8 directly to UTF-16 wide characters, bypassing
/// any system ANSI active code page (CP_ACP/GBK/Shift-JIS) distortions.
inline dbfs::path u8path(std::string_view u8str) {
#if defined(_WIN32)
  if (u8str.empty()) return dbfs::path();
  int len = MultiByteToWideChar(CP_UTF8, 0, u8str.data(), static_cast<int>(u8str.size()), nullptr, 0);
  if (len <= 0) return dbfs::path(std::string(u8str));
  std::wstring wstr(static_cast<std::size_t>(len), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, u8str.data(), static_cast<int>(u8str.size()), &wstr[0], len);
  return dbfs::path(wstr);
#elif defined(DECKBOY_GHC_FILESYSTEM)
  return ghc::filesystem::u8path(u8str.begin(), u8str.end());
#else
  return std::filesystem::path(u8str);
#endif
}

inline dbfs::path u8path(const std::string& u8str) {
  return u8path(std::string_view(u8str));
}

inline dbfs::path u8path(const char* u8str) {
  return u8str ? u8path(std::string_view(u8str)) : dbfs::path();
}

/// Converts a dbfs::path to a UTF-8 encoded std::string portably.
inline std::string u8string(const dbfs::path& p) {
#if defined(_WIN32)
  const std::wstring& w = p.native();
  if (w.empty()) return std::string();
  int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  if (len <= 0) return std::string();
  std::string s(static_cast<std::size_t>(len), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], len, nullptr, nullptr);
  return s;
#else
  return p.string();
#endif
}

}  // namespace deckboy
