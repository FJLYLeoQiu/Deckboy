// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors

#include "core/preview_audio.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using deckboy::core::PreviewAudioPolicy;
using deckboy::core::PreviewDeviceStatus;
using deckboy::core::SpeechReminderAudioRoute;

static int failures = 0;

static void expect(bool cond, const char* msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << "\n";
    ++failures;
  }
}

int main() {
  const std::string hdmi = "HDMI Audio";
  const std::string bt = "Bluetooth Headphones";
  const std::vector<std::string> live {hdmi, bt};

  // Configured and present: open that device, not the program one.
  {
    std::string resolved, message;
    const auto st = PreviewAudioPolicy::resolvePreviewDevice(bt, live, hdmi, &resolved, &message);
    expect(st == PreviewDeviceStatus::Open, "present preview device opens");
    expect(resolved == bt, "resolved name is the preview device");
    expect(resolved != hdmi, "resolved name is not the program device");
  }

  // Missing preview device: muted, never substituted with HDMI / default.
  {
    std::string resolved, message;
    const auto st = PreviewAudioPolicy::resolvePreviewDevice(
        "Missing Speakers", live, hdmi, &resolved, &message);
    expect(st == PreviewDeviceStatus::MissingMuted, "missing preview device is muted");
    expect(resolved.empty(), "missing preview does not resolve to any device");
    expect(resolved != hdmi, "missing preview does not fall back to HDMI");
    expect(message.find("will not fall back") != std::string::npos,
           "operator message says there is no fallback");
  }

  // Empty preview name: idle / silent on purpose, not the system default.
  {
    std::string resolved, message;
    const auto st = PreviewAudioPolicy::resolvePreviewDevice("", live, hdmi, &resolved, &message);
    expect(st == PreviewDeviceStatus::Idle, "empty preview name is idle");
    expect(resolved.empty(), "idle preview resolves to no device");
  }

  // Isolation + independent gain: two cues mixed onto two buses.
  {
    const std::vector<std::int16_t> programCue {1000, 1000, 2000, 2000};
    const std::vector<std::int16_t> previewCue {3000, -3000, 4000, -4000};
    std::vector<std::int16_t> programBus;
    std::vector<std::int16_t> previewBus;
    PreviewAudioPolicy::mixInto(programBus, programCue, 1.0f);
    PreviewAudioPolicy::mixInto(previewBus, previewCue, 0.5f);
    expect(programBus == programCue, "program bus contains only the program cue at unity");
    expect(previewBus.size() == previewCue.size(), "preview bus size matches preview cue");
    expect(previewBus[0] == 1500 && previewBus[1] == -1500, "preview gain 0.5 applied");
    // Changing preview gain again must leave the already-mixed program bus alone.
    std::vector<std::int16_t> previewBus2;
    PreviewAudioPolicy::mixInto(previewBus2, previewCue, 1.0f);
    expect(programBus == programCue, "changing preview gain does not alter program samples");
    expect(previewBus2[0] == 3000, "preview unity gain is independent");
    // Program samples must not appear on the preview bus (no accidental mix).
    expect(previewBus2 != programBus, "preview and program buses stay distinct");
    // The program cue was never mixed into the preview bus. A single sample
    // value may coincide by chance, so compare the whole buffers.
    expect(previewBus2 == previewCue, "preview bus is exactly the preview cue");
    expect(programBus != previewCue, "program bus does not hold the preview cue");
  }

  // TAKE of the same cue stops preview; a different cue does not.
  expect(PreviewAudioPolicy::shouldStopPreviewOnTake("cue-a", "cue-a"),
         "taking the previewed cue stops the preview");
  expect(!PreviewAudioPolicy::shouldStopPreviewOnTake("cue-a", "cue-b"),
         "taking a different cue leaves preview running");
  expect(!PreviewAudioPolicy::shouldStopPreviewOnTake("", "cue-a"),
         "no preview id means nothing to stop");

  // Reminder audio route: default token is control, not audience.
  expect(std::string(PreviewAudioPolicy::reminderRouteToken(SpeechReminderAudioRoute::Control)) == "control",
         "control route token");
  expect(PreviewAudioPolicy::reminderRouteFromToken("audience") == SpeechReminderAudioRoute::Audience,
         "audience token parses");
  expect(PreviewAudioPolicy::reminderRouteFromToken("") == SpeechReminderAudioRoute::Control,
         "unknown / empty reminder route stays on control (cautious default)");
  expect(PreviewAudioPolicy::reminderRouteFromToken("both") == SpeechReminderAudioRoute::Control,
         "an unknown 'both' does not enable audience routing");

  if (failures) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "preview_audio_check: ok\n";
  return 0;
}
