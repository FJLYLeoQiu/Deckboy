// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#include "powerpoint_play.hpp"

#include "core/paths.hpp"
#include "core/subprocess.hpp"

#include <chrono>
#include <thread>
#include <filesystem>

#ifdef _WIN32
#  include <windows.h>
#  include <io.h>
#  include <fcntl.h>
#endif

namespace deckboy {
namespace platform {
namespace {

using deckboy::core::Paths;

#ifdef _WIN32
std::string helperPath() {
  const auto exe = Paths::executablePath();
  if (exe.empty()) return {};
  auto dir = exe.parent_path();
  auto candidate = dir / "deckboy-ppt-helper.exe";
  std::error_code ec;
  if (std::filesystem::exists(candidate, ec) && !ec) return candidate.string();
  candidate = dir / "deckboy-ppt-helper";
  if (std::filesystem::exists(candidate, ec) && !ec) return candidate.string();
  return {};
}

bool writeLine(int fd, const std::string& line) {
  if (fd < 0) return false;
  std::string payload = line;
  payload += '\n';
  const char* p = payload.data();
  int left = static_cast<int>(payload.size());
  while (left > 0) {
    const int n = _write(fd, p, left);
    if (n <= 0) return false;
    p += n;
    left -= n;
  }
  return true;
}

bool readLineTimeout(int fd, std::string& line, int timeoutMs) {
  line.clear();
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeoutMs);
  char ch;
  while (std::chrono::steady_clock::now() < deadline) {
    // Peek with a short wait via PeekNamedPipe is more work; a small sleep
    // loop keeps this client simple. The helper answers quickly.
#ifdef _WIN32
    HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
    DWORD avail = 0;
    if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) return false;
    if (avail == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(15));
      continue;
    }
#endif
    const int n = _read(fd, &ch, 1);
    if (n <= 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(15));
      continue;
    }
    if (ch == '\n') return true;
    if (ch != '\r') line += ch;
  }
  return false;
}
#endif

PptStatus makeError(const std::string& code, const std::string& error) {
  PptStatus st;
  st.ok = false;
  st.code = code;
  st.error = error;
  return st;
}

}  // namespace

struct PowerPointPlayer::Helper {
#ifdef _WIN32
  ChildProcess process;
#endif
};

PowerPointPlayer::~PowerPointPlayer() { shutdown(); }

bool PowerPointPlayer::platformSupported() {
#ifdef _WIN32
  return true;
#else
  return false;
#endif
}

std::string PowerPointPlayer::unsupportedMessageEn() {
  return "Native PowerPoint playback is only available on Windows with a "
         "desktop Microsoft PowerPoint installation. On this platform, import "
         "the deck as stills (which flattens animations) or open it in "
         "PowerPoint yourself.";
}

std::string PowerPointPlayer::unsupportedMessageZh() {
  return "PowerPoint 原生放映仅在已安装桌面版 Microsoft PowerPoint 的 Windows 上可用。"
         "当前系统不支持该路径。可将幻灯片导入为静帧（动画会被展平），或自行在 "
         "PowerPoint 中打开。";
}

std::string PowerPointPlayer::missingOfficeMessageEn() {
  return "Microsoft PowerPoint is not installed (desktop app). Native playback "
         "cannot run. WPS, PowerPoint for the web, and LibreOffice are not "
         "used for this path. You can still import the deck as stills.";
}

std::string PowerPointPlayer::missingOfficeMessageZh() {
  return "未安装桌面版 Microsoft PowerPoint，无法进行原生放映。"
         "此路径不会使用 WPS、PowerPoint 网页版或 LibreOffice。"
         "仍可将幻灯片导入为静帧。";
}

std::string PowerPointPlayer::missingHelperMessageEn() {
  return "deckboy-ppt-helper.exe was not found next to Deckboy.exe. "
         "Native PowerPoint playback needs this helper from the full Windows "
         "package (portable zip or installer), in the same folder as Deckboy.exe. "
         "Deckboy Mini does not include it. Macros stay disabled; Protected View "
         "is not bypassed.";
}

std::string PowerPointPlayer::missingHelperMessageZh() {
  return "未在 Deckboy.exe 同目录找到 deckboy-ppt-helper.exe。"
         "原生 PowerPoint 放映需要完整 Windows 安装包或便携包里、与 Deckboy.exe "
         "放在同一文件夹的该助手。Deckboy Mini 不含此文件。"
         "宏保持禁用，也不会绕过受保护视图。";
}

#ifdef _WIN32
bool PowerPointPlayer::ensureHelper(std::string& error, std::string& code) {
  if (helper_ && helper_->process.running()) return true;
  shutdown();
  const std::string path = helperPath();
  if (path.empty()) {
    error = missingHelperMessageEn();
    code = "no_helper";
    return false;
  }
  helper_ = new Helper();
  SpawnOptions opts;
  opts.stdinMode = StdioMode::Pipe;
  opts.stdoutMode = StdioMode::Pipe;
  opts.stderrMode = StdioMode::Merge;
  opts.showWindow = false;
  if (!spawnProcess(helper_->process, {path}, opts) || !helper_->process.running()) {
    error = "could not start deckboy-ppt-helper.exe (it is next to Deckboy, but it did not run)";
    code = "helper_start";
    delete helper_;
    helper_ = nullptr;
    return false;
  }
  return true;
}

PptStatus PowerPointPlayer::send(const PptCommand& cmd) {
  std::string error, code;
  if (!ensureHelper(error, code)) return makeError(code, error);
  const std::string line = encodePptCommand(cmd);
  if (!writeLine(helper_->process.writeFd, line)) {
    return makeError("helper_pipe", "could not write to the PowerPoint helper");
  }
  std::string reply;
  const int timeout = cmd.type == PptCommandType::Open ? 30000 : 5000;
  if (!readLineTimeout(helper_->process.readFd, reply, timeout)) {
    return makeError("helper_timeout", "the PowerPoint helper did not answer");
  }
  PptStatus st;
  std::string err;
  if (!decodePptStatus(reply, st, &err)) {
    return makeError("helper_protocol", "bad helper reply: " + err);
  }
  return st;
}
#else
bool PowerPointPlayer::ensureHelper(std::string&, std::string&) { return false; }

PptStatus PowerPointPlayer::send(const PptCommand&) {
  return makeError("unsupported_platform", unsupportedMessageEn());
}
#endif

PptStatus PowerPointPlayer::probe() { return send(PptCommand{PptCommandType::Probe, {}, 0}); }

PptStatus PowerPointPlayer::openShow(const PptOpenOptions& opts) {
  PptCommand cmd;
  cmd.type = PptCommandType::Open;
  cmd.open = opts;
  return send(cmd);
}

PptStatus PowerPointPlayer::nextBuild() { return send(PptCommand{PptCommandType::Next, {}, 0}); }
PptStatus PowerPointPlayer::prevBuild() { return send(PptCommand{PptCommandType::Prev, {}, 0}); }

PptStatus PowerPointPlayer::gotoSlide(int slideOneBased) {
  PptCommand cmd;
  cmd.type = PptCommandType::Goto;
  cmd.slide = slideOneBased;
  return send(cmd);
}

PptStatus PowerPointPlayer::endShow() { return send(PptCommand{PptCommandType::End, {}, 0}); }
PptStatus PowerPointPlayer::status() { return send(PptCommand{PptCommandType::Status, {}, 0}); }
PptStatus PowerPointPlayer::closeShow() { return send(PptCommand{PptCommandType::Close, {}, 0}); }

void PowerPointPlayer::shutdown() {
  if (!helper_) return;
#ifdef _WIN32
  if (helper_->process.running()) {
    PptCommand q;
    q.type = PptCommandType::Quit;
    writeLine(helper_->process.writeFd, encodePptCommand(q));
    helper_->process.finish(2000);
  }
  helper_->process.stop();
#endif
  delete helper_;
  helper_ = nullptr;
}

}  // namespace platform
}  // namespace deckboy
