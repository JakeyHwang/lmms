# LMMS — agent notes

Working notes for agents in this fork (`origin` → `JakeyHwang/lmms`, `upstream` → `lmms/lmms`).

`README.md` is upstream's file and is left alone — it tracks `upstream` and carries no agent-managed
content. Repo-state notes live here and in `.claude/context/PROJECT_CONTEXT.md` instead.

Blocks marked `claude:auto` are machine-maintained by the context-steward. Change the repo, not the
block; the next sync rewrites them from what it measures.

## Project structure

<!-- claude:auto:project-structure -->
| Path | What it holds |
|---|---|
| `src/core/` | Engine: `Song`, `Track`, `Mixer`, `DataFile`, `AudioEngine`, `ProjectJournal`, `RenderManager`, `PluginFactory`. Subdirs `audio/`, `lv2/`, `midi/`, `ai/` (agent harness: `AiAgentServer`, `AiToolRegistry`, `AiProjectTools`, `AiDiscoveryTools`, `AiActionTools`, `AiMetaTools`, `AiProjectSnapshot`, `AiPathPolicy`, `AiConfig`, plus the non-exported `AiToolHelpers.h`). |
| `src/gui/` | Qt widgets. Subdirs `editors/` (SongEditor, PianoRoll, AutomationEditor), `modals/` (SetupDialog, ExportProjectDialog, `*.ui`), `widgets/`, `tracks/`, `clips/`, `instrument/`, `menus/`. There is no `gui/ai/`: the in-app chat panel was removed in this fork's harness cutover. |
| `src/tracks/` | Track implementations (instrument, sample, pattern, automation). |
| `src/common/`, `src/3rdparty/` | Shared helpers; vendored `ringbuffer`, `weakjack`, `qt5-x11embed`, `jack2`. |
| `include/` | **All first-party headers, flat** — ~300 files, no subdirectories. Harness headers: `AiAgentServer.h`, `AiProjectSnapshot.h`, `AiToolRegistry.h`, `AiTools.h`, `AiPathPolicy.h`, `AiConfig.h`. |
| `plugins/` | 59 plugin directories (instruments, effects, tools), each its own CMake target. |
| `data/` | Seven installed asset directories: `presets/`, `samples/`, `themes/`, `projects/`, `wavetables/`, `backgrounds/`, `locale/` — one `ADD_SUBDIRECTORY` line each in `data/CMakeLists.txt`. |
| `tests/` | QTest executables — 8 upstream plus 5 under `tests/src/core/ai/` (`AiPathPolicy`, `AiActionTools`, `AiProjectTools`, `AiToolRegistry`, `AiAgentServer`) — plus `emptyproject.mmp` and `scripted/`, which now holds only upstream's `README.md`, `check-namespace`, `check-strings`, `verify`. |
| `doc/` | Man page, Doxyfile, AUTHORS/CONTRIBUTORS, `wiki/`. |
| `cmake/` | `modules/`, `toolchains/`, `install/`, `nsis/`, `apple/`, `linux/`. |
| `docs/superpowers/` | Fork-local agent docs: `specs/` (approved designs), `plans/` (task-by-task implementation plans). |
| `.claude/` | Repo-local agent assets, tracked: `skills/lmms-composer/` (`SKILL.md`, `references/theory.md`, `references/tools.md`, `scripts/lmmsctl.py`, `check_render.py`, `fetch_soundfont.py` and their `unittest` files) and `context/` (steward state; `ledger.jsonl`, `.baseline.json`, `.sync-state.json` are ignored by `.claude/context/.gitignore`). |

Entry points:

- `src/core/main.cpp` → the `lmms` executable (`src/CMakeLists.txt:114`). Argument parsing lives
  here; `render` / `--render` / `-r` sets `coreOnly` (`main.cpp:273`), which is the headless path
  (`lmms render <project.mmp>`).
- Everything else compiles into the `lmmsobjs` OBJECT library (`src/CMakeLists.txt:104`), which both
  `lmms` and every test link against.
- GUI bootstrap: `src/gui/MainApplication.cpp` → `GuiApplication.cpp` → `MainWindow.cpp`.
- Agent entry: `GuiApplication::startAgentServer()` (`src/gui/GuiApplication.cpp:207-241`), called
  from `src/core/main.cpp:915` **after** the initial project is loaded or created — not from the
  `GuiApplication` constructor, so an agent can never reach a song that does not exist yet. It is a
  no-op when already running or when `ai/agentserver` is `"0"` — on by default, an absent key
  meaning enabled (`AiConfig::load`, `src/core/ai/AiConfig.cpp:32-39`). It registers all four tool
  tables into the `AiToolRegistry` it owns and listens on `127.0.0.1`, ephemeral port. The external
  half is `.claude/skills/lmms-composer/scripts/lmmsctl.py`; there is no in-app UI for it beyond the
  settings checkbox.
<!-- /claude:auto:project-structure -->

## Build and test

<!-- claude:auto:build-and-test -->
- CMake, C++20 (`src/CMakeLists.txt:25`). Build instructions are upstream's
  [Compiling LMMS](https://github.com/LMMS/lmms/wiki/Compiling) wiki page, linked from `README.md`.
- Qt5 or Qt6, selected by `WANT_QT6`. Components requested:
  `Core Gui Widgets Xml Svg Network` (`CMakeLists.txt:278`). `Network` is linked in three places —
  the component list, `${Qt${QT_VERSION_MAJOR}Network_INCLUDE_DIRS}` in the
  `include_directories(SYSTEM …)` block (`CMakeLists.txt:288`), and `Qt${QT_VERSION_MAJOR}::Network`
  at the end of `set(QT_LIBRARIES …)` (`CMakeLists.txt:291-298`). It was added for the AI Composer
  and is still required: `AiAgentServer` is a `QTcpServer`.
- `vcpkg.json` pins Windows dependencies.
- Tests: CTest + QTest, one executable per entry in `set(LMMS_TESTS …)` (`tests/CMakeLists.txt:6`) —
  **13** entries: 8 upstream plus
  `src/core/ai/{AiPathPolicy,AiActionTools,AiAgentServer,AiProjectTools,AiToolRegistry}Test.cpp`.
  Each links `lmmsobjs` and is built with `LMMS_TESTING` defined.
- The skill's Python half has its own `unittest` suite, stdlib only, no CTest entry:
  `python -m unittest discover -s .claude/skills/lmms-composer/scripts -p 'test_*.py'` (15 tests).
- Headless tests cannot load instrument/effect plugin DLLs in this build (the plugins import symbols
  from `lmms.exe`), so plugin-dependent tool cases `QSKIP` — those paths are only covered by the
  live agent smoke.
- Live smoke: the server is on by default; untick *Settings → AI → "Allow an external agent to
  control LMMS over localhost"* (i.e. `<ai agentserver="0"/>` in `.lmmsrc.xml`) to disable it.
  Start `build/lmms.exe`, wait for
  `AiAgentServer: listening on 127.0.0.1:<port>`, then
  `python .claude/skills/lmms-composer/scripts/lmmsctl.py tools` — it lists **35** tools.
  `check_render.py` sanity-checks the resulting `.wav` (peak dBFS, clipped samples, silent bars).
- Local build in this fork is MSYS2 CLANG64 + Ninja in `build/`. A plain login shell has no `cmake`
  on `PATH`; the working form is
  `C:/msys64/usr/bin/bash.exe -lc 'export MSYSTEM=CLANG64 PATH=/clang64/bin:$PATH; cd /c/git_repos/lmms && cmake --build build'`.
  QTest executables print nothing to the MSYS console — run `build/tests/<Name>.exe -o <file>,txt`
  to read results.
- A dev build keeps its `.lmmsrc.xml` beside the executable, not in `$HOME`
  (`ConfigManager::initDevelopmentWorkingDir`, `src/core/ConfigManager.cpp:717`), while
  `workingDir()` still points at `Documents/lmms`. That is why `lmmsctl.py` searches a candidate
  list (`$LMMS_AGENT_FILE`, `~/.lmmsrc.xml`'s `workingdir`, `~/Documents/lmms`,
  `~/OneDrive/Documents/lmms`, `~/lmms`) for `.lmms-agent.json` rather than one path.
- Formatting and linting config present: `.clang-format`, `.clang-tidy`, `.editorconfig`,
  `.yamllint`.
<!-- /claude:auto:build-and-test -->

## Layout rules the build actually enforces

<!-- claude:auto:code-layout -->
- **Source lists are explicit — `.cpp` files are never globbed.** A new `.cpp` must be added by hand
  to the `set(LMMS_SRCS ${LMMS_SRCS} … PARENT_SCOPE)` list in the owning `src/<dir>/CMakeLists.txt`
  (pattern: the list filling most of `src/core/CMakeLists.txt`, where the harness's `core/ai/*.cpp`
  entries form their own blank-line-separated alphabetical group after `core/StepRecorder.cpp`,
  `src/core/CMakeLists.txt:94-102`). A file not in that list is silently not compiled. Headers are
  the exception — see the `GLOB` note below.
- **Headers go in `include/`, flat.** The sole first-party header under `src/` is
  `src/core/UpgradeExtendedNoteRange.h`, named in the source list explicitly
  (`src/core/CMakeLists.txt:87`); the one deliberate exception is `src/core/ai/AiToolHelpers.h`,
  which is include-only and therefore in no list at all.
- `AUTOMOC` and `AUTOUIC` are on (`src/CMakeLists.txt:20-21`); `.ui` files are searched for in
  `gui/modals` only (`AUTOUIC_SEARCH_PATHS`, `src/CMakeLists.txt:202`).
- A new `data/` subdirectory needs its own `CMakeLists.txt` plus an `ADD_SUBDIRECTORY` line in
  `data/CMakeLists.txt`, or it is not installed. `data/CMakeLists.txt` is `ADD_SUBDIRECTORY(…)` lines
  only; each subdirectory owns its install rule, and every one of the seven uses
  `INCLUDE(InstallHelpers)` + `INSTALL_DATA_SUBDIRS(…)`, which globs *sub*directories. Installing
  loose files sitting directly in the directory needs `FILE(GLOB …)` + `INSTALL(FILES … DESTINATION
  "${LMMS_DATA_DIR}/<dir>")` instead; the former `data/ai/` was the only example of that form and
  was deleted with the chat panel, so there is none left in tree.
- Panel toggles are `Ctrl+1` … `Ctrl+7`, bound to toolbar buttons in `MainWindow.cpp:431-458`
  (Song Editor, Pattern Editor, Piano Roll, Automation Editor, Mixer, Controller Rack, Project
  Notes). `Ctrl+8` has no window-level binding, and **`Ctrl+Alt+A` is free again** — the AI Composer
  toolbar button and its View-menu entry went with the panel. The only `Ctrl+Alt` combination
  `MainWindow` still claims is `Ctrl+Alt+S` (save as new version, `MainWindow.cpp:300-301`).
- **Checking whether a shortcut is free means grepping the editors' `keyPressEvent` switches, not
  just existing `setShortcut` calls.** A `MainWindow` toolbar-button/`QAction` shortcut has
  `Qt::WindowShortcut` context, so it fires before any MDI child editor sees the key. `Ctrl+Shift+A`
  is *not* free for that reason: `PianoRoll.cpp:1453-1461` uses it for "deselect all notes"
  (`clearSelectedNotes()`) and `SongEditor.cpp:511-513` for deselect-all-clips
  (`selectAllClips(!isShiftPressed)`). Both test `modifiers() & Qt::ControlModifier`
  non-exclusively, so *any* `Ctrl`+extra+`A` combination lands in their select-all branch unless a
  window-level shortcut claims it first — which is exactly what nothing does now that `Ctrl+Alt+A`
  is unbound.
- Subwindow panels register through `MainWindow::addWindowedWidget` (`include/MainWindow.h:69`);
  `src/gui/ControllerRackView.cpp:82` is the reference example. Nothing in the harness uses it.
- **With a GUI, never `delete` a `Track` before its `TrackView`.** `~InstrumentTrackView` reaches
  back through `model()` to tear down the instrument window and the MIDI-port menus, and the view
  is only closed by `destroyedTrack` and destroyed on the next event-loop pass — so deleting the
  model first leaves the view reading freed memory. Delete through
  `TrackContainerView::deleteTrackView` (`include/TrackContainerView.h:157`,
  `src/gui/editors/TrackContainerView.cpp:287`), which does view-then-track under the audio-engine
  change lock. Headless there is no view: `~Track` unlinks itself from the container and the caller
  takes `requestChangesGuard` itself. `deleteTrack` in `src/core/ai/AiProjectTools.cpp:441-458` is
  the reference for handling both, including the `sendPostedEvents()` needed because a view queued
  by `trackAdded` may not exist yet.
- **`include/*.h` is globbed at configure time** — `FILE(GLOB LMMS_INCLUDES …)`
  (`CMakeLists.txt:731`), compiled into `lmmsobjs` through `${LMMS_INCLUDES}`
  (`src/CMakeLists.txt:106`). A new header is invisible until CMake re-runs, and for a `Q_OBJECT`
  header AUTOMOC must also re-scan, which has not happened on its own in this tree: the link fails
  with `undefined symbol` on the class's signals or `vtable for lmms::<Class>`. Working sequence:
  `cmake build`, then `rm -f build/src/lmmsobjs_autogen/timestamp`, then `cmake --build build`.
  `include/AiAgentServer.h` is the current `Q_OBJECT` header that needed it.
- The Settings dialog is opened by the existing slot `MainWindow::showSettingsDialog()`
  (`include/MainWindow.h:156`, defined `src/gui/MainWindow.cpp:887`) — reuse it rather than adding
  another entry point. It is a plain `sd.exec()`; nothing reloads config behind it any more, and the
  AI page's one setting is documented as taking effect after restart.
<!-- /claude:auto:code-layout -->

## Active design work

<!-- claude:auto:active-specs -->
| Doc | Status | Scope |
|---|---|---|
| `docs/superpowers/specs/2026-09-29-agent-harness-design.md` | **Governing design, implemented.** Its header line `:4` now reads `Status: implemented (plan …, all tasks landed)` (`801be6760`); the stale `approved design, pending implementation plan` line the last sync flagged is gone. | External coding agent drives LMMS over loopback NDJSON instead of an in-app LLM. Keeps `AiToolRegistry` and the project/discovery/action tools; adds `AiAgentServer`, `AiProjectSnapshot`, five meta-tools and `add_sf2_track`; deletes everything that talked to a model. |
| `docs/superpowers/plans/2026-09-29-agent-harness.md` | Implementation plan, 11 tasks / **59 of 59 step boxes `[x]`** — Task 11's own "dispatch the context-steward and relay its report" was flipped in `805c31c5c`'s range. The only remaining `- [ ]` in the file is the boilerplate example in the for-agentic-workers preamble (`:3`), not a task. Task 10's success criterion is the user's ear on the shared render, and that verdict is still pending. | Build order: snapshot + meta-tools + `add_sf2_track` (1), `AiAgentServer` (2), removal of the in-app LLM path in a worktree lane (3), settings checkbox and start-up wiring (4), `lmmsctl.py` (5), `check_render.py` (6), `fetch_soundfont.py` (7), skill doc and references (8), integration (9), live music smoke (10), docs sync (11). |
| `docs/superpowers/specs/2026-09-16-ai-composer-design.md` | **Superseded**, and now marked so in the file itself (`:4`, commit `835de8f83`). History, not a build target: the panel, client, session and prompt it describes are all deleted. Its §2 tool table still describes the tool layer, which lives on behind the agent server. | In-app AI chat panel driving the open project through the `.mmp` XML surface; OpenAI-compatible chat-completions with tool calling, agent loop in-process. |
| `docs/superpowers/plans/2026-09-16-ai-composer.md` | Closed at **60/60**, and now plans a superseded design. Untouched by this range. | The original 13-task AI Composer build order. |

**What the harness cutover actually did**, `a929195ee..047bd874e`, 58 files, +4435/−2417:

- **Deleted**: `OpenAiClient`, `OpenAiStreamParser`, `AiClient.h`, `AiSession`, `AiPromptBuilder`,
  `gui::AiChatView` (and the whole `src/gui/ai/` directory), all of `data/ai/`,
  `tests/scripted/ai_mock_server.py`, and the four matching tests
  (`OpenAiClientTest`, `OpenAiStreamParserTest`, `AiSessionTest`, `AiPromptBuilderTest`). Nothing in
  the tree talks to a model any more, and there is no system prompt asset — the model-facing
  knowledge moved into `.claude/skills/lmms-composer/`.
- **Added**: `include/AiAgentServer.h` + `src/core/ai/AiAgentServer.cpp` (loopback NDJSON server),
  `include/AiProjectSnapshot.h` + `src/core/ai/AiProjectSnapshot.cpp`,
  `src/core/ai/AiMetaTools.cpp`, `tests/src/core/ai/AiAgentServerTest.cpp`, and the repo-local
  skill under `.claude/skills/lmms-composer/`.
- **Changed**: `AiConfig` is now a single `bool agentServer` behind `ConfigManager` class `"ai"`,
  attribute `agentserver` (`src/core/ai/AiConfig.cpp:33-42`) — `baseurl`, `apikey`, `model`,
  `maxtokens` and `disablethinking` are all gone. `AiPathPolicy` lost `allowFromUserText` and its
  user-named-file set; it is roots-only now (`include/AiPathPolicy.h:36-54`). The `AiSettings` page
  in `SetupDialog` is one checkbox plus the token-file path and a warning label
  (`src/gui/modals/SetupDialog.cpp:885-898`); the `ConfigTab` enum entry stays
  (`include/SetupDialog.h:55-63`).

**The wire protocol and the server.** Request `{"id", "token", "tool", "args"}`, response
`{"id", "result"}` — `{"id", "error"}` is for transport problems only (bad JSON, missing or
non-string `tool`, unknown tool, bad token, a line over `MaxLineBytes` = 4 MiB); a tool that fails
still answers `result.ok=false`. Binds `127.0.0.1` only, ephemeral port. Port and a per-launch token
are written to `ConfigManager::inst()->workingDir() + ".lmms-agent.json"`
(`AiAgentServer::defaultTokenFilePath()`, `src/core/ai/AiAgentServer.cpp:47-50`) and the file is
removed on `aboutToQuit` — `main.cpp` never deletes the `GuiApplication`, so the destructor would
not run (`src/gui/GuiApplication.cpp:238-240`). The token file is also narrowed to
`ReadOwner | WriteOwner` right after it is written (`AiAgentServer::start`,
`src/core/ai/AiAgentServer.cpp:75-77`) — best effort, unchecked, because on Windows Qt maps that
onto the read-only attribute only. Handlers run on the GUI thread, one at a time, in
arrival order; bytes arriving mid-handler are dispatched after it returns (`m_dispatching`).
The server is started late on purpose: `GuiApplication::startAgentServer()` is called from
`src/core/main.cpp:915`, after the recovery prompt and after `loadProject`/`createNewProject`, so
the first request can never land while the song is being set up (the old constructor-time start
let an agent remove tracks that project setup then deleted again).

**35 registered tools**, across four tables: `add_automation`, `add_clips`, `add_effect`,
`add_instrument_track`, `add_notes`, `add_sample_clip`, `add_sf2_track`, `add_track`, `checkpoint`,
`commit`, `describe_model_tree`, `get_head`, `get_mixer_xml`, `get_preset_xml`,
`get_project_summary`, `get_track_xml`, `list_effects`, `list_instruments`, `list_presets`,
`list_samples`, `list_tools`, `new_project`, `ping`, `play`, `remove_clip`, `remove_track`,
`render`, `replace_track`, `revert`, `save`, `set_head`, `set_mixer_xml`, `set_params`, `set_track`,
`stop`. Registration order is project (20), discovery (5), action (5), meta (5) —
`src/gui/GuiApplication.cpp:232-235`. XML tool results cap at 64 KB (`MaxXmlBytes`,
`src/core/ai/AiProjectTools.cpp:350`). `.claude/skills/lmms-composer/references/tools.md` documents
all 35 in prose; `lmmsctl.py tools --schema` is the authoritative schema.

**Undo/redo is now explicit, not per-turn.** `AiProjectSnapshot` holds one whole-project buffer:
`checkpoint` calls `take()` (`Song::saveProjectData(DataFile&)`, `include/Song.h:256`, and turns
journalling off), `revert` calls `restore()` (writes a temp file, **fails if the flush fails**, and
`Song::loadProject`s it), `commit` calls `drop()`. `revert` and `commit` error with
"No checkpoint held" when none is.
`AiSession`'s automatic per-turn revert went with the session.

Two engine-wide changes predate the cutover and survive it: `EffectChain::effects()`
(`include/EffectChain.h:70`), and `DataFile::findProblematicLadspaPlugins()` raising its modal
warning only when a GUI exists *and* the `DataFile` came from a file
(`src/core/DataFile.cpp:2041-2043`), so in-memory parses cannot block on a dialog.

Deviations from the 2026-09-29 design, both approved mid-run and recorded as controller rulings in
`.superpowers/sdd/2026-09-29-agent-harness/progress.md:54-55`:

- `lmmsctl.py` resolves the token file from a **candidate list** rather than the design's single
  `<workingdir>/.lmms-agent.json`, because a dev build's `.lmmsrc.xml` is not in `$HOME`.
- `fetch_soundfont.py` defaults its destination to `lmmsctl.working_dir()`, because the SoundFont
  has to sit under a path-policy root to be loadable.

One requirement from the *old* design lapsed rather than being decided: §4 (`:197-198`) promised
"tools that add a track scroll the Song Editor to it". No tool does; the only `SongEditor` use under
`src/core/ai/` is `moveTrackView` for `replace_track` positioning
(`src/core/ai/AiProjectTools.cpp:428`). The Song Editor survived the cutover, so this is still open.
The old `render`-blocking deviation is **closed**: the nested
`loop.exec(QEventLoop::ExcludeUserInputEvents)` (`src/core/ai/AiActionTools.cpp:130-134`) is what the
harness design §6 approves. Do not re-raise it.
<!-- /claude:auto:active-specs -->
