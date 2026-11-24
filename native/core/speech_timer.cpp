// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#include "speech_timer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace deckboy {
namespace core {
namespace {

std::string trimCopy(std::string s) {
  auto notSpace = [](unsigned char c) { return c != ' ' && c != '\t' && c != '\r'; };
  while (!s.empty() && !notSpace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
  while (!s.empty() && !notSpace(static_cast<unsigned char>(s.back()))) s.pop_back();
  return s;
}

bool hasFired(const std::vector<std::string>& fired, const std::string& id) {
  return std::find(fired.begin(), fired.end(), id) != fired.end();
}

int overlapArea(IntRect a, IntRect b) {
  const int x0 = std::max(a.x, b.x);
  const int y0 = std::max(a.y, b.y);
  const int x1 = std::min(a.x + a.w, b.x + b.w);
  const int y1 = std::min(a.y + a.h, b.y + b.h);
  if (x1 <= x0 || y1 <= y0) return 0;
  return (x1 - x0) * (y1 - y0);
}

std::string escapeField(const std::string& s) {
  if (s.find_first_of(",\"\n") == std::string::npos) return s;
  std::string out = "\"";
  for (char c : s) {
    if (c == '"') out += '"';
    out += c;
  }
  out += '"';
  return out;
}

}  // namespace

SpeechTimer::SpeechTimer() { config_ = defaultConfig(); }

SpeechTimerConfig SpeechTimer::defaultConfig() {
  SpeechTimerConfig cfg;
  cfg.durationSeconds = 600;
  cfg.displayTarget = SpeechTimerDisplayTarget::Audience;
  cfg.reminders = {
    {"rem-remain-120", SpeechReminderWhen::Remaining, 120,
     SpeechReminderSound{SpeechSoundKind::BuiltIn, 0, {}}, true, "2 minutes left"},
    {"rem-remain-60", SpeechReminderWhen::Remaining, 60,
     SpeechReminderSound{SpeechSoundKind::BuiltIn, 5, {}}, true, "1 minute left"},
    {"rem-zero", SpeechReminderWhen::AtZero, 0,
     SpeechReminderSound{SpeechSoundKind::BuiltIn, 1, {}}, true, "Time is up"},
  };
  return cfg;
}

std::string SpeechTimer::formatTime(double seconds, bool overtimePrefix) {
  const double mag = std::fabs(seconds);
  long total = static_cast<long>(std::llround(mag));
  if (total < 0) total = 0;
  const long hours = total / 3600;
  const long minutes = (total % 3600) / 60;
  const long secs = total % 60;
  char buf[32];
  if (hours > 0) {
    std::snprintf(buf, sizeof(buf), "%s%ld:%02ld:%02ld",
                  overtimePrefix ? "+" : "", hours, minutes, secs);
  } else {
    std::snprintf(buf, sizeof(buf), "%s%ld:%02ld",
                  overtimePrefix ? "+" : "", minutes, secs);
  }
  return buf;
}

std::string SpeechTimer::phaseLabel(SpeechTimerPhase phase) {
  switch (phase) {
    case SpeechTimerPhase::Running:        return "Running";
    case SpeechTimerPhase::Paused:         return "Paused";
    case SpeechTimerPhase::Overtime:       return "Overtime";
    case SpeechTimerPhase::OvertimePaused: return "Overtime paused";
    case SpeechTimerPhase::Idle:
    default:                               return "Ready";
  }
}

const char* SpeechTimer::builtInSoundName(int id) {
  switch (clampBuiltInId(id)) {
    case 1:  return "triple beep";
    case 2:  return "gong";
    case 3:  return "rising";
    case 4:  return "buzzer";
    case 5:  return "soft";
    default: return "bell";
  }
}

int SpeechTimer::clampBuiltInId(int id) {
  const int n = ((id % 6) + 6) % 6;
  return n;
}

IntRect SpeechTimer::clampRectToDisplays(IntRect wanted,
                                         const std::vector<IntRect>& displays,
                                         int minW,
                                         int minH,
                                         IntRect fallback) {
  minW = std::max(1, minW);
  minH = std::max(1, minH);
  wanted.w = std::max(minW, wanted.w);
  wanted.h = std::max(minH, wanted.h);

  IntRect area = fallback;
  if (!displays.empty()) {
    int best = -1;
    int bestOverlap = -1;
    for (int i = 0; i < static_cast<int>(displays.size()); ++i) {
      const int o = overlapArea(wanted, displays[static_cast<std::size_t>(i)]);
      if (o > bestOverlap) {
        bestOverlap = o;
        best = i;
      }
    }
    // No overlap: the saved display is gone. Use the first (primary) display.
    if (bestOverlap <= 0) best = 0;
    area = displays[static_cast<std::size_t>(best)];
  }
  if (area.w <= 0 || area.h <= 0) {
    // Nothing to clamp to; keep the requested size, origin at 0.
    return IntRect{0, 0, wanted.w, wanted.h};
  }
  IntRect out = wanted;
  out.w = std::min(out.w, area.w);
  out.h = std::min(out.h, area.h);
  out.w = std::max(minW, std::min(out.w, area.w));
  out.h = std::max(minH, std::min(out.h, area.h));
  out.x = std::min(std::max(wanted.x, area.x), area.x + area.w - out.w);
  out.y = std::min(std::max(wanted.y, area.y), area.y + area.h - out.h);
  return out;
}

void SpeechTimer::clampOverlayNorm(SpeechTimerConfig& cfg) {
  auto clamp01 = [](float v, float lo, float hi) {
    return std::min(hi, std::max(lo, v));
  };
  cfg.overlayW = clamp01(cfg.overlayW, 0.08f, 1.0f);
  cfg.overlayH = clamp01(cfg.overlayH, 0.06f, 1.0f);
  cfg.overlayX = clamp01(cfg.overlayX, 0.0f, 1.0f - cfg.overlayW);
  cfg.overlayY = clamp01(cfg.overlayY, 0.0f, 1.0f - cfg.overlayH);
  if (cfg.durationSeconds < 1) cfg.durationSeconds = 1;
  if (cfg.windowW < 280) cfg.windowW = 280;
  if (cfg.windowH < 120) cfg.windowH = 120;
  for (auto& rem : cfg.reminders) {
    rem.sound.builtInId = clampBuiltInId(rem.sound.builtInId);
    if (rem.offsetSeconds < 0) rem.offsetSeconds = 0;
    if (rem.id.empty()) rem.id = "rem";
  }
}

void SpeechTimer::setConfig(SpeechTimerConfig cfg) {
  clampOverlayNorm(cfg);
  config_ = std::move(cfg);
  applyElapsedLocked();
}

void SpeechTimer::start(std::int64_t monotonicMs, std::string startedAtIso) {
  if (phase_ == SpeechTimerPhase::Paused || phase_ == SpeechTimerPhase::OvertimePaused) {
    resume(monotonicMs);
    return;
  }
  if (phase_ == SpeechTimerPhase::Running || phase_ == SpeechTimerPhase::Overtime) {
    lastTickMs_ = monotonicMs;
    return;
  }
  elapsedSeconds_ = 0.0;
  firedIds_.clear();
  lastReminderText_.clear();
  lastTickMs_ = monotonicMs;
  phase_ = SpeechTimerPhase::Running;
  if (!startedAtIso.empty()) startedAtIso_ = std::move(startedAtIso);
  else if (startedAtIso_.empty()) startedAtIso_ = {};
}

void SpeechTimer::pause(std::int64_t monotonicMs) {
  if (phase_ == SpeechTimerPhase::Running || phase_ == SpeechTimerPhase::Overtime) {
    tick(monotonicMs);
    phase_ = (elapsedSeconds_ >= static_cast<double>(config_.durationSeconds))
               ? SpeechTimerPhase::OvertimePaused
               : SpeechTimerPhase::Paused;
    lastTickMs_ = -1;
  }
}

void SpeechTimer::resume(std::int64_t monotonicMs) {
  if (phase_ == SpeechTimerPhase::Paused || phase_ == SpeechTimerPhase::OvertimePaused) {
    lastTickMs_ = monotonicMs;
    phase_ = (elapsedSeconds_ >= static_cast<double>(config_.durationSeconds))
               ? SpeechTimerPhase::Overtime
               : SpeechTimerPhase::Running;
  }
}

void SpeechTimer::reset() {
  elapsedSeconds_ = 0.0;
  lastTickMs_ = -1;
  firedIds_.clear();
  lastReminderText_.clear();
  startedAtIso_.clear();
  phase_ = SpeechTimerPhase::Idle;
}

void SpeechTimer::beginNewSpeech(std::int64_t monotonicMs, std::string startedAtIso,
                                 std::string endedAtIso) {
  if (elapsedSeconds_ > 0.0) {
    SpeechSessionRecord row;
    row.startedAtIso = startedAtIso_;
    row.endedAtIso = std::move(endedAtIso);
    row.durationSeconds = config_.durationSeconds;
    row.usedSeconds = elapsedSeconds_;
    const double over = elapsedSeconds_ - static_cast<double>(config_.durationSeconds);
    row.overtimeSeconds = over > 0.0 ? over : 0.0;
    row.kind = "new_speech";
    records_.push_back(std::move(row));
  }
  reset();
  if (!startedAtIso.empty()) startedAtIso_ = startedAtIso;
  start(monotonicMs, startedAtIso);
}

void SpeechTimer::tick(std::int64_t monotonicMs) {
  if (phase_ != SpeechTimerPhase::Running && phase_ != SpeechTimerPhase::Overtime) {
    lastTickMs_ = -1;
    return;
  }
  if (lastTickMs_ < 0) {
    lastTickMs_ = monotonicMs;
    return;
  }
  const std::int64_t delta = monotonicMs - lastTickMs_;
  lastTickMs_ = monotonicMs;
  if (delta > 0) {
    elapsedSeconds_ += static_cast<double>(delta) / 1000.0;
  }
  applyElapsedLocked();
}

void SpeechTimer::applyElapsedLocked() {
  if (phase_ == SpeechTimerPhase::Idle) return;
  const double duration = static_cast<double>(std::max(1, config_.durationSeconds));
  const bool over = elapsedSeconds_ >= duration;
  if (phase_ == SpeechTimerPhase::Running && over) {
    phase_ = SpeechTimerPhase::Overtime;
  } else if (phase_ == SpeechTimerPhase::Paused && over) {
    phase_ = SpeechTimerPhase::OvertimePaused;
  } else if (phase_ == SpeechTimerPhase::Overtime && !over) {
    phase_ = SpeechTimerPhase::Running;
  } else if (phase_ == SpeechTimerPhase::OvertimePaused && !over) {
    phase_ = SpeechTimerPhase::Paused;
  }
}

int SpeechTimer::reminderSeverity(const SpeechReminder& rem) const {
  // Higher is more urgent. Overtime > zero > remaining (smaller remaining is
  // closer to the end, so more urgent).
  switch (rem.when) {
    case SpeechReminderWhen::Overtime:
      return 2000 + rem.offsetSeconds;
    case SpeechReminderWhen::AtZero:
      return 1000;
    case SpeechReminderWhen::Remaining:
    default:
      return 1000 - rem.offsetSeconds;
  }
}

bool SpeechTimer::reminderIsDue(const SpeechReminder& rem, double remaining,
                                double overtime) const {
  if (!rem.enabled) return false;
  switch (rem.when) {
    case SpeechReminderWhen::Remaining:
      return remaining <= static_cast<double>(rem.offsetSeconds);
    case SpeechReminderWhen::AtZero:
      return remaining <= 0.0;
    case SpeechReminderWhen::Overtime:
      return overtime >= static_cast<double>(rem.offsetSeconds) && overtime > 0.0;
  }
  return false;
}

SpeechReminderFire SpeechTimer::pollDueReminders(std::int64_t monotonicMs) {
  tick(monotonicMs);
  SpeechReminderFire fire;
  if (phase_ == SpeechTimerPhase::Idle) return fire;
  // Paused speeches do not fire new reminders (time is frozen).
  if (phase_ == SpeechTimerPhase::Paused || phase_ == SpeechTimerPhase::OvertimePaused) {
    return fire;
  }
  const double duration = static_cast<double>(std::max(1, config_.durationSeconds));
  const double remaining = duration - elapsedSeconds_;
  const double overtime = remaining < 0.0 ? -remaining : 0.0;

  const SpeechReminder* best = nullptr;
  int bestSev = -1;
  for (const auto& rem : config_.reminders) {
    if (hasFired(firedIds_, rem.id)) continue;
    if (!reminderIsDue(rem, remaining, overtime)) continue;
    fire.markedFiredIds.push_back(rem.id);
    const int sev = reminderSeverity(rem);
    if (sev > bestSev) {
      if (best) fire.suppressedIds.push_back(best->id);
      best = &rem;
      bestSev = sev;
    } else {
      fire.suppressedIds.push_back(rem.id);
    }
  }
  for (const auto& id : fire.markedFiredIds) {
    if (!hasFired(firedIds_, id)) firedIds_.push_back(id);
  }
  if (best) {
    fire.any = true;
    fire.toPlay = *best;
    lastReminderText_ = best->label;
    // The winner must not also sit in suppressedIds.
    fire.suppressedIds.erase(std::remove(fire.suppressedIds.begin(),
                                         fire.suppressedIds.end(),
                                         best->id),
                             fire.suppressedIds.end());
  }
  return fire;
}

SpeechTimerSnapshot SpeechTimer::makeSnapshot() const {
  SpeechTimerSnapshot s;
  s.phase = phase_;
  s.durationSeconds = config_.durationSeconds;
  s.elapsedSeconds = elapsedSeconds_;
  s.remainingSeconds = static_cast<double>(config_.durationSeconds) - elapsedSeconds_;
  s.overtime = s.remainingSeconds < 0.0;
  s.overtimeSeconds = s.overtime ? -s.remainingSeconds : 0.0;
  s.running = (phase_ == SpeechTimerPhase::Running || phase_ == SpeechTimerPhase::Overtime);
  s.paused = (phase_ == SpeechTimerPhase::Paused || phase_ == SpeechTimerPhase::OvertimePaused);
  if (s.overtime) {
    s.timeText = formatTime(s.overtimeSeconds, true);
  } else {
    s.timeText = formatTime(s.remainingSeconds, false);
  }
  s.stateText = phaseLabel(phase_);
  if (s.overtime) {
    s.stateText = "Overtime " + formatTime(s.overtimeSeconds, false);
  }
  s.reminderText = lastReminderText_;
  s.firedReminderIds = firedIds_;
  return s;
}

SpeechTimerSnapshot SpeechTimer::snapshot() const { return makeSnapshot(); }

std::string SpeechTimer::exportSessionCsv() const {
  std::ostringstream out;
  out << "started_at,ended_at,duration_s,used_s,overtime_s,kind\n";
  for (const auto& row : records_) {
    out << escapeField(row.startedAtIso) << ','
        << escapeField(row.endedAtIso) << ','
        << row.durationSeconds << ','
        << row.usedSeconds << ','
        << row.overtimeSeconds << ','
        << escapeField(row.kind) << '\n';
  }
  return out.str();
}

std::string SpeechTimer::saveConfigText() const {
  std::ostringstream out;
  out << "duration " << config_.durationSeconds << "\n";
  out << "display_target "
      << (config_.displayTarget == SpeechTimerDisplayTarget::Speaker ? "speaker" : "audience")
      << "\n";
  out << "overlay " << config_.overlayX << ' ' << config_.overlayY << ' '
      << config_.overlayW << ' ' << config_.overlayH << "\n";
  out << "window " << config_.windowX << ' ' << config_.windowY << ' '
      << config_.windowW << ' ' << config_.windowH << "\n";
  out << "font " << config_.fontSize << "\n";
  out << "layout_locked " << (config_.layoutLocked ? 1 : 0) << "\n";
  out << "always_on_top " << (config_.alwaysOnTop ? 1 : 0) << "\n";
  out << "window_visible " << (config_.windowVisible ? 1 : 0) << "\n";
  out << "orientation "
      << (config_.orientation == SpeechTimerOrientation::Horizontal ? "horizontal"
                                                                    : "vertical") << "\n";
  out << "mute " << (config_.muteReminders ? 1 : 0) << "\n";
  out << "reminder_audio_route "
      << PreviewAudioPolicy::reminderRouteToken(config_.reminderAudioRoute) << "\n";
  out << "auto_start_ppt " << (config_.autoStartWithPowerPoint ? 1 : 0) << "\n";
  out << "audio_device " << config_.reminderAudioDeviceName << "\n";
  for (const auto& rem : config_.reminders) {
    const char* when = rem.when == SpeechReminderWhen::AtZero ? "zero"
                      : rem.when == SpeechReminderWhen::Overtime ? "overtime"
                                                                 : "remaining";
    const char* kind = rem.sound.kind == SpeechSoundKind::File ? "file" : "builtin";
    out << "reminder " << rem.id << ' ' << when << ' ' << rem.offsetSeconds << ' '
        << kind << ' ' << rem.sound.builtInId << ' ' << (rem.enabled ? 1 : 0)
        << " {" << rem.label << "}";
    if (rem.sound.kind == SpeechSoundKind::File) {
      out << " [" << rem.sound.filePath << "]";
    }
    out << "\n";
  }
  return out.str();
}

bool SpeechTimer::loadConfigText(const std::string& text, std::string* error) {
  SpeechTimerConfig cfg = defaultConfig();
  cfg.reminders.clear();
  std::istringstream in(text);
  std::string line;
  int lineNo = 0;
  while (std::getline(in, line)) {
    ++lineNo;
    line = trimCopy(line);
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ls(line);
    std::string key;
    ls >> key;
    if (key == "duration") {
      ls >> cfg.durationSeconds;
    } else if (key == "display_target") {
      std::string v;
      ls >> v;
      cfg.displayTarget = (v == "speaker") ? SpeechTimerDisplayTarget::Speaker
                                           : SpeechTimerDisplayTarget::Audience;
    } else if (key == "overlay") {
      ls >> cfg.overlayX >> cfg.overlayY >> cfg.overlayW >> cfg.overlayH;
    } else if (key == "window") {
      ls >> cfg.windowX >> cfg.windowY >> cfg.windowW >> cfg.windowH;
    } else if (key == "font") {
      ls >> cfg.fontSize;
    } else if (key == "layout_locked") {
      int v = 0; ls >> v; cfg.layoutLocked = v != 0;
    } else if (key == "always_on_top") {
      int v = 0; ls >> v; cfg.alwaysOnTop = v != 0;
    } else if (key == "window_visible") {
      int v = 1; ls >> v; cfg.windowVisible = v != 0;
    } else if (key == "orientation") {
      std::string v;
      ls >> v;
      cfg.orientation = (v == "horizontal") ? SpeechTimerOrientation::Horizontal
                                            : SpeechTimerOrientation::Vertical;
    } else if (key == "mute") {
      int v = 0; ls >> v; cfg.muteReminders = v != 0;
    } else if (key == "reminder_audio_route") {
      std::string v;
      ls >> v;
      cfg.reminderAudioRoute = PreviewAudioPolicy::reminderRouteFromToken(v);
    } else if (key == "auto_start_ppt") {
      int v = 0; ls >> v; cfg.autoStartWithPowerPoint = v != 0;
    } else if (key == "audio_device") {
      std::string rest;
      std::getline(ls, rest);
      cfg.reminderAudioDeviceName = trimCopy(rest);
    } else if (key == "reminder") {
      SpeechReminder rem;
      std::string when, kind;
      int enabled = 1;
      ls >> rem.id >> when >> rem.offsetSeconds >> kind >> rem.sound.builtInId >> enabled;
      rem.when = when == "zero" ? SpeechReminderWhen::AtZero
               : when == "overtime" ? SpeechReminderWhen::Overtime
                                    : SpeechReminderWhen::Remaining;
      rem.sound.kind = kind == "file" ? SpeechSoundKind::File : SpeechSoundKind::BuiltIn;
      rem.enabled = enabled != 0;
      const auto brace = line.find('{');
      const auto braceEnd = line.find('}', brace == std::string::npos ? 0 : brace);
      if (brace != std::string::npos && braceEnd != std::string::npos && braceEnd > brace) {
        rem.label = line.substr(brace + 1, braceEnd - brace - 1);
      }
      const auto brack = line.find('[', braceEnd == std::string::npos ? 0 : braceEnd);
      const auto brackEnd = line.find(']', brack == std::string::npos ? 0 : brack);
      if (brack != std::string::npos && brackEnd != std::string::npos && brackEnd > brack) {
        rem.sound.filePath = line.substr(brack + 1, brackEnd - brack - 1);
      }
      if (rem.id.empty()) {
        if (error) *error = "reminder missing id at line " + std::to_string(lineNo);
        return false;
      }
      cfg.reminders.push_back(std::move(rem));
    } else {
      if (error) *error = "unknown key '" + key + "' at line " + std::to_string(lineNo);
      return false;
    }
  }
  if (cfg.reminders.empty()) {
    cfg.reminders = defaultConfig().reminders;
  }
  setConfig(std::move(cfg));
  return true;
}

}  // namespace core
}  // namespace deckboy
