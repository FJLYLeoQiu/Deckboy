// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// preview_audio.hpp — PROGRAM vs PREVIEW/CUE bus policy.
//
// Two independent buses:
//   Program  — the audience / formal playback device (and the same mix
//              tapped for recording, stream and NDI of that programme).
//   Preview  — the operator's listen / next-cue audition device.
//
// Rules this type encodes, and that the app must honour:
//   * A named preview device that is missing is MUTED and reported. It is
//     never silently replaced by the system default or the program device
//     (that would dump cue-preview into the HDMI / PA).
//   * Changing preview gain must not alter program samples.
//   * Preview samples must not be mixed into the program buffer (or the
//     record / stream / NDI tap, which is fed from the program engines).
//   * Taking a cue that is currently being previewed stops the preview
//     first, so the same file is not heard on both buses at once.
//   * Cross-device clock sync is not guaranteed (Bluetooth latency, HSP/HFP
//     hands-free profiles). Program AV sync stays on the existing program
//     clock; preview is an independent device with its own rate.
//
// This unit has no SDL dependency so the policy can be tested without
// hardware. Opening real devices is the app's job.
// ============================================================================

#ifndef DECKBOY_CORE_PREVIEW_AUDIO_HPP
#define DECKBOY_CORE_PREVIEW_AUDIO_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace deckboy {
namespace core {

enum class PreviewDeviceStatus {
  Idle = 0,          // no preview device configured (silent on purpose)
  Open = 1,          // wanted name is present in the live list
  MissingMuted = 2,  // wanted name is set but not in the live list — stay silent
};

enum class SpeechReminderAudioRoute {
  Control = 0,   // desk / UI / preview-listen path — the cautious default
  Audience = 1,  // programme / room / formal playback path
};

struct PreviewAudioPolicy {
  static constexpr float kDefaultPreviewVolume = 1.0f;
  static constexpr float kDefaultProgramBusVolume = 1.0f;

  // If wantedName is empty: Idle, resolved name empty (preview is off).
  // If wantedName is present in liveDeviceNames: Open, resolved = wanted.
  // Otherwise: MissingMuted, resolved empty — never programDeviceName, never
  // a "first available" or system-default substitute.
  static PreviewDeviceStatus resolvePreviewDevice(
      const std::string& wantedName,
      const std::vector<std::string>& liveDeviceNames,
      const std::string& programDeviceName,
      std::string* resolvedName,
      std::string* message);

  // Mix `src` into `dest` with a linear gain. dest is grown if needed and
  // left untouched where src does not cover it. Used by tests to prove
  // isolation and independent gains.
  static void mixInto(std::vector<std::int16_t>& dest,
                      const std::vector<std::int16_t>& src,
                      float gain);

  static bool shouldStopPreviewOnTake(const std::string& previewCueId,
                                      const std::string& takenCueId);

  static const char* reminderRouteToken(SpeechReminderAudioRoute route);
  static SpeechReminderAudioRoute reminderRouteFromToken(const std::string& token);
};

}  // namespace core
}  // namespace deckboy

#endif
