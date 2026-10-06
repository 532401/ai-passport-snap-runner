<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 资源目录（Assets）

本目录集中存放可复用的资源（字库、图片、音乐等），按资源类型分子目录管理。每个资源放在其类型对应的子目录，并记录放置路径、命名方式、集成方式与来源/许可。二进制资源（字体、图片、音频）不属于纯 markdown 文档，请勿与文档混放。涉及版权/授权的资源需注明来源与许可。

## 字库（fonts）

可复用的字库文件与生成的字库源码放在 `fonts/`。

- 命名要能反映字族、字重、字级与格式。
- 记录来源、许可、字符范围、转换命令与目标放置路径。
- 添加字库前评估 Flash 与内部 RAM 影响；ESP32-C3 无 PSRAM。
- 不提交许可不允许分发的字库。

## 图片（images）

可复用的源图与生成的显示资产放在 `images/`。

| 文件 | 尺寸与格式 | 用途与来源 |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160，JPEG | 嵌入中英文项目 README 的产品主图，突出 AI Passport 产品形象与开放、人人可创作的理念。 |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724，PNG RGBA | 保留为可选技术参考图，不再用于首页主视觉。于 2026-09-17 使用内置图像生成工具为本仓库生成；已根据文档中的硬件能力契约核对图中的六项标签与参数。 |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336，PNG RGBA | 从仓库原始 `images/logo.png` 中精确裁切并去除背景的黑色字标；用于中英文项目 README 的浅色主题。 |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336，PNG RGBA | 提取字标的白色版本；README 使用 `<picture>` 在 GitHub 深色主题下显示。 |

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。

## 响指跃行字体

`fonts/NotoSansSC.ttf` 来自 [Google Fonts 的 Noto Sans SC 源文件](https://github.com/google/fonts/tree/main/ofl/notosanssc)，使用 SIL Open Font License 1.1，许可保存在 `fonts/OFL.txt`。保留原文件用于重现，不整份嵌入固件。`fonts/snap_font_16.c`、`fonts/snap_font_22.c` 为 16/22 px、Medium（字重 500）、4 bpp、未压缩、无字偶距子集，由 `main/CMakeLists.txt` 编译。两个字号覆盖相同固定文字及 ASCII。`fonts/snap_fonts.json` 记录原文件／产物哈希及转换器版本。

使用 `python3 tools/snap_fonts.py` 重新生成（需要 fontTools 和 Node/npx，固定 lv_font_conv 1.5.3），再执行 `python3 tools/snap_fonts.py --check`。`main/snap_glyphs.h` 为生成的字符清单；启动及桌面渲染程序检查两个字号的实际 LVGL 字形描述符。设备真实显示仍需单独验收。原创像素精灵和场景在 `main/snap_ui.c` 中绘制，没有使用外部精灵素材。

## 响指跃行社区封面

`images/snap-runner-cover.png` 是 2026-10-04 使用内置图像生成工具制作的玩法示意插画，以真实桌面游戏渲染作为角色与配色参考，展示薄荷色精灵、响指手势、夜景、坑和石块。图片已标注为插画，不是设备截图或实拍。实际检查的 PNG 为 1086 × 1448（竖版 3:4），SHA-256 为 `7b0e21c4378083c2ea1dd336dad6762d48e4baae8a900372b0bb7569bfa5dc4d`。它仅作为社区发布素材，不编译进固件。

## 山妖洞穴音乐

音乐关卡使用格里格《在山魔王的宫殿里》，Musopen Symphony（捷克国家交响乐团，2012）演奏。
曲作与此录音均为公版；Musopen 对录音的公版声明及补充的无限制使用许可见
[Commons 来源页](https://commons.wikimedia.org/wiki/File:Grieg_-_Peer_Gynt_Suite_No._1,_Op._46_-_IV._In_the_Hall_of_the_Mountain_King_(Musopen_Symphony).flac)。
音频的公版地位独立于软件许可证。

- `music/mountain-king-source.json`：来源、固定版本、许可、原始 FLAC SHA-1
  `7d34c97620149eff83bc14a1e838ef5d8e66746b`（25,778,747 字节）。
- `music/mountain-king-musopen.mp3`：参考转码，96 kbps、44.1 kHz 单声道，1,850,123 字节，
  SHA-256 `b88cf7a3184ed34ed8038a4a70f87f15d05738cfaca8eb56593259767a6a13c7`。
  不嵌入固件；元数据保留下载阶段的 MP3 文件名。
- `music/mountain-king.pcm`：实际嵌入的 16 kHz、16 位小端有符号单声道原始 PCM，
  4,930,912 字节、154.091 秒。原速完整曲目，经四阶 1.5 kHz 低通减少麦克风反馈，再离线平滑归一化响度以适应小喇叭。开头 RMS 由约 382 提升到 2,484，全曲峰值 18,765，无削顶；仅改变增益，2,465,456 个样本的位置及信号极性不变。来源 JSON 记录处理前后 PCM 哈希和参数（FFmpeg 8.1.1）。
  占 Flash 约 4.70 MiB，按 80 样本小块播放，不把整曲加载到内部 RAM。
- `music/mountain-king-beats.json`：本录音的 4/4 逐拍时间表与分段速度，见[节奏分析](../docs/apps/snap-rhythm.zh_CN.md)。
- `music/mountain-king-chart.json`：39 个小节第一拍／尾声重音起跳点、PCM／拍表 SHA-256 与生成参数，
  配套 `main/snap_chart_data.h`。

用上述哈希对应的原始 FLAC 重现（需要 FFmpeg；谱面生成只用 Python 标准库）：

```sh
ffmpeg -i original.flac -ac 1 -ar 44100 -codec:a libmp3lame -b:a 96k mountain-king-musopen.mp3
mkdir -p build/music-mastering
ffmpeg -i original.flac -af 'lowpass=f=1500:p=2,lowpass=f=1500:p=2' -ar 16000 -ac 1 -f s16le build/music-mastering/reference.pcm
ffmpeg -f s16le -ar 16000 -ac 1 -i build/music-mastering/reference.pcm -af 'dynaudnorm=f=500:g=31:p=0.73:m=16:r=0.12:c=0:b=1' -f s16le assets/music/mountain-king.pcm
python3 tools/snap_music_chart.py
python3 tools/snap_music_chart.py --check
```

`main/CMakeLists.txt` 仅链接 PCM，JSON／头文件把路线绑定到准确波形。换曲后必须重建谱面，
复测时机、跳跃安全间隔及实际声学反馈。
