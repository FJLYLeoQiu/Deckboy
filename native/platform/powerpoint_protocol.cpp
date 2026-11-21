// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#include "powerpoint_protocol.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace deckboy {
namespace platform {
namespace {

std::string jsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (unsigned char c : s) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"':  out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += static_cast<char>(c);
        }
        break;
    }
  }
  return out;
}

bool skipWs(const std::string& s, std::size_t& i) {
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
  return i < s.size();
}

bool parseString(const std::string& s, std::size_t& i, std::string& out) {
  if (!skipWs(s, i) || s[i] != '"') return false;
  ++i;
  out.clear();
  while (i < s.size()) {
    char c = s[i++];
    if (c == '"') return true;
    if (c != '\\') { out += c; continue; }
    if (i >= s.size()) return false;
    char e = s[i++];
    switch (e) {
      case '\\': out += '\\'; break;
      case '"':  out += '"'; break;
      case 'n':  out += '\n'; break;
      case 'r':  out += '\r'; break;
      case 't':  out += '\t'; break;
      case 'u': {
        if (i + 4 > s.size()) return false;
        int code = 0;
        for (int n = 0; n < 4; ++n) {
          char h = s[i++];
          code <<= 4;
          if (h >= '0' && h <= '9') code += h - '0';
          else if (h >= 'a' && h <= 'f') code += 10 + h - 'a';
          else if (h >= 'A' && h <= 'F') code += 10 + h - 'A';
          else return false;
        }
        out += static_cast<char>(code < 0x80 ? code : '?');
        break;
      }
      default: out += e; break;
    }
  }
  return false;
}

bool parseBare(const std::string& s, std::size_t& i, std::string& tok) {
  if (!skipWs(s, i)) return false;
  const std::size_t start = i;
  while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) ||
                          s[i] == '_' || s[i] == '+' || s[i] == '-' || s[i] == '.')) {
    ++i;
  }
  if (i == start) return false;
  tok = s.substr(start, i - start);
  return true;
}

bool parseInt(const std::string& tok, long& value) {
  if (tok.empty()) return false;
  char* end = nullptr;
  value = std::strtol(tok.c_str(), &end, 10);
  return end && *end == '\0';
}

// Very small object field walker: finds "key": value in a single-level object.
bool findField(const std::string& s, const char* key, std::size_t& valueAt) {
  const std::string pat = std::string("\"") + key + "\"";
  std::size_t i = 0;
  while (true) {
    const auto at = s.find(pat, i);
    if (at == std::string::npos) return false;
    std::size_t j = at + pat.size();
    if (!skipWs(s, j) || s[j] != ':') { i = at + 1; continue; }
    ++j;
    if (!skipWs(s, j)) return false;
    valueAt = j;
    return true;
  }
}

bool fieldString(const std::string& s, const char* key, std::string& out) {
  std::size_t i = 0;
  if (!findField(s, key, i)) return false;
  return parseString(s, i, out);
}

bool fieldBool(const std::string& s, const char* key, bool& out) {
  std::size_t i = 0;
  if (!findField(s, key, i)) return false;
  std::string tok;
  if (!parseBare(s, i, tok)) return false;
  if (tok == "true") { out = true; return true; }
  if (tok == "false") { out = false; return true; }
  return false;
}

bool fieldLong(const std::string& s, const char* key, long& out) {
  std::size_t i = 0;
  if (!findField(s, key, i)) return false;
  std::string tok;
  if (!parseBare(s, i, tok)) return false;
  return parseInt(tok, out);
}

std::string lowerExt(const std::string& path) {
  auto slash = path.find_last_of("/\\");
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  auto dot = name.find_last_of('.');
  if (dot == std::string::npos) return {};
  std::string ext = name.substr(dot);
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext;
}

}  // namespace

std::string pptCommandTypeName(PptCommandType type) {
  switch (type) {
    case PptCommandType::Ping:   return "ping";
    case PptCommandType::Probe:  return "probe";
    case PptCommandType::Open:   return "open";
    case PptCommandType::Next:   return "next";
    case PptCommandType::Prev:   return "prev";
    case PptCommandType::Goto:   return "goto";
    case PptCommandType::End:    return "end";
    case PptCommandType::Status: return "status";
    case PptCommandType::Close:  return "close";
    case PptCommandType::Quit:   return "quit";
    case PptCommandType::Unknown:
    default:                     return "unknown";
  }
}

PptCommandType pptCommandTypeFromName(const std::string& name) {
  if (name == "ping") return PptCommandType::Ping;
  if (name == "probe") return PptCommandType::Probe;
  if (name == "open") return PptCommandType::Open;
  if (name == "next") return PptCommandType::Next;
  if (name == "prev") return PptCommandType::Prev;
  if (name == "goto") return PptCommandType::Goto;
  if (name == "end") return PptCommandType::End;
  if (name == "status") return PptCommandType::Status;
  if (name == "close") return PptCommandType::Close;
  if (name == "quit") return PptCommandType::Quit;
  return PptCommandType::Unknown;
}

std::string encodePptCommand(const PptCommand& cmd) {
  std::ostringstream out;
  out << "{\"cmd\":\"" << pptCommandTypeName(cmd.type) << "\"";
  if (cmd.type == PptCommandType::Open) {
    out << ",\"path\":\"" << jsonEscape(cmd.open.path) << "\"";
    out << ",\"startSlide\":" << cmd.open.startSlide;
    out << ",\"windowed\":" << (cmd.open.windowed ? "true" : "false");
    out << ",\"left\":" << cmd.open.left;
    out << ",\"top\":" << cmd.open.top;
    out << ",\"width\":" << cmd.open.width;
    out << ",\"height\":" << cmd.open.height;
  }
  if (cmd.type == PptCommandType::Goto) {
    out << ",\"slide\":" << cmd.slide;
  }
  out << "}";
  return out.str();
}

bool decodePptCommand(const std::string& line, PptCommand& out, std::string* error) {
  out = PptCommand{};
  std::string cmd;
  if (!fieldString(line, "cmd", cmd)) {
    if (error) *error = "missing cmd";
    return false;
  }
  out.type = pptCommandTypeFromName(cmd);
  if (out.type == PptCommandType::Unknown) {
    if (error) *error = "unknown cmd";
    return false;
  }
  if (out.type == PptCommandType::Open) {
    fieldString(line, "path", out.open.path);
    long v = 0;
    if (fieldLong(line, "startSlide", v)) out.open.startSlide = static_cast<int>(v);
    bool b = true;
    if (fieldBool(line, "windowed", b)) out.open.windowed = b;
    if (fieldLong(line, "left", v)) out.open.left = static_cast<int>(v);
    if (fieldLong(line, "top", v)) out.open.top = static_cast<int>(v);
    if (fieldLong(line, "width", v)) out.open.width = static_cast<int>(v);
    if (fieldLong(line, "height", v)) out.open.height = static_cast<int>(v);
  }
  if (out.type == PptCommandType::Goto) {
    long v = 0;
    if (fieldLong(line, "slide", v)) out.slide = static_cast<int>(v);
  }
  return true;
}

std::string encodePptStatus(const PptStatus& st) {
  std::ostringstream out;
  out << "{\"ok\":" << (st.ok ? "true" : "false");
  if (!st.error.empty()) out << ",\"error\":\"" << jsonEscape(st.error) << "\"";
  if (!st.code.empty()) out << ",\"code\":\"" << jsonEscape(st.code) << "\"";
  out << ",\"available\":" << (st.available ? "true" : "false");
  out << ",\"running\":" << (st.running ? "true" : "false");
  out << ",\"ended\":" << (st.ended ? "true" : "false");
  out << ",\"slide\":" << st.slide;
  out << ",\"slides\":" << st.slides;
  out << ",\"state\":\"" << jsonEscape(st.state) << "\"";
  out << ",\"hwnd\":" << st.hwnd;
  out << ",\"attachedToExisting\":" << (st.attachedToExisting ? "true" : "false");
  out << "}";
  return out.str();
}

bool decodePptStatus(const std::string& line, PptStatus& out, std::string* error) {
  out = PptStatus{};
  if (!fieldBool(line, "ok", out.ok) && line.find("\"ok\"") == std::string::npos) {
    if (error) *error = "missing ok";
    return false;
  }
  fieldString(line, "error", out.error);
  fieldString(line, "code", out.code);
  fieldBool(line, "available", out.available);
  fieldBool(line, "running", out.running);
  fieldBool(line, "ended", out.ended);
  fieldString(line, "state", out.state);
  long v = 0;
  if (fieldLong(line, "slide", v)) out.slide = static_cast<int>(v);
  if (fieldLong(line, "slides", v)) out.slides = static_cast<int>(v);
  if (fieldLong(line, "hwnd", v)) out.hwnd = static_cast<std::uint64_t>(v < 0 ? 0 : v);
  fieldBool(line, "attachedToExisting", out.attachedToExisting);
  return true;
}

bool isPptFilename(const std::string& path) {
  const std::string ext = lowerExt(path);
  return ext == ".ppt" || ext == ".pptx" || ext == ".pps" || ext == ".ppsx" ||
         ext == ".pptm" || ext == ".ppsm" || ext == ".pot" || ext == ".potx";
}

bool isPptMacroFilename(const std::string& path) {
  const std::string ext = lowerExt(path);
  return ext == ".pptm" || ext == ".ppsm" || ext == ".potm";
}

}  // namespace platform
}  // namespace deckboy
