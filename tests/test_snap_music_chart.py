"""Regression checks for musical phase and accidental chart/audio drift."""
import copy
import array
import hashlib
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('snap_music_chart', ROOT / 'tools/snap_music_chart.py')
chart_tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(chart_tool)


class MusicalTimingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.raw = chart_tool.PCM.read_bytes()
        grid_raw = chart_tool.GRID.read_bytes()
        cls.grid = json.loads(grid_raw)
        cls.digest = hashlib.sha256(grid_raw).hexdigest()
        cls.chart = json.loads(chart_tool.CHART.read_text())

    def test_score_anchors_and_half_time_correction(self):
        bars = {b['bar']: b for b in self.grid['beats'] if b['position'] == 1}
        # Independently identified theme and printed Piu vivo entry. Raw model
        # downbeat labels drift and cannot be substituted for these barlines.
        self.assertEqual(bars[2]['time_ms'], 4380)
        self.assertEqual(bars[50]['time_ms'], 101980)
        self.assertEqual(bars[63]['time_ms'], 120130)
        self.assertEqual(bars[63]['method'], 'half-tempo midpoint')
        self.assertFalse(bars[74]['playable'])
        chart_tool.validate(self.chart, self.grid, self.digest, self.raw)

    def test_offbeat_event_is_rejected_even_if_header_is_regenerated(self):
        chart = copy.deepcopy(self.chart)
        chart['events'][3]['jump_ms'] += 100
        with self.assertRaisesRegex(AssertionError, 'Off-beat'):
            chart_tool.validate(chart, self.grid, self.digest, self.raw)

    def test_unsafe_spacing_is_rejected(self):
        chart = copy.deepcopy(self.chart)
        chart['events'][1]['jump_ms'] = chart['events'][0]['jump_ms'] + 1000
        with self.assertRaises(AssertionError):
            chart_tool.validate(chart, self.grid, self.digest, self.raw)

    def test_coda_attacks_include_final_hit_and_keep_transition_safe(self):
        events = self.chart['events']
        tail = [e for e in events if 'accent_id' in e]
        self.assertEqual([e['jump_ms'] for e in tail], [135160, 139100, 143000, 149540])
        self.assertEqual(tail[-1]['accent_id'], self.grid['coda_accents'][-1]['id'])
        self.assertNotIn(132810, [e['jump_ms'] for e in events])
        for previous, current in zip(events, events[1:]):
            self.assertGreaterEqual(current['jump_ms'] - previous['jump_ms'], 2500)
        self.assertTrue(all('bar' not in e for e in tail))

    def test_coda_cannot_drift_or_claim_a_barline(self):
        chart = copy.deepcopy(self.chart)
        chart['events'][-1]['jump_ms'] += 100
        with self.assertRaisesRegex(AssertionError, 'Off-beat'):
            chart_tool.validate(chart, self.grid, self.digest, self.raw)
        chart = copy.deepcopy(self.chart)
        chart['events'][-1]['bar'] = 74
        with self.assertRaisesRegex(AssertionError, 'invent a barline'):
            chart_tool.validate(chart, self.grid, self.digest, self.raw)

    def test_shifted_audio_is_rejected(self):
        with self.assertRaisesRegex(AssertionError, 'Audio changed'):
            chart_tool.validate(self.chart, self.grid, self.digest, b'\x00\x00' + self.raw[:-2])

    def test_speaker_master_has_level_headroom_and_unchanged_duration(self):
        import sys
        samples = array.array('h')
        samples.frombytes(self.raw)
        if sys.byteorder != 'little':
            samples.byteswap()
        self.assertEqual(len(samples), 2465456)
        self.assertLessEqual(max(abs(x) for x in samples), 24000)
        opening = samples[4 * 16000:30 * 16000]
        rms = (sum(x * x for x in opening) / len(opening)) ** 0.5
        self.assertGreater(rms, 2000)  # Previous inaudible master was about 382.
        self.assertLess(rms, 4000)
        source = json.loads((ROOT / 'assets/music/mountain-king-source.json').read_text())
        mastering = source['speaker_mastering']
        self.assertEqual(mastering['pcm_sha256'], hashlib.sha256(self.raw).hexdigest())
        self.assertEqual(mastering['reference_pcm_sha256'], self.grid['analysis']['reference_pcm_sha256'])


if __name__ == '__main__':
    unittest.main()
