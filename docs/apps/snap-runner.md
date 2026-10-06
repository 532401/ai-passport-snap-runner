[简体中文](snap-runner.zh_CN.md) · **English**

# Snap Runner

An offline, portrait 240 × 320 runner for AI Passport. A mint pixel creature runs
through a scrolling night landscape. The original BSP is reused; no demo menu,
page or visual shell is linked into the application. The public-domain Musopen
recording of Grieg’s *In the Hall of the Mountain King* drives a fixed music level.
No network is needed. Physical buttons navigate; microphone transients trigger jumps.

## Accepted behavior

- First use: home, three loud snaps, confirmation, three soft snaps, practice,
  then a new run. Keep the device and hand at the same distance throughout.
- Max is the minimum **linear peak amplitude** of the three loud samples. Min is
  the arithmetic mean of the three soft amplitudes. A valid range requires
  Max/Min ≥ 1.6. Clipped samples and noisy-room samples are rejected; cancel or
  failure preserves the previously saved calibration.
- The logarithmic interval between Min and Max is divided into ten equal bins.
  Recognized snaps below Min use level 1; those at/above Max use level 10.
  Detection remains a separate noise gate: not every quiet sound is a command.
- Ten jump heights span 18–90 logical pixels. Gravity is 800 px/s². No double
  jump or queued air jump. Low jumps land sooner; higher jumps clear wider/taller
  obstacles. There are no ceilings or explicit penalties for using maximum power.
- Three lives. A rock collision or unsupported landing in a pit costs one life.
  The struck hazard is removed to create a safe recovery area; 1.5 seconds of
  invulnerability prevents cascading damage. The third hit ends the run.
- Distance follows map movement, with 10 pixels represented as one game metre.
  Speed is fixed at 105 px/s. The 154.091-second recording has 39 fixed takeoff
  cues, at least 2.5 seconds apart. Each pit/rock centre arrives 280 ms after its
  takeoff cue. Cues follow reviewed 4/4 downbeats with whole-bar skips; not every
  beat requires a snap. The coda adds four independent accent cues, including the
  final closing hit, with the same 2.5-second minimum gap.
  Surviving the song wins.
- A shrinking marker near the player indicates an approaching cue; the progress
  bar shows song position. UP/DOWN adjust speaker volume from 0 to 50 (default 30).
  Practice and calibration are silent. Pause freezes both music and map; restart
  starts both at zero. The same recording always produces the same route.
- Calibration and ended-run records use NVS namespace `snap_runner`, version-2
  `rhythm_profile`. The new microphone filter requires fresh calibration; the old
  `profile` is left intact but not reused. NVS failure is shown without an erase.

## Controls

| Screen | UP | DOWN | OK |
| --- | --- | --- | --- |
| Home with calibration | Practice | Recalibrate | Start run |
| Home without calibration | Begin calibration | Begin calibration | Begin calibration |
| Loud/soft capture | Cancel to home | No action | No action |
| Between capture stages | Cancel | No action | Begin soft capture |
| Calibration rejected | Cancel | No action | Retry |
| Practice | Home | Recalibrate | New scored run |
| Run | Volume +5 | Volume −5 | Pause |
| Paused | No action | Home | Resume if mic ready |
| Game over | Home | Recalibrate | New run |

Press events are used once per press. A 300 ms key guard and timestamp checks
reject button noise and stale microphone events across transitions. Capture has
an initial one-second quiet interval and a 700 ms spacing guard. A saved profile
can start a run directly; practice is always available from home.

## Audio and task ownership

One audio worker owns both BSP input and output, including codec sleep/wake.
It writes and reads 80-sample blocks (5 ms) of 16 kHz, 16-bit mono audio. The
4,930,912-byte PCM track lives in Flash; only small blocks are copied into RAM.
The reference 96 kbps MP3 is not embedded and no decoder is needed. See the
[asset provenance and conversion](../../assets/README.md#mountain-king-music).

Game volume is linear amplitude: 0 mutes, 30 is 60%, and 50 is unity gain,
matching the browser player. The application converts this to codec-dev's
logarithmic volume curve before calling the BSP; passing 30 directly would
attenuate by 35 dB and make this recording's quiet opening nearly inaudible.
Volume changes log both game and codec values with the song position. This
volume conversion does not change PCM timing or beat timestamps. Audible output and speaker feedback at the
corrected level still require device listening tests.

For silent-output diagnosis, the audio owner reads the ES8311 clock, DAC
interface/power/mute/volume and routing registers when enabling audible volume.
While playing it logs PCM peak, submitted samples, maximum I/O time and stack
headroom every two seconds. These distinguish silent buffers, stalled transport
and codec settings; they cannot prove analog speaker output. Register reads are
read-only and report errors without substituting cached values.

`CONFIG_SNAP_BOOT_AUDIO_PROBE` is off by default. A diagnostic build can enable
it to play two 500 ms, 1 kHz tones before the UI starts: output alone, then duplex
record/playback, at codec volume 80. Both use the BSP and leave saved settings
intact. The user heard both tones on the board; that establishes these two
output paths work, not that the quiet recording or full gameplay is audible.

The soundtrack has an offline fourth-order 1.5 kHz low-pass filter and smooth
gain normalization: the quiet opening gains about 16 dB, with the loud ending
attenuated to retain peak headroom. Sample positions and cue times are unchanged.
Calibration
and gameplay microphone input share a fourth-order 3.2 kHz high-pass filter.
This separates much of the playback energy from snap transients; it is not echo
cancellation. Speaker distortion, acoustic coupling, human snap accuracy and
latency still require testing at actual volume, distance and room conditions.
The detector retains its 800 ms warmup, onset gate, 30 ms transient window,
high-frequency/decay checks, 180 ms cooldown and quiet rearm. Claps and taps may
still trigger it. No microphone recording is retained.

`tools/snap_music_chart.py` generates the fixed chart from a recording-specific
beat grid checked against 4/4 score structure. See [rhythm analysis](snap-rhythm.md)
for measured tempo sections, half-time correction, exact timestamps and audition.
Generation and `--check` require only Python's standard library. Checks bind the
PCM, beat-grid hash, JSON and C header and reject off-downbeat or unsafe cues.

The application owns the session, NVS and UI under `bsp_lvgl_lock()`; callbacks
only enqueue inputs. Scored physics uses 10 ms steps driven by the submitted
sample count minus `CONFIG_SNAP_OUTPUT_DELAY_MS` (default 90 ms, nominal BSP
DMA queue capacity, not a measured speaker timestamp). Stop/resume changes the
transport generation, mutes and fills the output queue, discards queued capture
through `bsp_audio_discard_input()`, then resumes from the paused game cursor.
Discarding stale input avoids carrying output-priming delay into snap detection. Audio I/O failure, an audible producer stall over 40 ms,
a clock regression or an application catch-up over 250 ms pauses the run.
Actual audible phase and full-duplex stability need device calibration. NVS writes
wait until playback has acknowledged stopping. Practice uses the wall clock.

Idle menus dim after 60 seconds. At 180 seconds the microphone worker stops
PCM reads and suspends the codec, then acknowledges before the application turns
off rendering and enters 100 ms timer-woken light-sleep slices with 30 ms button
scan windows. A held/pressed button wakes the display and requests codec resume
and a fresh ambient estimate. No GPIO wake source is invented for the ADC ladder.
Sleep/wake behavior and idle current still require device measurement.

## Fonts and validation

Noto Sans SC Medium (weight 500) is subset to 16 and 22 px, 4 bpp, uncompressed constant data in Flash.
See [assets](../../assets/README.md) and `tools/snap_fonts.py`. Every fixed UI
character plus printable ASCII is included. The application checks every glyph
against both actual LVGL fonts at boot; placeholders remain enabled.

```sh
python3 tools/snap_fonts.py --check
./tools/validate.sh --static
# Activate ESP-IDF 5.5.3 first:
./tools/validate.sh
```

For the real LVGL desktop rendering/coverage probe (after managed components
have been resolved by an IDF build):

```sh
cmake -S tests/snap_preview -B build/preview-host -DCMAKE_BUILD_TYPE=Release
cmake --build build/preview-host -j 6
mkdir -p build/preview
build/preview-host/snap_preview build/preview
```

This produces PPM captures of every page and error samples, checks label bounds
and both fonts' actual glyph descriptors, then renders 300 game frames using a
24 KB LVGL pool. It does not emulate microphone hardware or validate LCD timing.
Application-level audio stop/acknowledgement, actual sleep/wake and NVS power-loss
behavior remain device checks; existing BSP fault tests cover codec failures.

The host suite covers calibration, all ten bins, jump arcs, collision/recovery,
pause, completion, high scores, transport priming/seek/failure/EOF and clock
regression/stall handling. Complete-chart runs at levels 3–10 pass without damage
with alternating early/on-time/late inputs (−80/0/+80 ms). No-input runs lose all
three lives. A filtered music-only fixture produces zero detections; adding 51
synthetic snaps produces 51 detections (the test requires at least 95% recall
and no extra detections). This is not a human-snap accuracy measurement. Shared
gates also check BSP faults, fonts, chart integrity and merged firmware layout.

## Device acceptance and delivery

Build/host results and exact artifact identities are reported at handoff. Device
tests remain NOT RUN until authorized flashing and observed acceptance. Verify:

1. Every Chinese page and error state, clipped labels, battery fallback, and keys.
2. Quiet-room calibration with three hard and three soft snaps; reject clipping,
   ambient noise and insufficient range. Verify saved calibration after restart.
3. Practice all ten levels at a consistent distance; observe latency, weak-snap
   misses and false triggers from speech, claps, taps and button presses.
4. Complete runs with pits and rocks, safe recovery, three-hit game over, pause,
   restart, new-record persistence, and microphone failure behavior. Check all music
   cues, song completion, pause/resume phase, and speaker feedback at several volumes.
5. Sustained capture/rendering: free/minimum heap, largest block, task stacks,
   frame smoothness, idle brightness and battery drain.

Deliver only the verified merged `build/FoloToy-AI-Passport-full.bin` at offset
`0x0` with its matching content-addressed ELF/MAP archive. This writes padding
across NVS and may reset existing data, including calibration and records.
Flashing requires explicit approval; building never opens or writes a USB port.

## Initial validation checkpoint — 2026-10-03

- Branch: `feature/custom-passport-app`, baseline `0b9e4c81`; implementation remains uncommitted.
- Environment: macOS arm64, ESP-IDF v5.5.3, official Espressif source mirror and tool downloads; previous installations retained.
- Build: PASS — complete `./tools/validate.sh` gate, merged-image/layout checks and independent archive verification.
- Host tests: PASS — existing suite, new runner suite, AddressSanitizer/UndefinedBehaviorSanitizer run, fixed-font inventory and actual LVGL glyph/layout probe.
- Device tests: NOT RUN — read-only enumeration found no AI Passport USB port; nothing flashed.
- Two-axis review: Standards and Spec findings resolved and re-reviewed with GPT-6 Astra / Medium.
- Desktop preview: all nine pages plus three error samples; 300 rendered frames; measured LVGL allocator use 12,032 bytes with 9,208 bytes free. This is a host measurement, not board heap data.
- Linker: static D/IRAM 109,198 bytes, 212,098 bytes remaining before runtime allocations. Actual heap/stack headroom requires device logs.
- Merged image: 837,216 bytes; application: 771,680 bytes, within its 8,323,072-byte factory partition.
- Unverified: human-snap accuracy and latency, LCD glyph appearance/frame rate, keys, NVS across power loss, capture-stop acknowledgement, sleep/wake and battery current.

Exact merged image: `build/firmware/47505de2c858fa3478e418905caccf76cf4459ca3884002f5e2d4d9ed2290fad/FoloToy-AI-Passport-full.bin`. Matching ELF/MAP and `manifest.json` are in that same directory; the convenience copy now points to the latest candidate below.

```text
full_bin_sha256: 47505de2c858fa3478e418905caccf76cf4459ca3884002f5e2d4d9ed2290fad
app_elf_sha256:  16945f5d935b864e369685cdcad95cc84d592b8415834427d88fd24602f333d4
```

## First device trial — watchdog regression

The original `47505de2...2290fad` image was flashed with user approval on
2026-10-03 to the sole USB-Serial/JTAG ESP32-C3 (revision v1.1, embedded 8 MB
Flash). Write and readback hash verification passed. The boot log confirmed ELF
`16945f5d9...`. Startup reported 216,484 free heap bytes and an application stack
watermark of 3,888 bytes.

Device log health: **FAIL**. An IDLE task watchdog warning recurred at about
5 and 10 seconds after a controlled restart. Symbolication with the exact ELF
located samples in the transient filter and LVGL's busy-wait for LCD flush.
This does not prove which sample consumed most CPU. No visual or human-snap
acceptance was confirmed. Raw logs are retained locally under `build/device-test/`.

The candidate repair removes continuous static-screen repainting, caps game
rendering at approximately 30 fps (menu updates at 10 Hz), and makes the control
loop block for at least 5 ms even after an overrun instead of continuously
catching up to an expired periodic deadline. The real-LVGL static-home flush
regression failed before the change and passed after it; the scheduler bounds
and existing physics/detection tests pass. No watchdog configuration is relaxed.
This addresses evidenced sources of excess CPU load; watchdog resolution still
requires a fresh authorized device trial.

A bounded repeatable health check is available after authorization to restart
the selected board (run using ESP-IDF Python):

```sh
python tools/snap_device_log.py --port /dev/cu.usbmodem101 --reset --seconds 15 --output build/device-test/repro.log
```

It detects watchdog/panic/assert/brownout logs, confirms startup when resetting,
closes the port, and never flashes. A PASS applies only to the observed window.

## Watchdog repair candidate — 2026-10-03

- Build: PASS — complete `./tools/validate.sh`, merged layout and independent debug-archive verification.
- Host tests: PASS — runner/BSP tests, static-home flush regression, scheduler bounds, both fonts and all page layouts, and 300 rendered frames.
- Device tests: PASS for verified flashing and a 45-second startup-log window only. The repair image replaced the original failing image with user approval on the same USB device. Visual and gameplay acceptance remain pending.
- Unverified: sustained gameplay/watchdog stability beyond the observed startup window, real snap detection, LCD/buttons, persistence, sleep/wake and current consumption.
- Merged image: 837,392 bytes; application: 771,856 bytes. Flash offset: `0x0`; merged padding overwrites NVS, resetting calibration and records.
- Matching ELF, MAP, components and manifest: `build/firmware/2cde0aabc8809671ee8eeff657c81d60ea60572ecf26f9fb77e1894740b92382/`.
- The convenience copy now points to the latest density increment below.

```text
full_bin_sha256: 2cde0aabc8809671ee8eeff657c81d60ea60572ecf26f9fb77e1894740b92382
app_elf_sha256:  c38bcbfe26565f0a6372bcc47971a7196f93bf9e346b86434f8f4d202b6b774c
```

The exact candidate was flashed at `0x0` on `/dev/cu.usbmodem101` after renewed
user approval. Esptool verified the written data hash. A controlled restart
confirmed ELF `c38bcbfe2...`; the following 45-second serial window contained no
watchdog, panic, assertion or brownout diagnostics. Startup free heap was
217,068 bytes, largest free block 114,688 bytes, and application stack watermark
3,936 bytes. The monitor closed after the bounded observation. Raw local logs:
`build/device-test/repair-flash.log` and `build/device-test/repair-startup.log`.
This does not establish physical screen quality or human-snap accuracy.

## Progressive density increment — 2026-10-03

- Requirement: increase pit/rock density with distance without unavoidable damage from insufficient spacing.
- Implementation: world-position-based 1,000-m ramp, capped random gap range and a maximum-speed safety floor; existing visible hazards retain their spacing.
- Build: PASS — complete `./tools/validate.sh` and independent archive verification.
- Host tests: PASS — density regression failed on the previous implementation and passed after the change; 120 seeded streams, 144 minimum-gap size/kind/speed/timing combinations, AddressSanitizer and UndefinedBehaviorSanitizer.
- Review: no actionable findings in Standards or Spec review.
- Device tests: PASS for this image's verified flash and 45-second startup-log window only. Physical progression and gameplay acceptance remain pending.
- Unverified: actual progression feel, human snap response and sustained device gameplay/watchdog stability.
- Merged image: 837,568 bytes, application: 772,032 bytes. Verified for `0x0`; merged flashing resets NVS calibration and records.
- Exact bundle: `build/firmware/3388102f776e1bd0c1b872f29d00d7e153edab47c257ac6612d2aaecff66e1a9/`, containing the merged image and matching ELF/MAP/manifest.
- Convenience copy: `build/FoloToy-AI-Passport-full.bin` (this increment). Flashed with user approval.

```text
full_bin_sha256: 3388102f776e1bd0c1b872f29d00d7e153edab47c257ac6612d2aaecff66e1a9
app_elf_sha256:  ce5ccc2ff285e1a54d70900c4f53a4b7eb4fdda22eba023cf6042457ecd6ba8f
```

The same USB device reappeared as `/dev/cu.usbmodem1101` and was identified by
its stable USB serial before flashing the approved density image at `0x0`.
Esptool verified the written hash. A controlled restart confirmed ELF
`ce5ccc2ff...`; a 45-second log window showed no watchdog, panic, assertion or
brownout diagnostics. Startup free heap was 217,068 bytes, largest free block
114,688 bytes, and application stack watermark 3,936 bytes. The serial port was
closed after observation. Logs remain local in `build/device-test/density-flash.log`
and `build/device-test/density-startup.log`. The user was asked to recalibrate
and try the density progression; no physical gameplay result has been reported.

## Desktop simulator and USB debug workbench

Run `python3 tools/snap_debug.py` and open the printed
`http://127.0.0.1:8765` address. The launcher builds a native shared library with
CMake; it needs a C compiler, CMake, Python 3.9+, and the resolved
`managed_components/lvgl__lvgl` dependency. On Mac it can find the existing
ESP-IDF CMake installation. On a fresh clone, build firmware once to resolve LVGL.
The server listens on loopback only and uses no cloud service. Stop with `Ctrl+C`;
use `--http-port 8766` if the default port is busy. macOS is verified; Linux paths
are supported but not tested. The launcher does not currently support Windows.

### Without a device

- Desktop mode compiles the real `snap_model`, `snap_session`, `snap_ui`, and both
  Chinese fonts. It does not duplicate game rules in JavaScript. Frames come from
  the LVGL RGB565 framebuffer.
- Arrow up/down and Enter correspond to physical keys. Space injects a snap at
  the selected strength; digits 1-9 and 0 inject levels 1-10. Keys keep their normal
  editing behavior when a form input is focused.
- Freeze stops the simulation clock. Advance by 10 or 100 ms to inspect takeoff,
  collisions and landing. The game's OK pause remains independent of debug freeze.
- Enable music sync, then press OK: browser playback time drives the real C model.
  Pause/freeze/exit stop playback. The desktop bridge accepts up to 1.5 seconds
  between clock positions and advances every physics step; a larger jump or
  three seconds without a clock update pauses safely. This accommodates browser
  timer throttling without changing the firmware’s 250 ms limit. Resume acquires
  media playback permission directly from the click/keypress, and transport
  epochs reject requests left over from a pause/reset. With music disabled,
  silent wall-clock simulation and manual stepping remain available.
- Reset and press OK to replay the fixed chart. The run ID does not change the map.
  Identical input timing produces identical state. The default simulated calibration is
  Min 1000 / Max 10000. Uncheck simulated calibration before reset to exercise the
  three-strong/three-soft workflow. Expand exceptional inputs to inject an amplitude,
  clipping or noisy-room flag.
- The yellow box shows player geometry, and the yellow point marks the foot centre
  used for pits. Rocks check horizontal overlap and foot height; pits check ground
  support at the centre. The panel shows edge gaps, speed, height, lives and input results.
- Simulation state stays in RAM. Reset never touches device calibration or records.
  This injects already-detected events, does not capture the computer microphone,
  and cannot validate real sound classification. Recorded microphone WAV injection is not implemented. Soundtrack playback is supported.

### Observing a device

After authorized flashing of the new telemetry-enabled firmware, activate the
ESP-IDF Python environment:

```sh
python tools/snap_debug.py --list-ports
python tools/snap_debug.py --port /dev/cu.usbmodemXXXX
# Optional: save parsed diagnostic records to a new file, never overwrite one.
python tools/snap_debug.py --port /dev/cu.usbmodemXXXX --record build/debug/session.jsonl
```

Use the actual enumerated port. USB mode requires `pyserial`, available in ESP-IDF
Python. The launcher never auto-selects a port, sends control commands, requests a
reset, or flashes. It deasserts DTR/RTS before opening; OS/driver opening transients
still need physical verification. Close any monitor holding the port first.
Disconnects retain the last values with a stale-data indication; reconnect and
restart the tool. Without hardware, the demo button exercises charts with explicitly
labelled synthetic data. Demo mode cannot run alongside a real serial connection.

Firmware enables `CONFIG_SNAP_DEBUG_TELEMETRY` by default. Disable it in the Snap
Runner menuconfig menu for a quiet build. The delivery gate reads
`sdkconfig.defaults`, so explicitly disable it there to deliver that configuration.
Version 1 JSON lines follow the `@snap ` prefix on the existing USB console:

| Type | Rate / trigger | Data |
| --- | --- | --- |
| `audio` | About 10 Hz | Maximum peak over 100 ms, final-frame RMS, noise floor, detector stage |
| `detect` | Each complete candidate window | Accepted transient, insufficient decay or high-frequency content, window peak |
| `input` | Detected event reaches application | Jump, airborne ignore, calibration sample, clipping/noise rejection, guard or stale input |
| `state` | About 5 Hz; stops during sleep | Page, calibration, level, seed, distance, lives, height and hazards |

Timestamps are device uptime in milliseconds. Audio/application tasks enqueue
without waiting into an eight-record diagnostic queue. A separate low-priority
writer formats and sends USB output. A full queue discards diagnostics and
increments `dropped`, without waiting for USB. This count excludes underlying USB
byte loss and game-input queue drops. If diagnostic task allocation fails, the
game continues. Only values/events are transmitted, never recordings. The dashboard
retains at most 300 audio points and 80 events. Older firmware has no such stream.

`--record` writes parsed JSONL only into a new file under Git-ignored `build/`.
Ordinary serial logs are excluded. A large backwards timestamp clears the chart
on restart; minor cross-task reordering does not falsely signal a reboot.

### Validation scope

```sh
python3 tests/test_snap_debug.py
# After the launcher builds, verify the native C / LVGL bridge and HTTP API on macOS:
SNAP_HOST_LIBRARY="$PWD/build/snap-debug/libsnap_host.dylib" python3 tests/test_snap_debug.py
# Linux uses libsnap_host.so; pseudo-serial tests need pyserial.
```

The static gate tests serialization, input reasons, fragmented/overlong/invalid
serial lines and recording boundaries. `SNAP_HOST_LIBRARY` additionally enables
real UI frame/font, deterministic map, calibration, freeze/step and local HTTP tests.
Pseudo-serial tests cover connection, fragments and disconnection; they are not
physical USB acceptance. Music-clock pause/resume, stale run IDs and WAV byte
ranges are also tested. Heap/stack, latency, USB stability, extended
play and sleep/wake with telemetry enabled require the new firmware on hardware.

### Debug tools validation record — 2026-10-05

- Branch: `Kenwood/snap-debug-tools`. Previous uncommitted cover/publishing document changes remain intact.
- Build: PASS, ESP-IDF 5.5.3 complete gate, merged image and independent archive verification.
- Host tests: PASS, existing tests plus seven debug-tool tests including native C/LVGL and pseudo-serial,
  AddressSanitizer/UndefinedBehaviorSanitizer on the C diagnostic paths, Chinese fonts and 300-frame preview.
- Browser checks: start, freeze, level-10 input, stepped jump height, collision overlays,
  Chinese layout and demo chart passed.
- Device tests: NOT RUN; this version has not been flashed. USB enumeration found no AI Passport.
- Unverified: physical USB telemetry, diagnostic task stack/overhead, human snap recognition,
  extended play and sleep/wake.
- Merged image: 841,296 bytes; application: 775,760 bytes. Partitions and saved profile format unchanged.
- Flashing the merged image at `0x0` covers the NVS padding region and may reset calibration/records;
  flashing needs separate authorization.

```text
full_bin_sha256: 810d64a83eb533181ffc104c69fa3a2f3637206440303dc811a6f9d5c5b5862c
app_elf_sha256:  303126046bd93c9b9732d6bc73aa63f3ea59b6ad4eb85c624ef8ed866808d8e0
```

The exact bundle is under `build/firmware/810d64a83eb533181ffc104c69fa3a2f3637206440303dc811a6f9d5c5b5862c/`,
with `FoloToy-AI-Passport-full.bin`, matching ELF/MAP and `manifest.json`.

## Music level validation — 2026-10-06

- Branch: `Kenwood/snap-music-level`; previous uncommitted changes preserved. No commit, push or publication.
- Build: PASS — complete ESP-IDF 5.5.3 gate, merged layout and independent archive verification.
- Host tests: PASS — fixed chart at levels 3–10 with ±80 ms timing variation;
  transport, completion and volume bounds; eight native/HTTP/telemetry tests;
  BSP input-discard empty/partial/error/bounded-drain cases; AddressSanitizer and
  UndefinedBehaviorSanitizer on the runner/audio transport and BSP audio tests.
  The final static gate was repeated after the last host-only test/tool edits.
- UI: both fonts cover 244 code points; all pages, completion and audio-error
  layouts checked; 300 rendered frames. Host LVGL pool used 12,088 bytes with
  9,152 free. Browser music start, pause/resume, freeze/step/resume and game-over
  paths checked. These are host results, not board measurements or listening acceptance.
- Device tests: NOT RUN. No AI Passport USB port detected; nothing flashed.
- Unverified: musical cue feel, actual speaker phase/delay, human snap recognition
  with playback, nonlinear speaker leakage, full-duplex underruns, sustained
  heap/stack/watchdog health, LCD appearance, persistence and sleep/wake.
- Merged image: 5,783,648 bytes; application: 5,718,112 / 8,323,072 bytes.
  Factory partition layout unchanged; exact bundle below includes ELF/MAP/manifest.
- Flash offset: `0x0`. Merged padding can reset NVS calibration and records;
  the new filter also requires new calibration. Flashing still requires approval.

```text
full_bin_sha256: 237690cabd9c3ed8b8792e2abf2ddccf66049a5ae3fe0faf3a9a2aa87545f7cf
app_elf_sha256:  b8fbbf274309ee4584de3bec8aa7b8ac19f396765f1b161f7a7b308346c4097b
bundle: build/firmware/237690cabd9c3ed8b8792e2abf2ddccf66049a5ae3fe0faf3a9a2aa87545f7cf/
```

### Desktop resume repair — 2026-10-06

The previous desktop bridge reused the device's 250 ms clock-gap limit. A
350 ms browser update gap after 1.1 seconds reproduced an unwanted pause at
11 game metres. Desktop updates now allow bounded 1.5-second gaps, advancing
every collision step; missing updates still pause after three seconds. Transport
epochs reject pre-pause/pre-reset requests. Continue requests media playback
inside the user gesture, before HTTP awaits. Playback failures use an idempotent
pause, and cancelled old playback promises cannot undo newer controls.

The native/HTTP/protocol suite and JavaScript media-controller regressions cover
this failure, stale epochs, permission rejection, resume, and the complete chart
with 50/350/1,000 ms updates. JavaScript tests require Node.js and run via
`node --test tests/test_snap_browser.js`; Python invokes them when Node is present.
The local service was restarted; reload an already-open page to load the matching
client protocol. Browser UI automation was unavailable during this repair, so
actual in-browser interaction remains a user check. Firmware source is unchanged;
no device was opened or flashed.

Repair validation: Build **PASS** (complete `./tools/validate.sh`); Host tests
**PASS** (11 tool tests, including three frontend cases, complete-chart replay,
and separate HTTP integration); Device tests **NOT RUN**. Unverified: real
browser click/playback acceptance because UI automation was unavailable.
Local validation log: `build/snap-resume-validation.log`.

## Downbeat chart checkpoint — 2026-10-06

Branch `Kenwood/snap-downbeat-chart` preserves the existing uncommitted work.
The old acoustic-peak chart is replaced by 36 score-phased takeoff cues from the
[recording analysis](snap-rhythm.md). The simulator has been rebuilt and restarted;
its music panel links to the tempo/downbeat audition page.

Build: **PASS** — full `./tools/validate.sh` gate and independent archive check.
Host tests: **PASS** — four musical timing regressions, full-chart runs at levels
3–10 with +/-80 ms inputs, and 11 native/tool tests including three browser
controller cases. The report routes, embedded WAV duration and JS syntax passed.
Device tests: **NOT RUN** — no AI Passport USB port detected; nothing flashed.
Unverified: expert listening acceptance, interpolated fast-section beat phase,
actual browser audio/visual output (browser control authentication unavailable),
and physical speaker/display/snap latency. Coda tracking after 134 s is excluded
from gameplay instead of extrapolated.

Merged image: 5,783,568 bytes, for offset `0x0`; app 5,718,032 bytes within the
8,323,072-byte factory partition. The merged image may reset NVS/calibration.
Exact archive: `build/firmware/d73e1effa47e5d802679743b9a4e752ee2ee60b0c9989516e99d26d2d087905c/`.
Matching ELF/MAP and the full image are bound by its verified `manifest.json`.

```text
full_bin_sha256: d73e1effa47e5d802679743b9a4e752ee2ee60b0c9989516e99d26d2d087905c
app_elf_sha256:  220647af6823f636886f98402cb68ec3b7c2b009b51b5b7eeb4152697861f1a7
```

## Coda accents checkpoint — 2026-10-06

Added takeoffs at 135.160, 139.100, 143.000 and 149.540 seconds, including the
final closing accent. Removed the conflicting 132.810-second regular cue.
There are now 39 jumps with a 2.5-second minimum gap. Simulator and audition
report are updated. Build: **PASS** (complete gate and independent archive
validation). Host tests: **PASS** (six chart regressions, full-song timing
margins, eleven native/tool tests). Device tests: **NOT RUN**; no Passport USB
port detected. Unverified: coda onset listening acceptance and real audio,
input and display latency. No flashing, commit or publication was performed.

Merged image: 5,783,584 bytes for `0x0`; matching ELF/MAP and manifest are under
`build/firmware/9a2d256c4542463b763d4e3905e3f5831b7faacf906003e37548f2d209ffa72e/`.
Merged flashing may reset NVS.

```text
full_bin_sha256: 9a2d256c4542463b763d4e3905e3f5831b7faacf906003e37548f2d209ffa72e
app_elf_sha256:  0f2a34f46799b741e65d2e476ea963324432e93bab59aad553f4b68b9e1ed798
```

## Authorized device test and startup repair — 2026-10-06

The first authorized flash of the coda candidate succeeded, but that image
rebooted on a stack-protection fault in `snap_debug`. Matching ELF analysis
showed a 2,352-byte writer frame plus 320 bytes for `snap_debug_format`, 1,152
for newlib formatting and 416 for float conversion, exceeding the 4 KB task
stack before other calls/interrupt overhead. The single writer's 2 KB output
buffer is now static; its compiled frame is 304 bytes. Stack protection remains
enabled and the task stack size is unchanged. The state formatter logs its
remaining stack once for physical verification.

Build: **PASS** (complete gate, independently verified archive). Host tests:
**PASS**. Device tests: **PASS for flashing, startup, capture/calibration and
initial gameplay only**. The corrected image was flashed at `0x0` to the detected
ESP32-C3 USB device; esptool verified the data. A bounded 40-second observation
showed one boot, no panic, 1,436 bytes of debug-task stack remaining, microphone
ready, six calibration samples, practice and gameplay transitions, one accepted
jump, and an advancing song cursor through 5.8 seconds. The serial port is
released. Unverified: physical audio/visual quality, full-song/coda listening
acceptance and long-duration stability. The failed candidate is superseded;
do not reflash it.

Corrected merged image: 5,783,712 bytes; matching ELF/MAP and manifest:
`build/firmware/e238eb333d70a3ec4fd21720ed86752a2edc04fb97775a5528711d015ce9bca2/`.
Raw flash/startup logs remain local under ignored `build/`. No Git publication.

```text
full_bin_sha256: e238eb333d70a3ec4fd21720ed86752a2edc04fb97775a5528711d015ce9bca2
app_elf_sha256:  0e5457cca6c6fdeafccb8738f16631b57424a4d9c060f02dc106caf5f15911a3
```

## Quiet-output correction — 2026-10-06

The user reported no audible music, including at game volume 50. The application
had passed its linear 0–50 scale directly to codec-dev: default 30 meant −35 dB,
and maximum 50 still meant −25 dB. The PCM opening measures roughly −39 dBFS RMS
from 4–30 seconds. The corrected conversion matches browser amplitude (30 maps
to codec 91, 50 to 100); the original PCM and every cue timestamp stay unchanged.
Host tests cover mute, saturation, monotonicity and amplitude error under 3%
across the whole game volume range. The complete validation gate passed.

- Merged image SHA-256: `4d68988545d77e145b991300f88192540f7ab05f87f596fcdde16786e4660962`.
- Matching ELF SHA-256: `bda49f41f08c0daed2b1fed104fa78a89de76e5bbf949831fd5bc46e3ef822a9`.
- Bundle: `build/firmware/4d68988545d77e145b991300f88192540f7ab05f87f596fcdde16786e4660962/`.
- After the user woke the device, the verified application was written at
  `0x10000` on `/dev/cu.usbmodem1101`; esptool verified its hash. The partition
  table matches the preceding device build, and the bootloader differs only in
  build timestamp/checksum. Neither NVS nor the existing bootloader was written.
  Startup ELF prefix `bda49f41f` matches the archive. The microphone is ready,
  the saved calibration remains present, and the debug task retains 1,436 bytes
  of stack headroom. Audible output and acoustic feedback still need listening
  acceptance; successful flashing and startup do not establish sound quality.

## Continued silent-output diagnosis — 2026-10-06

The user still reported no music after the volume correction. A subsequent
capture on that exact build observed a scored run advancing beyond 40 seconds,
`music volume=30 codec=91`, and accepted snap inputs. Thus the volume correction
has not established an audible fix; the remaining cause is unresolved.

The next build adds read-only ES8311 output-register snapshots and two-second
PCM transport diagnostics. BSP tests cover actual register values, failed reads
without partial result publication, and rejection during sleep. The complete
gate and archive verification passed. After waking the device this build was
flashed with hash verification. At game volume 30 / codec 91, physical register
readback was `clock=3f interface=0c power=02 dac_power=00 mute=00 volume=bd route=08`.
Submitted PCM peaks reached 2,445, I/O intervals stayed within 15 ms, and the
audio worker retained 1,528 bytes of stack. The user still heard no music.

- Merged image SHA-256: `871cece02d4180b7be2f4b5291e7710f92a4834c0035817fa72375273610e09f`.
- ELF SHA-256: `bb668471076737554c935fd9061464934d1905e848820dae863f3bbcb6ac93d6`.
- Bundle: `build/firmware/871cece02d4180b7be2f4b5291e7710f92a4834c0035817fa72375273610e09f/`.

A separate boot-tone diagnostic image
`b7929e0188fa9090005bb241026b7beb2de114113ec909833d51ef10372c34b1`
(ELF `bca45d6ac546e282e867b6bad86b1a0fcb739850b125a02be54598b1a4149b14`)
was then flashed. The user heard both 1 kHz tones; duplex capture peak was 4,838.
The user also confirmed an earlier birthday-candle game could play synthesized
music. These observations support investigating recording level rather than
claiming the speaker is broken. They do not prove the game's streaming music.

The speaker master now raises opening RMS from about 382 to 2,484 (+16.3 dB),
with peak 18,765, no clipping, identical sample count and no polarity reversals.
The 39 chart events remain identical. Host fixtures detect no pure-music events
and all 51 injected snaps. Normal builds disable the two boot tones. The user
has now confirmed audible game music; acoustic feedback and perceived beat
alignment still need dedicated device testing.

The normal (boot probes disabled) speaker-master build passed the complete gate
and independent bundle verification. Its application image contains the exact
mastered PCM, and the diagnostic tone strings are absent. After the device
returned, the application was flashed at `0x10000` with successful hash
verification, preserving NVS and the existing bootloader. Startup ELF prefix
`aa9961e4f` matches the archive. Playback reached 86.1 seconds before game over;
reported audio I/O failures remained zero and maximum logged I/O duration was
10 ms. The user confirmed music was audible. Volume was raised from 30 to 50
during this run, so audibility at every individual setting is not established.
Serial capture subsequently ended on USB disconnection; its cause is unknown.

The findings identify two gain problems: the original game-to-codec volume
mapping attenuated playback excessively, and the recording's quiet opening
remained too weak after that mapping was fixed. Normalizing the recording
resolved the reported inaudibility in this test. Both output-only and duplex
test tones were already audible before normalization. No speaker frequency
response has been measured, so its contribution remains an inference.

- Merged image: 5,785,392 bytes; SHA-256 `a2e705b88a0262ef64cf0fcceb0965253f749d515c00cdb7a5d68e6d01ca6a47`.
- ELF SHA-256: `aa9961e4f87a5fa6498e13501c14789f7a27e5b55b30fbd21bd4cb52b97806f8`.
- Bundle: `build/firmware/a2e705b88a0262ef64cf0fcceb0965253f749d515c00cdb7a5d68e6d01ca6a47/`.
- Build: PASS; Host tests: PASS; Device tests: PASS for flashing, startup and
  user-confirmed audible gameplay. Unverified: full-song listening, acoustic
  false-trigger rate, perceived beat alignment and long-term stability.
