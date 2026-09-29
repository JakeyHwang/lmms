#!/usr/bin/env python3
"""Download and verify the GeneralUser GS SoundFont for LMMS.

GeneralUser GS by S. Christian Collins is the general-MIDI SoundFont the
lmms-composer skill expects for Sf2Player instruments. This script fetches a
pinned revision, verifies its SHA-256, and drops it plus its licence text into
the LMMS working directory's ``samples/soundfonts/``.

Usage::

    python fetch_soundfont.py [--dest DIR] [--force]

Prints the absolute path of the ``.sf2`` on success.

Exit codes: 0 success, 1 download/IO failure, 3 checksum or size mismatch.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import sys
import urllib.error
import urllib.request
from pathlib import Path

# --- Pinned upstream artefact -------------------------------------------------
# Source repository: https://github.com/mrbumpy409/GeneralUser-GS
# The repository publishes no release assets; the SoundFont lives at the repo
# root. The URL below is pinned to commit 9704918 ("Update SoundFont to v2.0.3
# and documentation to r5") so the bytes -- and therefore SHA256 -- are stable.
VERSION = "2.0.3"
COMMIT = "97049183643d5fc5a9322a69c5b09efb667c6c3a"
FILENAME = "GeneralUser-GS.sf2"
URL = (
    "https://raw.githubusercontent.com/mrbumpy409/GeneralUser-GS/"
    f"{COMMIT}/GeneralUser-GS.sf2"
)
SHA256 = "9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe"
SIZE = 32319396

LICENSE_FILENAME = "GeneralUser-GS-LICENSE.txt"
LICENSE_URL = (
    "https://raw.githubusercontent.com/mrbumpy409/GeneralUser-GS/"
    f"{COMMIT}/documentation/LICENSE.txt"
)

_CHUNK = 1 << 16
_PROGRESS_STEP = 5  # percent


def _log(message: str) -> None:
    print(message, file=sys.stderr, flush=True)


def default_dest() -> Path:
    """``<workingdir>/samples/soundfonts``.

    The LMMS working directory is resolved through ``lmmsctl.token_file_path()``
    -- the same rule the control client uses to find ``.lmms-agent.json``. The
    import is deliberately lazy and guarded: ``--dest`` must keep working even
    when ``lmmsctl.py`` is missing or broken.
    """
    base = None
    try:
        script_dir = str(Path(__file__).resolve().parent)
        if script_dir not in sys.path:
            sys.path.insert(0, script_dir)
        import lmmsctl  # noqa: PLC0415 -- lazy on purpose, see docstring

        base = Path(lmmsctl.token_file_path()).resolve().parent
    except Exception as exc:  # noqa: BLE001 -- any failure falls back
        _log(f"note: falling back to the default working directory ({exc})")
    if base is None:
        base = Path.home() / "lmms"
    return base / "samples" / "soundfonts"


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(_CHUNK), b""):
            digest.update(chunk)
    return digest.hexdigest()


def is_soundfont(path: Path) -> bool:
    """True when *path* looks like a RIFF/sfbk SoundFont."""
    try:
        with path.open("rb") as handle:
            header = handle.read(12)
    except OSError:
        return False
    return header[:4] == b"RIFF" and header[8:12] == b"sfbk"


def _download(url: str, target: Path, expected_size: int | None) -> None:
    """Stream *url* into *target*, logging progress every 5 %."""
    request = urllib.request.Request(url, headers={"User-Agent": "lmms-composer/1"})
    with urllib.request.urlopen(request) as response:  # noqa: S310 -- pinned https URL
        total = expected_size or int(response.headers.get("Content-Length") or 0)
        done = 0
        next_mark = _PROGRESS_STEP
        with target.open("wb") as handle:
            while True:
                chunk = response.read(_CHUNK)
                if not chunk:
                    break
                handle.write(chunk)
                done += len(chunk)
                if total:
                    percent = done * 100 // total
                    if percent >= next_mark:
                        _log(f"  {percent:3d}%  {done:,} / {total:,} bytes")
                        next_mark = (percent // _PROGRESS_STEP + 1) * _PROGRESS_STEP
    if not total:
        _log(f"  done  {done:,} bytes")


def _fetch_license(dest: Path) -> None:
    target = dest / LICENSE_FILENAME
    try:
        _download(LICENSE_URL, target, None)
    except (urllib.error.URLError, OSError) as exc:
        _log(f"warning: could not fetch the licence text: {exc}")
        return
    _log(f"licence: {target}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=f"Download GeneralUser GS {VERSION} for LMMS.",
    )
    parser.add_argument(
        "--dest",
        type=Path,
        default=None,
        help="destination directory (default: <lmms working dir>/samples/soundfonts)",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="re-download even when a verified copy is already present",
    )
    args = parser.parse_args(argv)

    dest = (args.dest if args.dest is not None else default_dest()).expanduser()
    target = dest.resolve() / FILENAME
    part = target.with_name(target.name + ".part")

    try:
        dest.mkdir(parents=True, exist_ok=True)
    except OSError as exc:
        _log(f"error: cannot create {dest}: {exc}")
        return 1

    if target.exists() and not args.force:
        if sha256_of(target) == SHA256:
            _log(f"already present: GeneralUser GS {VERSION}, verified")
            print(target)
            return 0
        _log("existing file does not match the pinned checksum; re-downloading")

    _log(f"downloading GeneralUser GS {VERSION} ({SIZE:,} bytes)")
    _log(f"  from {URL}")
    try:
        _download(URL, part, SIZE)
    except (urllib.error.URLError, OSError) as exc:
        part.unlink(missing_ok=True)
        _log(f"error: download failed: {exc}")
        return 1

    actual_size = part.stat().st_size
    if actual_size != SIZE:
        part.unlink(missing_ok=True)
        _log(f"error: size mismatch: expected {SIZE}, got {actual_size}")
        return 3

    actual_sha = sha256_of(part)
    if actual_sha != SHA256:
        part.unlink(missing_ok=True)
        _log(f"error: SHA-256 mismatch:\n  expected {SHA256}\n  got      {actual_sha}")
        return 3

    if not is_soundfont(part):
        part.unlink(missing_ok=True)
        _log("error: downloaded file is not a RIFF/sfbk SoundFont")
        return 3

    os.replace(part, target)
    _log(f"verified SHA-256 {SHA256}")

    _fetch_license(dest)
    _log(f"licence terms: {LICENSE_URL}")

    print(target)
    return 0


if __name__ == "__main__":
    sys.exit(main())
