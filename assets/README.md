<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Assets

This directory stores reusable fonts, images, music, and sound effects, organized by asset type.

Keep each asset in the matching subdirectory and document its destination, naming, integration method, and source/license. Do not mix binary assets with Markdown documentation.

## Fonts

Store reusable font files and generated font sources in `fonts/`.

- Use descriptive names that include the family, weight, size, and format when relevant.
- Document the source, license, character range, conversion command, and expected destination.
- Check Flash and internal-RAM impact before adding a font; the ESP32-C3 has no PSRAM.
- Do not commit fonts whose license does not permit redistribution.

## Images

Store reusable source images and generated display assets in `images/`.

| File | Dimensions and format | Use and source |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160, JPEG | Product hero image embedded in both project README files to foreground AI Passport and its open, maker-oriented identity. |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724, PNG RGBA | Optional technical infographic retained as a reference asset; it is no longer used as the homepage hero. Generated for this repository with the built-in image generation tool on 2026-09-17; the six labels and values were checked against the documented hardware contract. |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336, PNG RGBA | Transparent black wordmark extracted from the repository's original `images/logo.png`; embedded in both project README files for light backgrounds. |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336, PNG RGBA | White version of the extracted wordmark, used by the README `<picture>` element when GitHub is in dark mode. |

- Use descriptive names and document dimensions, pixel format, conversion steps, and destination.
- Prefer formats suitable for the 240 × 320 RGB565 display and account for Flash and internal RAM.
- Preserve editable sources where licensing permits, and record the source and license.
- Never commit device QR secrets, credentials, or personal data in images.

## Music and sound effects

Store reusable music and sound-effect sources in `music/`.

- Document the source, license, sample rate, bit depth, channels, conversion command, and destination.
- Prefer 16 kHz, 16-bit mono PCM when it matches the current BSP audio path.
- Check Flash and internal-RAM cost before embedding audio; stream or chunk long recordings.
- Do not commit media without redistribution permission.

## Snap Runner fonts

`fonts/NotoSansSC.ttf` comes from the [Google Fonts Noto Sans SC source](https://github.com/google/fonts/tree/main/ofl/notosanssc), under the SIL Open Font License 1.1 retained in `fonts/OFL.txt`. It is retained as the reproducible source, not embedded wholesale. `fonts/snap_font_16.c` and `fonts/snap_font_22.c` are 16/22 px, Medium (weight 500), 4 bpp, uncompressed subsets with no kerning, compiled by `main/CMakeLists.txt`. Both UI font sizes cover the same fixed text and ASCII inventory. `fonts/snap_fonts.json` records source/output hashes and converter version.

Regenerate with `python3 tools/snap_fonts.py` (requires fontTools and Node/npx; pins lv_font_conv 1.5.3), then run `python3 tools/snap_fonts.py --check`. `main/snap_glyphs.h` is the generated inventory; boot and the desktop rendering harness verify real LVGL glyph descriptors in both sizes. Actual device rendering remains a separate acceptance check. The original pixel creature and landscape are drawn in `main/snap_ui.c`; no external sprite assets are used.

## Snap Runner community cover

`images/snap-runner-cover.png` is an AI-generated gameplay illustration created with the built-in image generation tool on 2026-10-04. It uses the real desktop game render as a character/palette reference and shows the mint creature, finger-snap gesture, night city, pit and rock. It is labelled as an illustration, not a device screenshot or photograph. The exact inspected PNG is 1086 x 1448 (portrait 3:4), SHA-256 `7b0e21c4378083c2ea1dd336dad6762d48e4baae8a900372b0bb7569bfa5dc4d`. It is a community publishing asset and is not compiled into the firmware.

## Mountain King music

The music level uses Grieg’s *In the Hall of the Mountain King*, performed by
Musopen Symphony (Czech National Symphony Orchestra, 2012). Both the composition
and this particular recording are public domain; Musopen's recording dedication
and fallback unrestricted permission are recorded on the
[Commons source page](https://commons.wikimedia.org/wiki/File:Grieg_-_Peer_Gynt_Suite_No._1,_Op._46_-_IV._In_the_Hall_of_the_Mountain_King_(Musopen_Symphony).flac).
This media retains its public-domain status independently of the software license.

- `music/mountain-king-source.json`: source URL, dated revision, rights and original
  FLAC SHA-1 `7d34c97620149eff83bc14a1e838ef5d8e66746b` (25,778,747 bytes).
- `music/mountain-king-musopen.mp3`: reference conversion, 96 kbps mono / 44.1 kHz,
  1,850,123 bytes; SHA-256 `b88cf7a3184ed34ed8038a4a70f87f15d05738cfaca8eb56593259767a6a13c7`.
  Not embedded. The metadata retains the download-stage MP3 filename.
- `music/mountain-king.pcm`: embedded raw signed little-endian 16-bit mono at
  16 kHz, 4,930,912 bytes / 154.091 s. Original tempo, complete duration, fourth-order
  1.5 kHz low-pass to reduce microphone feedback, followed by offline smooth gain
  normalization for the small speaker. Opening RMS rises from about 382 to 2,484;
  peak is 18,765 with no clipped samples. Only gain changes: all 2,465,456 samples
  retain their positions and signal polarity. The source JSON records both PCM
  hashes and mastering parameters (FFmpeg 8.1.1). Flash cost is about 4.70 MiB;
  playback uses 80-sample chunks rather than loading the song into internal RAM.
- `music/mountain-king-beats.json`: recording-specific 4/4 beat grid and tempo
  sections; see [rhythm analysis](../docs/apps/snap-rhythm.md).
- `music/mountain-king-chart.json`: 39 downbeat/coda-accent cues with PCM/beat-grid hashes
  and generation settings; paired with `main/snap_chart_data.h`.

Reproduce from the original FLAC identified above (FFmpeg required; chart generation uses Python standard library):

```sh
ffmpeg -i original.flac -ac 1 -ar 44100 -codec:a libmp3lame -b:a 96k mountain-king-musopen.mp3
mkdir -p build/music-mastering
ffmpeg -i original.flac -af 'lowpass=f=1500:p=2,lowpass=f=1500:p=2' -ar 16000 -ac 1 -f s16le build/music-mastering/reference.pcm
ffmpeg -f s16le -ar 16000 -ac 1 -i build/music-mastering/reference.pcm -af 'dynaudnorm=f=500:g=31:p=0.73:m=16:r=0.12:c=0:b=1' -f s16le assets/music/mountain-king.pcm
python3 tools/snap_music_chart.py
python3 tools/snap_music_chart.py --check
```

Only the PCM is linked by `main/CMakeLists.txt`; the JSON/header pair fixes the
route to the exact waveform. Replacing the music requires regenerating the chart
and retesting timing, safe jump spacing and physical acoustic feedback.
