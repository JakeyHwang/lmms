"""Tests for check_render.py — the rendered-audio sanity check.

Run from this directory:  python -m unittest test_check_render -v
"""

import io
import json
import math
import os
import struct
import sys
import tempfile
import unittest
import wave
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_render  # noqa: E402

RATE = 44100


def write_wav(path, frames, channels, sampwidth, rate=RATE):
    """frames: flat list of interleaved integer samples."""
    with wave.open(str(path), "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(sampwidth)
        w.setframerate(rate)
        if sampwidth == 2:
            data = struct.pack("<%dh" % len(frames), *frames)
        elif sampwidth == 3:
            data = b"".join(struct.pack("<i", v)[:3] for v in frames)
        elif sampwidth == 4:
            data = struct.pack("<%di" % len(frames), *frames)
        else:
            raise ValueError("unsupported sample width for the test helper")
        w.writeframes(data)


def write_float_wav(path, frames, channels, rate=RATE):
    """Write an IEEE-float WAV by hand: `wave` cannot, and LMMS's 32-bit
    export is float (libsndfile SF_FORMAT_FLOAT). The unused `fact` chunk is
    what libsndfile emits for non-PCM data, so the reader must skip it."""
    data = struct.pack("<%df" % len(frames), *frames)
    fmt = struct.pack("<HHIIHH", 3, channels, rate, rate * channels * 4, channels * 4, 32)
    fact = struct.pack("<I", len(frames) // channels)
    body = (
        b"WAVE"
        + b"fmt " + struct.pack("<I", len(fmt)) + fmt
        + b"fact" + struct.pack("<I", len(fact)) + fact
        + b"data" + struct.pack("<I", len(data)) + data
    )
    Path(path).write_bytes(b"RIFF" + struct.pack("<I", len(body)) + body)


def float_sine(n_frames, channels, amplitude, rate=RATE, freq=440.0):
    """Interleaved float sine, `amplitude` relative to 1.0 full scale."""
    out = []
    for i in range(n_frames):
        v = amplitude * math.sin(2.0 * math.pi * freq * i / rate)
        out.extend([v] * channels)
    return out


def sine(n_frames, channels, amplitude, rate=RATE, freq=440.0):
    """Interleaved integer sine, `amplitude` in raw sample units."""
    out = []
    for i in range(n_frames):
        v = int(round(amplitude * math.sin(2.0 * math.pi * freq * i / rate)))
        out.extend([v] * channels)
    return out


class CheckRenderTest(unittest.TestCase):
    def setUp(self):
        self._dir = tempfile.TemporaryDirectory()
        self.dir = Path(self._dir.name)

    def tearDown(self):
        self._dir.cleanup()

    def run_cli(self, *argv):
        buf = io.StringIO()
        with redirect_stdout(buf):
            code = check_render.main(list(argv))
        return code, buf.getvalue()

    def test_clean_sine_reports_minus_six_dbfs_and_exits_zero(self):
        path = self.dir / "sine.wav"
        amp = 32767 * (10.0 ** (-6.0 / 20.0))
        write_wav(path, sine(2 * RATE, 2, amp), channels=2, sampwidth=2)

        report = check_render.analyze(path)
        self.assertEqual(report["channels"], 2)
        self.assertEqual(report["sample_rate"], RATE)
        self.assertAlmostEqual(report["duration_s"], 2.0, places=3)
        self.assertAlmostEqual(report["peak_dbfs"], -6.0, delta=0.2)
        self.assertEqual(report["clipped_samples"], 0)
        self.assertNotIn("silent_bars", report)

        code, out = self.run_cli(str(path))
        self.assertEqual(code, 0)
        self.assertAlmostEqual(json.loads(out)["peak_dbfs"], -6.0, delta=0.2)

    def test_clipping_is_counted_and_exits_one(self):
        path = self.dir / "clipped.wav"
        amp = 32767 * (10.0 ** (-6.0 / 20.0))
        frames = sine(2 * RATE, 2, amp)
        for i in range(1000):
            frames[2 * i] = 32767
        write_wav(path, frames, channels=2, sampwidth=2)

        report = check_render.analyze(path)
        self.assertGreaterEqual(report["clipped_samples"], 1000)

        code, out = self.run_cli(str(path))
        self.assertEqual(code, 1)
        self.assertGreaterEqual(json.loads(out)["clipped_samples"], 1000)

    def test_silent_third_bar_is_reported(self):
        path = self.dir / "bars.wav"
        bar_frames = int(round(4 * 60.0 / 120.0 * RATE))  # 2 s at 120 BPM
        amp = 32767 * (10.0 ** (-6.0 / 20.0))
        loud = sine(bar_frames, 1, amp)
        frames = loud + loud + [0] * bar_frames + loud
        write_wav(path, frames, channels=1, sampwidth=2)

        report = check_render.analyze(path, bpm=120.0)
        self.assertEqual(report["silent_bars"], [3])

        code, _ = self.run_cli(str(path), "--bpm", "120")
        self.assertEqual(code, 0)

    def test_all_zero_file_exits_one(self):
        path = self.dir / "silence.wav"
        write_wav(path, [0] * (RATE // 2 * 2), channels=2, sampwidth=2)

        report = check_render.analyze(path)
        self.assertEqual(report["rms_dbfs"], -120.0)
        self.assertEqual(report["peak_dbfs"], -120.0)

        code, out = self.run_cli(str(path))
        self.assertEqual(code, 1)
        self.assertEqual(json.loads(out)["rms_dbfs"], -120.0)

    def test_24_bit_mono_file_reads(self):
        path = self.dir / "mono24.wav"
        amp = 8388607 * (10.0 ** (-6.0 / 20.0))
        write_wav(path, sine(RATE, 1, amp), channels=1, sampwidth=3)

        report = check_render.analyze(path)
        self.assertEqual(report["channels"], 1)
        self.assertEqual(report["sample_rate"], RATE)
        self.assertAlmostEqual(report["duration_s"], 1.0, places=3)
        self.assertAlmostEqual(report["peak_dbfs"], -6.0, delta=0.2)
        self.assertEqual(report["clipped_samples"], 0)

    def test_float32_is_read_with_unity_full_scale(self):
        """LMMS's 32-bit export is IEEE float, which `wave` refuses."""
        clean = self.dir / "float32.wav"
        amp = 10.0 ** (-6.0 / 20.0)
        write_float_wav(clean, float_sine(RATE, 2, amp), channels=2)

        report = check_render.analyze(clean, bpm=120.0)
        self.assertEqual(report["channels"], 2)
        self.assertEqual(report["sample_rate"], RATE)
        self.assertAlmostEqual(report["duration_s"], 1.0, places=3)
        self.assertAlmostEqual(report["peak_dbfs"], -6.0, delta=0.2)
        self.assertEqual(report["clipped_samples"], 0)
        self.assertEqual(report["silent_bars"], [])
        self.assertEqual(self.run_cli(str(clean))[0], 0)

        # Full scale is 1.0, so anything at or past it is clipping.
        hot = self.dir / "float32_hot.wav"
        frames = float_sine(RATE, 2, amp)
        frames[:8] = [1.25] * 8
        write_float_wav(hot, frames, channels=2)

        report = check_render.analyze(hot)
        self.assertEqual(report["clipped_samples"], 8)
        self.assertGreater(report["peak_dbfs"], 0.0)
        self.assertEqual(self.run_cli(str(hot))[0], 1)


if __name__ == "__main__":
    unittest.main()
