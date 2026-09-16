# AI Composer — design

Date: 2026-09-16
Status: approved design, pending implementation plan

## Goal

An in-app chat panel through which a user instructs an LLM to create and edit
music in the open LMMS project. The AI must be able to do anything LMMS can
persist (tracks, instruments, notes, effects, mixer routing, automation,
tempo, scales, …) plus runtime actions (play, stop, render, save).

Decisions made during brainstorming:

- Output is a native, editable LMMS project (not generated audio).
- Interaction is a chat panel inside LMMS with multi-turn iteration.
- LLM backend is the OpenAI-compatible chat-completions API with tool calling.
  Base URL, key and model are user-configured; the user supplies keys later.
- The agent loop runs inside `lmms.exe` (C++/Qt). No sidecar process.
- Approach C: the project XML (`.mmp` format) is the universal read/write
  surface, plus a thin layer of convenience, discovery and action tools.

## Non-goals (v1)

- Generated-audio providers (Suno/MusicGen style).
- Chat persistence across sessions, multiple sessions, image/audio input.
- Diff preview before applying changes (per-turn revert covers it).
- Anthropic-native or other non-OpenAI-shaped APIs.
- Any network surface exposed *by* LMMS (no HTTP/OSC server).

## 1. Architecture

```
gui/ai/AiChatView  --prompt-->  core/ai/AiSession  --HTTP JSON-->  core/ai/OpenAiClient
                                      |
                                      +--tool call-->  core/ai/AiToolRegistry
                                                            |-- ProjectTools   (DataFile / Track / Song / Mixer)
                                                            |-- DiscoveryTools (PluginFactory, presets, samples)
                                                            +-- ActionTools    (play / stop / render / save / new_project)
gui/SetupDialog "AI" page  --> ConfigManager ai/baseUrl, ai/apiKey, ai/model
```

Placement:

- `src/core/ai/` — `OpenAiClient`, `AiSession`, `AiToolRegistry`,
  `ProjectTools`, `DiscoveryTools`, `ActionTools`. No GUI dependencies except
  where a handler needs `gui::getGUI()` on the GUI thread (documented per
  handler).
- `src/gui/ai/` — `AiChatView`.
- `data/ai/system_prompt.md` — prompt text, loaded at runtime.
- Not a Tool plugin: plugins cannot reach the settings dialog or the View
  menu, and the panel belongs beside Song Editor / Mixer.

Build: add `Network` to the Qt component list in `CMakeLists.txt` and
`Qt::Network` to `QT_LIBRARIES`. The headless `lmms render` path is unchanged.

Config (`ConfigManager`): `ai/baseUrl` (default `https://api.openai.com/v1`),
`ai/apiKey`, `ai/model`. Stored in `.lmmsrc.xml` in plaintext like other
settings; the settings page says so.

### Units and contracts

- **OpenAiClient** — `send(messages, tools)`; emits `textDelta(QString)`,
  `completed(AssistantMessage)`, `failed(QString)`; `abort()`. Knows nothing
  about LMMS.
- **AiToolRegistry** — entries `{name, description, JSON schema, handler}`;
  `handler(QJsonObject args) -> QJsonObject result`; produces the `tools`
  array for the API. Handlers run on the GUI thread.
- **AiSession** — owns message history; runs the loop; snapshots the whole
  project (`Song::saveProjectData` into a `DataFile`) before each user turn and
  `revertLastTurn()` reloads it through `Song::loadProject`; `stop()`, `clear()`.
- **ProjectTools / DiscoveryTools / ActionTools** — handlers, see §2.

## 2. Tool surface

Args and results are JSON. XML is passed as strings. Every result is
`{ok: bool, error?: string, ...}`.

### Project document (universal read/write)

| Tool | Args | Behaviour |
|---|---|---|
| `get_project_summary` | — | Compact JSON: tempo, timesig, length, per-track `{index, type, name, instrument, mixerChannel, clips:[{pos, len, name, noteCount}]}`. The prompt instructs the model to call this first. |
| `get_track_xml` | `index` | `<track>` subtree as produced by `Track::saveSettings`. |
| `add_track` | `xml` | Validate via `DataFile` (upgrade chain, `hasLocalPlugins`), then `Track::create(QDomElement, song)`. Returns new index. |
| `replace_track` | `index, xml` | Remove and recreate at the same index. |
| `remove_track` | `index` | |
| `get_head` / `set_head` | `{bpm, timesigNum, timesigDen, masterVol, masterPitch}` | Via `Song::tempoModel`, `getTimeSigModel`, master models. |
| `get_mixer_xml` / `set_mixer_xml` | `xml` | `Mixer::saveSettings` / `loadSettings`. |

### Convenience (compact JSON for hot paths)

| Tool | Args | Behaviour |
|---|---|---|
| `add_instrument_track` | `name, instrument, preset?, mixerChannel?` | `Track::create` + `InstrumentTrack::loadInstrument`, or `replaceInstrument(DataFile(preset))`. Returns index. |
| `add_notes` | `track, clipPos, notes:[{pos, len, key, vol?, pan?}], clear?` | Ticks (192/bar in 4/4). Creates the clip at `clipPos` if none exists. `MidiClip::addNote(n, false)`. |
| `add_effect` | `track` or `mixerChannel`, `effect, params?:{name: value}` | `Effect::instantiate` + `EffectChain::appendEffect`; params set by `AutomatableModel` display name. |
| `set_params` | `track, target: "instrument" \| "effect:N" \| "track", params:{}` | Walks the model tree by display name. |
| `add_automation` | `track, target, model, points:[{pos, value}], progression?` | `AutomationClip::addObject` + `putValue(t, v, false)`. |
| `add_sample_clip` | `track?, file, pos` | Creates a SampleTrack if needed; `SampleClip::setSampleFile`. |

### Discovery

`list_instruments`, `list_effects` (name, displayName, description from
`PluginFactory::descriptors`), `list_presets(query?)` (walks the factory and
user preset dirs; returns paths), `get_preset_xml(path)`,
`list_samples(query?)`, `describe_model_tree(track)` (parameter names, ranges,
current values for instrument and effects — the vocabulary `set_params`
accepts).

### Actions

`play(fromBar?)`, `stop`, `render(path, format = wav|flac|ogg|mp3)` via
`RenderManager` (tool completes when `finished()` fires), `save(path?)`,
`new_project`.

### Guarantees

- All XML submitted by the model passes through `DataFile`, so version
  upgrades and the `local:` plugin check apply. Malformed XML returns an
  error; the project is untouched.
- Handlers run on the GUI thread inside
  `Engine::audioEngine()->requestChangesGuard()` unless the callee locks
  internally (`Track::create`, `EffectChain::appendEffect`, `Song::stop`,
  `Song::loadProject`) — `std::mutex` is non-recursive.
- XML results are capped at 64 KB per call; larger returns
  `{ok:false, error:"too large; use get_project_summary or a narrower query"}`.

## 3. Agent loop and LLM client

### OpenAiClient (`src/core/ai/OpenAiClient.{h,cpp}`)

- `QNetworkAccessManager`; `POST {baseUrl}/chat/completions`;
  `Authorization: Bearer <key>`; body
  `{model, messages, tools, tool_choice: "auto", stream: true}`.
- Parses SSE `data:` lines. Accumulates `delta.content` (emits `textDelta`)
  and `delta.tool_calls[].function.arguments` fragments by index; emits
  `completed` with the assembled assistant message on `finish_reason`.
- Accepts non-streaming JSON bodies too (proxies that ignore `stream`).
- Non-2xx → `failed(status + body excerpt)`.

### AiSession (`src/core/ai/AiSession.{h,cpp}`)

- History as a `QJsonArray` of OpenAI-shaped messages:
  `system, user, assistant(tool_calls), tool…, assistant…`.
- System prompt = `data/ai/system_prompt.md` + generated appendix: instrument
  and effect names, `.mmp` XML cheat-sheet (head, track, instrumenttrack,
  midiclip/note, automationclip, sampleclip, fxchain), tick units, the
  "call `get_project_summary` first" rule.
- Turn: `ProjectJournal::addJournalCheckPoint()` → send → while the reply
  has tool calls: dispatch sequentially, append `{role:"tool", tool_call_id,
  content}` for each, resend → plain text ends the turn.
- Caps: 40 tool calls per turn, or 5 consecutive tool errors → stop with a
  message to the user.
- Context guard: if the request body exceeds ~120 k characters, replace the
  oldest tool-result contents with `"[elided]"`, keeping user and assistant
  turns.
- `stop()` aborts the in-flight request and ends the turn. `clear()` resets
  history.
- Chat history is session-only; not saved into the project file.

## 4. GUI

### AiChatView (`src/gui/ai/AiChatView.{h,cpp}`)

Registered via `MainWindow::addWindowedWidget`; toggled from
*View → AI Composer* (`Ctrl+Alt+A`) and a toolbar button, following the
Controller Rack pattern.

Layout, top to bottom:

- Transcript: user bubbles; assistant text streamed as it arrives; collapsed
  tool rows (`▸ add_notes(track 2, 32 notes) ✓`) expandable to args/result
  JSON. Tool errors returned to the model in amber; HTTP/config failures in
  red.
- Status line: "Thinking…", "Running add_effect…", "Done (7 tools, 12 s)".
- Input: multi-line `QPlainTextEdit`; Enter sends, Shift+Enter newline.
  Buttons: Send/Stop (toggles during a turn), New chat, Revert turn (enabled
  after a turn that ran tools; reloads the pre-turn snapshot, which also
  discards edits made after the turn).
- No API key configured → the transcript area is replaced by a banner with an
  "Open Settings" link.

### Settings

New *AI* page in `SetupDialog`: Base URL, API key (masked, show toggle),
Model, "Test connection" (`GET {baseUrl}/models`). Note under the key:
stored in plaintext in `.lmmsrc.xml`.

### Project interaction

Tool mutations flow through existing `dataChanged` / `trackAdded` signals, so
Song Editor, Piano Roll and Mixer repaint without extra wiring. Tools that add
a track scroll the Song Editor to it. Revert reloads a full pre-turn project
snapshot: the `ProjectJournal` cannot restore a whole song (`Song::restoreState`
deletes tracks under their views and never covers `<head>`), so journalling is
simply off during a turn.

## 5. Threading, errors, safety

Threads: `QNetworkAccessManager` is asynchronous on the GUI thread; no worker
thread. Tool handlers run on the GUI thread in the reply-finished slot, so
`gui::getGUI()`, widget signals and `Song` mutations are legal. `render` is
the exception: `RenderManager` runs its own thread; the tool returns
`{ok:true, started:true}` and the session waits for `finished()` before
resuming the loop; playback is disabled meanwhile, as in the Export dialog.

Locking: see §2 guarantees. Guard placement is per tool and documented at
each handler.

Errors:

- Tool errors (bad index, unknown plugin, invalid XML, out-of-range tick) →
  `{ok:false, error}` returned to the model so it can self-correct; visible
  in the transcript.
- Network/HTTP/auth/parse failures → the turn ends, a red message appears,
  and history is rolled back to before the failed request so retry is clean.
- Handlers catch everything; the pre-turn snapshot always allows a revert once
  a tool has run.

Safety:

- `DataFile::hasLocalPlugins()` enforced on every XML the model submits.
- `add_sample_clip`, `get_preset_xml`, `render`, `save` accept paths only
  under the project directory, the LMMS data directory, the user working
  directory (`ConfigManager::workingDir`), or a path the user typed into the
  chat during this session. Anything else → error.
- No shell or general file-system tools. The API key is never included in
  prompts or logs.

## 6. Testing

Unit (existing `tests/` CTest layout, QTest):

- `OpenAiClient` SSE parser against recorded fixtures: streamed text,
  streamed multi-tool-call fragments, non-stream body, error body.
- `AiSession` loop against a fake client: dispatch order, per-turn cap,
  consecutive-error cap, history elision, rollback on failure.
- Tool handlers against an in-memory `Song`: `add_notes` → notes readable via
  `MidiClip::notes()`; `add_track(xml)` round-trips `get_track_xml`; invalid
  XML leaves the track count unchanged; path policy rejects paths outside the
  allowed roots.

Smoke: a small local OpenAI-compatible mock server returning canned tool
calls drives "create a 4-bar drum loop" end to end; tracks appear in the Song
Editor and `render` writes a WAV. Then a real provider once a key exists.

Regression: the existing 8 tests stay green; `lmms render tests/emptyproject.mmp`
still works headless.
