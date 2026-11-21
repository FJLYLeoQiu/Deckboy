# Native PowerPoint, speech timer, and dual-bus audio

This is an unofficial extension of Deckboy on the `feature/powerpoint-native-playback` branch. It does not change the default branch. English remains the default interface language.

## 1. Native PowerPoint playback

**What it is.** On Windows, with a licensed desktop Microsoft PowerPoint installed, a *PowerPoint Show* cue drives PowerPoint’s own slideshow through `deckboy-ppt-helper` (late-bound COM, dedicated STA process). Next / previous use `SlideShowView.Next` / `Previous`, so animation **builds** advance, not only whole slides. TAKE on a show that is already open advances a build; TAKE on a different file closes only the presentation this feature opened and starts the new one.

**What it is not.** The slideshow lives in a PowerPoint window. v1 does **not** composite that picture into Deckboy’s programme, NDI, recording, or stream. A window-capture cue can still be used as a separate, existing workflow if you need the show inside the programme picture — that path is not automatic here.

**Environment.** Windows + desktop Microsoft PowerPoint. WPS, PowerPoint for the web, and LibreOffice are not used for this path. Other platforms show a clear message and can still use the existing “import as stills” (PDF flatten) path, which drops builds and transitions.

**Safety.** `AutomationSecurity` is forced to disable macros. Protected View is not enabled, disabled, or bypassed. If the file opens in Protected View, Deckboy reports that and leaves it alone. Only the presentation this feature opened is closed. The helper never kills `POWERPNT.EXE`. `Application.Quit` is used only if PowerPoint was not already running and no other presentations remain.

**PowerPoint audio / video.** Sound and video inside the PowerPoint process do **not** pass through Deckboy’s mixer. Programme / preview device routing, bus faders, mute, record and stream taps do not apply to them. There is no supported public API used here to redirect that audio into Deckboy’s buses. If the audience is watching PowerPoint’s own fullscreen window, a Deckboy speech-timer overlay on the Deckboy programme output will **not** appear on that PowerPoint window. Coverage of PowerPoint exclusive fullscreen and multi-screen placement has **not** been verified on real hardware and is not guaranteed.

**How to add a cue.** Desk menu: *Add cue → PowerPoint Show (native, Windows + desktop PowerPoint)*. Existing drag-and-drop / “import slides” still flattens to stills and is unchanged.

**Tests.** `deckboy-powerpoint-protocol-check` covers the helper protocol without Office. `--self-test` on the helper (Windows) exercises the protocol only. A runner or this Linux box without Office is **not** a real PowerPoint verification.

## 2. Speech timer

**Clock.** Custom duration (default 10 minutes). Default reminders: 2 minutes left, 1 minute left, and time-up — all editable. Each reminder can use a built-in sound or a local audio file (WAV, or anything bundled ffmpeg can decode), and can be auditioned on the desk. You can add or remove reminders (up to eight). Start / pause / resume / reset. At zero the clock continues as overtime (`+mm:ss` or `+hh:mm:ss`). Pause is excluded from used / overtime. The clock is monotonic. Each reminder fires once per speech; if several are crossed in one tick, only the most urgent sound plays. Reset does not write a session row; *New speech* does. Sessions can be exported as CSV.

**Display vs settings.** The settings / control panel lives only on the **desk (control window)**. The countdown display shows only the time, the state (including near-end and overtime, not colour-only), and the current reminder text. No settings buttons, no file paths, and no control panel are drawn on the audience display.

**Display target (defaults to Audience).**

- **Audience (default):** the overlay is composited into Deckboy **programme** outputs (`window` and `stream`). The programme window, and the recording / stream / NDI picture taken from that same compositor frame, agree. It is not drawn on presenter / prompter / multiview outputs.
- **Speaker:** the overlay is composited into **presenter** outputs only, not into programme / NDI / record of the programme.
- The overlay can be dragged, and a corner of the countdown can be dragged to resize it (no settings chrome is drawn on the audience display). On the desk you can type a width and height in pixels (for example `520 200`) or use the W/H buttons. Layout lock and reset-position are also on the desk. Geometry is persisted. If a display is gone on restore, the fallback window is clamped onto a visible display.

**Fallback window.** If no matching programme or presenter output exists, an optional desktop utility window can show the same display-only countdown. That window is **not** in NDI, recording, or the programme stream. A full-desktop OS capture can still record it; that is an OS capture, not Deckboy compositing.

**Timer vs PowerPoint.** An optional auto-start when a PowerPoint show opens never stops or interrupts the slideshow when the clock hits zero.

**Reminder audio** is a **separate** setting from the visual target. The default route is **Control** (desk / UI path). It is not automatically mixed onto the programme / live / record bus just because the visual default is the audience window. You may switch the route to Audience explicitly; that still uses the desk sound path and may share a physical device with programme if the machine’s default output is the PA — do not assume a second card. File sounds that are not simple WAV may not audition.

## 3. Programme vs preview (cue-listen) audio

**Already in Deckboy, kept:** each playlist has a programme device (`audioOutputDeviceName`) and optional extra sends; a **monitor** device can hear a copy of one playing playlist and is never silently swapped for another device; AUDITION / PFL still holds a deck off the output and is **not** the same as cue-preview (audition replaces that deck’s on-air picture).

**What this extension adds.** A second, independent **preview / cue-listen bus**:

- Configure a **preview audio device** and its own **volume / mute**. Changing preview volume does not change programme volume.
- **Cue preview** plays the *selected* video / audio / tone cue on a **second engine** (`previewMediaEngine_`) while the on-air engine keeps playing the current formal item. It does **not** call TAKE and does not black the programme.
- Preview PCM is opened only on the named preview device. The preview engine is constructed with an **empty audio tap**, so those samples are **not** fed to recording, stream, or NDI (those taps come from programme engines).
- If the named preview device is missing or cannot be opened without using the system default, preview is **muted** and the operator is told. It does **not** fall back to the programme / HDMI / default device.
- TAKE of the same cue stops the preview first so the same file is not heard on both buses (and to avoid a double-play pop).
- Programme AV sync stays on the existing programme clock. Preview is a separate SDL device; sample-rate conversion is whatever SDL already does per device. **Cross-device sync is not guaranteed.** Bluetooth often adds latency; a hands-free (HSP/HFP) profile may be mono, low-bitrate, or unusable for programme-quality listen. Prefer an A2DP / stereo headset profile when the OS offers one.

**Program bus fader.** `programBusVolume` (default 1.0) multiplies the existing master volume on programme engines only. Preview uses `previewVolume` only.

**Not covered.** Native PowerPoint audio (see above). ASIO programme sound on the driver is an existing engine limit and is not extended here.

## 4. Automated tests vs hardware

The following run without Office, HDMI, or Bluetooth:

- `deckboy-speech-timer-check` — defaults (audience display, control reminder route, 10 minutes, three reminders), pause exclusion, reset vs new speech, overtime text, one-shot / non-stacking reminders, persist, clamp.
- `deckboy-preview-audio-check` — preview device missing stays muted and does not resolve to the programme device; program and preview mixes stay isolated; preview gain does not change program samples; TAKE of the same cue stops preview; reminder route default is control.
- `deckboy-powerpoint-protocol-check` — helper protocol encode/decode, filename helpers.

**Not done on this machine, and not claimed:** real Microsoft PowerPoint; a Deckboy overlay covering PowerPoint exclusive fullscreen; real HDMI programme + Bluetooth preview listen at the same time; measuring Bluetooth delay; confirming NDI/record of a live show with the overlay; Windows installer smoke on a physical PC. Those are for the operator’s own final check.

## 5. Desk usage (short)

1. Open *SPEECH TIMER* on the control window. Set duration, display target (Audience by default, or Speaker), lock / reset position, reminder mute and reminder audio route. Start / pause / reset / new speech.
2. Settings → Audio: set the playlist’s programme device and the new **preview audio device**. Use *Cue preview* (speech-timer panel or the same action) to listen to the selected next item on the preview device while the current item stays on programme.
3. Add a PowerPoint Show cue on Windows, TAKE to open, TAKE again for the next build, previous for a build back, stop/close to end only this presentation.
