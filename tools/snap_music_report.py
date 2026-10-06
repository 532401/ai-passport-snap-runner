#!/usr/bin/env python3
"""Create an offline tempo/downbeat report and sample-aligned click-track WAV."""
import array
import base64
import csv
import io
import json
import math
import re
from pathlib import Path
import sys
import wave

ROOT = Path(__file__).resolve().parents[1]


def main():
    grid = json.loads((ROOT / 'assets/music/mountain-king-beats.json').read_text())
    chart = json.loads((ROOT / 'assets/music/mountain-king-chart.json').read_text())
    out = ROOT / 'build/music-analysis'
    out.mkdir(parents=True, exist_ok=True)
    cues = {e['bar']: e['jump_ms'] for e in chart['events'] if 'bar' in e}
    accent_cues = {e['accent_id']: e['jump_ms'] for e in chart['events'] if 'accent_id' in e}
    bars = [b for b in grid['beats'] if b['position'] == 1]
    with (out / 'beats.csv').open('w', newline='', encoding='utf-8') as f:
        writer = csv.writer(f)
        writer.writerow(('time_ms', 'score_bar', 'beat_in_bar', 'takeoff_ms', 'method', 'playable'))
        for beat in grid['beats']:
            writer.writerow((beat['time_ms'], beat['bar'], beat['position'],
                             cues.get(beat['bar'], '') if beat['position'] == 1 else '',
                             beat['method'], beat['playable']))
        for accent in grid['coda_accents']:
            writer.writerow((accent['time_ms'], '', 'coda_accent', accent_cues.get(accent['id'], ''),
                             accent['method'], True))
    pcm = array.array('h', (ROOT / 'assets/music/mountain-king.pcm').read_bytes())
    if sys.byteorder != 'little':
        pcm.byteswap()
    samples = array.array('h', (round(v * .65) for v in pcm))
    clicks = grid['beats'] + [dict(a, playable=True, position=0) for a in grid['coda_accents']]
    for beat in clicks:
        if not beat['playable']:
            continue
        jump = (beat['position'] == 1 and beat['bar'] in cues) or beat.get('id') in accent_cues
        strong = beat['position'] in (0, 1)
        duration, freq, gain = (.065, 1600, .30) if jump else ((.04, 1100, .20) if strong else (.022, 700, .08))
        start = round(beat['time_ms'] * 16)
        for j in range(round(duration * 16000)):
            if start + j >= len(samples):
                break
            envelope = math.exp(-j / (duration * 16000 / 4)) * min(1, j / 16)
            click = round(32767 * gain * envelope * math.sin(2 * math.pi * freq * j / 16000))
            samples[start+j] = max(-32768, min(32767, samples[start+j] + click))
    if sys.byteorder != 'little':
        samples.byteswap()
    buf = io.BytesIO()
    with wave.open(buf, 'wb') as wav:
        wav.setnchannels(1); wav.setsampwidth(2); wav.setframerate(16000)
        wav.writeframes(samples.tobytes())
    (out / 'clicks.wav').write_bytes(buf.getvalue())
    rows = ''.join(f'<tr><td>{s["label_zh"]}</td><td>{s["start_ms"]/1000:.2f}–{s["end_ms"]/1000:.2f} s</td><td>{s["bpm_min"]}–{s["bpm_max"]}</td></tr>' for s in grid['sections'])
    points = []
    for a, b in zip(bars, bars[1:]):
        bpm = 240000 / (b['time_ms'] - a['time_ms'])
        points.append(f'{50+a["time_ms"]/154091*900:.1f},{220-(bpm-90)*1.4:.1f}')
    markers = ''.join(f'<line x1="{50+e["jump_ms"]/154091*900:.1f}" x2="{50+e["jump_ms"]/154091*900:.1f}" y1="246" y2="262" stroke="#fac984"/>' for e in chart['events'])
    buttons = ''.join(f'<button data-ms="{b["time_ms"]}"><span>第 {b["bar"]} 小节{ " · 跳" if b["bar"] in cues else ""}</span><b>{b["time_ms"]/1000:.3f}s</b></button>' for b in bars if b['playable'])
    buttons += ''.join(f'<button data-ms="{a["time_ms"]}"><span>尾声重音 {i+1}{" · 跳" if a["id"] in accent_cues else " · 间隔过近跳过"}</span><b>{a["time_ms"]/1000:.3f}s</b></button>' for i, a in enumerate(grid['coda_accents']))
    payload = json.dumps(grid['beats'], ensure_ascii=False).replace('<', '\\u003c')
    html = '''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>山妖洞穴 · 节奏分析</title>
<style>*{box-sizing:border-box}body{margin:0;background:#11202b;color:#edf5f2;font:16px/1.7 system-ui,sans-serif}main{max-width:1050px;margin:auto;padding:40px 24px}h1{font-size:36px;margin:8px 0}h2{margin-top:32px;font-size:22px}p{color:#b5c7ce}small{color:#b5c7ce}.tag{color:#76deb2;letter-spacing:2px}section{background:#192c37;border:1px solid #354752;border-radius:14px;padding:22px;margin:22px 0}audio{width:100%;margin:16px 0}table{width:100%;border-collapse:collapse}td,th{text-align:left;padding:10px;border-bottom:1px solid #354752}.clock{font-size:26px;font-variant-numeric:tabular-nums;color:#fac984}svg{width:100%;height:auto}button{background:#213844;color:#edf5f2;border:1px solid #425963;border-radius:8px;padding:10px;cursor:pointer;font:inherit}button:hover,button.active{border-color:#76deb2;background:#2c4e51}button span{font-size:12px;display:block;color:#b5c7ce}button b{font-variant-numeric:tabular-nums}#bars{display:grid;grid-template-columns:repeat(auto-fit,minmax(120px,1fr));gap:8px}a{color:#76deb2}</style>
<main><div class="tag">SNAP RUNNER / RHYTHM MAP</div><h1>先听清每一拍，再起跳。</h1><p>同一份 154.091 秒音乐，按录音的实际加速建立逐拍时间表。BPM 是各小节的平均速度范围，不是游戏用来外推时间的固定值。</p>
<section><div class="clock" id="clock">0.000 s · 就绪</div><audio id="audio" controls preload="metadata" src="data:audio/wav;base64,__AUDIO__"></audio><p>试听提示：轻声 = 普通拍；清脆声 = 小节第一拍或尾声重音；更高的提示音 = 本次需要响指起跳。点击下方小节，可从它前 1 秒试听。</p><small>提示音已按采样位置混入音乐，不依赖浏览器定时器触发。此文件只用于对拍，游戏音乐没有这些提示音。</small></section>
<h2>速度与起跳点</h2><svg viewBox="0 0 1000 300" role="img" aria-label="节奏由约103 BPM加速至约202 BPM，黄色竖线为起跳点"><rect x="833" y="20" width="117" height="244" fill="#34414a"/><path d="M50 20V264H950" fill="none" stroke="#758a94"/><text x="4" y="38" fill="#b5c7ce">220</text><text x="4" y="180" fill="#b5c7ce">120</text><text x="50" y="289" fill="#b5c7ce">0 秒</text><text x="893" y="289" fill="#b5c7ce">154 秒</text><polyline points="__POINTS__" fill="none" stroke="#76deb2" stroke-width="3"/>__MARKERS__<line id="cursor" x1="50" x2="50" y1="20" y2="264" stroke="#eef5f2" stroke-width="2"/></svg>
<table><thead><tr><th>录音段落</th><th>时间</th><th>四分音符 BPM</th></tr></thead><tbody>__ROWS__<tr><td>引子／延长音</td><td>0–4.38 s</td><td>不拟合固定速度</td></tr><tr><td>尾声／休止与孤立重音</td><td>134–154.091 s</td><td>按独立重音安排障碍，不外推 BPM</td></tr></tbody></table>
<p>前段大约每 4.4–4.6 秒跳一次，后段通常每 2.5–3.7 秒。共 __COUNT__ 个起跳点；规则段跳过整小节，尾声跳过间隔过近的重音，让手指有休息时间。坑或石块的中心在起跳点之后 280 ms 到达角色。</p>
<h2>小节第一拍与尾声重音 · 逐点试听</h2><div id="bars">__BUTTONS__</div>
<section><h2>分析依据与边界</h2><p>Beat This! 的 final0、final1 两个模型在规则段的拍点相差不超过 20 ms；用乐谱 4/4 结构，从 4.380 秒的主题入口计数确定小节相位。模型原始“强拍”标签出现错相，已按小节结构修正。119.13 秒后两个模型都转为半速计拍，缺失的四分拍取相邻真实拍点的中点。</p><p>这是一份结合乐谱结构校正的自动分析，不是音乐专家逐拍听写。快速段插值拍点需重点试听；134 秒后按单独定位的管弦乐突起安排障碍；最小间隔仍为 2.5 秒，最后一下收束重音也需要跳跃。真机的扬声器、显示和响指检测延迟仍需实测。</p><p><a href="https://github.com/CPJKU/beat_this">识别模型</a> · <a href="https://www.mutopiaproject.org/ftp/GriegE/O46/Dans_l_antre_du_roi_de_la_montagne/Dans_l_antre_du_roi_de_la_montagne-a4.pdf">公版乐谱</a> · <a href="/">返回模拟器</a></p></section>
<script>const beats=__BEATS__;const audio=document.getElementById('audio');const clock=document.getElementById('clock');const cursor=document.getElementById('cursor');document.querySelectorAll('[data-ms]').forEach(b=>b.addEventListener('click',()=>{audio.currentTime=Math.max(0,Number(b.dataset.ms)/1000-1);audio.play().catch(()=>{clock.textContent='请按播放器的播放键';});}));function frame(){const ms=audio.currentTime*1000;let beat=null;for(const b of beats){if(b.time_ms>ms)break;beat=b;}clock.textContent=audio.currentTime.toFixed(3)+' s · '+(ms>=134000?'尾声':beat?'第 '+beat.bar+' 小节 / 第 '+beat.position+' 拍':'引子');const x=50+ms/154091*900;cursor.setAttribute('x1',x);cursor.setAttribute('x2',x);requestAnimationFrame(frame);}frame();</script></main></html>'''
    replacements = {'__AUDIO__': base64.b64encode(buf.getvalue()).decode(), '__POINTS__': ' '.join(points),
                    '__MARKERS__': markers, '__ROWS__': rows, '__COUNT__': str(len(chart['events'])),
                    '__BUTTONS__': buttons, '__BEATS__': payload}
    for key, value in replacements.items():
        html = html.replace(key, value)
    # External executable code/styles also work under the debug server's CSP.
    style = re.search(r'<style>(.*?)</style>', html, re.S).group(1)
    script = re.search(r'<script>(.*?)</script>', html, re.S).group(1)
    script = script.replace('const beats=' + payload + ';',
                            "const beats=JSON.parse(document.getElementById('beatData').textContent);")
    html = re.sub(r'<style>.*?</style>', '<link rel="stylesheet" href="report.css">', html, flags=re.S)
    html = re.sub(r'<script>.*?</script>', lambda _: '<script id="beatData" type="application/json">' + payload +
                  '</script><script src="report.js"></script>', html, flags=re.S)
    (out / 'report.css').write_text(style, encoding='utf-8')
    (out / 'report.js').write_text(script, encoding='utf-8')
    (out / 'index.html').write_text(html, encoding='utf-8')
    print(f'Analysis report, beat CSV and click-track: {out}')


if __name__ == '__main__':
    main()
