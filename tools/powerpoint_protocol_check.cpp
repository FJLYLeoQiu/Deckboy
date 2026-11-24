// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// Protocol encode/decode checks. No PowerPoint, no COM, no Windows.

#include "platform/powerpoint_protocol.hpp"

#include <iostream>

using namespace deckboy::platform;

static int gFails = 0;
static void expect(bool ok, const char* what) {
  if (!ok) { std::cerr << "FAIL: " << what << "\n"; ++gFails; }
}

int main() {
  expect(isPptFilename("Talk.pptx"), "pptx is a deck");
  expect(isPptFilename("C:\\\\Show\\\\file.PPT"), "ppt is a deck, case-insensitive");
  expect(isPptFilename("deck.ppsx"), "ppsx is a deck");
  expect(!isPptFilename("notes.pdf"), "pdf is not a live PowerPoint deck");
  expect(isPptMacroFilename("talk.pptm"), "pptm is a macro format");
  expect(!isPptMacroFilename("talk.pptx"), "pptx is not a macro format");

  PptCommand open;
  open.type = PptCommandType::Open;
  open.open.path = "C:\\Talks\\Q&A \"final\".pptx";
  open.open.startSlide = 3;
  open.open.windowed = true;
  open.open.left = 100;
  open.open.top = 40;
  open.open.width = 1920;
  open.open.height = 1080;
  const std::string line = encodePptCommand(open);
  PptCommand back;
  std::string err;
  expect(decodePptCommand(line, back, &err), "decode open");
  expect(err.empty(), "no decode error");
  expect(back.type == PptCommandType::Open, "cmd is open");
  expect(back.open.path == open.open.path, "path round-trips, including quotes");
  expect(back.open.startSlide == 3 && back.open.width == 1920, "open numeric fields");
  expect(back.open.windowed, "windowed true");

  PptCommand openUnicode;
  openUnicode.type = PptCommandType::Open;
  openUnicode.open.path = "D:\\资料\\重大项目.pptx";
  const std::string lineU = encodePptCommand(openUnicode);
  PptCommand backU;
  expect(decodePptCommand(lineU, backU, &err), "decode unicode open");
  expect(backU.open.path == openUnicode.open.path, "unicode path round-trips");

  PptCommand backEscaped;
  expect(decodePptCommand("{\"cmd\":\"open\",\"path\":\"\\u6f14\\u793a.pptx\"}", backEscaped, &err), "decode \\u escaped unicode");
  expect(!backEscaped.open.path.empty() && backEscaped.open.path != "??.pptx", "\\u escapes decoded as utf8 rather than ?");

  PptCommand go;
  go.type = PptCommandType::Goto;
  go.slide = 12;
  PptCommand go2;
  expect(decodePptCommand(encodePptCommand(go), go2, &err) && go2.slide == 12, "goto");

  for (auto t : {PptCommandType::Ping, PptCommandType::Probe, PptCommandType::Next,
                 PptCommandType::Prev, PptCommandType::End, PptCommandType::Status,
                 PptCommandType::Close, PptCommandType::Quit}) {
    PptCommand c; c.type = t;
    PptCommand d;
    expect(decodePptCommand(encodePptCommand(c), d, &err) && d.type == t,
           "simple command round-trip");
  }

  PptStatus st;
  st.ok = true;
  st.available = true;
  st.running = true;
  st.slide = 4;
  st.slides = 20;
  st.state = "running";
  st.hwnd = 42;
  st.attachedToExisting = true;
  PptStatus st2;
  expect(decodePptStatus(encodePptStatus(st), st2, &err), "status decode");
  expect(st2.ok && st2.available && st2.running, "status flags");
  expect(st2.slide == 4 && st2.slides == 20 && st2.hwnd == 42, "status numbers");
  expect(st2.attachedToExisting && st2.state == "running", "status strings");

  PptStatus bad;
  bad.ok = false;
  bad.code = "no_powerpoint";
  bad.error = "Microsoft PowerPoint is not installed";
  PptStatus bad2;
  expect(decodePptStatus(encodePptStatus(bad), bad2, &err), "error status");
  expect(!bad2.ok && bad2.code == "no_powerpoint", "error code");

  PptCommand unk;
  expect(!decodePptCommand("{\"cmd\":\"explode\"}", unk, &err), "unknown cmd rejected");

  if (gFails) {
    std::cerr << gFails << " check(s) failed\n";
    return 1;
  }
  std::cout << "powerpoint_protocol_check: ok\n";
  return 0;
}
