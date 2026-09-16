#!/usr/bin/env python3
"""Minimal OpenAI-compatible mock: scripted tool calls, then a final message.

Two scripts, selected with the AI_MOCK_SCRIPT environment variable:
  drumloop (default) -- a 4-bar drum loop: kicker + oscillator hats with an effect.
  song               -- a short 3-section song (intro/verse/outro) on three tracks,
                        built with add_clips and mixed with set_track.
Each script finishes with a final text message.

Run:   python tests/scripted/ai_mock_server.py            (listens on http://127.0.0.1:8765/v1)
       python tests/scripted/ai_mock_server.py 9000       (custom port)
       AI_MOCK_SCRIPT=song python tests/scripted/ai_mock_server.py
Point LMMS Settings -> AI at base URL http://127.0.0.1:8765/v1, any key, any model.

Endpoints: GET /v1/models, POST /v1/chat/completions (stream true/false).
Each user message restarts the script. Track indices are learned only from this turn's
add_instrument_track tool results (tool results from earlier turns may still sit in the
history) and substituted for the $T0/$T1/... placeholders on the "track" and "index" fields.
One line per request is printed to stdout: method, path, step, tool names, finish reason.
"""
import json
import os
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8765


def tool(i, name, args):
	return {"id": f"call{i}", "type": "function", "function": {"name": name, "arguments": json.dumps(args)}}


# --- drumloop script -----------------------------------------------------------------------

KICK = [{"pos": p, "len": 24, "key": 36} for p in range(0, 768, 96)]
SNARE = [{"pos": p, "len": 24, "key": 38} for p in range(48, 768, 96)]
HAT = [{"pos": p, "len": 12, "key": 42, "vol": 70} for p in range(0, 768, 24)]

DRUMLOOP_SCRIPT = [
	[tool(1, "get_project_summary", {})],
	[tool(2, "set_head", {"bpm": 90})],
	[tool(3, "add_instrument_track", {"name": "Kick", "instrument": "kicker"})],
	[tool(4, "add_notes", {"track": "$T0", "clipPos": 0, "notes": KICK})],
	[tool(5, "add_instrument_track", {"name": "Hats", "instrument": "tripleoscillator"})],
	[tool(6, "add_notes", {"track": "$T1", "clipPos": 0, "notes": HAT})],
	[tool(7, "add_effect", {"track": "$T1", "effect": "amplifier", "params": {"Volume": 60}})],
	[tool(8, "get_project_summary", {})],
]
DRUMLOOP_FINAL = "Built a 4-bar drum loop at 90 BPM: Kick and Hats tracks."

# --- song script (AI_MOCK_SCRIPT=song) ------------------------------------------------------
# A short 3-section song at 120 BPM: Intro bars 1-4, Verse bars 5-12, Outro bars 13-16.
# Three instrument tracks, one add_clips per track, one set_track per track. Bar = 192 ticks.
BASS_PAT = [{"pos": p, "len": 48, "key": 40} for p in range(0, 768, 96)]          # 8 notes
BASS_VERSE = [{"pos": p, "len": 48, "key": 40} for p in range(0, 768, 48)]        # 16 root pulses
CHORDS_PAT = [{"pos": 0, "len": 192, "key": 45}, {"pos": 0, "len": 192, "key": 52}] * 2  # 4 notes
CHORDS_VERSE = [
	{"pos": 0, "len": 192, "key": 45}, {"pos": 0, "len": 192, "key": 52},
	{"pos": 192, "len": 192, "key": 50}, {"pos": 192, "len": 192, "key": 57},
	{"pos": 384, "len": 192, "key": 53}, {"pos": 384, "len": 192, "key": 60},
	{"pos": 576, "len": 192, "key": 55}, {"pos": 576, "len": 192, "key": 62},
]                                                                                # 8 notes
LEAD_PAT = [{"pos": p, "len": 24, "key": 60, "vol": 80} for p in range(0, 768, 48)]  # 16 notes
LEAD_VERSE = LEAD_PAT + [
	{"pos": 768, "len": 24, "key": 63, "vol": 100}, {"pos": 840, "len": 24, "key": 63, "vol": 100},
	{"pos": 912, "len": 48, "key": 60, "vol": 120},
]                                                                                # 19 notes

SONG_SCRIPT = [
	[tool(1, "get_project_summary", {})],
	[tool(2, "set_head", {"bpm": 120})],
	[tool(3, "add_instrument_track", {"name": "Bass", "instrument": "tripleoscillator"})],
	[tool(4, "add_clips", {"track": "$T0", "clips": [
		{"clipPos": 0, "len": 768, "name": "Intro", "notes": BASS_PAT},
		{"clipPos": 768, "len": 1536, "name": "Verse", "notes": BASS_VERSE},
	]})],
	[tool(5, "set_track", {"index": "$T0", "name": "Bass", "volume": 90, "pan": -10})],
	[tool(6, "add_instrument_track", {"name": "Chords", "instrument": "watsyn"})],
	[tool(7, "add_clips", {"track": "$T1", "clips": [
		{"clipPos": 0, "len": 768, "name": "Intro", "notes": CHORDS_PAT},
		{"clipPos": 768, "len": 1536, "name": "Verse", "notes": CHORDS_VERSE},
	]})],
	[tool(8, "set_track", {"index": "$T1", "name": "Chords", "volume": 70, "pan": 15})],
	[tool(9, "add_instrument_track", {"name": "Lead", "instrument": "tripleoscillator"})],
	[tool(10, "add_clips", {"track": "$T2", "clips": [
		{"clipPos": 768, "len": 1536, "name": "Verse", "notes": LEAD_VERSE},
		{"clipPos": 2304, "len": 768, "name": "Outro", "notes": LEAD_PAT[:4]},
	]})],
	[tool(11, "set_track", {"index": "$T2", "name": "Lead", "volume": 85})],
	[tool(12, "get_project_summary", {})],
]
SONG_FINAL = ("Built a short song at 120 BPM: Intro 4 / Verse 8 / Outro 4. "
			  "Bass pulses 8th-note roots, Chords lay down the progression, Lead carries the hook.")

SCRIPTS = {
	"drumloop": (DRUMLOOP_SCRIPT, DRUMLOOP_FINAL),
	"song": (SONG_SCRIPT, SONG_FINAL),
}
SCRIPT_NAME = os.environ.get("AI_MOCK_SCRIPT", "drumloop").lower()
if SCRIPT_NAME not in SCRIPTS:
	print(f"AI_MOCK_SCRIPT={SCRIPT_NAME!r} unknown; using drumloop (choices: {', '.join(SCRIPTS)})", flush=True)
	SCRIPT_NAME = "drumloop"
SCRIPT, FINAL_TEXT = SCRIPTS[SCRIPT_NAME]
STATE = {"step": 0, "tracks": [], "seen_user": False}


def learn_tracks(msgs):
	"""Collect this turn's add_instrument_track indices, in call order.
	Tool results from earlier turns may still sit in the history, so only results produced
	after the latest user message are considered; $Tn resolves into STATE["tracks"]."""
	if not STATE["seen_user"]:
		STATE["tracks"] = []
		STATE["seen_user"] = True
	turn_start = max((i for i, m in enumerate(msgs) if m.get("role") == "user"), default=0)
	call_names = {}
	for m in msgs[turn_start:]:
		if m.get("role") == "assistant":
			for c in m.get("tool_calls") or []:
				call_names[c.get("id")] = (c.get("function") or {}).get("name")
	for m in msgs[turn_start:]:
		if m.get("role") != "tool" or call_names.get(m.get("tool_call_id")) != "add_instrument_track":
			continue
		try:
			r = json.loads(m.get("content") or "")
		except ValueError:
			continue  # e.g. "[elided]" content once the session trims history
		if isinstance(r, dict) and r.get("ok") and isinstance(r.get("index"), int) and r["index"] not in STATE["tracks"]:
			STATE["tracks"].append(r["index"])


def next_message():
	"""Advance the script; returns (message, finish_reason)."""
	if STATE["step"] >= len(SCRIPT):
		return {"role": "assistant", "content": FINAL_TEXT}, "stop"
	calls = json.loads(json.dumps(SCRIPT[STATE["step"]]))
	for c in calls:
		a = json.loads(c["function"]["arguments"])
		for key in ("track", "index"):
			t = a.get(key)
			if isinstance(t, str) and t.startswith("$T"):
				n = int(t[2:])
				if n >= len(STATE["tracks"]):
					raise LookupError(f"placeholder {t} unresolved: learned tracks {STATE['tracks']}")
				a[key] = STATE["tracks"][n]
		c["function"]["arguments"] = json.dumps(a)
	STATE["step"] += 1
	return {"role": "assistant", "content": None, "tool_calls": calls}, "tool_calls"


class Handler(BaseHTTPRequestHandler):
	protocol_version = "HTTP/1.1"  # keep-alive by default; Content-Length is always sent

	def do_GET(self):
		if self.path.rstrip("/").endswith("/models"):
			print(f"GET {self.path} -> 200 models", flush=True)
			self._json({"object": "list", "data": [{"id": "mock-model", "object": "model"}]})
		else:
			print(f"GET {self.path} -> 404", flush=True)
			self._json({"error": {"message": f"unknown path {self.path}"}}, status=404)

	def do_POST(self):
		if not self.path.rstrip("/").endswith("/chat/completions"):
			print(f"POST {self.path} -> 404", flush=True)
			self._json({"error": {"message": f"unknown path {self.path}"}}, status=404)
			return
		try:
			length = int(self.headers.get("Content-Length") or 0)
			body = json.loads(self.rfile.read(length) or b"{}")
			msgs = body["messages"]
			if not isinstance(msgs, list) or not msgs:
				raise ValueError("messages missing or empty")
		except (ValueError, KeyError, TypeError) as e:
			print(f"POST {self.path} -> 400 bad request: {e}", flush=True)
			self._json({"error": {"message": f"bad request: {e}"}}, status=400)
			return

		if msgs[-1].get("role") == "user":
			STATE["step"] = 0
			STATE["seen_user"] = False
		learn_tracks(msgs)
		step = STATE["step"]
		try:
			msg, finish = next_message()
		except LookupError as e:
			print(f"POST {self.path} step={step} -> 500 {e}", flush=True)
			self._json({"error": {"message": str(e)}}, status=500)
			return

		stream = bool(body.get("stream"))
		names = [c["function"]["name"] for c in msg.get("tool_calls") or []]
		print(f"POST {self.path} script={SCRIPT_NAME} stream={stream} step={step}/{len(SCRIPT)} "
		      f"tools={names or '-'} finish={finish} tracks={STATE['tracks']}", flush=True)
		if stream:
			self._sse(msg, finish)
		else:
			self._json({"id": "chatcmpl-mock", "object": "chat.completion", "model": body.get("model", "mock-model"),
			            "choices": [{"index": 0, "message": msg, "finish_reason": finish}]})

	def _sse(self, msg, finish):
		delta = dict(msg)
		if "tool_calls" in delta:
			delta["tool_calls"] = [dict(c, index=i) for i, c in enumerate(delta["tool_calls"])]
		chunks = [
			{"id": "chatcmpl-mock", "object": "chat.completion.chunk",
			 "choices": [{"index": 0, "delta": delta, "finish_reason": None}]},
			{"id": "chatcmpl-mock", "object": "chat.completion.chunk",
			 "choices": [{"index": 0, "delta": {}, "finish_reason": finish}]},
		]
		data = "".join(f"data: {json.dumps(c)}\n\n" for c in chunks) + "data: [DONE]\n\n"
		self._send(data.encode(), "text/event-stream", 200)

	def _json(self, obj, status=200):
		self._send(json.dumps(obj).encode(), "application/json", status)

	def _send(self, data, content_type, status):
		# The whole body is known up front, so plain Content-Length framing works for both
		# JSON and SSE; the connection then stays open unless the client asked to close it.
		self.send_response(status)
		self.send_header("Content-Type", content_type)
		self.send_header("Content-Length", str(len(data)))
		if content_type == "text/event-stream":
			self.send_header("Cache-Control", "no-cache")
		if self.headers.get("Connection", "").lower() == "close":
			self.close_connection = True
			self.send_header("Connection", "close")
		self.end_headers()
		self.wfile.write(data)
		self.wfile.flush()

	def log_message(self, *a):
		pass  # request lines are printed by the handlers above


if __name__ == "__main__":
	print(f"mock OpenAI server on http://127.0.0.1:{PORT}/v1 (script: {SCRIPT_NAME})", flush=True)
	try:
		ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
	except KeyboardInterrupt:
		pass
