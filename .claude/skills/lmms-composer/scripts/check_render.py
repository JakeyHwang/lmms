#!/usr/bin/env python3
"""Sanity-check a rendered WAV: level, clipping and silent bars.

Stdlib only. Use it after `lmms render` (or the `render` tool) to find the two
failures you cannot hear from a tool result: a mix that clips, and a bar that
came out empty because a clip was placed outside the pattern it belongs to.

    python check_render.py out.wav --bpm 140
    python check_render.py out.wav --bpm 140 --bars 16

Prints a JSON report and exits 1 when the render is bad (any clipped sample, or
the whole file below -60 dBFS RMS), 0 when it is usable, 2 on a read error.

Handles every format LMMS exports: 16- and 24-bit PCM, and the 32-bit setting,
which libsndfile writes as IEEE float (`SF_FORMAT_FLOAT`, format tag 3 — the
stdlib `wave` module refuses those, so they are parsed here).
"""

import argparse
import json
import math
import operator
import struct
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

WAVE_FORMAT_PCM = 1
WAVE_FORMAT_IEEE_FLOAT = 3
WAVE_FORMAT_EXTENSIBLE = 0xFFFE

# 8-bit WAV is unsigned; this re-centres a whole buffer in one C-level pass.
_U8_TO_S8 = bytes((b - 128) & 0xFF for b in range(256))
# Sign-extension byte for a 24-bit sample, keyed by its most significant byte.
_SIGN_PLANE = bytes(0xFF if b & 0x80 else 0x00 for b in range(256))

# math.sumprod is C-level and lands in 3.12; map/mul is the fallback elsewhere.
_SUMPROD = getattr(math, "sumprod", None)


class _Audio:
    """Decoded samples plus everything needed to scale them to dBFS."""

    __slots__ = ("samples", "channels", "rate", "frames", "full_scale", "is_float")

    def __init__(self, samples, channels, rate, full_scale, is_float):
        self.samples = samples
        self.channels = channels
        self.rate = rate
        self.full_scale = float(full_scale)
        self.is_float = is_float
        self.frames = len(samples) // channels if channels else 0


def _sum_squares(block) -> float:
    """Sum of squares over one block of samples, without copying it."""
    if _SUMPROD is not None:
        return _SUMPROD(block, block)
    return sum(map(operator.mul, block, block))


def _decode_pcm(raw: bytes, sampwidth: int) -> tuple[array, int]:
    """Signed integer samples plus the format's positive full-scale value."""
    if sampwidth == 1:
        return array("b", raw.translate(_U8_TO_S8)), 127
    if sampwidth == 2:
        samples = array("h")
        samples.frombytes(raw[: len(raw) - len(raw) % 2])
        if sys.byteorder == "big":
            samples.byteswap()
        return samples, 32767
    if sampwidth == 3:
        # No array typecode is three bytes wide, so widen to four: split the
        # buffer into byte planes, then add a sign-extension plane. Slicing and
        # translate() are C-level, which a per-sample loop would not be.
        raw = raw[: len(raw) - len(raw) % 3]
        wide = bytearray(4 * (len(raw) // 3))
        wide[0::4] = raw[0::3]
        wide[1::4] = raw[1::3]
        wide[2::4] = raw[2::3]
        wide[3::4] = raw[2::3].translate(_SIGN_PLANE)
        samples = array("i")
        samples.frombytes(bytes(wide))
        if sys.byteorder == "big":
            samples.byteswap()
        return samples, 8388607
    if sampwidth == 4:
        samples = array("i")
        if samples.itemsize != 4:
            raise wave.Error("no 4-byte integer array type on this platform")
        samples.frombytes(raw[: len(raw) - len(raw) % 4])
        if sys.byteorder == "big":
            samples.byteswap()
        return samples, 2147483647
    raise wave.Error(f"unsupported sample width: {sampwidth} bytes")


def _read_riff_float(path: Path) -> _Audio:
    """Parse an IEEE-float WAV, which the stdlib `wave` module rejects."""
    with open(path, "rb") as fh:
        header = fh.read(12)
        if len(header) < 12 or header[:4] != b"RIFF" or header[8:12] != b"WAVE":
            raise wave.Error("not a RIFF/WAVE file")

        fmt = b""
        data = b""
        while True:
            chunk = fh.read(8)
            if len(chunk) < 8:
                break
            chunk_id, size = struct.unpack("<4sI", chunk)
            if chunk_id == b"fmt ":
                fmt = fh.read(size)
            elif chunk_id == b"data":
                # A streamed file can claim 0xFFFFFFFF; read what is there.
                data = fh.read(size)
                break
            else:
                fh.seek(size + (size & 1), 1)
                continue
            fh.seek(size & 1, 1)

    if len(fmt) < 16:
        raise wave.Error("missing or truncated fmt chunk")
    tag, channels, rate, _byte_rate, _align, bits = struct.unpack("<HHIIHH", fmt[:16])
    if tag == WAVE_FORMAT_EXTENSIBLE and len(fmt) >= 40:
        # The real tag is the first two bytes of the SubFormat GUID.
        (tag,) = struct.unpack("<H", fmt[24:26])
    if tag != WAVE_FORMAT_IEEE_FLOAT:
        raise wave.Error(f"unsupported WAV format tag: {tag}")
    if channels < 1 or rate < 1:
        raise wave.Error("invalid channel count or sample rate")

    typecode = {32: "f", 64: "d"}.get(bits)
    if typecode is None:
        raise wave.Error(f"unsupported float sample width: {bits} bits")
    samples = array(typecode)
    if samples.itemsize != bits // 8:
        raise wave.Error(f"no {bits}-bit float array type on this platform")
    frame_bytes = samples.itemsize * channels
    samples.frombytes(data[: len(data) - len(data) % frame_bytes])
    if sys.byteorder == "big":
        samples.byteswap()
    # Float renders are normalised to +/-1.0, so that is full scale.
    return _Audio(samples, channels, rate, 1.0, True)


def _read(path: Path) -> _Audio:
    """Decode a WAV into interleaved samples, PCM or IEEE float."""
    try:
        with wave.open(str(path), "rb") as w:
            channels = w.getnchannels()
            sampwidth = w.getsampwidth()
            rate = w.getframerate()
            raw = w.readframes(w.getnframes())
    except wave.Error:
        # `wave` only speaks PCM; the 32-bit LMMS export is float.
        return _read_riff_float(path)

    if channels < 1 or rate < 1:
        raise wave.Error("invalid channel count or sample rate")
    samples, full_scale = _decode_pcm(raw, sampwidth)
    return _Audio(samples, channels, rate, full_scale, False)


def _peak_and_clipped(audio: _Audio) -> tuple[float, int]:
    """Peak magnitude and clipped-sample count, using C-level scans."""
    samples = audio.samples
    if not len(samples):
        return 0.0, 0

    peak = max(max(samples), -min(samples))
    if audio.is_float:
        # Float can genuinely overshoot full scale; report by how much, and
        # only pay for a counting pass when something actually reached 1.0.
        if peak < 1.0:
            return peak, 0
        return peak, sum(1 for v in samples if v >= 1.0 or v <= -1.0)

    max_v = int(audio.full_scale)
    clipped = samples.count(max_v) + samples.count(-max_v - 1)
    # The integer range is asymmetric: clamp the negative rail so a
    # full-negative sample reads 0.0 dBFS rather than +0.0003.
    return min(peak, audio.full_scale), clipped


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

    Every sample is squared exactly once, whether or not bars are requested.
    """
    if bpm is not None and bpm <= 0:
        raise ValueError("bpm must be positive")
    if ticks_per_bar <= 0:
        raise ValueError("ticks_per_bar must be positive")
    if bars is not None and bars <= 0:
        raise ValueError("bars must be positive")

    audio = _read(Path(path))
    channels, rate, frames = audio.channels, audio.rate, audio.frames
    peak, clipped = _peak_and_clipped(audio)
    block = memoryview(audio.samples)  # slices of this are views, not copies
    silent: list[int] | None = None

    if bpm is None:
        sumsq = _sum_squares(block)
    else:
        bar_s = BEATS_PER_BAR * 60.0 / bpm * (ticks_per_bar / DEFAULT_TICKS_PER_BAR)
        frames_per_bar = bar_s * rate
        if bars is not None:
            n_bars = bars
        elif frames_per_bar > 0:
            n_bars = max(1, math.ceil(frames / frames_per_bar - 1e-9))
        else:
            n_bars = 0

        sumsq = 0.0
        silent = []
        covered = 0
        for i in range(n_bars):
            lo = min(int(round(i * frames_per_bar)), frames)
            hi = min(int(round((i + 1) * frames_per_bar)), frames)
            # A bar past the end of the file counts as silence.
            if lo >= hi:
                silent.append(i + 1)
                continue
            bar_sumsq = _sum_squares(block[lo * channels : hi * channels])
            sumsq += bar_sumsq
            covered = hi
            bar_rms = math.sqrt(bar_sumsq / ((hi - lo) * channels))
            if _dbfs(bar_rms, audio.full_scale) < SILENCE_DBFS:
                silent.append(i + 1)
        # Audio past the last requested bar still counts towards the file RMS.
        if covered < frames:
            sumsq += _sum_squares(block[covered * channels :])

    count = len(audio.samples)
    rms = math.sqrt(sumsq / count) if count else 0.0

    report = {
        "channels": channels,
        "sample_rate": rate,
        "duration_s": round(frames / rate, 6),
        "peak_dbfs": _dbfs(peak, audio.full_scale),
        "rms_dbfs": _dbfs(rms, audio.full_scale),
        "clipped_samples": clipped,
    }
    if silent is not None:
        report["silent_bars"] = silent
    return report


def _positive(cast):
    def parse(text: str):
        try:
            value = cast(text)
        except ValueError:
            raise argparse.ArgumentTypeError(f"not a number: {text!r}") from None
        if value <= 0:
            raise argparse.ArgumentTypeError(f"must be greater than zero: {text}")
        return value

    return parse


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="check_render.py",
        description="Check a rendered WAV for clipping, silence and empty bars.",
    )
    parser.add_argument("wav", help="path to the rendered .wav file")
    parser.add_argument(
        "--bpm",
        type=_positive(float),
        default=None,
        help="project tempo; enables per-bar silence detection",
    )
    parser.add_argument(
        "--ticks-per-bar",
        type=_positive(int),
        default=DEFAULT_TICKS_PER_BAR,
        help="ticks in one bar (default: %(default)s)",
    )
    parser.add_argument(
        "--bars",
        type=_positive(int),
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
    except (OSError, wave.Error, ValueError, struct.error) as exc:
        print(f"check_render: {args.wav}: {exc}", file=sys.stderr)
        return 2

    print(json.dumps(report, indent=2))
    if report["clipped_samples"] > 0 or report["rms_dbfs"] < SILENCE_DBFS:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
