**English** · [简体中文](snap-rhythm.zh_CN.md)

# Mountain King rhythm analysis

This analysis replaces the earlier acoustic-peak chart. It uses the exact
154.091-second Musopen recording identified in [asset provenance](../../assets/README.md).
The speaker master now uses sample-aligned gain normalization; no trimming,
stretching, pitch change or MP3 decoder offset is introduced. The analysis keeps
the reference PCM hash as provenance, and all beat/coda timestamps are unchanged.
The game uses recorded timestamps, not a fixed BPM clock.

| Recording section | Time (seconds) | Quarter-note BPM range |
| --- | --- | --- |
| Opening fermata | 0–4.38 | No regular grid |
| Opening theme, score bars 2–25 | 4.38–58.06 | 102.6–110.1 |
| Gradual acceleration, bars 26–41 | 58.06–89.26 | 112.1–136.4 |
| Acceleration into climax, bars 42–49 | 89.26–101.98 | 142.9–160.0 |
| Piu vivo, bars 50–61 | 101.98–118.80 | 160.0–179.1 |
| Final regular phrase, bars 62–73 | 118.80–134.00 | 180.5–201.7 |
| Coda with rests and isolated accents | 134.00–154.091 | Independent accent timestamps; no fixed BPM |

Ranges are measured from successive barlines (`240000 / bar duration in ms`),
not instantaneous beat-to-beat rates. Section boundaries summarize the measured
acceleration; the Piu vivo entry also corresponds to the score's tempo marking.

## Method and limitations

[Beat This!](https://github.com/CPJKU/beat_this) 1.1.0, checkpoints `final0` and
`final1`, was run on the original full-band FLAC. Beat estimates agree within
20 ms through the regular section. The models' raw **downbeat labels are not
reliable here**: they sometimes label other beats or change phase. We anchor
4/4 counting at the opening B theme, 4.380 s, score bar 2, and check the structure
against [Grieg's public-domain piano score](https://www.mutopiaproject.org/ftp/GriegE/O46/Dans_l_antre_du_roi_de_la_montagne/Dans_l_antre_du_roi_de_la_montagne-a4.pdf).
Its opening F-sharp fermata, Piu vivo at bar 50, and quarter rests at the coda
provide structural checks. Score tempo numbers are not used as recording BPM.

From 119.130 s both models switch to half-time beat tracking. Missing quarter
beats are placed at the midpoint of adjacent detected anchors, preserving the
ongoing 4/4 phase and local acceleration. For example, bar 63 starts at 120.130 s,
not the raw model's 120.460 s accent. These interpolated points are less certain.
The coda has rests and accents on other beats. Its tracker disagreements are
not extrapolated as barlines. Eight isolated orchestral attacks were localized
in short windows of the original recording using the maximum 40 ms RMS rise
(10 ms window, 5 ms hop, timestamp at frame centre). Four are selected for play:
**135.160, 139.100, 143.000 and 149.540 seconds**. The others are too close to
the previous selected jump. The conflicting 132.810 s regular cue is removed
to preserve the first coda attack and the 2.5-second spacing floor. The final
closing accent is included. These acoustic onset estimates also need listening
acceptance; they are explicitly distinct from metric downbeats.

This is automatic analysis checked against score structure, **not expert
listening annotation**. Model resolution is 20 ms. Human listening acceptance,
particularly the interpolated fast section, and actual speaker/display/input
latency remain unverified. Model agreement is not a timing-accuracy guarantee.

## Frozen data and gameplay

- [Beat grid](../../assets/music/mountain-king-beats.json): 289 quarter-beat
  positions including the non-playable coda boundary, score bar/beat identity,
  interpolation flags, section statistics, source/model hashes and limitations.
  A separate `coda_accents` list records eight localized attacks and their search windows.
- [Game chart](../../assets/music/mountain-king-chart.json): 39 takeoff cues: 35 regular-section downbeats
  and four independently annotated coda accents. The first cues are 4.380, 9.000,
  13.480, 17.920 and 22.340 seconds. Minimum spacing remains 2.5 seconds; whole
  bars are skipped when needed; coda accents use the same minimum gap. No random placement.
- `main/snap_chart_data.h`: generated table. Each hazard centre crosses the
  player 280 ms after takeoff. The **snap/takeoff**, rather than screen entry or
  collision, belongs on the downbeat or annotated coda accent. The intro has no hazards.

```sh
python3 tools/snap_music_chart.py
python3 tools/snap_music_chart.py --check
python3 tests/test_snap_music_chart.py
python3 tools/snap_music_report.py
python3 tools/snap_debug.py
```

Open `http://127.0.0.1:8765/rhythm/` for the tempo curve, exact bar timestamps and
sample-aligned click-track audition. Quiet clicks mark quarter beats, stronger
clicks mark downbeats or coda accents, and higher clicks mark selected jumps. Click a bar to seek
one second before it. `build/music-analysis/beats.csv` contains all timestamps;
`clicks.wav` is an analysis aid only and is not embedded in firmware. The report
is a standalone HTML/CSS/JS artifact with embedded audio and no network dependency.

For a fresh model estimate, use a separate Python 3.10+ environment with
`beat-this==1.1.0`, SoundFile, NumPy and PyTorch, then run:

```sh
python tools/snap_music_analyze.py original.flac --model final0 --output build/music-analysis/final0.json
python tools/snap_music_analyze.py original.flac --model final1 --output build/music-analysis/final1.json
```

This writes unreviewed estimates and raw logits; it deliberately does not replace
the reviewed grid or firmware chart. Review meter, phase, half-time tracking and
rests first. Only the frozen grid/chart and embedded PCM are needed for normal
builds; models and ML libraries do not run on the device or in CI. The actual
analysis run reused local PyTorch 2.8.0 under Python 3.9 with postponed type
annotations added only to the isolated Beat This! package; inference code and
weights were unchanged. New analysis environments should use Python 3.10+.
