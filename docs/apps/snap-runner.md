[简体中文](snap-runner.zh_CN.md) · **English**

# Snap Runner

An offline, portrait 240 × 320 runner for AI Passport. A mint pixel creature runs
through a scrolling night landscape. The original BSP is reused; no demo menu,
page or visual shell is linked into the application. No network or speaker output
is used. Physical buttons navigate; microphone transients trigger jumps.

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
- Distance is accumulated map movement, with 10 pixels represented as one game
  metre. Speed rises from 90 to 125 px/s. Random rocks and pits have bounded size
  and spacing. Density increases over the first 1,000 game metres, then caps:
  extra spacing above the safety floor decreases from 75 to 0 px, and the random
  spacing allowance from 0–74 to 0–24.67 px. Progress uses each gap's world
  position, including pre-generated hazards; already placed hazards never move
  closer together. The edge-to-edge floor is 162.34 px: a level-10 flight plus
  a 350 ms recovery margin at maximum speed. This provides a feasible route
  with well-timed strong jumps, not immunity to weak or mistimed inputs. The
  first obstacle enters from off-screen.
- Calibration and highest completed-run score persist in the NVS namespace
  `snap_runner`, versioned `profile` blob. NVS failures are displayed; gameplay
  remains available for the session. No automatic partition erase.

## Controls

| Screen | UP | DOWN | OK |
| --- | --- | --- | --- |
| Home with calibration | Practice | Recalibrate | Start run |
| Home without calibration | Begin calibration | Begin calibration | Begin calibration |
| Loud/soft capture | Cancel to home | No action | No action |
| Between capture stages | Cancel | No action | Begin soft capture |
| Calibration rejected | Cancel | No action | Retry |
| Practice | Home | Recalibrate | New scored run |
| Run | No action | No action | Pause |
| Paused | No action | Home | Resume if mic ready |
| Game over | Home | Recalibrate | New run |

Press events are used once per press. A 300 ms key guard and timestamp checks
reject button noise and stale microphone events across transitions. Capture has
an initial one-second quiet interval and a 700 ms spacing guard. A saved profile
can start a run directly; practice is always available from home.

## Audio and task ownership

The dedicated microphone task reads 80 samples (5 ms) at 16 kHz, 16-bit mono
through `bsp_audio_read()`. It uses the BSP's fixed gain and high-pass filtering,
an 800 ms ambient estimate, onset thresholds, a 30 ms burst window, high-frequency
energy and decay checks, followed by a 180 ms refractory period and quiet rearm.
It queues amplitude events and publishes one latest-status snapshot; no recordings
are retained. It is a heuristic transient detector: claps, taps or sharp nearby
sounds can trigger it. Real snap sensitivity, latency and false positives require
physical testing with the actual microphone, room and playing distance.

The application task owns the session, physics, NVS, battery polling and UI
updates. LVGL access is enclosed by `bsp_lvgl_lock()`. Button callbacks only
queue events. Audio never accesses UI objects, and all screens share lifetime
objects, avoiding page teardown races. Microphone failure/stale status pauses a
run; recovery requires an explicit resume. Physics uses fixed 10 ms steps and a
scheduling stall pauses the scored run rather than teleporting through hazards.
NVS writes occur only after calibration or a new completed-run record.

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

The host suite covers calibration, all ten bins, invalid profiles, jump arcs,
airborne rejection, collision/recovery, pause, game over, high-score updates,
synthetic microphone inputs, and playable generated streams over 120 seeds.
Density tests verify monotonic tightening for matched random choices and the
late-game cap. At the minimum gap, all four ordered rock/pit combinations,
both width extremes and three speed bands pass without damage with up to
80 ms early/late timing variation; generated-stream tests cover the density ramp.
The shared gate additionally covers existing BSP fault paths and firmware layout.
Synthetic sound tests do not establish classification accuracy on human snaps.

## Device acceptance and delivery

Build/host results and exact artifact identities are reported at handoff. Device
tests remain NOT RUN until authorized flashing and observed acceptance. Verify:

1. Every Chinese page and error state, clipped labels, battery fallback, and keys.
2. Quiet-room calibration with three hard and three soft snaps; reject clipping,
   ambient noise and insufficient range. Verify saved calibration after restart.
3. Practice all ten levels at a consistent distance; observe latency, weak-snap
   misses and false triggers from speech, claps, taps and button presses.
4. Complete runs with pits and rocks, safe recovery, three-hit game over, pause,
   restart, new-record persistence, and microphone failure behavior.
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
