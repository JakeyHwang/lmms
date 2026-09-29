import json, os, socket, socketserver, tempfile, threading, unittest
from unittest import mock
from pathlib import Path
import lmmsctl

TOKEN = "a" * 64

class Handler(socketserver.StreamRequestHandler):
    def handle(self):
        for raw in self.rfile:
            req = json.loads(raw)
            rid = req.get("id")
            if req.get("token") != TOKEN:
                self.wfile.write(json.dumps({"id": rid, "error": "bad token"}).encode() + b"\n"); return
            tool = req.get("tool")
            if tool == "ping": out = {"id": rid, "result": {"ok": True, "version": "t"}}
            elif tool == "fail": out = {"id": rid, "result": {"ok": False, "error": "nope"}}
            elif tool == "echo": out = {"id": rid, "result": {"ok": True, "args": req.get("args", {})}}
            elif tool == "list_tools": out = {"id": rid, "result": {"ok": True, "tools": [{"type": "function", "function": {"name": "ping", "description": "d", "parameters": {}}}]}}
            else: out = {"id": rid, "error": f"unknown tool '{tool}'"}
            self.wfile.write(json.dumps(out).encode() + b"\n")

class LmmsctlTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.srv = socketserver.ThreadingTCPServer(("127.0.0.1", 0), Handler)
        threading.Thread(target=cls.srv.serve_forever, daemon=True).start()
        cls.tmp = tempfile.TemporaryDirectory()
        cls.file = Path(cls.tmp.name) / "agent.json"
        cls.file.write_text(json.dumps({"port": cls.srv.server_address[1], "token": TOKEN}))
        os.environ["LMMS_AGENT_FILE"] = str(cls.file)
    @classmethod
    def tearDownClass(cls):
        cls.srv.shutdown(); cls.tmp.cleanup()

    def test_call_returns_result(self):
        with lmmsctl.Lmms() as l:
            self.assertEqual(l.call("ping")["version"], "t")
            self.assertEqual(l.call("echo", {"x": [1, 2]})["args"], {"x": [1, 2]})
    def test_ok_raises_on_tool_error_but_call_does_not(self):
        with lmmsctl.Lmms() as l:
            self.assertFalse(l.call("fail")["ok"])
            with self.assertRaises(lmmsctl.LmmsToolError): l.ok("fail")
    def test_transport_error_raises(self):
        with lmmsctl.Lmms() as l:
            with self.assertRaises(lmmsctl.LmmsError): l.call("nope")
    def test_bad_token_raises(self):
        bad = Path(self.tmp.name) / "bad.json"
        bad.write_text(json.dumps({"port": self.srv.server_address[1], "token": "z" * 64}))
        with self.assertRaises(lmmsctl.LmmsError): lmmsctl.Lmms(bad).call("ping")
    def test_missing_file_raises_helpful_error(self):
        with self.assertRaisesRegex(lmmsctl.LmmsError, "Settings"): lmmsctl.Lmms(Path(self.tmp.name) / "none.json").call("ping")
    def test_cli_call_and_exit_codes(self):
        self.assertEqual(lmmsctl.main(["call", "ping"]), 0)
        self.assertEqual(lmmsctl.main(["call", "fail"]), 1)
        self.assertEqual(lmmsctl.main(["call", "nope"]), 2)
        self.assertEqual(lmmsctl.main(["tools"]), 0)
    def test_cli_args_from_file(self):
        f = Path(self.tmp.name) / "args.json"; f.write_text('{"k": 7}')
        self.assertEqual(lmmsctl.main(["call", "echo", "--args-file", str(f)]), 0)
    def test_env_override_wins(self):
        self.assertEqual(lmmsctl.token_file_path(), self.file)
        self.assertEqual(lmmsctl.working_dir(), self.file.parent)
    def test_falls_back_to_onedrive_documents(self):
        with tempfile.TemporaryDirectory() as home:
            found = Path(home) / "OneDrive" / "Documents" / "lmms" / ".lmms-agent.json"
            found.parent.mkdir(parents=True)
            found.write_text("{}")
            env = {k: v for k, v in os.environ.items() if k != "LMMS_AGENT_FILE"}
            with mock.patch.dict(os.environ, env, clear=True), mock.patch.object(lmmsctl.Path, "home", lambda: Path(home)):
                self.assertEqual(lmmsctl.token_file_path(), found)
                self.assertEqual(lmmsctl.working_dir(), found.parent)

if __name__ == "__main__": unittest.main()
