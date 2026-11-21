// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// speech_timer.hpp — Operator-configured speech countdown with reminders.
//
// This is the clock and policy only: no SDL, no audio device, no window.
// The app ticks it with a monotonic millisecond clock, plays at most one
// reminder sound per poll, and decides WHERE the display is drawn.
//
// Display target defaults to Audience (programme outputs). Settings live on
// the desk; the audience-facing overlay shows only the time and the current
// reminder text. Audio routing is a separate setting and does not follow the
// visual target automatically.
// ============================================================================

#ifndef DECKBOY_CORE_SPEECH_TIMER_HPP
#define DECKBOY_CORE_SPEECH_TIMER_HPP

#include "preview_audio.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace deckboy {
namespace core {

enum class SpeechReminderWhen {
  Remaining = 0,  // remaining speech time first reaches <= offsetSeconds
  AtZero = 1,     // remaining first reaches <= 0
  Overtime = 2,   // overtime first reaches >= offsetSeconds
};

enum class SpeechSoundKind {
  BuiltIn = 0,    // one of the bundled chime voices (id 0..5)
  File = 1,       // a local audio file the operator chose
};

enum class SpeechTimerPhase {
  Idle = 0,
  Running = 1,
  Paused = 2,
  Overtime = 3,
  OvertimePaused = 4,
};

// Where the DISPLAY is drawn. Independent of reminder audio.
// Audience is the default and means the project's programme outputs
// (the audience window/screen, and the same picture used for NDI / record
// of those outputs). Speaker means presenter-view outputs.
enum class SpeechTimerDisplayTarget {
  Audience = 0,
  Speaker = 1,
};

struct IntRect {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
};

struct SpeechReminderSound {
  SpeechSoundKind kind = SpeechSoundKind::BuiltIn;
  int builtInId = 0;          // 0=bell 1=triple beep 2=gong 3=rising 4=buzzer 5=soft
  std::string filePath;       // used when kind == File
};

struct SpeechReminder {
  std::string id;
  SpeechReminderWhen when = SpeechReminderWhen::Remaining;
  int offsetSeconds = 120;
  SpeechReminderSound sound;
  bool enabled = true;
  std::string label;          // short text shown on the display, e.g. "2 minutes left"
};

struct SpeechTimerConfig {
  int durationSeconds = 600;  // 10 minutes

  std::vector<SpeechReminder> reminders;

  SpeechTimerDisplayTarget displayTarget = SpeechTimerDisplayTarget::Audience;

  // Overlay rectangle, normalised to the destination frame (0..1).
  // Used when the timer is composited onto a programme or presenter output.
  float overlayX = 0.62f;
  float overlayY = 0.04f;
  float overlayW = 0.36f;
  float overlayH = 0.18f;

  // Fallback desktop window (only when no matching output exists). Pixels.
  // -1 position means "choose a default".
  int windowX = -1;
  int windowY = -1;
  int windowW = 520;
  int windowH = 200;

  int fontSize = 0;           // 0 = auto from overlay/window height
  bool layoutLocked = false;
  bool alwaysOnTop = false;   // fallback window only; a composited overlay is already in the frame
  bool windowVisible = true;

  // Audio is intentionally separate from displayTarget. An audience visual
  // does NOT automatically put reminder sounds on the programme / stream /
  // recording bus. Default route is Control (desk / preview-listen path).
  SpeechReminderAudioRoute reminderAudioRoute = SpeechReminderAudioRoute::Control;
  std::string reminderAudioDeviceName;  // empty = desk/UI or preview device
  bool muteReminders = false;

  // Optional link to a native PowerPoint cue. Never ends or interrupts the
  // PowerPoint show when the clock hits zero.
  bool autoStartWithPowerPoint = false;
};

struct SpeechTimerSnapshot {
  SpeechTimerPhase phase = SpeechTimerPhase::Idle;
  int durationSeconds = 0;
  double elapsedSeconds = 0.0;     // running time only; pauses are excluded
  double remainingSeconds = 0.0;   // negative once overtime has started
  double overtimeSeconds = 0.0;    // 0 until the clock has crossed zero
  bool overtime = false;
  bool running = false;
  bool paused = false;
  std::string timeText;            // "mm:ss", "hh:mm:ss", or "+mm:ss" / "+hh:mm:ss"
  std::string stateText;           // "Ready", "Running", "Paused", "Overtime", ...
  std::string reminderText;        // current / most recent reminder label (may be empty)
  std::vector<std::string> firedReminderIds;
};

struct SpeechSessionRecord {
  std::string startedAtIso;
  std::string endedAtIso;
  int durationSeconds = 0;
  double usedSeconds = 0.0;
  double overtimeSeconds = 0.0;
  std::string kind;                // "finished" | "new_speech" (reset does not write a row)
};

// One poll of due reminders. Every crossed reminder is marked fired so it
// cannot sound again this speech; only `toPlay` should actually be played,
// so jumping over several points does not stack their sounds.
struct SpeechReminderFire {
  bool any = false;
  SpeechReminder toPlay;
  std::vector<std::string> markedFiredIds;
  std::vector<std::string> suppressedIds;  // due, but not played, to avoid stacking
};

struct SpeechTimer {
  SpeechTimer();

  static SpeechTimerConfig defaultConfig();
  static std::string formatTime(double seconds, bool overtimePrefix);
  static std::string phaseLabel(SpeechTimerPhase phase);
  static const char* builtInSoundName(int id);
  static int clampBuiltInId(int id);

  // Place `wanted` inside the usable bounds of the connected displays.
  // If the saved display is gone (no overlap), move to `displays[0]`
  // (or `fallback` when the list is empty) and shrink if needed.
  static IntRect clampRectToDisplays(IntRect wanted,
                                     const std::vector<IntRect>& displays,
                                     int minW,
                                     int minH,
                                     IntRect fallback = IntRect{});

  static void clampOverlayNorm(SpeechTimerConfig& cfg);

  const SpeechTimerConfig& config() const { return config_; }
  void setConfig(SpeechTimerConfig cfg);

  // Start a new running speech if idle, or resume if paused.
  // Does not itself write a session row (use beginNewSpeech() for that).
  void start(std::int64_t monotonicMs, std::string startedAtIso = {});
  void pause(std::int64_t monotonicMs);
  void resume(std::int64_t monotonicMs);
  // Return to the full duration, clear fired reminders, go Idle.
  // Does not record a session. Distinct from beginNewSpeech().
  void reset();
  // If the current run has elapsed time, append a session row, then start
  // a fresh speech from the full duration.
  void beginNewSpeech(std::int64_t monotonicMs, std::string startedAtIso = {},
                      std::string endedAtIso = {});

  // Advance using a monotonic clock only (never wall time). Safe to call
  // while paused or idle (no elapsed change).
  void tick(std::int64_t monotonicMs);

  // Apply tick, then report at most one reminder to play.
  SpeechReminderFire pollDueReminders(std::int64_t monotonicMs);

  SpeechTimerSnapshot snapshot() const;
  const std::vector<SpeechSessionRecord>& sessionRecords() const { return records_; }
  std::string exportSessionCsv() const;

  // Persistence of the CONFIG (duration, reminders, display, geometry).
  // Session records are kept in memory for this run and can be exported.
  std::string saveConfigText() const;
  bool loadConfigText(const std::string& text, std::string* error = nullptr);

private:
  void applyElapsedLocked();
  int reminderSeverity(const SpeechReminder& rem) const;
  bool reminderIsDue(const SpeechReminder& rem, double remaining, double overtime) const;
  SpeechTimerSnapshot makeSnapshot() const;

  SpeechTimerConfig config_;
  SpeechTimerPhase phase_ = SpeechTimerPhase::Idle;
  double elapsedSeconds_ = 0.0;
  std::int64_t lastTickMs_ = -1;
  std::vector<std::string> firedIds_;
  std::string lastReminderText_;
  std::string startedAtIso_;
  std::vector<SpeechSessionRecord> records_;
};

}  // namespace core
}  // namespace deckboy

#endif
