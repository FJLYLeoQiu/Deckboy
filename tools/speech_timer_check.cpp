// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// Unit checks for the speech timer: start/pause/resume/reset, monotonic
// ticks, zero-crossing into overtime, one-shot reminders, and the
// "do not stack sounds when several reminders are crossed in one poll"
// rule. Also config persist and display-rect clamping.
//
// No PowerPoint, no SDL, no audio device. This is the clock and policy.

#include "core/speech_timer.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

using deckboy::core::IntRect;
using deckboy::core::SpeechReminderWhen;
using deckboy::core::SpeechTimer;
using deckboy::core::SpeechTimerDisplayTarget;
using deckboy::core::SpeechReminderAudioRoute;
using deckboy::core::SpeechTimerPhase;

static int gFails = 0;

static void expect(bool ok, const char* what) {
  if (!ok) {
    std::cerr << "FAIL: " << what << "\n";
    ++gFails;
  }
}

static void expectNear(double got, double want, double eps, const char* what) {
  if (std::fabs(got - want) > eps) {
    std::cerr << "FAIL: " << what << " got " << got << " want " << want << "\n";
    ++gFails;
  }
}

int main() {
  // --- defaults: 10 minutes, audience display, three reminders ---
  {
    const auto cfg = SpeechTimer::defaultConfig();
    expect(cfg.durationSeconds == 600, "default duration is 10 minutes");
    expect(cfg.displayTarget == SpeechTimerDisplayTarget::Audience,
           "display target defaults to audience, not speaker");
    expect(cfg.windowVisible, "default windowVisible is true");
    expect(cfg.orientation == deckboy::core::SpeechTimerOrientation::Vertical,
           "default orientation is vertical");
    expect(cfg.reminderAudioRoute == SpeechReminderAudioRoute::Control,
           "reminder audio defaults to the control path, not the audience bus");
    expect(cfg.reminders.size() == 3, "three default reminders");
    expect(cfg.reminders[0].when == SpeechReminderWhen::Remaining &&
               cfg.reminders[0].offsetSeconds == 120,
           "first reminder is 2 minutes remaining");
    expect(cfg.reminders[1].when == SpeechReminderWhen::Remaining &&
               cfg.reminders[1].offsetSeconds == 60,
           "second reminder is 1 minute remaining");
    expect(cfg.reminders[2].when == SpeechReminderWhen::AtZero,
           "third reminder is time-up");
  }

  // --- format ---
  expect(SpeechTimer::formatTime(0, false) == "0:00", "format zero");
  expect(SpeechTimer::formatTime(75, false) == "1:15", "format mm:ss");
  expect(SpeechTimer::formatTime(3601, false) == "1:00:01", "format hh:mm:ss");
  expect(SpeechTimer::formatTime(12, true) == "+0:12", "format overtime mm:ss");
  expect(SpeechTimer::formatTime(3600, true) == "+1:00:00", "format overtime hh:mm:ss");

  // --- start, pause does not count, resume continues ---
  {
    SpeechTimer t;
    t.start(10000);
    t.tick(10000 + 5000);   // 5s
    expectNear(t.snapshot().elapsedSeconds, 5.0, 0.001, "5s elapsed");
    t.pause(10000 + 5000);
    t.tick(10000 + 25000);  // 20 more wall seconds, paused
    expectNear(t.snapshot().elapsedSeconds, 5.0, 0.001, "pause does not count");
    expect(t.snapshot().paused, "paused flag");
    t.resume(10000 + 25000);
    t.tick(10000 + 28000);  // +3s
    expectNear(t.snapshot().elapsedSeconds, 8.0, 0.001, "resume continues");
    expect(t.snapshot().running, "running after resume");
  }

  // --- reset vs new speech ---
  {
    SpeechTimer t;
    t.start(0, "t0");
    t.tick(30000);  // 30s
    t.reset();
    expect(t.snapshot().phase == SpeechTimerPhase::Idle, "reset goes idle");
    expectNear(t.snapshot().elapsedSeconds, 0.0, 0.001, "reset clears elapsed");
    expect(t.sessionRecords().empty(), "reset does not write a session row");
    t.start(40000, "t1");
    t.tick(40000 + 20000);
    t.beginNewSpeech(80000, "t2", "end-t1");
    expect(t.sessionRecords().size() == 1, "new speech records the previous run");
    expect(t.sessionRecords()[0].kind == "new_speech", "row kind is new_speech");
    expectNear(t.sessionRecords()[0].usedSeconds, 20.0, 0.001, "recorded used time");
    expect(t.snapshot().running, "new speech is running");
    expectNear(t.snapshot().elapsedSeconds, 0.0, 0.2, "new speech starts at zero");
  }

  // --- zero crossing and overtime; clock keeps counting ---
  {
    SpeechTimer t;
    auto cfg = SpeechTimer::defaultConfig();
    cfg.durationSeconds = 10;
    t.setConfig(cfg);
    t.start(0);
    t.tick(9000);
    expect(!t.snapshot().overtime, "not overtime yet");
    expect(t.snapshot().timeText == "0:01", "one second left");
    t.tick(15000);  // 5s overtime
    expect(t.snapshot().overtime, "crossed into overtime");
    expect(t.snapshot().phase == SpeechTimerPhase::Overtime, "phase overtime");
    expectNear(t.snapshot().overtimeSeconds, 5.0, 0.001, "overtime duration");
    expect(t.snapshot().timeText == "+0:05", "overtime text uses +mm:ss");
    t.pause(15000);
    t.tick(20000);
    expectNear(t.snapshot().overtimeSeconds, 5.0, 0.001, "pause does not count overtime");
    expect(t.snapshot().phase == SpeechTimerPhase::OvertimePaused, "overtime paused");
  }

  // --- each reminder fires once; a jump does not stack sounds ---
  {
    SpeechTimer t;
    auto cfg = SpeechTimer::defaultConfig();
    cfg.durationSeconds = 200;
    t.setConfig(cfg);
    t.start(0);
    // Jump from the start past the 2-minute and 1-minute marks, still before zero.
    auto fire = t.pollDueReminders(150000);  // 50s remaining
    expect(fire.any, "a reminder was due");
    expect(fire.toPlay.offsetSeconds == 60 &&
               fire.toPlay.when == SpeechReminderWhen::Remaining,
           "only the most urgent (1 minute) is played");
    expect(fire.markedFiredIds.size() == 2, "both remaining reminders marked fired");
    expect(fire.suppressedIds.size() == 1, "the earlier one was suppressed, not stacked");
    auto again = t.pollDueReminders(151000);
    expect(!again.any, "already-fired reminders do not repeat");
    // Now cross zero: only the time-up reminder should play.
    auto zero = t.pollDueReminders(250000);
    expect(zero.any && zero.toPlay.when == SpeechReminderWhen::AtZero,
           "time-up fires once at zero");
    auto zero2 = t.pollDueReminders(260000);
    expect(!zero2.any, "time-up does not repeat");
  }

  // --- wall-clock jumps must not be used; only the monotonic argument counts ---
  {
    SpeechTimer t;
    auto cfg = SpeechTimer::defaultConfig();
    cfg.durationSeconds = 100;
    t.setConfig(cfg);
    t.start(1'000'000);
    t.tick(1'000'000 + 2000);
    expectNear(t.snapshot().elapsedSeconds, 2.0, 0.001, "monotonic delta only");
  }

  // --- persist round trip, including audience default ---
  {
    SpeechTimer t;
    auto cfg = t.config();
    cfg.displayTarget = SpeechTimerDisplayTarget::Speaker;
    cfg.durationSeconds = 900;
    cfg.overlayX = 0.1f;
    cfg.muteReminders = true;
    cfg.reminderAudioDeviceName = "Speakers";
    cfg.windowVisible = false;
    cfg.orientation = deckboy::core::SpeechTimerOrientation::Horizontal;
    t.setConfig(cfg);
    const std::string text = t.saveConfigText();
    SpeechTimer u;
    std::string err;
    expect(u.loadConfigText(text, &err), "loadConfigText succeeds");
    expect(err.empty(), "no load error");
    expect(u.config().displayTarget == SpeechTimerDisplayTarget::Speaker, "target persisted");
    expect(u.config().durationSeconds == 900, "duration persisted");
    expect(u.config().muteReminders, "mute persisted");
    expect(u.config().reminderAudioDeviceName == "Speakers", "audio device persisted");
    expect(!u.config().windowVisible, "windowVisible persisted");
    expect(u.config().orientation == deckboy::core::SpeechTimerOrientation::Horizontal,
           "orientation persisted");
    expect(u.config().reminders.size() == 3, "reminders persisted");
  }

  // --- empty / default load keeps audience ---
  {
    SpeechTimer t;
    expect(t.loadConfigText("# just comments\n"), "empty config loads defaults");
    expect(t.config().displayTarget == SpeechTimerDisplayTarget::Audience,
           "loaded default target is still audience");
  }

  // --- clamp window to displays: missing display is not lost off-screen ---
  {
    const std::vector<IntRect> one = {{0, 0, 1920, 1080}};
    const IntRect saved{2000, 100, 400, 200};  // was on a second monitor
    const IntRect got = SpeechTimer::clampRectToDisplays(saved, one, 280, 120);
    expect(got.x >= 0 && got.y >= 0, "clamped origin on the remaining display");
    expect(got.x + got.w <= 1920 && got.y + got.h <= 1080, "fully inside primary");
    const std::vector<IntRect> two = {{0, 0, 1920, 1080}, {1920, 0, 1280, 1024}};
    const IntRect keep = SpeechTimer::clampRectToDisplays({2000, 10, 400, 200}, two, 280, 120);
    expect(keep.x >= 1920, "kept on the second display when it still exists");
    const IntRect tiny = SpeechTimer::clampRectToDisplays({0, 0, 8000, 8000}, one, 280, 120);
    expect(tiny.w <= 1920 && tiny.h <= 1080, "oversized window shrinks to the display");
    const IntRect noDisp = SpeechTimer::clampRectToDisplays({10, 10, 400, 200}, {}, 280, 120);
    expect(noDisp.w == 400 && noDisp.h == 200, "no displays: keep size at origin");
  }

  // --- CSV export ---
  {
    SpeechTimer t;
    auto cfg = SpeechTimer::defaultConfig();
    cfg.durationSeconds = 5;
    t.setConfig(cfg);
    t.start(0, "start");
    t.tick(8000);
    t.beginNewSpeech(8000, "start2", "end1");
    const std::string csv = t.exportSessionCsv();
    expect(csv.find("started_at,ended_at,duration_s,used_s,overtime_s,kind") == 0,
           "CSV header");
    expect(csv.find("new_speech") != std::string::npos, "CSV has the row");
    expect(csv.find("8") != std::string::npos, "CSV includes used time");
  }

  if (gFails) {
    std::cerr << gFails << " check(s) failed\n";
    return 1;
  }
  std::cout << "speech_timer_check: ok\n";
  return 0;
}
