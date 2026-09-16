#!/usr/bin/env python3
"""Minimal OpenAI-compatible mock: scripted tool calls to build a 4-bar drum loop, then a final message.

Run:   python tests/scripted/ai_mock_server.py            (listens on http://127.0.0.1:8765/v1)
       python tests/scripted/ai_mock_server.py 9000       (custom port)
Point LMMS Settings -> AI at base URL http://127.0.0.1:8765/v1, any key, any model.

Endpoints: GET /v1/models, POST /v1/chat/completions (stream true/false).
Each user message restarts the script. Track indices returned by add_instrument_track
are learned from the tool results and substituted for the $T0/$T1 placeholders.
One line per request is printed to stdout: method, path, step, tool names, finish reason.
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8765


def tool(i, name, args):
	return {"id": f"call{i}", "type": "function", "function": {"name": name, "arguments": json.dumps(args)}}


KICK = [{"pos": p, "len": 24, "key": 36} for p in range(0, 768, 96)]
SNARE = [{"pos": p, "len": 24, "key": 38} for p in range(48, 768, 96)]
HAT = [{"pos": p, "len": 12, "key": 42, "vol": 70} for p in range(0, 768, 24)]

SCRIPT = [
	[tool(1, "get_project_summary", {})],
	[tool(2, "set_head", {"bpm": 90})],
	[tool(3, "add_instrument_track", {"name": "Kick", "instrument": "kicker"})],
	[tool(4, "add_notes", {"track": "$T0", "clipPos": 0, "notes": KICK})],
	[tool(5, "add_instrument_track", {"name": "Hats", "instrument": "tripleoscillator"})],
	[tool(6, "add_notes", {"track": "$T1", "clipPos": 0, "notes": HAT})],
	[tool(7, "add_effect", {"track": "$T1", "effect": "amplifier", "params": {"Volume": 60}})],
	[tool(8, "get_project_summary", {})],
]
FINAL_TEXT = "Built a 4-bar drum loop at 90 BPM: Kick and Hats tracks."
STATE = {"step": 0, "tracks": []}


def learn_tracks(msgs):
	"""Collect track indices from add_instrument_track results, in call order."""
	call_names = {}
	for m in msgs:
		if m.get("role") == "assistant":
			for c in m.get("tool_calls") or []:
				call_names[c.get("id")] = (c.get("function") or {}).get("name")
	for m in msgs:
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
		t = a.get("track")
		if isinstance(t, str) and t.startswith("$T"):
			n = int(t[2:])
			if n >= len(STATE["tracks"]):
				raise LookupError(f"placeholder {t} unresolved: learned tracks {STATE['tracks']}")
			a["track"] = STATE["tracks"][n]
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
			STATE["tracks"] = []
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
		print(f"POST {self.path} stream={stream} step={step}/{len(SCRIPT)} "
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
	print(f"mock OpenAI server on http://127.0.0.1:{PORT}/v1", flush=True)
	try:
		ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
	except KeyboardInterrupt:
		pass
