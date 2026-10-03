[简体中文](README.zh_CN.md) · **English**

# Snap Runner

An offline finger-snap runner for **FoloToy AI Passport**. A pixel creature runs
through a scrolling night landscape; snap your fingers to jump over rocks and
pits. The microphone measures snap strength and maps it to ten jump heights.

Built on the [official AI Passport project](https://gitee.com/FoloToy/ai-passport),
reusing its BSP with an application-specific Chinese interface. Target hardware:
ESP32-C3, 8 MB Flash, no PSRAM, and a 240 × 320 display. No network connection or
speaker playback is required.

## Play

1. Keep your hand at a consistent distance from the microphone in a quiet room.
2. On first use, press OK and make three strong snaps. The quietest becomes Max.
3. Confirm, then make three soft snaps. Their mean becomes Min.
4. Try the practice screen, then press OK to start a scored run.

The calibrated range is divided into ten logarithmic strength bands. Stronger
snaps jump higher; jumps cannot be chained in mid-air. Three collisions end a
run. Distance is the score; calibration and the highest completed-run score are
saved locally.

Hazards become denser over the first 1,000 game metres, then density caps.
Spacing always preserves a complete maximum-height jump plus 350 ms at maximum
speed, providing a feasible strong-jump route. Weak or mistimed jumps can still
cause damage. Detection uses a transient heuristic: claps and taps can also
trigger jumps.

| Screen | UP | DOWN | OK |
| --- | --- | --- | --- |
| Calibrated home | Practice | Recalibrate | Start |
| Calibration | Cancel | No action | Confirm between stages |
| Practice / game over | Home | Recalibrate | New run |
| Running | No action | No action | Pause |
| Paused | No action | Home | Resume |

See the [application guide](docs/apps/snap-runner.md) for calibration validation,
all controls, model details, and device acceptance checks.

## Build

Install and activate **ESP-IDF 5.5.3** using the
[environment guide](docs/development/engineering/environment-setup.md), then:

```sh
git clone --branch feature/custom-passport-app https://github.com/532401/ai-passport-snap-runner.git
cd ai-passport-snap-runner
./tools/validate.sh --static
./tools/validate.sh
```

The full gate checks host behavior, fonts and firmware layout, and produces
`build/FoloToy-AI-Passport-full.bin` with its matching ELF/MAP archive under
`build/firmware/<sha256>/`. Flash the verified **merged** image at `0x0`; do not
use the application-only image at that offset. A merged flash resets saved
calibration and records. Follow the [flashing policy](docs/development/engineering/firmware-layout.md).

## Validation status

- Build: PASS — ESP-IDF 5.5.3 and merged-image verification.
- Host tests: PASS — calibration, detection, physics, session flow, 120 seeded
  map streams and 144 minimum-gap combinations, plus sanitizers and font checks.
- Device tests: PASS only for flashing and a 45-second startup-log window of
  the density version; no watchdog or crash diagnostics were observed.
- Unverified: human-snap accuracy, real display/control acceptance, long runs,
  progression feel, persistence across power loss, and sleep/current behavior.

Exact tested firmware identities and evidence are in the
[application guide](docs/apps/snap-runner.md). Build outputs and raw device logs
are kept out of Git. The game is developed on `feature/custom-passport-app`;
`main` retains the upstream baseline.

## License and credits

Snap Runner's original additions and modifications use [Apache License 2.0](LICENSE).
Inherited AI Passport code retains its [MIT license](LICENSES/AI-Passport-MIT.txt)
and FoloToy attribution. See [NOTICE](NOTICE) for the scope and upstream provenance.
Noto Sans SC and generated font subsets retain the
[SIL Open Font License](assets/fonts/OFL.txt); source provenance is documented in
[assets](assets/README.md). ESP-IDF and managed dependencies keep their own licenses.

Apache-2.0 permits commercial use, modification and redistribution, including in
closed-source products, subject to its license and notice requirements. It
includes an express contributor patent grant and does not require downstream
modifications to be published. See the [official license](https://www.apache.org/licenses/LICENSE-2.0)
for the complete terms.
