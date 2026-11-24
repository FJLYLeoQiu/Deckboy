// Part of class App — included inside the class body in main.cpp.
// Native PowerPoint playback: drives an external slideshow through
// deckboy-ppt-helper. This is not programme / NDI compositing.

  bool firePowerPointShowCue(int deckIndex, int cueIndex) {
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) return false;
    Deck& deck = project_.decks[deckIndex];
    if (cueIndex < 0 || cueIndex >= static_cast<int>(deck.cues.size())) return false;
    const Cue& cue = deck.cues[cueIndex];
    if (cue.kind != CueKind::PowerPointShow) return false;

    if (!deckboy::platform::PowerPointPlayer::platformSupported()) {
      triggerToast(deckboy::platform::PowerPointPlayer::unsupportedMessageEn(), ToastKind::Warning, kToastReadableMs);
      return false;
    }
    std::error_code ec;
    const fs::path cuePath = deckboy::u8path(cue.path);
    if (cue.path.empty() || !fs::exists(cuePath, ec)) {
      triggerToast("PowerPoint file is missing: " + cue.name, ToastKind::Warning, kToastReadableMs);
      playUiSound(UiSoundEffect::Error);
      return false;
    }

    deckboy::platform::PptStatus st = pptPlayer_.status();
    const bool sameOpen = st.ok && st.running && pptOpenPath_ == cue.path;
    if (sameOpen) {
      st = pptPlayer_.nextBuild();
      triggerToast(st.ok ? ("PowerPoint next: " + cue.name)
                         : ("PowerPoint: " + st.error),
                   st.ok ? ToastKind::Help : ToastKind::Warning, kToastReadableMs);
      return st.ok;
    }
    if (pptOpenPath_.size()) {
      (void)pptPlayer_.closeShow();
      pptOpenPath_.clear();
    }
    deckboy::platform::PptOpenOptions opts;
    opts.path = cue.path;
    opts.startSlide = 1;
    opts.windowed = true;
    st = pptPlayer_.openShow(opts);
    if (!st.ok) {
      const std::string msg = st.code == "no_powerpoint"
                                  ? deckboy::platform::PowerPointPlayer::missingOfficeMessageEn()
                                  : st.code == "no_helper"
                                  ? deckboy::platform::PowerPointPlayer::missingHelperMessageEn()
                                  : ("PowerPoint: " + (st.error.empty() ? st.code : st.error));
      triggerToast(msg, ToastKind::Warning, kToastReadableMs);
      playUiSound(UiSoundEffect::Error);
      return false;
    }
    pptOpenPath_ = cue.path;
    deck.activeIndex = cueIndex;
    triggerToast("PowerPoint show (external window, not in program/NDI): " + cue.name);
    playUiSound(UiSoundEffect::Take);
    if (speechTimer_.config().autoStartWithPowerPoint) {
      speechTimer_.start(static_cast<std::int64_t>(SDL_GetTicks()), nowIsoLocal());
    }
    return true;
  }

  bool powerPointPrevBuild() {
    deckboy::platform::PptStatus st = pptPlayer_.prevBuild();
    triggerToast(st.ok ? "PowerPoint previous" : ("PowerPoint: " + st.error),
                 st.ok ? ToastKind::Help : ToastKind::Warning, kToastReadableMs);
    return st.ok;
  }

  bool endPowerPointShowIfOurs() {
    if (pptOpenPath_.empty()) return false;
    (void)pptPlayer_.closeShow();
    pptOpenPath_.clear();
    triggerToast("PowerPoint show closed (this presentation only)");
    return true;
  }

  void addPowerPointShowFromPath(const fs::path& document) {
    std::error_code ec;
    if (!fs::is_regular_file(document, ec)) {
      triggerToast("PowerPoint file not found", ToastKind::Warning, kToastReadableMs);
      return;
    }
    if (!deckboy::platform::isPptFilename(deckboy::u8string(document))) {
      triggerToast("Not a .ppt / .pptx / .pps / .ppsx file", ToastKind::Warning, kToastReadableMs);
      return;
    }
    Cue cue;
    cue.kind = CueKind::PowerPointShow;
    cue.path = deckboy::u8string(document);
    cue.name = deckboy::u8string(document.stem());
    cue.formatName = "powerpoint";
    cue.color = {80, 90, 150, 255};
    Deck& deck = focusedDeckMutable();
    const int at = deck.selectedIndex >= 0 ? deck.selectedIndex + 1
                                           : static_cast<int>(deck.cues.size());
    deck.cues.insert(deck.cues.begin() + at, cue);
    remapDeckCueIndices(project_.focusedDeckIndex, [at](int index) {
      return index >= at ? index + 1 : index;
    });
    deck.selectedIndex = at;
    deck.selectedIndices.clear();
    normalizeProject(project_);
    onSelectionChanged();
    markProjectDirty();
    playUiSound(UiSoundEffect::Import);
    triggerToast("PowerPoint show cue added (native playback; import-as-stills is unchanged)");
  }

  void addPowerPointShowFromDialog() {
    // filters must be static: SDL requires they live until the callback.
    static const std::vector<SDL_DialogFileFilter> kFilters = {
      {"PowerPoint", "pptx;ppt;pps;ppsx"},
      {"All files", "*"},
    };
    showOpenFileDialog(kFilters, true, [this](const std::vector<std::string>& paths) {
      for (const auto& p : paths) addPowerPointShowFromPath(deckboy::u8path(p));
    });
  }
