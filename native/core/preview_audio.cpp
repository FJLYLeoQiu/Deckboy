// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#include "preview_audio.hpp"

#include <algorithm>
#include <cmath>

namespace deckboy {
namespace core {
namespace {

std::string trimCopy(std::string s) {
  auto notSpace = [](unsigned char c) { return c != ' ' && c != '\t' && c != '\r' && c != '\n'; };
  while (!s.empty() && !notSpace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
  while (!s.empty() && !notSpace(static_cast<unsigned char>(s.back()))) s.pop_back();
  return s;
}

}  // namespace

PreviewDeviceStatus PreviewAudioPolicy::resolvePreviewDevice(
    const std::string& wantedName,
    const std::vector<std::string>& liveDeviceNames,
    const std::string& programDeviceName,
    std::string* resolvedName,
    std::string* message) {
  const std::string wanted = trimCopy(wantedName);
  if (resolvedName) resolvedName->clear();
  if (wanted.empty()) {
    if (message) {
      *message = "No preview audio device is set; cue preview stays silent.";
    }
    return PreviewDeviceStatus::Idle;
  }
  for (const auto& live : liveDeviceNames) {
    if (trimCopy(live) == wanted) {
      if (resolvedName) *resolvedName = wanted;
      if (message) message->clear();
      return PreviewDeviceStatus::Open;
    }
  }
  // Missing: stay muted. Do not even mention falling back to programDeviceName.
  (void)programDeviceName;
  if (message) {
    *message = "Preview audio device \"" + wanted +
               "\" was not found. Preview is muted; it will not fall back "
               "to the program / audience device.";
  }
  return PreviewDeviceStatus::MissingMuted;
}

void PreviewAudioPolicy::mixInto(std::vector<std::int16_t>& dest,
                                 const std::vector<std::int16_t>& src,
                                 float gain) {
  if (src.size() > dest.size()) dest.resize(src.size(), 0);
  const float g = std::clamp(gain, 0.0f, 4.0f);
  for (std::size_t i = 0; i < src.size(); ++i) {
    const float mixed = static_cast<float>(dest[i]) + static_cast<float>(src[i]) * g;
    const float clipped = std::clamp(mixed, -32768.0f, 32767.0f);
    dest[i] = static_cast<std::int16_t>(std::lrintf(clipped));
  }
}

bool PreviewAudioPolicy::shouldStopPreviewOnTake(const std::string& previewCueId,
                                                 const std::string& takenCueId) {
  if (previewCueId.empty() || takenCueId.empty()) return false;
  return previewCueId == takenCueId;
}

const char* PreviewAudioPolicy::reminderRouteToken(SpeechReminderAudioRoute route) {
  return route == SpeechReminderAudioRoute::Audience ? "audience" : "control";
}

SpeechReminderAudioRoute PreviewAudioPolicy::reminderRouteFromToken(const std::string& token) {
  return token == "audience" ? SpeechReminderAudioRoute::Audience
                             : SpeechReminderAudioRoute::Control;
}

}  // namespace core
}  // namespace deckboy
