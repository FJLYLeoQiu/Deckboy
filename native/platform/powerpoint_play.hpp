// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// powerpoint_play.hpp — Client for the native PowerPoint slideshow helper.
//
// On Windows this talks to deckboy-ppt-helper over stdin/stdout. Elsewhere
// every call returns a clear "unsupported platform" status; the existing
// "import as stills" path (PDF flatten) remains available.
//
// This is NOT a renderer. It drives Microsoft PowerPoint's own slideshow
// (animation builds, transitions, embedded media). The show lives in a
// PowerPoint window. It is not, by itself, composited into Deckboy's
// programme / NDI / recording picture.
// ============================================================================

#ifndef DECKBOY_PLATFORM_POWERPOINT_PLAY_HPP
#define DECKBOY_PLATFORM_POWERPOINT_PLAY_HPP

#include "powerpoint_protocol.hpp"

#include <string>

namespace deckboy {
namespace platform {

class PowerPointPlayer {
public:
  PowerPointPlayer() = default;
  ~PowerPointPlayer();

  PowerPointPlayer(const PowerPointPlayer&) = delete;
  PowerPointPlayer& operator=(const PowerPointPlayer&) = delete;

  // Compile-time + runtime: true only on Windows. Availability of Office
  // itself is reported by probe().
  static bool platformSupported();

  // Status with code "unsupported_platform", "no_powerpoint", or a live probe.
  PptStatus probe();

  PptStatus openShow(const PptOpenOptions& opts);
  PptStatus nextBuild();
  PptStatus prevBuild();
  PptStatus gotoSlide(int slideOneBased);
  PptStatus endShow();
  PptStatus status();
  PptStatus closeShow();

  void shutdown();

  static std::string unsupportedMessageEn();
  static std::string unsupportedMessageZh();
  static std::string missingOfficeMessageEn();
  static std::string missingOfficeMessageZh();

private:
  PptStatus send(const PptCommand& cmd);
  bool ensureHelper(std::string& error, std::string& code);

  // Opaque so this header does not pull subprocess.hpp into every unit.
  struct Helper;
  Helper* helper_ = nullptr;
};

}  // namespace platform
}  // namespace deckboy

#endif
