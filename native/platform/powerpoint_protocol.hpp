// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// powerpoint_protocol.hpp — Line protocol between Deckboy and the Windows
// PowerPoint helper (deckboy-ppt-helper).
//
// One JSON object per line, UTF-8. The helper speaks late-bound COM on a
// dedicated STA thread; this file is only the messages, so it can be tested
// without Office and on every platform.
// ============================================================================

#ifndef DECKBOY_PLATFORM_POWERPOINT_PROTOCOL_HPP
#define DECKBOY_PLATFORM_POWERPOINT_PROTOCOL_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace deckboy {
namespace platform {

enum class PptCommandType {
  Ping,
  Probe,
  Open,
  Next,
  Prev,
  Goto,
  End,
  Status,
  Close,
  Quit,
  Unknown,
};

struct PptOpenOptions {
  std::string path;
  int startSlide = 1;          // 1-based; 0 means start at the beginning
  bool windowed = true;        // ppShowTypeWindow so we can place the show
  int left = 0;                // pixels; helper converts to points
  int top = 0;
  int width = 0;               // 0 = let PowerPoint choose
  int height = 0;
};

struct PptCommand {
  PptCommandType type = PptCommandType::Unknown;
  PptOpenOptions open;
  int slide = 0;               // Goto
};

struct PptStatus {
  bool ok = false;
  std::string error;
  std::string code;            // "no_powerpoint", "protected_view", "not_running", ...
  bool available = false;      // PowerPoint appears to be installed
  bool running = false;        // a show we opened is in a slideshow
  bool ended = false;
  int slide = 0;               // 1-based current position, 0 if unknown
  int slides = 0;
  std::string state;           // "idle","running","paused","ended"
  std::uint64_t hwnd = 0;      // slideshow window, 0 if unknown
  bool attachedToExisting = false;
};

std::string pptCommandTypeName(PptCommandType type);
PptCommandType pptCommandTypeFromName(const std::string& name);

// Encode / decode a single line (no trailing newline required on input).
std::string encodePptCommand(const PptCommand& cmd);
bool decodePptCommand(const std::string& line, PptCommand& out, std::string* error = nullptr);

std::string encodePptStatus(const PptStatus& st);
bool decodePptStatus(const std::string& line, PptStatus& out, std::string* error = nullptr);

bool isPptFilename(const std::string& path);
// Macro-capable formats we will open with automation security forced off,
// but never "enable content" or run macros.
bool isPptMacroFilename(const std::string& path);

}  // namespace platform
}  // namespace deckboy

#endif
