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
