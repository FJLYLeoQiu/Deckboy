// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// deckboy-ppt-helper — Dedicated STA process for PowerPoint COM automation.
//
// Late binding only (IDispatch). The Windows CI runner has no Office typelibs,
// and a typelib import would fail the build there.
//
// Security:
//   - Application.AutomationSecurity = msoAutomationSecurityForceDisable (3)
//     before any Open, so document macros do not run.
//   - Protected View is not enabled, not disabled, and not bypassed. If the
//     file opens in Protected View we report that and leave it alone.
//   - Automatic link updates are not turned on.
//   - Only the presentation this process opened is closed. The helper never
//     kills POWERPNT.EXE. Application.Quit is used only if PowerPoint was
//     not already running and no other presentations remain.
//
// --self-test exercises the protocol with a mock, without COM / Office.
// ============================================================================

#ifdef _WIN32

#include "platform/powerpoint_protocol.hpp"

#include <ole2.h>
#include <windows.h>

#include <iostream>
#include <string>

using deckboy::platform::PptCommand;
using deckboy::platform::PptCommandType;
using deckboy::platform::PptStatus;
using deckboy::platform::decodePptCommand;
using deckboy::platform::encodePptCommand;
using deckboy::platform::encodePptStatus;
using deckboy::platform::decodePptStatus;

// Office constants we need, without a typelib.
static const long kMsoTrue = -1;
static const long kMsoFalse = 0;
static const long kMsoAutomationSecurityForceDisable = 3;  // msoAutomationSecurityForceDisable
static const long kPpShowTypeWindow = 2;                   // ppShowTypeWindow
static const long kPpShowAll = 1;                          // ppShowAll

static void writeStatus(const PptStatus& st) {
  std::cout << encodePptStatus(st) << std::endl;
  std::cout.flush();
}

static PptStatus err(const std::string& code, const std::string& error) {
  PptStatus st;
  st.ok = false;
  st.code = code;
  st.error = error;
  return st;
}

static PptStatus mockSelfTest() {
  // Round-trip the protocol and a tiny state machine. No COM.
  PptCommand ping;
  ping.type = PptCommandType::Ping;
  PptCommand back;
  std::string e;
  if (!decodePptCommand(encodePptCommand(ping), back, &e) ||
      back.type != PptCommandType::Ping) {
    return err("self_test", "ping round-trip failed");
  }
  PptStatus st;
  st.ok = true;
  st.available = false;
  st.state = "self-test";
  st.code = "self_test";
  return st;
}

#ifdef _WIN32
// --- minimal IDispatch helpers ---

static HRESULT invokeNamed(IDispatch* obj, const wchar_t* name, WORD flags,
                           DISPPARAMS* params, VARIANT* result) {
  if (!obj) return E_POINTER;
  DISPID id = 0;
  OLECHAR* n = const_cast<OLECHAR*>(name);
  HRESULT hr = obj->GetIDsOfNames(IID_NULL, &n, 1, LOCALE_USER_DEFAULT, &id);
  if (FAILED(hr)) return hr;
  EXCEPINFO ex {};
  UINT argErr = 0;
  return obj->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, flags, params, result, &ex, &argErr);
}

static HRESULT putLong(IDispatch* obj, const wchar_t* name, long value) {
  VARIANT arg;
  VariantInit(&arg);
  arg.vt = VT_I4;
  arg.lVal = value;
  DISPID named = DISPID_PROPERTYPUT;
  DISPPARAMS dp {&arg, &named, 1, 1};
  return invokeNamed(obj, name, DISPATCH_PROPERTYPUT, &dp, nullptr);
}

static HRESULT getDispatch(IDispatch* obj, const wchar_t* name, IDispatch** out) {
  VARIANT result;
  VariantInit(&result);
  DISPPARAMS dp {};
  HRESULT hr = invokeNamed(obj, name, DISPATCH_PROPERTYGET, &dp, &result);
  if (SUCCEEDED(hr) && (result.vt == VT_DISPATCH) && result.pdispVal) {
    *out = result.pdispVal;
    (*out)->AddRef();
  } else {
    hr = FAILED(hr) ? hr : E_FAIL;
  }
  VariantClear(&result);
  return hr;
}

static HRESULT getLong(IDispatch* obj, const wchar_t* name, long* out) {
  VARIANT result;
  VariantInit(&result);
  DISPPARAMS dp {};
  HRESULT hr = invokeNamed(obj, name, DISPATCH_PROPERTYGET, &dp, &result);
  if (SUCCEEDED(hr)) {
    VARIANT n;
    VariantInit(&n);
    hr = VariantChangeType(&n, &result, 0, VT_I4);
    if (SUCCEEDED(hr)) *out = n.lVal;
    VariantClear(&n);
  }
  VariantClear(&result);
  return hr;
}

static HRESULT call0(IDispatch* obj, const wchar_t* name) {
  DISPPARAMS dp {};
  return invokeNamed(obj, name, DISPATCH_METHOD, &dp, nullptr);
}

static std::wstring utf8ToWide(const std::string& s) {
  if (s.empty()) return L"";
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  std::wstring w(n > 0 ? static_cast<std::size_t>(n - 1) : 0, L'\0');
  if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
  return w;
}

struct PptSession {
  IDispatch* app = nullptr;
  IDispatch* pres = nullptr;
  IDispatch* show = nullptr;
  bool attachedToExisting = false;
  std::string openedPath;
  int slides = 0;

  void releaseShow() {
    if (show) { show->Release(); show = nullptr; }
  }
  void releasePres() {
    releaseShow();
    if (pres) { pres->Release(); pres = nullptr; }
    openedPath.clear();
    slides = 0;
  }
  void releaseAll() {
    releasePres();
    if (app) { app->Release(); app = nullptr; }
    attachedToExisting = false;
  }
};

static bool powerpointInstalled() {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\POWERPNT.EXE",
                    0, KEY_READ, &key) == ERROR_SUCCESS) {
    RegCloseKey(key);
    return true;
  }
  CLSID clsid {};
  return SUCCEEDED(CLSIDFromProgID(L"PowerPoint.Application", &clsid));
}

static HRESULT openApplication(PptSession& ses, PptStatus& st) {
  CLSID clsid {};
  HRESULT hr = CLSIDFromProgID(L"PowerPoint.Application", &clsid);
  if (FAILED(hr)) {
    st = err("no_powerpoint", "Microsoft PowerPoint is not installed");
    return hr;
  }
  IUnknown* existing = nullptr;
  ses.attachedToExisting = SUCCEEDED(GetActiveObject(clsid, nullptr, &existing));
  if (ses.attachedToExisting && existing) {
    hr = existing->QueryInterface(IID_IDispatch, reinterpret_cast<void**>(&ses.app));
    existing->Release();
    if (FAILED(hr)) ses.attachedToExisting = false;
  }
  if (!ses.app) {
    hr = CoCreateInstance(clsid, nullptr, CLSCTX_LOCAL_SERVER, IID_IDispatch,
                          reinterpret_cast<void**>(&ses.app));
    if (FAILED(hr) || !ses.app) {
      st = err("no_powerpoint", "could not start Microsoft PowerPoint");
      return hr;
    }
  }
  // Force-disable macros. This is a tighter policy, not a bypass.
  putLong(ses.app, L"AutomationSecurity", kMsoAutomationSecurityForceDisable);
  return S_OK;
}

static PptStatus doProbe() {
  PptStatus st;
  st.ok = true;
  st.available = powerpointInstalled();
  st.state = st.available ? "idle" : "unavailable";
  if (!st.available) {
    st.ok = false;
    st.code = "no_powerpoint";
    st.error = "Microsoft PowerPoint is not installed";
  }
  return st;
}

static PptStatus doOpen(PptSession& ses, const PptCommand& cmd) {
  ses.releasePres();
  PptStatus st;
  if (!powerpointInstalled()) return err("no_powerpoint", "Microsoft PowerPoint is not installed");
  HRESULT hr = openApplication(ses, st);
  if (FAILED(hr) || !ses.app) return st.ok ? err("no_powerpoint", "could not start PowerPoint") : st;

  IDispatch* presentations = nullptr;
  hr = getDispatch(ses.app, L"Presentations", &presentations);
  if (FAILED(hr) || !presentations) {
    return err("com", "Presentations collection missing");
  }

  // Presentations.Open(FileName, ReadOnly, Untitled, WithWindow)
  const std::wstring wpath = utf8ToWide(cmd.open.path);
  VARIANT args[4];
  for (auto& a : args) VariantInit(&a);
  args[3].vt = VT_BSTR; args[3].bstrVal = SysAllocString(wpath.c_str()); // FileName (reversed)
  args[2].vt = VT_I4;   args[2].lVal = kMsoTrue;   // ReadOnly
  args[1].vt = VT_I4;   args[1].lVal = kMsoFalse;  // Untitled
  args[0].vt = VT_I4;   args[0].lVal = kMsoTrue;   // WithWindow — needed for a slideshow
  DISPPARAMS dp {args, nullptr, 4, 0};
  VARIANT result;
  VariantInit(&result);
  hr = invokeNamed(presentations, L"Open", DISPATCH_METHOD, &dp, &result);
  presentations->Release();
  VariantClear(&args[3]);
  if (FAILED(hr) || result.vt != VT_DISPATCH || !result.pdispVal) {
    VariantClear(&result);
    return err("open_failed", "PowerPoint could not open the file (it may be in Protected View, or a password may be required)");
  }
  ses.pres = result.pdispVal;
  ses.pres->AddRef();
  VariantClear(&result);
  ses.openedPath = cmd.open.path;

  // If Protected View caught it, do not call Edit/Enable. Report and close.
  // Note: Only report protected_view if an item in ProtectedViewWindows actually corresponds
  // to THIS presentation. WPS Office and other third-party suites or preexisting background
  // sessions can report a non-zero count of dummy/unrelated windows (e.g. SourceName='default').
  IDispatch* pvWindows = nullptr;
  if (SUCCEEDED(getDispatch(ses.app, L"ProtectedViewWindows", &pvWindows)) && pvWindows) {
    long count = 0;
    getLong(pvWindows, L"Count", &count);
    bool matchFound = false;
    std::wstring targetName = utf8ToWide(cmd.open.path);
    auto pos = targetName.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
      targetName = targetName.substr(pos + 1);
    }
    for (long i = 1; i <= count; ++i) {
      VARIANT idx;
      VariantInit(&idx);
      idx.vt = VT_I4;
      idx.lVal = i;
      DISPPARAMS dp {&idx, nullptr, 1, 0};
      VARIANT item;
      VariantInit(&item);
      if (SUCCEEDED(invokeNamed(pvWindows, L"Item", DISPATCH_METHOD, &dp, &item)) &&
          item.vt == VT_DISPATCH && item.pdispVal) {
        IDispatch* pvItem = item.pdispVal;
        VARIANT nameVal;
        VariantInit(&nameVal);
        DISPPARAMS noParams {};
        if (SUCCEEDED(invokeNamed(pvItem, L"SourceName", DISPATCH_PROPERTYGET, &noParams, &nameVal)) &&
            nameVal.vt == VT_BSTR && nameVal.bstrVal) {
          if (_wcsicmp(nameVal.bstrVal, targetName.c_str()) == 0) {
            matchFound = true;
          }
        }
        VariantClear(&nameVal);
        pvItem->Release();
      }
      VariantClear(&item);
      if (matchFound) break;
    }
    pvWindows->Release();
    if (matchFound) {
      // Opening a blocked file may have added a Protected View window and
      // not a real Presentation. Leave Protected View untouched.
      ses.releasePres();
      return err("protected_view",
                 "The file opened in Microsoft Office Protected View. "
                 "Deckboy will not bypass Protected View. Unblock the file "
                 "in Windows or Office and try again.");
    }
  }

  long slideCount = 0;
  IDispatch* slides = nullptr;
  if (SUCCEEDED(getDispatch(ses.pres, L"Slides", &slides)) && slides) {
    getLong(slides, L"Count", &slideCount);
    slides->Release();
  }
  ses.slides = static_cast<int>(slideCount);

  IDispatch* settings = nullptr;
  if (FAILED(getDispatch(ses.pres, L"SlideShowSettings", &settings)) || !settings) {
    ses.releasePres();
    return err("com", "SlideShowSettings missing");
  }
  putLong(settings, L"ShowType", kPpShowTypeWindow);
  putLong(settings, L"RangeType", kPpShowAll);
  putLong(settings, L"ShowWithAnimation", kMsoTrue);
  if (cmd.open.startSlide > 1) {
    putLong(settings, L"StartingSlide", cmd.open.startSlide);
  }

  VARIANT runResult;
  VariantInit(&runResult);
  DISPPARAMS none {};
  hr = invokeNamed(settings, L"Run", DISPATCH_METHOD, &none, &runResult);
  settings->Release();
  if (SUCCEEDED(hr) && runResult.vt == VT_DISPATCH && runResult.pdispVal) {
    ses.show = runResult.pdispVal;
    ses.show->AddRef();
  }
  VariantClear(&runResult);

  // In WPS Office and some environments, SlideShowSettings.Run() returns VT_EMPTY (void)
  // rather than returning the SlideShowWindow dispatch directly. Retrieve it via the
  // presentation's SlideShowWindow property or the Application's SlideShowWindows collection.
  if (!ses.show && ses.pres) {
    (void)getDispatch(ses.pres, L"SlideShowWindow", &ses.show);
  }
  if (!ses.show && ses.app) {
    IDispatch* windows = nullptr;
    if (SUCCEEDED(getDispatch(ses.app, L"SlideShowWindows", &windows)) && windows) {
      long winCount = 0;
      getLong(windows, L"Count", &winCount);
      if (winCount > 0) {
        VARIANT idx;
        VariantInit(&idx);
        idx.vt = VT_I4;
        idx.lVal = 1;
        DISPPARAMS dp {&idx, nullptr, 1, 0};
        VARIANT item;
        VariantInit(&item);
        if (SUCCEEDED(invokeNamed(windows, L"Item", DISPATCH_METHOD, &dp, &item)) &&
            item.vt == VT_DISPATCH && item.pdispVal) {
          ses.show = item.pdispVal;
          ses.show->AddRef();
        }
        VariantClear(&item);
      }
      windows->Release();
    }
  }

  if (ses.show && (cmd.open.width > 0 || cmd.open.height > 0)) {
    HDC hdc = GetDC(nullptr);
    const int dpi = hdc ? GetDeviceCaps(hdc, LOGPIXELSX) : 96;
    if (hdc) ReleaseDC(nullptr, hdc);
    const double scale = 72.0 / (dpi > 0 ? dpi : 96);
    auto toPoints = [&](int px) { return static_cast<long>(px * scale + 0.5); };
    if (cmd.open.width > 0) putLong(ses.show, L"Width", toPoints(cmd.open.width));
    if (cmd.open.height > 0) putLong(ses.show, L"Height", toPoints(cmd.open.height));
    putLong(ses.show, L"Left", toPoints(cmd.open.left));
    putLong(ses.show, L"Top", toPoints(cmd.open.top));
  }

  st.ok = true;
  st.available = true;
  st.running = ses.show != nullptr;
  st.slides = ses.slides;
  st.slide = cmd.open.startSlide > 0 ? cmd.open.startSlide : 1;
  st.state = st.running ? "running" : "idle";
  st.attachedToExisting = ses.attachedToExisting;
  if (ses.show) {
    long hwnd = 0;
    if (SUCCEEDED(getLong(ses.show, L"HWND", &hwnd))) {
      st.hwnd = static_cast<std::uint64_t>(static_cast<unsigned long>(hwnd));
    }
  }
  return st;
}

static IDispatch* viewOf(PptSession& ses) {
  if (!ses.show) return nullptr;
  IDispatch* view = nullptr;
  if (FAILED(getDispatch(ses.show, L"View", &view))) return nullptr;
  return view;
}

static PptStatus currentStatus(PptSession& ses, bool okIfIdle) {
  PptStatus st;
  st.ok = okIfIdle || ses.show != nullptr;
  st.available = powerpointInstalled();
  st.attachedToExisting = ses.attachedToExisting;
  st.slides = ses.slides;
  st.running = ses.show != nullptr;
  st.state = ses.show ? "running" : "idle";
  if (IDispatch* view = viewOf(ses)) {
    long pos = 0;
    if (SUCCEEDED(getLong(view, L"CurrentShowPosition", &pos))) {
      st.slide = static_cast<int>(pos);
    }
    long state = 0;
    if (SUCCEEDED(getLong(view, L"State", &state))) {
      // ppSlideShowRunning=1, ppSlideShowPaused=2, ppSlideShowDone=5 (typical)
      if (state == 2) st.state = "paused";
      if (state == 5) { st.state = "ended"; st.ended = true; st.running = false; }
    }
    view->Release();
  }
  if (ses.show) {
    long hwnd = 0;
    if (SUCCEEDED(getLong(ses.show, L"HWND", &hwnd))) {
      st.hwnd = static_cast<std::uint64_t>(static_cast<unsigned long>(hwnd));
    }
  }
  return st;
}

static PptStatus doNextPrev(PptSession& ses, bool next) {
  if (!ses.show) return err("not_running", "no slideshow is running");
  IDispatch* view = viewOf(ses);
  if (!view) return err("not_running", "slideshow view missing");
  HRESULT hr = call0(view, next ? L"Next" : L"Previous");
  view->Release();
  if (FAILED(hr)) return err("com", next ? "View.Next failed" : "View.Previous failed");
  return currentStatus(ses, true);
}

static PptStatus doGoto(PptSession& ses, int slide) {
  if (!ses.show) return err("not_running", "no slideshow is running");
  IDispatch* view = viewOf(ses);
  if (!view) return err("not_running", "slideshow view missing");
  VARIANT arg;
  VariantInit(&arg);
  arg.vt = VT_I4;
  arg.lVal = slide;
  DISPPARAMS dp {&arg, nullptr, 1, 0};
  HRESULT hr = invokeNamed(view, L"GotoSlide", DISPATCH_METHOD, &dp, nullptr);
  view->Release();
  if (FAILED(hr)) return err("com", "View.GotoSlide failed");
  return currentStatus(ses, true);
}

static PptStatus doEnd(PptSession& ses) {
  if (ses.show) {
    IDispatch* view = viewOf(ses);
    if (view) {
      call0(view, L"Exit");
      view->Release();
    }
    ses.releaseShow();
  }
  PptStatus st = currentStatus(ses, true);
  st.ended = true;
  st.running = false;
  st.state = "ended";
  st.ok = true;
  return st;
}

static PptStatus doClose(PptSession& ses) {
  doEnd(ses);
  // Close only the presentation we opened.
  if (ses.pres) {
    call0(ses.pres, L"Close");
  }
  ses.releasePres();
  // Quit only if we started PowerPoint and nothing else is open.
  if (ses.app && !ses.attachedToExisting) {
    IDispatch* presentations = nullptr;
    long count = 0;
    if (SUCCEEDED(getDispatch(ses.app, L"Presentations", &presentations)) && presentations) {
      getLong(presentations, L"Count", &count);
      presentations->Release();
    }
    if (count <= 0) {
      call0(ses.app, L"Quit");
    }
  }
  ses.releaseAll();
  PptStatus st;
  st.ok = true;
  st.available = powerpointInstalled();
  st.state = "idle";
  return st;
}

static int runHelper() {
  HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(hr)) {
    writeStatus(err("com", "CoInitializeEx failed"));
    return 1;
  }
  PptSession ses;
  std::string line;
  while (std::getline(std::cin, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    PptCommand cmd;
    std::string parseErr;
    if (!decodePptCommand(line, cmd, &parseErr)) {
      writeStatus(err("helper_protocol", parseErr));
      continue;
    }
    PptStatus st;
    switch (cmd.type) {
      case PptCommandType::Ping:
        st.ok = true;
        st.available = powerpointInstalled();
        st.state = "idle";
        break;
      case PptCommandType::Probe:
        st = doProbe();
        break;
      case PptCommandType::Open:
        st = doOpen(ses, cmd);
        break;
      case PptCommandType::Next:
        st = doNextPrev(ses, true);
        break;
      case PptCommandType::Prev:
        st = doNextPrev(ses, false);
        break;
      case PptCommandType::Goto:
        st = doGoto(ses, cmd.slide);
        break;
      case PptCommandType::End:
        st = doEnd(ses);
        break;
      case PptCommandType::Status:
        st = currentStatus(ses, true);
        st.ok = true;
        break;
      case PptCommandType::Close:
        st = doClose(ses);
        break;
      case PptCommandType::Quit:
        doClose(ses);
        CoUninitialize();
        return 0;
      default:
        st = err("helper_protocol", "unknown command");
        break;
    }
    writeStatus(st);
  }
  doClose(ses);
  CoUninitialize();
  return 0;
}
#endif  // inner windows helpers used always in this file

int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "--self-test") {
    PptStatus st = mockSelfTest();
    writeStatus(st);
    return st.ok ? 0 : 1;
  }
  // Keep the process from showing a console error box on a missing typelib, etc.
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
  return runHelper();
}

#else  // !_WIN32

#include <iostream>
int main() {
  std::cerr << "deckboy-ppt-helper is a Windows program\n";
  return 1;
}

#endif
