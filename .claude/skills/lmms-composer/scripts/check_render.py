#!/usr/bin/env python3
"""Sanity-check a rendered WAV: level, clipping and silent bars.

Stdlib only. Use it after `lmms render` (or the `render` tool) to find the two
failures you cannot hear from a tool result: a mix that clips, and a bar that
came out empty because a clip was placed outside the pattern it belongs to.

    python check_render.py out.wav --bpm 140
    python check_render.py out.wav --bpm 140 --bars 16

Prints a JSON report and exits 1 when the render is bad (any clipped sample, or
the whole file below -60 dBFS RMS), 0 when it is usable, 2 on a read error.
"""

import argparse
import json
import math
import sys
import wave
from array import array
from pathlib import Path

# A bar is four beats; `ticks_per_bar` scales that for non-4/4 time signatures,
# LMMS's default being 192 ticks per bar.
DEFAULT_TICKS_PER_BAR = 192
BEATS_PER_BAR = 4

# Anything quieter than this is treated as silence, and -inf is reported as a
# finite floor so the JSON stays valid.
SILENCE_DBFS = -60.0
DBFS_FLOOR = -120.0


def _limits(sampwidth: int) -> tuple[int, int]:
    """Largest and smallest sample value for a signed sample of this width."""
    bits = sampwidth * 8
    return (1 << (bits - 1)) - 1, -(1 << (bits - 1))


def _read_samples(path: Path) -> tuple[array, int, int, int, int]:
    """Return (samples, channels, sample_rate, sampwidth, frames).

    `samples` is a flat, channel-interleaved array of signed integers.
    """
    with wave.open(str(path), "rb") as w:
        channels = w.getnchannels()
        sampwidth = w.getsampwidth()
        rate = w.getframerate()
        frames = w.getnframes()
        raw = w.readframes(frames)

    if sampwidth == 1:
        # 8-bit WAV is unsigned; centre it so the rest of the code is uniform.
        samples = array("b", bytes((b - 128) & 0xFF for b in raw))
    elif sampwidth == 2:
        samples = array("h")
        samples.frombytes(raw)
        if sys.byteorder == "big":
            samples.byteswap()
    elif sampwidth == 3:
        # No array typecode for 24-bit: unpack three little-endian bytes and
        # sign-extend by hand.
        samples = array("i", bytes(4 * (len(raw) // 3)))
        for i in range(len(samples)):
            j = 3 * i
            v = raw[j] | (raw[j + 1] << 8) | (raw[j + 2] << 16)
            samples[i] = v - 0x1000000 if v & 0x800000 else v
    elif sampwidth == 4:
        samples = array("i")
        if samples.itemsize != 4:
            raise wave.Error("no 4-byte integer array type on this platform")
        samples.frombytes(raw)
        if sys.byteorder == "big":
            samples.byteswap()
    else:
        raise wave.Error(f"unsupported sample width: {sampwidth} bytes")

    # Trust the decoded length over the header's frame count.
    if channels > 0:
        frames = len(samples) // channels
    return samples, channels, rate, sampwidth, frames


def _stats(samples, max_v: int, min_v: int) -> tuple[int, int, int, int]:
    """(peak magnitude, sum of squares, sample count, clipped count)."""
    if not len(samples):
        return 0, 0, 0, 0
    peak = max(max(samples), -min(samples))
    sumsq = sum(v * v for v in samples)
    clipped = 0
    for v in samples:
        if v >= max_v or v <= min_v:
            clipped += 1
    return peak, sumsq, len(samples), clipped


def _dbfs(level: float, full_scale: float) -> float:
    if level <= 0.0:
        return DBFS_FLOOR
    return max(DBFS_FLOOR, round(20.0 * math.log10(level / full_scale), 2))


def analyze(
    path: Path,
    bpm: float | None = None,
    ticks_per_bar: int = DEFAULT_TICKS_PER_BAR,
    bars: int | None = None,
) -> dict:
    """Measure a rendered WAV.

    Returns `channels`, `sample_rate`, `duration_s`, `peak_dbfs`, `rms_dbfs`
    and `clipped_samples`. With `bpm` given it also returns `silent_bars`, the
    1-based indices of bars whose RMS is below -60 dBFS.
    """
    if bpm is not None and bpm <= 0:
        raise ValueError("bpm must be positive")
    if ticks_per_bar <= 0:
        raise ValueError("ticks_per_bar must be positive")
    if bars is not None and bars <= 0:
        raise ValueError("bars must be positive")

    samples, channels, rate, sampwidth, frames = _read_samples(Path(path))
    max_v, min_v = _limits(sampwidth)
    full_scale = float(max_v)

    peak, sumsq, count, clipped = _stats(samples, max_v, min_v)
    rms = math.sqrt(sumsq / count) if count else 0.0

    report = {
        "channels": channels,
        "sample_rate": rate,
        "duration_s": round(frames / rate, 6) if rate else 0.0,
        "peak_dbfs": _dbfs(peak, full_scale),
        "rms_dbfs": _dbfs(rms, full_scale),
        "clipped_samples": clipped,
    }

    if bpm is not None:
        bar_s = BEATS_PER_BAR * 60.0 / bpm * (ticks_per_bar / DEFAULT_TICKS_PER_BAR)
        frames_per_bar = bar_s * rate
        if bars is not None:
            n_bars = bars
        elif frames_per_bar > 0:
            n_bars = max(1, math.ceil(frames / frames_per_bar - 1e-9))
        else:
            n_bars = 0

        silent = []
        for i in range(n_bars):
            lo = min(int(round(i * frames_per_bar)), frames) * channels
            hi = min(int(round((i + 1) * frames_per_bar)), frames) * channels
            # A bar past the end of the file counts as silence.
            if lo >= hi:
                silent.append(i + 1)
                continue
            _, bar_sumsq, bar_count, _ = _stats(samples[lo:hi], max_v, min_v)
            bar_rms = math.sqrt(bar_sumsq / bar_count) if bar_count else 0.0
            if _dbfs(bar_rms, full_scale) < SILENCE_DBFS:
                silent.append(i + 1)
        report["silent_bars"] = silent

    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="check_render.py",
        description="Check a rendered WAV for clipping, silence and empty bars.",
    )
    parser.add_argument("wav", help="path to the rendered .wav file")
    parser.add_argument(
        "--bpm",
        type=float,
        default=None,
        help="project tempo; enables per-bar silence detection",
    )
    parser.add_argument(
        "--ticks-per-bar",
        type=int,
        default=DEFAULT_TICKS_PER_BAR,
        help="ticks in one bar (default: %(default)s)",
    )
    parser.add_argument(
        "--bars",
        type=int,
        default=None,
        help="expected bar count; bars past the end of the file are silent",
    )
    args = parser.parse_args(argv)

    try:
        report = analyze(
            Path(args.wav),
            bpm=args.bpm,
            ticks_per_bar=args.ticks_per_bar,
            bars=args.bars,
        )
    except (OSError, wave.Error, ValueError) as exc:
        print(f"check_render: {args.wav}: {exc}", file=sys.stderr)
        return 2

    print(json.dumps(report, indent=2))
    if report["clipped_samples"] > 0 or report["rms_dbfs"] < SILENCE_DBFS:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
