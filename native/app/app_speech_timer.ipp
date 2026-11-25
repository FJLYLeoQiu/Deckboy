// Part of class App — included inside the class body in main.cpp.
// Speech countdown: desk-only settings/control, display overlay on the
// chosen programme (audience, default) or presenter (speaker) outputs.
// Reminder audio routing is independent of the visual target.

  static constexpr int kSpeechTimerActionOpen = 9100;
  static constexpr int kSpeechTimerActionStart = 9101;
  static constexpr int kSpeechTimerActionPause = 9102;
  static constexpr int kSpeechTimerActionReset = 9103;
  static constexpr int kSpeechTimerActionNew = 9104;
  static constexpr int kSpeechTimerActionDurDec = 9105;
  static constexpr int kSpeechTimerActionDurInc = 9106;
  static constexpr int kSpeechTimerActionTarget = 9107;
  static constexpr int kSpeechTimerActionLock = 9108;
  static constexpr int kSpeechTimerActionResetPos = 9109;
  static constexpr int kSpeechTimerActionMute = 9110;
  static constexpr int kSpeechTimerActionRoute = 9111;
  static constexpr int kSpeechTimerActionExport = 9112;
  static constexpr int kSpeechTimerActionClose = 9113;
  static constexpr int kSpeechTimerActionCuePreview = 9114;
  static constexpr int kSpeechTimerActionSizeEdit = 9115;
  static constexpr int kSpeechTimerActionWidthDec = 9116;
  static constexpr int kSpeechTimerActionWidthInc = 9117;
  static constexpr int kSpeechTimerActionHeightDec = 9118;
  static constexpr int kSpeechTimerActionHeightInc = 9119;
  static constexpr int kSpeechTimerActionAudition0 = 9120; // +0..5 builtin
  static constexpr int kSpeechTimerActionAddReminder = 9130;
  static constexpr int kSpeechTimerActionVisibility = 9131;
  static constexpr int kSpeechTimerActionOrientation = 9132;
  static constexpr int kSpeechTimerActionTimePos = 9133;
  static constexpr int kSpeechTimerActionTimeFontDec = 9134;
  static constexpr int kSpeechTimerActionTimeFontInc = 9135;
  static constexpr int kSpeechTimerActionLabelFontDec = 9136;
  static constexpr int kSpeechTimerActionLabelFontInc = 9137;
  static constexpr int kSpeechTimerActionTimeColorEdit = 9138;
  static constexpr int kSpeechTimerActionAccentColorEdit = 9139;
  static constexpr int kSpeechTimerActionBgColorEdit = 9140;
  static constexpr int kSpeechTimerActionOvertimeColorEdit = 9141;
  static constexpr int kSpeechTimerActionResetStyle = 9142;
  static constexpr int kSpeechTimerActionRemFileBase = 9200;     // +index
  static constexpr int kSpeechTimerActionRemAuditionBase = 9220; // +index
  static constexpr int kSpeechTimerActionRemKindBase = 9240;     // +index
  static constexpr int kSpeechTimerActionRemRemoveBase = 9260;   // +index
  static constexpr int kSpeechTimerActionRemOffsetBase = 9280;   // +index
  static constexpr int kMaxSpeechReminders = 8;

  fs::path speechTimerConfigPath() const {
    return Paths::stateDir() / "speech_timer.txt";
  }

  void loadSpeechTimerConfig() {
    std::error_code ec;
    const auto path = speechTimerConfigPath();
    if (!fs::exists(path, ec)) return;
    std::ifstream in(path);
    if (!in) return;
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string err;
    if (!speechTimer_.loadConfigText(text, &err) && !err.empty()) {
      triggerToast("speech timer config: " + err, ToastKind::Warning, kToastReadableMs);
    }
  }

  void saveSpeechTimerConfig() {
    std::error_code ec;
    fs::create_directories(Paths::stateDir(), ec);
    std::ofstream out(speechTimerConfigPath(), std::ios::trunc);
    if (!out) return;
    out << speechTimer_.saveConfigText();
  }

  static std::string nowIsoLocal() {
    const std::time_t t = std::time(nullptr);
    std::tm tm {};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    return buf;
  }

  std::vector<IntRect> connectedDisplayRects() const {
    std::vector<IntRect> out;
    int count = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&count);
    if (!ids) return out;
    for (int i = 0; i < count; ++i) {
      SDL_Rect b {};
      if (SDL_GetDisplayBounds(ids[i], &b)) {
        out.push_back(IntRect{b.x, b.y, b.w, b.h});
      }
    }
    SDL_free(ids);
    return out;
  }

  bool speechTimerHasMatchingOutput() const {
    const auto target = speechTimer_.config().displayTarget;
    for (const auto& output : project_.outputs) {
      if (!output.enabled) continue;
      const std::string typ = normalizeOutputType(output.outputType);
      if (target == SpeechTimerDisplayTarget::Audience &&
          (typ == "window" || typ == "stream")) return true;
      if (target == SpeechTimerDisplayTarget::Speaker && typ == "presenter") return true;
    }
    return false;
  }

  void tickSpeechTimer(std::int64_t nowMs) {
    auto fire = speechTimer_.pollDueReminders(nowMs);
    if (fire.any && !speechTimer_.config().muteReminders) {
      playSpeechTimerReminder(fire.toPlay);
    }
    serviceSpeechTimerFallbackWindow();
  }

  bool enqueuePcmOnUiStream(const std::int16_t* samples, int count) {
    if (!samples || count <= 0) return false;
    if (!ensureUiAudioDevice() || !uiAudioStream_) return false;
    deckboySetAudioPaused(uiAudioStream_, false);
    return SDL_PutAudioStreamData(uiAudioStream_, samples,
                                  count * static_cast<int>(sizeof(std::int16_t))) == 0;
  }

  bool playWavFileOnUiStream(const std::string& path) {
    SDL_AudioSpec spec {};
    Uint8* buf = nullptr;
    Uint32 len = 0;
    if (!SDL_LoadWAV(path.c_str(), &spec, &buf, &len) || !buf) return false;
    SDL_AudioSpec dst {};
    dst.freq = kAudioRate;
    dst.format = kAudioFormat;
    dst.channels = kAudioChannels;
    Uint8* converted = nullptr;
    int convertedLen = 0;
    bool ok = false;
    if (SDL_ConvertAudioSamples(&spec, buf, static_cast<int>(len), &dst, &converted, &convertedLen)
        && converted && convertedLen > 0) {
      ok = enqueuePcmOnUiStream(reinterpret_cast<const std::int16_t*>(converted),
                                convertedLen / static_cast<int>(sizeof(std::int16_t)));
      SDL_free(converted);
    }
    SDL_free(buf);
    return ok;
  }

  bool playReminderFileViaFfmpeg(const std::string& path) {
    // Short file only: cap at six seconds so a mistaken long clip cannot
    // stall the desk. stdout is raw s16le stereo at the engine rate.
    ChildProcess proc;
    const std::vector<std::string> args {
      "ffmpeg", "-nostdin", "-hide_banner", "-v", "error",
      "-t", "6", "-i", path,
      "-f", "s16le", "-acodec", "pcm_s16le",
      "-ac", "2", "-ar", std::to_string(kAudioRate), "pipe:1"
    };
    if (!spawnProcess(proc, args, SpawnOptions::pipedStdout()) || !proc.running()) {
      return false;
    }
    std::vector<std::int16_t> pcm;
    pcm.reserve(static_cast<std::size_t>(kAudioRate) * 2 * 6);
    std::int16_t chunk[2048];
    const int fd = proc.readFd;
    while (fd >= 0 && pcm.size() < static_cast<std::size_t>(kAudioRate) * 2 * 6) {
#ifdef _WIN32
      const int n = _read(fd, chunk, sizeof(chunk));
#else
      const int n = static_cast<int>(::read(fd, chunk, sizeof(chunk)));
#endif
      if (n <= 0) break;
      const int samples = n / static_cast<int>(sizeof(std::int16_t));
      pcm.insert(pcm.end(), chunk, chunk + samples);
    }
    proc.finish(2000);
    proc.stop();
    if (pcm.empty()) return false;
    return enqueuePcmOnUiStream(pcm.data(), static_cast<int>(pcm.size()));
  }

  bool playReminderSoundFile(const std::string& path) {
    std::error_code ec;
    if (path.empty() || !fs::is_regular_file(fs::path(path), ec)) {
      triggerToast("reminder file is missing", ToastKind::Warning, kToastReadableMs);
      return false;
    }
    std::string ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    if (ext == ".wav" && playWavFileOnUiStream(path)) return true;
    if (playReminderFileViaFfmpeg(path)) return true;
    if (ext != ".wav" && playWavFileOnUiStream(path)) return true;
    triggerToast("could not play reminder file (try WAV, or a file ffmpeg can decode)",
                 ToastKind::Warning, kToastReadableMs);
    return false;
  }

  void playSpeechTimerReminder(const SpeechReminder& rem) {
    // Visual target does not decide this. Default is the control path.
    const auto route = speechTimer_.config().reminderAudioRoute;
    if (route == SpeechReminderAudioRoute::Audience) {
      triggerToast("speech reminder (audience route): " + rem.label,
                   ToastKind::Help, kToastReadableMs);
    }
    if (rem.sound.kind == SpeechSoundKind::File) {
      playReminderSoundFile(rem.sound.filePath);
      return;
    }
    playTimerChime(rem.sound.builtInId, 2);
  }

  bool parseSpeechTimerSizeText(const std::string& text, int& w, int& h) const {
    std::string s = text;
    for (char& c : s) {
      if (c == ',' || c == 'x' || c == 'X' || c == '*' || c == '/') c = ' ';
    }
    std::istringstream in(s);
    if (!(in >> w >> h)) return false;
    return w > 0 && h > 0;
  }

  void currentSpeechTimerPixelSize(int& w, int& h) const {
    if (speechTimerLastOverlayPx_.w > 0 && speechTimerLastOverlayPx_.h > 0) {
      w = speechTimerLastOverlayPx_.w;
      h = speechTimerLastOverlayPx_.h;
      return;
    }
    const auto& cfg = speechTimer_.config();
    w = std::max(200, cfg.windowW);
    h = std::max(80, cfg.windowH);
  }

  void applySpeechTimerPixelSize(int w, int h) {
    w = std::clamp(w, 160, 4000);
    h = std::clamp(h, 64, 3000);
    SpeechTimerConfig cfg = speechTimer_.config();
    if (!speechTimerHasMatchingOutput() || speechTimerFallbackWindow_) {
      cfg.windowW = w;
      cfg.windowH = h;
      if (speechTimerFallbackWindow_) {
        SDL_SetWindowSize(speechTimerFallbackWindow_, w, h);
      }
    } else {
      const int rw = speechTimerLastDestW_ > 0 ? speechTimerLastDestW_ : 1920;
      const int rh = speechTimerLastDestH_ > 0 ? speechTimerLastDestH_ : 1080;
      cfg.overlayW = static_cast<float>(w) / static_cast<float>(std::max(1, rw));
      cfg.overlayH = static_cast<float>(h) / static_cast<float>(std::max(1, rh));
      SpeechTimer::clampOverlayNorm(cfg);
    }
    speechTimer_.setConfig(cfg);
    saveSpeechTimerConfig();
  }

  void pickReminderSoundFile(int index) {
    static const std::vector<SDL_DialogFileFilter> kFilters = {
      {"Audio", "wav;mp3;m4a;aac;flac;ogg;aiff;aif;wma"},
      {"WAV", "wav"},
      {"All files", "*"},
    };
    showOpenFileDialog(kFilters, false, [this, index](const std::vector<std::string>& paths) {
      if (paths.empty()) return;
      SpeechTimerConfig cfg = speechTimer_.config();
      if (index < 0 || index >= static_cast<int>(cfg.reminders.size())) return;
      cfg.reminders[static_cast<std::size_t>(index)].sound.kind = SpeechSoundKind::File;
      cfg.reminders[static_cast<std::size_t>(index)].sound.filePath = paths.front();
      speechTimer_.setConfig(cfg);
      saveSpeechTimerConfig();
      triggerToast("reminder file: " + fs::path(paths.front()).filename().string());
    });
  }

  static SDL_Color toSdlColor(const SpeechTimerColor& c) {
    return SDL_Color{c.r, c.g, c.b, c.a};
  }

  static std::string formatSpeechTimerHexColor(const SpeechTimerColor& c) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", c.r, c.g, c.b);
    return buf;
  }

  static bool parseSpeechTimerHexColor(const std::string& text, SpeechTimerColor& out) {
    std::string s = trim(text);
    if (!s.empty() && s[0] == '#') s.erase(s.begin());
    if (s.size() == 6) {
      unsigned int rgb = 0;
      std::istringstream iss(s);
      if ((iss >> std::hex >> rgb) && iss.eof()) {
        out.r = static_cast<std::uint8_t>((rgb >> 16) & 0xFF);
        out.g = static_cast<std::uint8_t>((rgb >> 8) & 0xFF);
        out.b = static_cast<std::uint8_t>(rgb & 0xFF);
        return true;
      }
    }
    int r = 0, g = 0, b = 0;
    for (char& ch : s) {
      if (ch == ',' || ch == ';') ch = ' ';
    }
    std::istringstream nums(s);
    if ((nums >> r >> g >> b) && r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
      out.r = static_cast<std::uint8_t>(r);
      out.g = static_cast<std::uint8_t>(g);
      out.b = static_cast<std::uint8_t>(b);
      return true;
    }
    return false;
  }

  TTF_Font* speechTimerScaledFont(TTF_Font* base, int scalePct) const {
    if (!base) return nullptr;
    const int clamped = std::clamp(scalePct, 50, 250);
    if (clamped == 100) return base;
    const auto it = fontLadders_.find(base);
    const int basePx = (it != fontLadders_.end() && it->second.baseSize > 0)
                           ? it->second.baseSize
                           : std::max(12, TTF_GetFontHeight(base));
    const int targetPx = std::clamp((basePx * clamped) / 100, 10, 160);
    TTF_Font* sized = fontAtSize(base, targetPx);
    return sized ? sized : base;
  }

  static const char* speechTimerPositionLabel(SpeechTimerTimePosition pos) {
    switch (pos) {
      case SpeechTimerTimePosition::Bottom: return "Time: Bottom";
      case SpeechTimerTimePosition::Left:   return "Time: Left";
      case SpeechTimerTimePosition::Right:  return "Time: Right";
      case SpeechTimerTimePosition::Top:
      default:                              return "Time: Top";
    }
  }

  void drawSpeechTimerOverlay(int /*outputIndex*/, SDL_Renderer* renderer,
                              int renderW, int renderH, const std::string& viewType) {
    if (!speechTimer_.config().windowVisible) return;
    const auto target = speechTimer_.config().displayTarget;
    const bool audienceView = (viewType == "window" || viewType == "stream");
    const bool speakerView = (viewType == "presenter");
    if (target == SpeechTimerDisplayTarget::Audience && !audienceView) return;
    if (target == SpeechTimerDisplayTarget::Speaker && !speakerView) return;
    if (renderW <= 0 || renderH <= 0 || !renderer) return;

    SpeechTimerConfig cfg = speechTimer_.config();
    SpeechTimer::clampOverlayNorm(cfg);
    speechTimerLastDestW_ = renderW;
    speechTimerLastDestH_ = renderH;
    const int x = static_cast<int>(std::lround(cfg.overlayX * renderW));
    const int y = static_cast<int>(std::lround(cfg.overlayY * renderH));
    const int w = std::max(8, static_cast<int>(std::lround(cfg.overlayW * renderW)));
    const int h = std::max(8, static_cast<int>(std::lround(cfg.overlayH * renderH)));
    speechTimerLastOverlayPx_ = SDL_Rect{x, y, w, h};

    const auto snap = speechTimer_.snapshot();
    SDL_Color bg = toSdlColor(cfg.bgColor);
    SDL_Color fg = toSdlColor(cfg.timeColor);
    SDL_Color accent = toSdlColor(cfg.accentColor);
    std::string stateWord = snap.overtime
        ? deckboy::core::i18n::format("Overtime {1}", {SpeechTimer::formatTime(snap.overtimeSeconds, false)})
        : deckboy::core::i18n::translate(snap.stateText);
    if (snap.phase == SpeechTimerPhase::Overtime ||
        snap.phase == SpeechTimerPhase::OvertimePaused) {
      bg = toSdlColor(cfg.overtimeBgColor);
      fg = toSdlColor(cfg.overtimeTimeColor);
      accent = toSdlColor(cfg.overtimeAccentColor);
      if (snap.overtime) {
        stateWord = cfg.overtimeLabel.empty()
            ? deckboy::core::i18n::format("OVERTIME {1}", {snap.timeText})
            : (cfg.overtimeLabel + " " + snap.timeText);
      }
    } else if (snap.remainingSeconds <= 60.0 && snap.running && !snap.overtime) {
      bg = {48, 28, 4, cfg.bgColor.a};
      fg = toSdlColor(cfg.timeColor);
      accent = {255, 200, 80, 255};
      stateWord = deckboy::core::i18n::translate("NEAR END");
    } else if (snap.paused) {
      bg = {24, 24, 16, cfg.bgColor.a};
      accent = {240, 220, 120, 255};
    }

    std::string reminderWord;
    if (!snap.reminderText.empty()) {
      if (snap.reminderText == "Time is up" && !cfg.timeUpLabel.empty()) {
        reminderWord = cfg.timeUpLabel;
      } else {
        reminderWord = deckboy::core::i18n::translate(snap.reminderText);
      }
    }

    SDL_Rect box {x, y, w, h};
    Primitives::drawFramedPanel(renderer, box, bg, pal.deep, accent);
    TTF_Font* timeFont = speechTimerScaledFont(fontLarge_ ? fontLarge_ : fontBase_, cfg.timeFontScale);
    TTF_Font* labelFont = speechTimerScaledFont(fontSmall_ ? fontSmall_ : fontBase_, cfg.labelFontScale);
    const int pad = std::max(6, h / 12);

    auto drawInfoBlock = [&](const SDL_Rect& infoRect) {
      if (!reminderWord.empty()) {
        const int subH = std::max(12, infoRect.h / 2);
        drawTextSafe(renderer, labelFont,
                     SDL_Rect{infoRect.x, infoRect.y, infoRect.w, subH},
                     stateWord, accent);
        drawTextSafe(renderer, labelFont,
                     SDL_Rect{infoRect.x, infoRect.y + subH, infoRect.w, infoRect.h - subH},
                     reminderWord, fg);
      } else {
        drawTextSafe(renderer, labelFont, infoRect, stateWord, accent);
      }
    };

    if (cfg.timePosition == SpeechTimerTimePosition::Left ||
        cfg.timePosition == SpeechTimerTimePosition::Right) {
      const int availW = std::max(32, box.w - pad * 3);
      const int timeW = std::max(16, availW * 55 / 100);
      const int infoW = std::max(16, availW - timeW);
      const int timeX = (cfg.timePosition == SpeechTimerTimePosition::Left)
                            ? (box.x + pad)
                            : (box.x + pad + infoW + pad);
      const int infoX = (cfg.timePosition == SpeechTimerTimePosition::Left)
                            ? (box.x + pad + timeW + pad)
                            : (box.x + pad);
      drawTextSafe(renderer, timeFont,
                   SDL_Rect{timeX, box.y + pad, timeW, std::max(16, box.h - pad * 2)},
                   snap.timeText, fg);
      drawInfoBlock(SDL_Rect{infoX, box.y + pad, infoW, std::max(16, box.h - pad * 2)});
    } else if (cfg.timePosition == SpeechTimerTimePosition::Bottom) {
      const int availH = std::max(28, box.h - pad * 2);
      const int infoH = std::max(14, availH * 48 / 100);
      const int timeH = std::max(16, availH - infoH);
      drawInfoBlock(SDL_Rect{box.x + pad, box.y + pad, std::max(16, box.w - pad * 2), infoH});
      drawTextSafe(renderer, timeFont,
                   SDL_Rect{box.x + pad, box.y + pad + infoH, std::max(16, box.w - pad * 2), timeH},
                   snap.timeText, fg);
    } else {
      const int availH = std::max(28, box.h - pad * 2);
      const int timeH = std::max(16, availH * 50 / 100);
      const int infoH = std::max(14, availH - timeH);
      drawTextSafe(renderer, timeFont,
                   SDL_Rect{box.x + pad, box.y + pad, std::max(16, box.w - pad * 2), timeH},
                   snap.timeText, fg);
      drawInfoBlock(SDL_Rect{box.x + pad, box.y + pad + timeH, std::max(16, box.w - pad * 2), infoH});
    }
    // No settings, no file paths, no buttons on the audience/speaker display.
  }

  bool speechTimerOverlayHit(int outputIndex, int x, int y) const {
    (void)outputIndex;
    return pointInRect(x, y, speechTimerLastOverlayPx_);
  }

  void beginSpeechTimerOverlayDrag(int x, int y, int renderW, int renderH) {
    if (speechTimer_.config().layoutLocked) return;
    if (renderW <= 0 || renderH <= 0) return;
    const SDL_Rect box = speechTimerLastOverlayPx_;
    const int grip = std::max(12, std::min(box.w, box.h) / 5);
    speechTimerResizeBox_ = box;
    if (box.w > 0 && box.h > 0 &&
        x >= box.x + box.w - grip && y >= box.y + box.h - grip) {
      speechTimerResizing_ = true;
      speechTimerDragging_ = false;
      return;
    }
    speechTimerResizing_ = false;
    speechTimerDragging_ = true;
    speechTimerDragOffX_ = static_cast<float>(x) / static_cast<float>(renderW) -
                           speechTimer_.config().overlayX;
    speechTimerDragOffY_ = static_cast<float>(y) / static_cast<float>(renderH) -
                           speechTimer_.config().overlayY;
  }

  void moveSpeechTimerOverlayDrag(int x, int y, int renderW, int renderH) {
    if (speechTimer_.config().layoutLocked) return;
    if (renderW <= 0 || renderH <= 0) return;
    SpeechTimerConfig cfg = speechTimer_.config();
    if (speechTimerResizing_) {
      const int nw = std::max(80, x - speechTimerResizeBox_.x);
      const int nh = std::max(40, y - speechTimerResizeBox_.y);
      cfg.overlayW = static_cast<float>(nw) / static_cast<float>(renderW);
      cfg.overlayH = static_cast<float>(nh) / static_cast<float>(renderH);
      SpeechTimer::clampOverlayNorm(cfg);
      speechTimer_.setConfig(cfg);
      return;
    }
    if (!speechTimerDragging_) return;
    cfg.overlayX = static_cast<float>(x) / static_cast<float>(renderW) - speechTimerDragOffX_;
    cfg.overlayY = static_cast<float>(y) / static_cast<float>(renderH) - speechTimerDragOffY_;
    SpeechTimer::clampOverlayNorm(cfg);
    speechTimer_.setConfig(cfg);
  }

  void endSpeechTimerOverlayDrag() {
    if (!speechTimerDragging_ && !speechTimerResizing_) return;
    speechTimerDragging_ = false;
    speechTimerResizing_ = false;
    saveSpeechTimerConfig();
  }

  void resetSpeechTimerPosition() {
    SpeechTimerConfig cfg = speechTimer_.config();
    const auto d = SpeechTimer::defaultConfig();
    cfg.overlayX = d.overlayX;
    cfg.overlayY = d.overlayY;
    cfg.overlayW = d.overlayW;
    cfg.overlayH = d.overlayH;
    cfg.windowX = -1;
    cfg.windowY = -1;
    cfg.windowW = d.windowW;
    cfg.windowH = d.windowH;
    speechTimer_.setConfig(cfg);
    saveSpeechTimerConfig();
    if (speechTimerFallbackWindow_) {
      SDL_SetWindowPosition(speechTimerFallbackWindow_,
                            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
      SDL_SetWindowSize(speechTimerFallbackWindow_, cfg.windowW, cfg.windowH);
    }
    triggerToast("speech timer position reset");
  }

  void closeSpeechTimerFallbackWindow() {
    if (speechTimerFallbackRenderer_) {
      SDL_DestroyRenderer(speechTimerFallbackRenderer_);
      speechTimerFallbackRenderer_ = nullptr;
    }
    if (speechTimerFallbackWindow_) {
      SDL_DestroyWindow(speechTimerFallbackWindow_);
      speechTimerFallbackWindow_ = nullptr;
    }
  }

  void serviceSpeechTimerFallbackWindow() {
    const bool need = speechTimer_.config().windowVisible && !speechTimerHasMatchingOutput();
    if (!need) {
      closeSpeechTimerFallbackWindow();
      return;
    }
    SpeechTimerConfig cfg = speechTimer_.config();
    if (!speechTimerFallbackWindow_) {
      IntRect wanted {cfg.windowX, cfg.windowY, cfg.windowW, cfg.windowH};
      if (wanted.x < 0 || wanted.y < 0) {
        wanted.x = 80;
        wanted.y = 80;
      }
      IntRect clamped = SpeechTimer::clampRectToDisplays(
          wanted, connectedDisplayRects(), 200, 80, IntRect{80, 80, 520, 200});
      cfg.windowX = clamped.x;
      cfg.windowY = clamped.y;
      cfg.windowW = clamped.w;
      cfg.windowH = clamped.h;
      speechTimer_.setConfig(cfg);
      Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_UTILITY;
      if (cfg.alwaysOnTop) flags |= SDL_WINDOW_ALWAYS_ON_TOP;
      speechTimerFallbackWindow_ = SDL_CreateWindow(
          "Speech timer", clamped.w, clamped.h, flags);
      if (!speechTimerFallbackWindow_) return;
      SDL_SetWindowPosition(speechTimerFallbackWindow_, clamped.x, clamped.y);
      speechTimerFallbackRenderer_ = deckboyCreateRenderer(speechTimerFallbackWindow_);
    } else {
      int x = 0, y = 0, w = 0, h = 0;
      SDL_GetWindowPosition(speechTimerFallbackWindow_, &x, &y);
      SDL_GetWindowSize(speechTimerFallbackWindow_, &w, &h);
      if (x != cfg.windowX || y != cfg.windowY || w != cfg.windowW || h != cfg.windowH) {
        cfg.windowX = x;
        cfg.windowY = y;
        cfg.windowW = w;
        cfg.windowH = h;
        IntRect clamped = SpeechTimer::clampRectToDisplays(
            IntRect{x, y, w, h}, connectedDisplayRects(), 200, 80, IntRect{80, 80, 520, 200});
        if (clamped.x != x || clamped.y != y || clamped.w != w || clamped.h != h) {
          SDL_SetWindowPosition(speechTimerFallbackWindow_, clamped.x, clamped.y);
          SDL_SetWindowSize(speechTimerFallbackWindow_, clamped.w, clamped.h);
          cfg.windowX = clamped.x;
          cfg.windowY = clamped.y;
          cfg.windowW = clamped.w;
          cfg.windowH = clamped.h;
        }
        speechTimer_.setConfig(cfg);
      }
    }
    if (!speechTimerFallbackRenderer_) return;
    SDL_Renderer* r = speechTimerFallbackRenderer_;
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(speechTimerFallbackWindow_, &w, &h);
    SDL_SetRenderDrawColor(r, 8, 10, 14, 255);
    SDL_RenderClear(r);
    // Same display-only chrome as the composited overlay; no settings here.
    drawSpeechTimerOverlay(-1, r, w, h,
                           speechTimer_.config().displayTarget == SpeechTimerDisplayTarget::Speaker
                               ? "presenter" : "window");
    SDL_RenderPresent(r);
  }

  void renderSpeechTimerControlPanel(int width, int height) {
    speechTimerBtns_.clear();
    // Placed in the top toolbar's right-anchored group (to the left of
    // fullscreen/blackout) so it never overlaps those buttons.
    const int bw = uiScaled(148);
    const int bh = uiScaled(28);
    SDL_Rect toggle = speechTimerToggleRect_.w > 0
        ? speechTimerToggleRect_
        : SDL_Rect{width - bw - uiScaled(12), uiScaled(8), bw, bh};
    if (toggle.y + toggle.h > height) toggle.y = uiScaled(8);
    TTF_Font* btnFont = fontPixelSmall_ ? fontPixelSmall_ : fontSmall_;
    Primitives::drawFramedPanel(controlRenderer_, toggle,
                                speechTimerPanelOpen_ ? pal.dark : pal.tile,
                                pal.deep, pal.mid);
    drawCenteredTextSafe(controlRenderer_, btnFont, toggle, "SPEECH TIMER",
                         speechTimerPanelOpen_ ? pal.light : pal.fg);
    speechTimerBtns_.push_back({toggle, kSpeechTimerActionOpen});
    if (!speechTimerPanelOpen_) return;

    const int pw = std::min(width - uiScaled(24), uiScaled(560));
    const int ph = std::min(height - toggle.y - toggle.h - uiScaled(16), uiScaled(740));
    const int px = std::clamp(toggle.x + toggle.w - pw, uiScaled(12), std::max(uiScaled(12), width - pw - uiScaled(12)));
    SDL_Rect panel {px, toggle.y + toggle.h + uiScaled(8), pw, ph};
    Primitives::drawFramedPanel(controlRenderer_, panel, pal.shellInner, pal.deep, pal.shellOuter);
    const auto snap = speechTimer_.snapshot();
    const auto& cfg = speechTimer_.config();
    int y = panel.y + uiScaled(10);
    const int pad = uiScaled(12);
    const int rowH = uiScaled(28);
    const std::string panelState = snap.overtime
        ? deckboy::core::i18n::format("Overtime {1}", {SpeechTimer::formatTime(snap.overtimeSeconds, false)})
        : deckboy::core::i18n::translate(snap.stateText);
    drawTextSafe(controlRenderer_, fontBase_,
                 SDL_Rect{panel.x + pad, y, panel.w - pad * 2, uiScaled(32)},
                 snap.timeText + "  " + panelState, pal.fg);
    y += uiScaled(36);
    if (!snap.reminderText.empty()) {
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect{panel.x + pad, y, panel.w - pad * 2, rowH},
                   snap.reminderText, pal.mid);
      y += rowH;
    }

    auto addBtn = [&](int x, const std::string& label, int action, int btnW = 0) {
      const int w = btnW > 0 ? btnW : uiScaled(88);
      SDL_Rect r {x, y, w, rowH};
      Primitives::drawFramedPanel(controlRenderer_, r, pal.dark, pal.deep, pal.mid);
      drawCenteredTextSafe(controlRenderer_, fontSmall_, r, label, pal.light);
      speechTimerBtns_.push_back({r, action});
      return w;
    };
    int x = panel.x + pad;
    x += addBtn(x, snap.paused || !snap.running ? "START" : "PAUSE",
                snap.paused || !snap.running ? kSpeechTimerActionStart
                                             : kSpeechTimerActionPause) + uiScaled(6);
    x += addBtn(x, "RESET", kSpeechTimerActionReset) + uiScaled(6);
    addBtn(x, "NEW SPEECH", kSpeechTimerActionNew, uiScaled(110));
    y += rowH + uiScaled(8);

    drawTextSafe(controlRenderer_, fontSmall_,
                 SDL_Rect{panel.x + pad, y, uiScaled(180), rowH},
                 deckboy::core::i18n::format("Duration {1} min", {std::to_string(cfg.durationSeconds / 60)}),
                 pal.fg);
    addBtn(panel.x + panel.w - pad - uiScaled(132), "-", kSpeechTimerActionDurDec, uiScaled(60));
    addBtn(panel.x + panel.w - pad - uiScaled(64), "+", kSpeechTimerActionDurInc, uiScaled(60));
    y += rowH + uiScaled(6);

    const char* target = cfg.displayTarget == SpeechTimerDisplayTarget::Speaker
                             ? "Display: Speaker window"
                             : "Display: Audience window (default)";
    addBtn(panel.x + pad, target, kSpeechTimerActionTarget, panel.w - pad * 2);
    y += rowH + uiScaled(6);
    const std::string visLabel = cfg.windowVisible ? "Display: Visible" : "Display: Hidden";
    addBtn(panel.x + pad, visLabel, kSpeechTimerActionVisibility, uiScaled(140));
    addBtn(panel.x + pad + uiScaled(148), speechTimerPositionLabel(cfg.timePosition),
           kSpeechTimerActionTimePos, uiScaled(140));
    addBtn(panel.x + pad + uiScaled(296),
           cfg.orientation == SpeechTimerOrientation::Horizontal ? "Layout: Horizontal" : "Layout: Vertical",
           kSpeechTimerActionOrientation, uiScaled(140));
    y += rowH + uiScaled(6);

    drawTextSafe(controlRenderer_, fontSmall_,
                 SDL_Rect{panel.x + pad, y, uiScaled(136), rowH},
                 deckboy::core::i18n::format("Time font {1}%", {std::to_string(cfg.timeFontScale)}),
                 pal.fg);
    addBtn(panel.x + pad + uiScaled(140), "A-", kSpeechTimerActionTimeFontDec, uiScaled(44));
    addBtn(panel.x + pad + uiScaled(188), "A+", kSpeechTimerActionTimeFontInc, uiScaled(44));
    drawTextSafe(controlRenderer_, fontSmall_,
                 SDL_Rect{panel.x + pad + uiScaled(244), y, uiScaled(136), rowH},
                 deckboy::core::i18n::format("Label font {1}%", {std::to_string(cfg.labelFontScale)}),
                 pal.fg);
    addBtn(panel.x + pad + uiScaled(384), "A-", kSpeechTimerActionLabelFontDec, uiScaled(44));
    addBtn(panel.x + pad + uiScaled(432), "A+", kSpeechTimerActionLabelFontInc, uiScaled(44));
    y += rowH + uiScaled(6);

    addBtn(panel.x + pad, "Time color", kSpeechTimerActionTimeColorEdit, uiScaled(116));
    addBtn(panel.x + pad + uiScaled(122), "Status color", kSpeechTimerActionAccentColorEdit, uiScaled(116));
    addBtn(panel.x + pad + uiScaled(244), "Overtime color", kSpeechTimerActionOvertimeColorEdit, uiScaled(124));
    addBtn(panel.x + pad + uiScaled(374), "Bg color", kSpeechTimerActionBgColorEdit, uiScaled(88));
    addBtn(panel.x + pad + uiScaled(468), "Reset", kSpeechTimerActionResetStyle, uiScaled(64));
    y += rowH + uiScaled(6);

    drawTextSafe(controlRenderer_, fontSmall_,
                 SDL_Rect{panel.x + pad, y, panel.w - pad * 2, rowH * 2},
                 "Settings stay on this desk. The countdown on the chosen "
                 "output shows only the time and the current reminder.", pal.mid);
    y += rowH * 2 + uiScaled(4);

    addBtn(panel.x + pad, cfg.layoutLocked ? "Unlock layout" : "Lock layout",
           kSpeechTimerActionLock, uiScaled(140));
    addBtn(panel.x + pad + uiScaled(148), "Reset position",
           kSpeechTimerActionResetPos, uiScaled(140));
    y += rowH + uiScaled(6);

    int sizeW = 0, sizeH = 0;
    currentSpeechTimerPixelSize(sizeW, sizeH);
    const std::string sizeLabel = deckboy::core::i18n::format(
        "Size {1} x {2} px (type or - / +)",
        {std::to_string(sizeW), std::to_string(sizeH)});
    drawTextSafe(controlRenderer_, fontSmall_,
                 SDL_Rect{panel.x + pad, y, uiScaled(260), rowH}, sizeLabel, pal.fg);
    addBtn(panel.x + pad + uiScaled(266), "Edit", kSpeechTimerActionSizeEdit, uiScaled(56));
    addBtn(panel.x + pad + uiScaled(328), "W-", kSpeechTimerActionWidthDec, uiScaled(40));
    addBtn(panel.x + pad + uiScaled(372), "W+", kSpeechTimerActionWidthInc, uiScaled(40));
    addBtn(panel.x + pad + uiScaled(416), "H-", kSpeechTimerActionHeightDec, uiScaled(40));
    addBtn(panel.x + pad + uiScaled(460), "H+", kSpeechTimerActionHeightInc, uiScaled(40));
    y += rowH + uiScaled(6);

    const char* route = cfg.reminderAudioRoute == SpeechReminderAudioRoute::Audience
                            ? "Reminder audio: Audience (caution)"
                            : "Reminder audio: Control (default)";
    addBtn(panel.x + pad, route, kSpeechTimerActionRoute, panel.w - pad * 2);
    y += rowH + uiScaled(6);
    addBtn(panel.x + pad, cfg.muteReminders ? "Reminders muted" : "Mute reminders",
           kSpeechTimerActionMute, uiScaled(180));
    addBtn(panel.x + pad + uiScaled(188), "Export CSV", kSpeechTimerActionExport, uiScaled(120));
    y += rowH + uiScaled(6);
    addBtn(panel.x + pad, cuePreviewActive_ ? "Stop cue preview" : "Cue preview selected",
           kSpeechTimerActionCuePreview, panel.w - pad * 2);
    y += rowH + uiScaled(8);

    drawTextSafe(controlRenderer_, fontSmall_,
                 SDL_Rect{panel.x + pad, y, panel.w - pad * 2, rowH},
                 "Reminders (each can use a built-in or a local file):", pal.fg);
    y += rowH;
    for (int i = 0; i < static_cast<int>(cfg.reminders.size()) && i < kMaxSpeechReminders; ++i) {
      const auto& rem = cfg.reminders[static_cast<std::size_t>(i)];
      const std::string when = rem.when == SpeechReminderWhen::AtZero ? "at 0"
                             : rem.when == SpeechReminderWhen::Overtime
                                 ? ("+" + std::to_string(rem.offsetSeconds) + "s")
                                 : (std::to_string(rem.offsetSeconds) + "s left");
      std::string sound = rem.sound.kind == SpeechSoundKind::File
        ? (rem.sound.filePath.empty() ? "file (none)"
                                      : fs::path(rem.sound.filePath).filename().string())
        : std::string(SpeechTimer::builtInSoundName(rem.sound.builtInId));
      if (sound.size() > 18) sound.resize(18);
      drawTextSafe(controlRenderer_, fontSmall_,
                   SDL_Rect{panel.x + pad, y, uiScaled(150), rowH},
                   rem.label.empty() ? ("Reminder " + std::to_string(i + 1)) : rem.label,
                   pal.fg);
      addBtn(panel.x + pad + uiScaled(154), when, kSpeechTimerActionRemOffsetBase + i, uiScaled(80));
      addBtn(panel.x + pad + uiScaled(238), sound, kSpeechTimerActionRemKindBase + i, uiScaled(90));
      addBtn(panel.x + pad + uiScaled(332), "File", kSpeechTimerActionRemFileBase + i, uiScaled(50));
      addBtn(panel.x + pad + uiScaled(386), "Play", kSpeechTimerActionRemAuditionBase + i, uiScaled(50));
      addBtn(panel.x + pad + uiScaled(440), "X", kSpeechTimerActionRemRemoveBase + i, uiScaled(28));
      y += rowH + uiScaled(4);
    }
    if (static_cast<int>(cfg.reminders.size()) < kMaxSpeechReminders) {
      addBtn(panel.x + pad, "Add reminder", kSpeechTimerActionAddReminder, uiScaled(140));
      y += rowH + uiScaled(6);
    }
    drawTextSafe(controlRenderer_, fontSmall_,
                 SDL_Rect{panel.x + pad, y, panel.w - pad * 2, rowH * 3},
                 "Reminder audio does not follow the visual target. "
                 "Default is the control path, not the live / record bus. "
                 "A full-desktop capture can still record a fallback window. "
                 "Drag a corner of the countdown to resize; type a size on this desk.", pal.mid);
  }

  bool handleSpeechTimerButton(int action) {
    const std::int64_t now = static_cast<std::int64_t>(SDL_GetTicks());
    SpeechTimerConfig cfg = speechTimer_.config();
    switch (action) {
      case kSpeechTimerActionOpen:
        speechTimerPanelOpen_ = !speechTimerPanelOpen_;
        return true;
      case kSpeechTimerActionStart:
        speechTimer_.start(now, nowIsoLocal());
        return true;
      case kSpeechTimerActionPause:
        if (speechTimer_.snapshot().paused) speechTimer_.resume(now);
        else speechTimer_.pause(now);
        return true;
      case kSpeechTimerActionReset:
        speechTimer_.reset();
        return true;
      case kSpeechTimerActionNew:
        speechTimer_.beginNewSpeech(now, nowIsoLocal(), nowIsoLocal());
        return true;
      case kSpeechTimerActionDurDec:
        cfg.durationSeconds = std::max(30, cfg.durationSeconds - 60);
        speechTimer_.setConfig(cfg);
        saveSpeechTimerConfig();
        return true;
      case kSpeechTimerActionDurInc:
        cfg.durationSeconds = std::min(6 * 3600, cfg.durationSeconds + 60);
        speechTimer_.setConfig(cfg);
        saveSpeechTimerConfig();
        return true;
      case kSpeechTimerActionTarget:
        cfg.displayTarget = (cfg.displayTarget == SpeechTimerDisplayTarget::Audience)
                                ? SpeechTimerDisplayTarget::Speaker
                                : SpeechTimerDisplayTarget::Audience;
        speechTimer_.setConfig(cfg);
        saveSpeechTimerConfig();
        triggerToast(cfg.displayTarget == SpeechTimerDisplayTarget::Audience
                         ? "speech timer display: audience window (programme)"
                         : "speech timer display: speaker window (presenter)");
        return true;
      case kSpeechTimerActionVisibility:
        cfg.windowVisible = !cfg.windowVisible;
        speechTimer_.setConfig(cfg);
        saveSpeechTimerConfig();
        triggerToast(cfg.windowVisible ? "speech timer: visible" : "speech timer: hidden");
        return true;
      case kSpeechTimerActionOrientation:
        cfg.orientation = (cfg.orientation == SpeechTimerOrientation::Horizontal)
                              ? SpeechTimerOrientation::Vertical
                              : SpeechTimerOrientation::Horizontal;
        cfg.timePosition = (cfg.orientation == SpeechTimerOrientation::Horizontal)
                               ? SpeechTimerTimePosition::Left
                               : SpeechTimerTimePosition::Top;
        speechTimer_.setConfig(cfg);
        saveSpeechTimerConfig();
        triggerToast(cfg.orientation == SpeechTimerOrientation::Horizontal
                         ? "speech timer layout: horizontal"
                         : "speech timer layout: vertical");
        return true;
      case kSpeechTimerActionTimePos: {
        const int nextPos = (static_cast<int>(cfg.timePosition) + 1) % 4;
        cfg.timePosition = static_cast<SpeechTimerTimePosition>(nextPos);
        cfg.orientation = (cfg.timePosition == SpeechTimerTimePosition::Left ||
                           cfg.timePosition == SpeechTimerTimePosition::Right)
                              ? SpeechTimerOrientation::Horizontal
                              : SpeechTimerOrientation::Vertical;
        speechTimer_.setConfig(cfg);
        saveSpeechTimerConfig();
        triggerToast(speechTimerPositionLabel(cfg.timePosition));
        return true;
      }
      case kSpeechTimerActionTimeFontDec:
        cfg.timeFontScale = std::clamp(cfg.timeFontScale - 15, 50, 250);
        speechTimer_.setConfig(cfg);
        saveSpeechTimerConfig();
        return true;
      case kSpeechTimerActionTimeFontInc:
        cfg.timeFontScale = std::clamp(cfg.timeFontScale + 15, 50, 250);
        speechTimer_.setConfig(cfg);
        saveSpeechTimerConfig();
        return true;
      case kSpeechTimerActionLabelFontDec:
        cfg.labelFontScale = std::clamp(cfg.labelFontScale - 15, 50, 250);
        speechTimer_.setConfig(cfg);
        saveSpeechTimerConfig();
        return true;
      case kSpeechTimerActionLabelFontInc:
        cfg.labelFontScale = std::clamp(cfg.labelFontScale + 15, 50, 250);
        speechTimer_.setConfig(cfg);
        saveSpeechTimerConfig();
        return true;
      case kSpeechTimerActionTimeColorEdit:
        openInlineTextEditor("speech_timer.time_color", "Time color",
                             "Hex (#RRGGBB) or R G B:",
                             formatSpeechTimerHexColor(cfg.timeColor),
                             [this](const std::string& value) {
                               SpeechTimerConfig next = speechTimer_.config();
                               if (!parseSpeechTimerHexColor(value, next.timeColor)) {
                                 triggerToast("enter #RRGGBB or R G B", ToastKind::Warning, kToastReadableMs);
                                 return;
                               }
                               speechTimer_.setConfig(next);
                               saveSpeechTimerConfig();
                             });
        return true;
      case kSpeechTimerActionAccentColorEdit:
        openInlineTextEditor("speech_timer.accent_color", "Status color",
                             "Hex (#RRGGBB) or R G B:",
                             formatSpeechTimerHexColor(cfg.accentColor),
                             [this](const std::string& value) {
                               SpeechTimerConfig next = speechTimer_.config();
                               if (!parseSpeechTimerHexColor(value, next.accentColor)) {
                                 triggerToast("enter #RRGGBB or R G B", ToastKind::Warning, kToastReadableMs);
                                 return;
                               }
                               speechTimer_.setConfig(next);
                               saveSpeechTimerConfig();
                             });
        return true;
      case kSpeechTimerActionOvertimeColorEdit:
        openInlineTextEditor("speech_timer.overtime_color", "Overtime color",
                             "Hex (#RRGGBB) or R G B:",
                             formatSpeechTimerHexColor(cfg.overtimeAccentColor),
                             [this](const std::string& value) {
                               SpeechTimerConfig next = speechTimer_.config();
                               if (!parseSpeechTimerHexColor(value, next.overtimeAccentColor)) {
                                 triggerToast("enter #RRGGBB or R G B", ToastKind::Warning, kToastReadableMs);
                                 return;
                               }
                               speechTimer_.setConfig(next);
                               saveSpeechTimerConfig();
                             });
        return true;
      case kSpeechTimerActionBgColorEdit:
        openInlineTextEditor("speech_timer.bg_color", "Background color",
                             "Hex (#RRGGBB) or R G B:",
                             formatSpeechTimerHexColor(cfg.bgColor),
                             [this](const std::string& value) {
                               SpeechTimerConfig next = speechTimer_.config();
                               if (!parseSpeechTimerHexColor(value, next.bgColor)) {
                                 triggerToast("enter #RRGGBB or R G B", ToastKind::Warning, kToastReadableMs);
                                 return;
                               }
                               speechTimer_.setConfig(next);
                               saveSpeechTimerConfig();
                             });
        return true;
      case kSpeechTimerActionResetStyle: {
        const auto d = SpeechTimer::defaultConfig();
        cfg.timeFontScale = d.timeFontScale;
        cfg.labelFontScale = d.labelFontScale;
        cfg.bgColor = d.bgColor;
        cfg.timeColor = d.timeColor;
        cfg.accentColor = d.accentColor;
        cfg.overtimeBgColor = d.overtimeBgColor;
        cfg.overtimeTimeColor = d.overtimeTimeColor;
        cfg.overtimeAccentColor = d.overtimeAccentColor;
        speechTimer_.setConfig(cfg);
        saveSpeechTimerConfig();
        triggerToast("speech timer style reset");
        return true;
      }
      case kSpeechTimerActionLock:
        cfg.layoutLocked = !cfg.layoutLocked;
        speechTimer_.setConfig(cfg);
        saveSpeechTimerConfig();
        return true;
      case kSpeechTimerActionResetPos:
        resetSpeechTimerPosition();
        return true;
      case kSpeechTimerActionSizeEdit: {
        int tw = 0, th = 0;
        currentSpeechTimerPixelSize(tw, th);
        openInlineTextEditor("speech_timer.size", "Countdown size",
                             "Width and height in pixels (for example 520 200):",
                             std::to_string(tw) + " " + std::to_string(th),
                             [this](const std::string& value) {
                               int nw = 0, nh = 0;
                               if (!parseSpeechTimerSizeText(value, nw, nh)) {
                                 triggerToast("enter width and height, e.g. 520 200",
                                              ToastKind::Warning, kToastReadableMs);
                                 return;
                               }
                               applySpeechTimerPixelSize(nw, nh);
                               triggerToast("countdown size " + std::to_string(nw) +
                                            " x " + std::to_string(nh) + " px");
                             });
        return true;
      }
      case kSpeechTimerActionWidthDec:
      case kSpeechTimerActionWidthInc:
      case kSpeechTimerActionHeightDec:
      case kSpeechTimerActionHeightInc: {
        int tw = 0, th = 0;
        currentSpeechTimerPixelSize(tw, th);
        const int step = 20;
        if (action == kSpeechTimerActionWidthDec) tw -= step;
        if (action == kSpeechTimerActionWidthInc) tw += step;
        if (action == kSpeechTimerActionHeightDec) th -= step;
        if (action == kSpeechTimerActionHeightInc) th += step;
        applySpeechTimerPixelSize(tw, th);
        return true;
      }
      case kSpeechTimerActionAddReminder: {
        if (static_cast<int>(cfg.reminders.size()) >= kMaxSpeechReminders) return true;
        SpeechReminder rem;
        rem.id = "rem-custom-" + std::to_string(cfg.reminders.size() + 1);
        rem.when = SpeechReminderWhen::Remaining;
        rem.offsetSeconds = 30;
        rem.label = "Custom reminder";
        rem.sound.kind = SpeechSoundKind::BuiltIn;
        rem.sound.builtInId = 0;
        cfg.reminders.push_back(std::move(rem));
        speechTimer_.setConfig(cfg);
        saveSpeechTimerConfig();
        return true;
      }
      case kSpeechTimerActionMute:
        cfg.muteReminders = !cfg.muteReminders;
        speechTimer_.setConfig(cfg);
        saveSpeechTimerConfig();
        return true;
      case kSpeechTimerActionRoute:
        cfg.reminderAudioRoute = (cfg.reminderAudioRoute == SpeechReminderAudioRoute::Control)
                                     ? SpeechReminderAudioRoute::Audience
                                     : SpeechReminderAudioRoute::Control;
        speechTimer_.setConfig(cfg);
        saveSpeechTimerConfig();
        triggerToast(cfg.reminderAudioRoute == SpeechReminderAudioRoute::Control
                         ? "reminder audio: control path (not auto-mixed to live)"
                         : "reminder audio: audience path (may share a physical device)");
        return true;
      case kSpeechTimerActionCuePreview:
        toggleCuePreview();
        return true;
      case kSpeechTimerActionExport: {
        const auto csv = speechTimer_.exportSessionCsv();
        const auto path = Paths::stateDir() / "speech_timer_sessions.csv";
        std::error_code ec;
        fs::create_directories(Paths::stateDir(), ec);
        std::ofstream out(path, std::ios::trunc);
        if (out) {
          out << csv;
          triggerToast("wrote " + path.string());
        } else {
          triggerToast("could not write speech timer CSV", ToastKind::Warning, kToastReadableMs);
        }
        return true;
      }
      default: {
        auto inRange = [&](int base) {
          return action >= base && action < base + kMaxSpeechReminders;
        };
        if (inRange(kSpeechTimerActionRemFileBase)) {
          pickReminderSoundFile(action - kSpeechTimerActionRemFileBase);
          return true;
        }
        if (inRange(kSpeechTimerActionRemAuditionBase)) {
          const int i = action - kSpeechTimerActionRemAuditionBase;
          if (i >= 0 && i < static_cast<int>(cfg.reminders.size())) {
            const auto& rem = cfg.reminders[static_cast<std::size_t>(i)];
            if (rem.sound.kind == SpeechSoundKind::File) playReminderSoundFile(rem.sound.filePath);
            else playTimerChime(rem.sound.builtInId, 1);
          }
          return true;
        }
        if (inRange(kSpeechTimerActionRemKindBase)) {
          const int i = action - kSpeechTimerActionRemKindBase;
          if (i >= 0 && i < static_cast<int>(cfg.reminders.size())) {
            auto& rem = cfg.reminders[static_cast<std::size_t>(i)];
            if (rem.sound.kind == SpeechSoundKind::File) {
              rem.sound.kind = SpeechSoundKind::BuiltIn;
              rem.sound.builtInId = 0;
            } else {
              rem.sound.builtInId = (rem.sound.builtInId + 1) % 6;
            }
            speechTimer_.setConfig(cfg);
            saveSpeechTimerConfig();
          }
          return true;
        }
        if (inRange(kSpeechTimerActionRemRemoveBase)) {
          const int i = action - kSpeechTimerActionRemRemoveBase;
          if (i >= 0 && i < static_cast<int>(cfg.reminders.size()) && cfg.reminders.size() > 1) {
            cfg.reminders.erase(cfg.reminders.begin() + i);
            speechTimer_.setConfig(cfg);
            saveSpeechTimerConfig();
          }
          return true;
        }
        if (inRange(kSpeechTimerActionRemOffsetBase)) {
          const int i = action - kSpeechTimerActionRemOffsetBase;
          if (i >= 0 && i < static_cast<int>(cfg.reminders.size())) {
            auto rem = cfg.reminders[static_cast<std::size_t>(i)];
            openInlineTextEditor("speech_timer.rem_offset", "Reminder point",
                                 "Seconds remaining, or 0 for time-up (prefix + for overtime):",
                                 (rem.when == SpeechReminderWhen::Overtime ? "+" : "") +
                                     std::to_string(rem.offsetSeconds),
                                 [this, i](const std::string& value) {
                                   SpeechTimerConfig next = speechTimer_.config();
                                   if (i < 0 || i >= static_cast<int>(next.reminders.size())) return;
                                   std::string s = value;
                                   bool overtime = false;
                                   if (!s.empty() && s[0] == '+') { overtime = true; s.erase(s.begin()); }
                                   int secs = 0;
                                   try { secs = std::stoi(s); } catch (...) { return; }
                                   auto& rem = next.reminders[static_cast<std::size_t>(i)];
                                   if (overtime) {
                                     rem.when = SpeechReminderWhen::Overtime;
                                     rem.offsetSeconds = std::max(0, secs);
                                     rem.label = "+" + std::to_string(rem.offsetSeconds) + "s overtime";
                                   } else if (secs <= 0) {
                                     rem.when = SpeechReminderWhen::AtZero;
                                     rem.offsetSeconds = 0;
                                     rem.label = "Time is up";
                                   } else {
                                     rem.when = SpeechReminderWhen::Remaining;
                                     rem.offsetSeconds = secs;
                                     rem.label = std::to_string(secs) + " seconds left";
                                   }
                                   speechTimer_.setConfig(next);
                                   saveSpeechTimerConfig();
                                 });
          }
          return true;
        }
        if (action >= kSpeechTimerActionAudition0 && action < kSpeechTimerActionAudition0 + 6) {
          playTimerChime(action - kSpeechTimerActionAudition0, 1);
          return true;
        }
        return false;
      }
    }
  }

  bool handleSpeechTimerControlClick(int x, int y) {
    for (const auto& b : speechTimerBtns_) {
      if (pointInRect(x, y, b.rect)) {
        lastInlineEditorAnchorRect_ = b.rect;
        return handleSpeechTimerButton(b.action);
      }
    }
    return false;
  }

  bool closeSpeechTimerFallbackIfEvent(const SDL_Event& event) {
    if (!speechTimerFallbackWindow_) return false;
    if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
        event.window.windowID == SDL_GetWindowID(speechTimerFallbackWindow_)) {
      closeSpeechTimerFallbackWindow();
      SpeechTimerConfig cfg = speechTimer_.config();
      cfg.windowVisible = false;
      speechTimer_.setConfig(cfg);
      saveSpeechTimerConfig();
      return true;
    }
    return false;
  }
