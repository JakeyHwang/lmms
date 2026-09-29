#!/usr/bin/env python3
"""Command-line client for the LMMS agent server.

LMMS listens on a loopback NDJSON socket when
``Settings > AI > "Allow an external agent to control LMMS"`` is enabled. The
port and token are written to ``<workingdir>/.lmms-agent.json``. That file is
looked for at ``$LMMS_AGENT_FILE``, then in the working directory named by
``~/.lmmsrc.xml``, then ``~/Documents/lmms``, ``~/OneDrive/Documents/lmms`` and
``~/lmms`` — a development build keeps its ``.lmmsrc.xml`` beside the
executable, so the working directory has to be guessed.

Usage:
    python lmmsctl.py tools [--schema]
    python lmmsctl.py summary
    python lmmsctl.py call <tool> [<json>]
    python lmmsctl.py call <tool> --args-file <file>
    python lmmsctl.py call <tool> -          # arguments as JSON on stdin

Examples:
    python lmmsctl.py tools
    python lmmsctl.py call ping
    python lmmsctl.py call add_track '{"instrument": "TripleOscillator"}'
    echo '{"bpm": 128}' | python lmmsctl.py call set_tempo -

Exit codes: 0 when the tool reports ``ok: true``, 1 when it reports
``ok: false``, 2 when LMMS could not be reached at all.

As a library:
    import lmmsctl
    with lmmsctl.Lmms() as lmms:
        lmms.ok("add_track", {"instrument": "TripleOscillator"})
"""

from __future__ import annotations

import argparse
import json
import os
import socket
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path
from typing import Any

__all__ = [
	"LmmsError",
	"LmmsToolError",
	"candidate_token_files",
	"token_file_path",
	"working_dir",
	"Lmms",
	"main",
]

#: Name of the connection file LMMS writes inside its working directory.
AGENT_FILE_NAME = ".lmms-agent.json"

#: Environment variable overriding the connection file location.
AGENT_FILE_ENV = "LMMS_AGENT_FILE"

#: Default per-call timeout, in seconds. Renders and exports can be slow.
DEFAULT_TIMEOUT = 600.0


class LmmsError(Exception):
	"""LMMS could not be reached, or refused the request outright."""


class LmmsToolError(LmmsError):
	"""A tool ran and reported failure (``result.ok == False``)."""


def _home() -> Path:
	"""Home directory, without raising when the environment has no HOME."""
	try:
		return Path.home()
	except (RuntimeError, OSError, KeyError):
		fallback = os.environ.get("USERPROFILE") or os.environ.get("HOME")
		return Path(fallback) if fallback else Path.cwd()


def _rc_working_dir() -> Path | None:
	"""Working directory named by ``~/.lmmsrc.xml``, when that file exists."""
	rc_file = _home() / ".lmmsrc.xml"
	try:
		if not rc_file.is_file():
			return None
		paths = ElementTree.parse(rc_file).getroot().find("paths")
		if paths is not None:
			value = paths.get("workingdir")
			if value:
				return Path(value)
	except (ElementTree.ParseError, OSError):
		pass
	return None


def candidate_token_files() -> list[Path]:
	"""Every connection file location to try, best guess first.

	A development build keeps its ``.lmmsrc.xml`` next to the executable rather
	than in ``$HOME``, so the working directory has to be guessed: LMMS defaults
	it to the Documents folder, which OneDrive may have redirected.
	"""
	override = os.environ.get(AGENT_FILE_ENV)
	if override:
		return [Path(override)]
	home = _home()
	directories = [_rc_working_dir(), home / "Documents" / "lmms", home / "OneDrive" / "Documents" / "lmms", home / "lmms"]
	candidates: list[Path] = []
	for directory in directories:
		if directory is None:
			continue
		path = directory / AGENT_FILE_NAME
		if path not in candidates:
			candidates.append(path)
	return candidates


def token_file_path() -> Path:
	"""Path of the connection file LMMS writes when the agent server is on.

	The first candidate that exists wins; with none present the first candidate
	is returned so the caller can name it in an error message.
	"""
	candidates = candidate_token_files()
	for path in candidates:
		try:
			if path.is_file():
				return path
		except OSError:
			continue
	return candidates[0]


def working_dir() -> Path:
	"""LMMS working directory: the folder holding the connection file."""
	return token_file_path().parent


class Lmms:
	"""Client for one LMMS instance. Connects on the first call."""

	def __init__(self, path: Path | None = None, timeout: float = DEFAULT_TIMEOUT) -> None:
		self._path = Path(path) if path is not None else None
		self._explicit = path is not None
		self._timeout = timeout
		self._token: str | None = None
		self._socket: socket.socket | None = None
		self._reader = None
		self._next_id = 0

	@property
	def path(self) -> Path:
		"""The connection file this client reads."""
		if self._path is None:
			self._path = token_file_path()
		return self._path

	def _read_token_file(self) -> tuple[int, str]:
		path = self.path
		try:
			raw = path.read_text(encoding="utf-8")
		except FileNotFoundError:
			tried = [path] if self._explicit else candidate_token_files()
			listing = "\n".join(f"  {candidate}" for candidate in tried)
			raise LmmsError(
				"No agent connection file found. Tried:\n" + listing + "\nIn LMMS enable "
				"Settings > AI > 'Allow an external agent\u2026' and restart LMMS."
			) from None
		except OSError as exc:
			raise LmmsError(f"Cannot read agent connection file {path}: {exc}") from None
		try:
			info = json.loads(raw)
		except ValueError as exc:
			raise LmmsError(f"Agent connection file {path} is not valid JSON: {exc}") from None
		if not isinstance(info, dict):
			raise LmmsError(f"Agent connection file {path} is not a JSON object.")
		port = info.get("port")
		token = info.get("token")
		if not isinstance(port, int) or not isinstance(token, str) or not token:
			raise LmmsError(f"Agent connection file {path} has no usable 'port' and 'token'.")
		return port, token

	def _connect(self) -> None:
		if self._socket is not None:
			return
		port, token = self._read_token_file()
		try:
			sock = socket.create_connection(("127.0.0.1", port), self._timeout)
		except OSError as exc:
			raise LmmsError(
				f"Cannot connect to LMMS on 127.0.0.1:{port} ({exc}). Is LMMS still "
				f"running with the agent server enabled? Connection file: {self.path}"
			) from None
		sock.settimeout(self._timeout)
		self._socket = sock
		self._reader = sock.makefile("rb")
		self._token = token

	def call(self, tool: str, args: dict | None = None) -> dict:
		"""Run one tool and return its result object, ``ok: false`` included."""
		self._connect()
		assert self._socket is not None and self._reader is not None
		self._next_id += 1
		request = {
			"id": self._next_id,
			"token": self._token,
			"tool": tool,
			"args": args if args is not None else {},
		}
		line = json.dumps(request, separators=(",", ":")).encode("utf-8") + b"\n"
		try:
			self._socket.sendall(line)
			raw = self._reader.readline()
		except OSError as exc:
			self.close()
			raise LmmsError(f"Lost the connection to LMMS while calling '{tool}': {exc}") from None
		if not raw:
			self.close()
			raise LmmsError(f"LMMS closed the connection while calling '{tool}'.")
		try:
			reply = json.loads(raw)
		except ValueError as exc:
			raise LmmsError(f"LMMS sent a malformed reply for '{tool}': {exc}") from None
		if not isinstance(reply, dict):
			raise LmmsError(f"LMMS sent a non-object reply for '{tool}'.")
		if "error" in reply:
			raise LmmsError(str(reply["error"]))
		result = reply.get("result")
		if not isinstance(result, dict):
			raise LmmsError(f"LMMS sent a reply without a result object for '{tool}'.")
		return result

	def ok(self, tool: str, args: dict | None = None) -> dict:
		"""Like :meth:`call`, but raise :class:`LmmsToolError` on ``ok: false``."""
		result = self.call(tool, args)
		if not result.get("ok"):
			raise LmmsToolError(str(result.get("error", "tool failed")))
		return result

	def tools(self) -> list[dict]:
		"""The tool schemas LMMS exposes."""
		listed = self.ok("list_tools").get("tools", [])
		return listed if isinstance(listed, list) else []

	def close(self) -> None:
		"""Drop the connection. Further calls reconnect."""
		reader, sock = self._reader, self._socket
		self._reader, self._socket, self._token = None, None, None
		for handle in (reader, sock):
			if handle is not None:
				try:
					handle.close()
				except OSError:
					pass

	def __enter__(self) -> "Lmms":
		return self

	def __exit__(self, *_exc: Any) -> None:
		self.close()

	def __del__(self) -> None:
		self.close()


def _first_sentence(text: str) -> str:
	text = " ".join(str(text).split())
	head, sep, _tail = text.partition(". ")
	return head + "." if sep else text


def _print_tools(schemas: list, schema: bool) -> None:
	"""One ``name — first sentence`` line per tool, or the raw schema array."""
	if schema:
		print(json.dumps(schemas, indent=2))
		return
	for entry in schemas:
		function = entry.get("function", entry) if isinstance(entry, dict) else {}
		name = function.get("name", "?")
		description = _first_sentence(function.get("description", ""))
		print(f"{name} \u2014 {description}" if description else name)


def _collect_args(inline: str | None, args_file: str | None) -> dict:
	"""Read tool arguments from exactly one of: inline JSON, a file, stdin."""
	if inline is not None and args_file is not None:
		raise LmmsError("Give tool arguments either inline or with --args-file, not both.")
	if inline == "-":
		text = sys.stdin.read()
	elif inline is not None:
		text = inline
	elif args_file is not None:
		if args_file == "-":
			text = sys.stdin.read()
		else:
			try:
				text = Path(args_file).read_text(encoding="utf-8")
			except OSError as exc:
				raise LmmsError(f"Cannot read --args-file: {exc}") from None
	else:
		return {}
	text = text.strip()
	if not text:
		return {}
	try:
		args = json.loads(text)
	except ValueError as exc:
		raise LmmsError(f"Tool arguments are not valid JSON: {exc}") from None
	if not isinstance(args, dict):
		raise LmmsError("Tool arguments must be a JSON object.")
	return args


def _build_parser() -> argparse.ArgumentParser:
	parser = argparse.ArgumentParser(
		prog="lmmsctl.py",
		description="Drive a running LMMS instance over its loopback agent socket.",
	)
	parser.add_argument(
		"--timeout",
		type=float,
		default=DEFAULT_TIMEOUT,
		metavar="SECONDS",
		help=f"per-call socket timeout (default: {DEFAULT_TIMEOUT:g})",
	)
	sub = parser.add_subparsers(dest="command", required=True, metavar="COMMAND")

	tools = sub.add_parser("tools", help="list the tools LMMS exposes")
	tools.add_argument("--schema", action="store_true", help="dump the full JSON schema array")

	sub.add_parser("summary", help="print get_project_summary for the open project")

	call = sub.add_parser("call", help="run one tool")
	call.add_argument("tool", help="tool name, e.g. add_track")
	call.add_argument(
		"args",
		nargs="?",
		metavar="JSON",
		help="tool arguments as a JSON object, or - to read them from stdin",
	)
	call.add_argument("--args-file", metavar="FILE", help="read tool arguments from FILE")
	return parser


def _harden_streams() -> None:
	"""Never die on a narrow console code page; escape what it cannot show."""
	for stream in (sys.stdout, sys.stderr):
		reconfigure = getattr(stream, "reconfigure", None)
		if reconfigure is not None:
			try:
				reconfigure(errors="backslashreplace")
			except (ValueError, OSError):
				pass


def main(argv: list[str] | None = None) -> int:
	_harden_streams()
	options = _build_parser().parse_args(argv)
	try:
		if options.command == "call":
			args = _collect_args(options.args, options.args_file)
		with Lmms(timeout=options.timeout) as lmms:
			if options.command == "tools":
				_print_tools(lmms.tools(), options.schema)
				return 0
			if options.command == "summary":
				tool = "get_project_summary"
				result = lmms.call(tool, {})
			else:
				tool = options.tool
				result = lmms.call(tool, args)
	except LmmsToolError as error:
		print(f"lmmsctl: {error}", file=sys.stderr)
		return 1
	except LmmsError as error:
		print(f"lmmsctl: {error}", file=sys.stderr)
		return 2
	print(json.dumps(result, indent=2))
	if not result.get("ok"):
		print(f"lmmsctl: {tool} failed: {result.get('error', 'tool failed')}", file=sys.stderr)
		return 1
	return 0


if __name__ == "__main__":
	sys.exit(main())
