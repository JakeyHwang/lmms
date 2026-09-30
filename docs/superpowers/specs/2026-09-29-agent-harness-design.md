# Agent harness — design

Date: 2026-09-29
Status: implemented (plan `../plans/2026-09-29-agent-harness.md`, all tasks landed)
Supersedes: `2026-09-16-ai-composer-design.md` (the in-app LLM chat panel)

## Goal

Turn LMMS into a harness that an external coding agent drives directly. The
tool layer built for the AI Composer (`AiToolRegistry` and the 29 project,
discovery and action tools) stays; the in-process LLM loop, chat panel and
provider settings go. LMMS exposes the tools over a loopback socket, a small
Python client calls them, and a repo-local skill teaches the agent how to make
music that sounds like music.

Why: testing the in-app composer on "Creep" produced tool calls that ran, but
the result was an octave sharp, echoey and synthetic. Causes found in review:

- `data/ai/system_prompt.md` told the model `basenote="57"`; this tree's
  default base key is 69 (`include/Note.h:93`, MIDI numbering), so every
  XML-built track played one octave high.
- The only instruments the prompt offered were raw synth oscillators
  (TripleOscillator, LB302, Watsyn, Kicker). LMMS ships no General MIDI sound
  set, and the prompt applied ReverbSC at defaults to most tracks.
- The model guessed tempo/key/structure instead of researching them.

Decisions made during brainstorming:

- Transport: newline-delimited JSON over TCP on `127.0.0.1`, ephemeral port,
  per-launch token written to a file.
- The chat panel, OpenAI client and LLM settings are removed (clean cutover).
- A General MIDI SoundFont (GeneralUser GS) is fetched into the user's
  SoundFont directory and becomes the default instrument palette.
- Agent knowledge lives in a repo-local skill, `.claude/skills/lmms-composer/`.
- The `Ai*` prefix is kept for retained classes and files.

## Non-goals

- Any in-app chat or model integration. No provider settings.
- Remote access: the server binds loopback only; there is no TLS, no
  multi-user story.
- Audio analysis beyond peak/RMS/clipping/silence checks; the user listens.
- Committing the SoundFont to the repo.

## 1. Architecture

```
agent / script                                  lmms.exe (GUI build)
 .claude/skills/lmms-composer/                  ┌─ core/ai/AiAgentServer   QTcpServer on 127.0.0.1, GUI thread
   scripts/lmmsctl.py   --NDJSON + token-->     │     │ one request at a time
                                                │     v
                                                │ AiToolRegistry ── AiProjectTools / AiDiscoveryTools / AiActionTools
                                                │                   AiPathPolicy (roots only)
                                                │                   AiProjectSnapshot (checkpoint / revert / commit)
                                                └─ SetupDialog "AI" page: enable checkbox + token-file path
```

Ownership: `GuiApplication` creates the server after `MainWindow` exists
(handlers call `gui::getGUI()`), only when `ai/agentserver` is `1`. The
headless `lmms render` path never starts it.

### Removed

| Item | Notes |
|---|---|
| `src/core/ai/OpenAiClient.cpp`, `OpenAiStreamParser.cpp`, `include/OpenAiClient.h`, `OpenAiStreamParser.h`, `AiClient.h` | HTTP client and SSE parser |
| `src/core/ai/AiSession.cpp`, `include/AiSession.h` | agent loop; its snapshot half moves to `AiProjectSnapshot` |
| `src/core/ai/AiPromptBuilder.cpp`, `include/AiPromptBuilder.h`, `data/ai/`, `data/ai/CMakeLists.txt` and its `ADD_SUBDIRECTORY` line | prompt text moves into the skill, corrected |
| `src/gui/ai/AiChatView.cpp`, `include/AiChatView.h`, `GuiApplication::m_aiChatView` / `aiChatView()`, `MainWindow::toggleAiChatWin`, the toolbar button, `Ctrl+Alt+A`, the View-menu entry, the three `reloadConfig()` calls after `SetupDialog::exec()` | |
| `AiConfig` fields `baseUrl`, `apiKey`, `model`, `maxTokens`, `disableThinking`; config keys `ai/baseurl`, `ai/apikey`, `ai/model`, `ai/maxtokens`, `ai/disablethinking` | replaced by one `bool agentServer` / `ai/agentserver` |
| AI settings page widgets except the new checkbox and path label; `SetupDialog::testAiConnection` and the `m_ai*` members | `ConfigTab::AiSettings` stays |
| `AiPathPolicy::allowFromUserText` and `m_files` | no caller once chat is gone |
| `tests/src/core/ai/{OpenAiClient,OpenAiStreamParser,AiSession,AiPromptBuilder}Test.cpp`, their `LMMS_TESTS` entries, `tests/scripted/ai_mock_server.py` | |

Qt `Network` remains in the component list: `QTcpServer` needs it.

### Retained unchanged

`AiToolRegistry`, `AiProjectTools`, `AiDiscoveryTools`, `AiActionTools`,
`AiToolHelpers.h`, `AiPathPolicy` (roots + `allows`), their tests.

### New

- `include/AiAgentServer.h`, `src/core/ai/AiAgentServer.cpp` — §2.
- `include/AiProjectSnapshot.h`, `src/core/ai/AiProjectSnapshot.cpp` — §3.
- Meta-tools and `add_sf2_track` — §3.
- `.claude/skills/lmms-composer/` — §5.

## 2. AiAgentServer

```cpp
class LMMS_EXPORT AiAgentServer : public QObject
{
public:
	static constexpr int MaxLineBytes = 4 * 1024 * 1024;
	//! Listens on 127.0.0.1 (ephemeral port unless `port` > 0), writes the token file.
	//! Returns false and listens on nothing if bind or the file write fails.
	bool start(AiToolRegistry* registry, const QString& tokenFilePath, quint16 port = 0);
	void stop();          // closes sockets, removes the token file
	quint16 port() const;
	QString token() const;
	static QString defaultTokenFilePath(); // ConfigManager::workingDir() + ".lmms-agent.json"
};
```

Token file: `{"port": N, "token": "<64 hex chars from QRandomGenerator::system()>"}`,
written with `QSaveFile`. Removed in `stop()`, which the destructor calls.

Wire format, one JSON object per `\n`-terminated line, UTF-8:

```
→ {"id": 7, "token": "…", "tool": "add_notes", "args": {…}}
← {"id": 7, "result": {"ok": true, "clipPos": 0, "noteCount": 32}}
← {"id": 7, "error": "unknown tool 'add_note'"}
```

- `id` is echoed verbatim (any JSON value; absent → `null`).
- `args` optional, defaults to `{}`.
- `error` covers transport problems only: line not a JSON object, missing or
  non-string `tool`, unknown tool, bad token, line over `MaxLineBytes`. Tool
  failures stay in `result` as `{ok:false, error}` — the existing contract.
- Bad token → error response, then the socket is closed. Over-long line →
  error, socket closed.
- Dispatch happens on the GUI thread in the `readyRead` slot. A
  `m_dispatching` flag makes re-entrant `readyRead` calls (possible while
  `render` spins its nested event loop) return without dispatching; the outer
  call drains any buffered lines after the handler returns. Requests are
  therefore answered strictly in arrival order, per socket and across sockets.
- Per-socket read buffers are `QHash<QTcpSocket*, QByteArray>`; a socket is
  forgotten on `disconnected`.

## 3. Tools added

Registered in the same `AiToolRegistry` so `list_tools` describes them.

| Tool | Args | Behaviour |
|---|---|---|
| `ping` | — | `{ok:true, version: LMMS_VERSION}` |
| `list_tools` | — | `{ok:true, tools: <registry specs()>}` — the JSON schemas |
| `checkpoint` | — | `AiProjectSnapshot::take()`: `Song::saveProjectData` into memory, journalling off. A second `checkpoint` replaces the snapshot. |
| `revert` | — | `AiProjectSnapshot::restore()`: reload the snapshot via the temp-file path `AiSession::revertLastTurn` used; project file name and modified flag restored; journalling on; snapshot cleared. Error if none held. |
| `commit` | — | drop the snapshot, journalling on. Error if none held. |
| `add_sf2_track` | `name, file, bank?=0, patch?=0, mixerChannel?` | Builds `<track type="0" name><instrumenttrack …><instrument name="sf2player"><sf2player src bank patch/></instrument></instrumenttrack></track>` and routes it through the same validation as `add_track`. `file` gated by the path policy. Returns `{index}`. GM drum kits are bank 128. |
| `export_midi` | `path` (required) | Added after this design landed, for DAW/GarageBand hand-off. Instantiates the `midiexport` plugin and calls `ExportFilter::tryExport` over `Song::tracks()` + `Engine::patternStore()->tracks()` (not `Song::exportProjectMidi`, which swallows failures); `.mid` appended when missing, same path-policy gate as `save`/`render`. Returns `{path, bytes}`. |
| `import_midi` | `path` (required), `soundfont?` | Added after this design landed, for faithful recreation from a real transcription. Drives the `midiimport` plugin through `ImportFilter::import`: one `sf2player` track per MIDI channel on the default SoundFont (channel 10 → drum bank 128), plus tempo and time-signature automation tracks. Every condition the plugin would report in a modal dialog is checked first (playback stopped, path allowed, `MThd`/`RIFF` magic, default SoundFont set, plugin present), because a blocking handler hangs the server. Returns `{tracksAdded, tracks:[{index,name}]}`. |

`AiProjectSnapshot` is the extracted `beginTurnCheckpoint` / `revertLastTurn`
logic from `AiSession.cpp:206-249`, as a plain class with `take()`, `restore()`,
`drop()`, `held()`. Registered by a new `registerAiMetaTools(AiToolRegistry&, AiProjectSnapshot&)` in `AiTools.h`; `add_sf2_track` lives in `AiProjectTools.cpp` beside `add_instrument_track`.

Meta-tools are registered by the server's owner, not by `AiAgentServer`
itself, so tests can run the server against a registry with only `ping`.

## 4. Settings page

`ConfigTab::AiSettings` keeps its tab. Contents:

- Checkbox "Allow an external agent to control LMMS over localhost (takes
  effect after restart)". Bound to `AiConfig::agentServer`.
- Read-only label: "Connection details are written to `<path>` while LMMS
  runs." with `AiAgentServer::defaultTokenFilePath()`.
- One sentence that the token grants full control of the open project to any
  local process that can read the file.

`AiConfig` becomes `struct { bool agentServer = true; static load(); static save(); }` —
on by default (user decision 2026-09-30); an absent `ai/agentserver` key means enabled, `"0"` disables.

## 5. Skill: `.claude/skills/lmms-composer/`

```
SKILL.md
references/tools.md       one section per tool: args, result, gotchas (from list_tools + handler source)
references/theory.md      ticks, keys (MIDI numbering, basenote 69), scales, chords, drum map, GM patch table
scripts/lmmsctl.py        client CLI + importable class
scripts/fetch_soundfont.py
scripts/check_render.py
```

`SKILL.md` frontmatter description triggers on any request to create, edit,
arrange or mix music in LMMS, or to recreate a song, in this repo. Body:

1. **Connect.** Read `.lmms-agent.json`; if missing, tell the user to enable
   the setting and restart, or start LMMS via the process supervisor.
   `lmmsctl.py tools` confirms the link.
2. **Research first.** For a named song, artist, genre or mood: look up tempo,
   key, chord progression, form (section map with bar counts), instrumentation
   and feel with `web_search`; state them with sources before building. Never
   guess a named song's tempo or key. Melodic lines are written from that
   research and the agent's ear, not transcribed note-for-note.
3. **Checkpoint**, then `get_project_summary`, `set_head`.
4. **Palette.** Default to `add_sf2_track` with GeneralUser GS patches (table
   in `theory.md`): kit on bank 128, bass, guitars, keys, strings, pads.
   Use LMMS synths when the style is synth-native (EDM, chiptune, acid).
5. **Write sections** with `add_clips`, one call per track, absolute ticks.
6. **Mix discipline.** Levels per role; reverb only on pads/leads/keys and at
   ≤ 25 % wet (or one send channel); drums and bass dry; no effect unless it
   serves a stated purpose; check `describe_model_tree` before `set_params`.
7. **Verify.** `render` to `<workingDir>/renders/<name>.wav`, run
   `check_render.py` (peak dBFS, RMS, clipped-sample count, silent bars); fix
   what it flags; `get_project_summary` to confirm the section map.
8. **Hand over.** `commit` (or `revert` on request), `save` if asked, report
   the section map and one line per track.

`lmmsctl.py` (stdlib only):

- `lmmsctl.py tools` — names + one-line descriptions; `--schema` prints JSON.
- `lmmsctl.py summary` — `get_project_summary` pretty-printed.
- `lmmsctl.py call <tool> [<json>] [--args-file f] [-]` — args from an inline
  string, a file, or stdin (Windows shell quoting makes inline JSON painful).
- `from lmmsctl import Lmms; lmms = Lmms(); lmms.call("add_clips", {...})` —
  raises `LmmsError` on transport errors, returns the `result` object
  otherwise; `lmms.ok("add_clips", {...})` additionally raises on
  `ok:false`. Per-song build scripts use this.
- Connection: `LMMS_AGENT_FILE` env, else `~/lmms/.lmms-agent.json`; the
  working directory is resolved the way `ConfigManager` does by default
  (`$HOME/lmms/`).

`fetch_soundfont.py`: downloads GeneralUser GS (URL and SHA-256 pinned in the
script) into the LMMS SoundFont directory (`~/lmms/samples/soundfonts/`, or
`--dest`), skips when the checksum already matches, prints the absolute path.
The licence file that ships in the archive is kept next to the `.sf2`.

`check_render.py <wav> [--bars N --ticks-per-bar 192 --bpm B]`: 16/24/32-bit
PCM WAV via stdlib `wave`; prints peak dBFS, RMS dBFS, count of samples at
full scale, and, when tempo is given, which bars are silent (< −60 dBFS RMS).
Exit code 1 on clipping or an all-silent file.

## 6. Threading, errors, safety

- Server and handlers on the GUI thread, as before; `requestChangesGuard` use
  inside handlers is unchanged.
- The server is started from `main()` once the initial project exists
  (`GuiApplication::startAgentServer()`), never from the `GuiApplication` constructor: no request
  may be dispatched while the recovery dialog spins its nested loop or before
  `loadProject()`/`createNewProject()` has run.
- `render` still blocks in its nested event loop; the dispatch guard in §2
  keeps other requests queued until it returns.
- Token file lives in the user's working directory; anyone able to read it
  controls the open project. This is stated on the settings page. The server
  never binds a non-loopback address.
- Path policy roots unchanged: project directory, LMMS data directory,
  working directory. The SoundFont directory and `renders/` are under the
  working directory, so no new roots.
- A failed `start()` (port bind, token write) logs to `qWarning` and LMMS runs
  without the server; nothing else changes.

## 7. Testing

- `tests/src/core/ai/AiAgentServerTest.cpp` (new `LMMS_TESTS` entry):
  registry with `ping` only; server on an ephemeral port with a temp token
  file; `QTcpSocket` client. Cases: `ping` round-trip echoes `id` and returns
  `ok:true`; wrong token → `error` then `disconnected`; malformed JSON →
  `error`, socket still open, a following `ping` works; unknown tool →
  `error`; two requests in one `write` answered in order; token file exists
  while running with the right port and is gone after `stop()`.
- `AiProjectToolsTest`: `checkpoint` → `add_track` (instrument-less XML, which
  loads headless) → `revert` restores track count and tempo; `commit` then `revert` errors;
  `revert` without a checkpoint errors.
- `add_sf2_track`: schema validation runs headless (missing file → error;
  path outside roots → error); the plugin-loading path `QSKIP`s like the other
  plugin cases.
- `AiPathPolicyTest`: cases for `allowFromUserText` removed.
- Smoke, in-app: enable the setting, restart, `lmmsctl.py tools` lists 35
  tools, `fetch_soundfont.py`, build an 8-bar drum-kit + bass sketch through
  `add_sf2_track` / `add_clips`, `render`, `check_render.py` reports no
  clipping and no silent bars. The user listens.
- Regression: the 8 upstream tests and the 5 retained AI tests stay green;
  `lmms render tests/emptyproject.mmp` still works.

## 8. Documentation

- This file; the 2026-09-16 spec gets `Status: superseded by
  2026-09-29-agent-harness-design.md` at the top and is otherwise left.
- `CLAUDE.md` auto blocks are resynced after implementation (project
  structure, build-and-test test list, layout rules: `Ctrl+Alt+A` is free
  again, `data/ai/` is gone, active-specs table).
