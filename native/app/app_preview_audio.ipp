// Part of class App — included inside the class body in main.cpp.
// Independent PROGRAM vs PREVIEW/CUE audio buses. Preview never falls back
// onto the program / audience device, and never feeds the record/stream/NDI
// tap (previewMediaEngine_ is constructed with an empty AudioTapCallback).

  std::vector<std::string> livePlaybackDeviceNames() const {
    std::vector<std::string> names;
    int count = 0;
    if (SDL_AudioDeviceID* ids = SDL_GetAudioPlaybackDevices(&count)) {
      for (int i = 0; i < count; ++i) {
        if (const char* n = SDL_GetAudioDeviceName(ids[i])) names.emplace_back(n);
      }
      SDL_free(ids);
    }
    return names;
  }

  std::string primaryProgramAudioDeviceName() const {
    if (project_.decks.empty()) return {};
    return trim(project_.decks.front().audioOutputDeviceName);
  }

  void closePreviewAudioStream() {
    if (previewMediaEngine_) {
      previewMediaEngine_->detachAudioDevice();
      previewMediaEngine_->setMasterGain(0.0f);
    }
    if (previewAudioStream_) {
      SDL_DestroyAudioStream(previewAudioStream_);
      previewAudioStream_ = nullptr;
    }
    previewAudioDeviceInUse_.clear();
  }

  void stopCuePreview(bool announce = false) {
    if (previewMediaEngine_) {
      previewMediaEngine_->stopAll();
    }
    cuePreviewCueId_.clear();
    cuePreviewActive_ = false;
    if (announce) triggerToast("cue preview stopped");
  }

  PreviewDeviceStatus ensurePreviewAudioDevice() {
    std::string resolved, message;
    const auto st = PreviewAudioPolicy::resolvePreviewDevice(
        project_.previewAudioDeviceName,
        livePlaybackDeviceNames(),
        primaryProgramAudioDeviceName(),
        &resolved,
        &message);
    if (st != PreviewDeviceStatus::Open) {
      closePreviewAudioStream();
      if (st == PreviewDeviceStatus::MissingMuted && !previewMissingWarned_) {
        previewMissingWarned_ = true;
        triggerToast(message, ToastKind::Warning, kToastReadableMs);
      }
      if (st == PreviewDeviceStatus::Idle) previewMissingWarned_ = false;
      return st;
    }
    previewMissingWarned_ = false;
    if (previewAudioStream_ && previewAudioDeviceInUse_ == resolved) {
      return st;
    }
    closePreviewAudioStream();
    applyAudioBufferSizeHint();
    SDL_AudioSpec desired {};
    desired.freq = kAudioRate;
    desired.format = kAudioFormat;
    desired.channels = kAudioChannels;
    const SDL_AudioDeviceID id = audioPlaybackDeviceIdForName(resolved);
    if (id == 0 || id == SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK) {
      // Name resolved in the live list but SDL would only give us the default
      // — refuse, rather than risk the HDMI / program device.
      triggerToast("Preview audio device could not be opened without using "
                   "the default (program) device. Preview stays muted.",
                   ToastKind::Warning, kToastReadableMs);
      return PreviewDeviceStatus::MissingMuted;
    }
    previewAudioStream_ = SDL_OpenAudioDeviceStream(id, &desired, nullptr, nullptr);
    if (!previewAudioStream_) {
      triggerToast("Preview audio device failed to open. Preview stays muted "
                   "and will not fall back to the program device.",
                   ToastKind::Warning, kToastReadableMs);
      return PreviewDeviceStatus::MissingMuted;
    }
    previewAudioDeviceInUse_ = resolved;
    if (previewMediaEngine_) {
      previewMediaEngine_->setAudioDevice(previewAudioStream_, kAudioChannels);
      previewMediaEngine_->setMasterGain(project_.previewMuted ? 0.0f : project_.previewVolume);
    }
    deckboySetAudioPaused(previewAudioStream_, false);
    return st;
  }

  void applyPreviewAndProgramGains() {
    const float program = std::clamp(
        static_cast<float>(project_.masterVolume) * project_.programBusVolume,
        0.0f, 2.0f);
    for (int deckIndex = 0; deckIndex < static_cast<int>(project_.decks.size()); ++deckIndex) {
      if (MediaEngine* engine = mediaEngineForDeck(deckIndex)) {
        engine->setMasterGain(program);
      }
    }
    if (previewMediaEngine_) {
      previewMediaEngine_->setMasterGain(
          (project_.previewMuted || !cuePreviewActive_) ? 0.0f
                                                        : std::clamp(project_.previewVolume, 0.0f, 2.0f));
    }
  }

  void reconcilePreviewAudioDevice() {
    if (project_.previewAudioDeviceName.empty() && !cuePreviewActive_) {
      closePreviewAudioStream();
      previewMissingWarned_ = false;
      return;
    }
    if (cuePreviewActive_ || !project_.previewAudioDeviceName.empty()) {
      const auto st = ensurePreviewAudioDevice();
      if (st != PreviewDeviceStatus::Open && cuePreviewActive_) {
        // Device vanished mid-preview: mute, do not jump to HDMI.
        if (previewMediaEngine_) previewMediaEngine_->setMasterGain(0.0f);
      }
    }
    applyPreviewAndProgramGains();
  }

  std::string startCuePreviewOfSelected() {
    const int deckIndex = project_.focusedDeckIndex;
    if (deckIndex < 0 || deckIndex >= static_cast<int>(project_.decks.size())) {
      return "no deck";
    }
    Deck& deck = project_.decks[deckIndex];
    if (deck.selectedIndex < 0 || deck.selectedIndex >= static_cast<int>(deck.cues.size())) {
      return "select a cue first";
    }
    const Cue& cue = deck.cues[deck.selectedIndex];
    if (cue.kind == CueKind::PowerPointShow) {
      triggerToast("PowerPoint audio is played by PowerPoint itself, not by "
                   "Deckboy's preview bus.",
                   ToastKind::Warning, kToastReadableMs);
      return "powerpoint not on preview bus";
    }
    if (cue.kind != CueKind::Video && cue.kind != CueKind::Audio &&
        cue.kind != CueKind::Tone) {
      triggerToast("cue preview plays video, audio or tone cues only");
      return "unsupported kind";
    }
    if (ensurePreviewAudioDevice() != PreviewDeviceStatus::Open) {
      return "preview device not open";
    }
    if (!previewMediaEngine_) {
      return "no preview engine";
    }
    // Hold this deck's ON-AIR engine alone: load a second engine, do not take.
    previewMediaEngine_->stopAll();
    previewMediaEngine_->setAudioDevice(previewAudioStream_, kAudioChannels);
    previewMediaEngine_->loadCue(&cue, true);
    previewMediaEngine_->setMasterGain(project_.previewMuted ? 0.0f : project_.previewVolume);
    cuePreviewCueId_ = cue.id;
    cuePreviewActive_ = true;
    const std::string msg = "cue preview (not on program / record / stream): " + cue.name;
    triggerToast(msg);
    return msg;
  }

  void stopCuePreviewIfTaking(const std::string& takenCueId) {
    if (!cuePreviewActive_) return;
    if (PreviewAudioPolicy::shouldStopPreviewOnTake(cuePreviewCueId_, takenCueId)) {
      stopCuePreview(false);
      triggerToast("cue preview stopped — same item is going to program");
    }
  }

  void toggleCuePreview() {
    if (cuePreviewActive_) stopCuePreview(true);
    else startCuePreviewOfSelected();
  }

  bool handlePreviewAudioSettingsClick(const SettingsButton& sb) {
    if (sb.action == kSettingsActionPreviewAudioDevice) {
      std::vector<std::pair<std::string, std::string>> choices;
      choices.emplace_back("", "(no preview device)");
      for (const auto& option : audioOutputDeviceDropdownChoices()) {
        if (!option.first.empty()) {
          choices.push_back(option);
        }
      }
      openDropdown("settings.preview_device", sb.rect, choices,
                   project_.previewAudioDeviceName,
                   [this](const std::string& value) {
                     project_.previewAudioDeviceName = value;
                     previewMissingWarned_ = false;
                     markProjectDirty();
                     reconcilePreviewAudioDevice();
                     triggerToast(value.empty()
                       ? std::string("preview audio off (stays silent, no HDMI fallback)")
                       : ("preview audio on " + value));
                   });
      return true;
    }
    if (sb.action == kSettingsActionPreviewMute) {
      project_.previewMuted = !project_.previewMuted;
      applyPreviewAndProgramGains();
      markProjectDirty();
      triggerToast(project_.previewMuted ? "preview muted" : "preview unmuted");
      return true;
    }
    if (sb.action == kSettingsActionPreviewVolumeDec) {
      project_.previewVolume = std::clamp(project_.previewVolume - 0.05f, 0.0f, 2.0f);
      applyPreviewAndProgramGains();
      markProjectDirty();
      return true;
    }
    if (sb.action == kSettingsActionPreviewVolumeInc) {
      project_.previewVolume = std::clamp(project_.previewVolume + 0.05f, 0.0f, 2.0f);
      applyPreviewAndProgramGains();
      markProjectDirty();
      return true;
    }
    if (sb.action == kSettingsActionProgramBusVolumeDec) {
      project_.programBusVolume = std::clamp(project_.programBusVolume - 0.05f, 0.0f, 2.0f);
      applyPreviewAndProgramGains();
      markProjectDirty();
      return true;
    }
    if (sb.action == kSettingsActionProgramBusVolumeInc) {
      project_.programBusVolume = std::clamp(project_.programBusVolume + 0.05f, 0.0f, 2.0f);
      applyPreviewAndProgramGains();
      markProjectDirty();
      return true;
    }
    if (sb.action == kSettingsActionCuePreviewToggle) {
      toggleCuePreview();
      return true;
    }
    return false;
  }
