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
| `src/core/` | Engine: `Song`, `Track`, `Mixer`, `DataFile`, `AudioEngine`, `ProjectJournal`, `RenderManager`, `PluginFactory`. Subdirs `audio/`, `lv2/`, `midi/`, `ai/` (AI Composer: `AiConfig`, `OpenAiStreamParser`, `OpenAiClient`, `AiToolRegistry`, `AiSession`, `AiProjectTools`, `AiDiscoveryTools`, `AiActionTools`, `AiPathPolicy`, `AiPromptBuilder`, plus the non-exported `AiToolHelpers.h`). |
| `src/gui/` | Qt widgets. Subdirs `editors/` (SongEditor, PianoRoll, AutomationEditor), `modals/` (SetupDialog, ExportProjectDialog, `*.ui`), `widgets/`, `tracks/`, `clips/`, `instrument/`, `menus/`, `ai/` (`AiChatView.cpp` — the AI Composer panel). |
| `src/tracks/` | Track implementations (instrument, sample, pattern, automation). |
| `src/common/`, `src/3rdparty/` | Shared helpers; vendored `ringbuffer`, `weakjack`, `qt5-x11embed`, `jack2`. |
| `include/` | **All first-party headers, flat** — ~300 files, no subdirectories. |
| `plugins/` | 59 plugin directories (instruments, effects, tools), each its own CMake target. |
| `data/` | `presets/`, `samples/`, `themes/`, `projects/`, `wavetables/`, `backgrounds/`, `locale/`, `ai/` (`system_prompt.md`, loaded at runtime from `ConfigManager::dataDir()`). |
| `tests/` | QTest executables — 8 upstream plus 8 under `tests/src/core/ai/` — plus `emptyproject.mmp` and `scripted/` (upstream checkers plus `ai_mock_server.py`). |
| `doc/` | Man page, Doxyfile, AUTHORS/CONTRIBUTORS, `wiki/`. |
| `cmake/` | `modules/`, `toolchains/`, `install/`, `nsis/`, `apple/`, `linux/`. |
| `docs/superpowers/` | Fork-local agent docs: `specs/` (approved designs), `plans/` (task-by-task implementation plans). |

Entry points:

- `src/core/main.cpp` → the `lmms` executable (`src/CMakeLists.txt:114`). Argument parsing lives
  here; `render` / `--render` / `-r` sets `coreOnly` (`main.cpp:273`), which is the headless path
  (`lmms render <project.mmp>`).
- Everything else compiles into the `lmmsobjs` OBJECT library (`src/CMakeLists.txt:104`), which both
  `lmms` and every test link against.
- GUI bootstrap: `src/gui/MainApplication.cpp` → `GuiApplication.cpp` → `MainWindow.cpp`.
<!-- /claude:auto:project-structure -->

## Build and test

<!-- claude:auto:build-and-test -->
- CMake, C++20 (`src/CMakeLists.txt:25`). Build instructions are upstream's
  [Compiling LMMS](https://github.com/LMMS/lmms/wiki/Compiling) wiki page, linked from `README.md`.
- Qt5 or Qt6, selected by `WANT_QT6`. Components requested:
  `Core Gui Widgets Xml Svg Network` (`CMakeLists.txt:278`). `Network` is linked in three places —
  the component list, `${Qt${QT_VERSION_MAJOR}Network_INCLUDE_DIRS}` in the
  `include_directories(SYSTEM …)` block (`CMakeLists.txt:288`), and `Qt${QT_VERSION_MAJOR}::Network`
  at the end of `set(QT_LIBRARIES …)` (`CMakeLists.txt:291-298`). It was added for the AI Composer.
- `vcpkg.json` pins Windows dependencies.
- Tests: CTest + QTest, one executable per entry in `set(LMMS_TESTS …)` (`tests/CMakeLists.txt:6`) — 16
  entries: 8 upstream plus `src/core/ai/{AiPathPolicy,AiActionTools,AiProjectTools,AiPromptBuilder,AiSession,AiToolRegistry,OpenAiClient,OpenAiStreamParser}Test.cpp`.
  Each links `lmmsobjs` and is built with `LMMS_TESTING` defined.
- `tests/scripted/ai_mock_server.py` is a standalone OpenAI-compatible mock (stdlib only, no CTest
  entry) for driving the chat panel without a key; run it by hand with any Python 3 — it listens on
  `http://127.0.0.1:8765/v1` and picks a canned tool-call script from `AI_MOCK_SCRIPT`
  (`drumloop`, the default, or `song`: a 3-section 16-bar arrangement on three tracks).
- Headless tests cannot load instrument/effect plugin DLLs in this build (the plugins import symbols
  from `lmms.exe`), so plugin-dependent tool cases `QSKIP` — those paths are only covered by the
  in-app smoke test.
- Local build in this fork is MSYS2 CLANG64 + Ninja in `build/`. A plain login shell has no `cmake`
  on `PATH`; the working form is
  `C:/msys64/usr/bin/bash.exe -lc 'export MSYSTEM=CLANG64 PATH=/clang64/bin:$PATH; cd /c/git_repos/lmms && cmake --build build'`.
  QTest executables print nothing to the MSYS console — run `build/tests/<Name>.exe -o <file>,txt`
  to read results.
- Formatting and linting config present: `.clang-format`, `.clang-tidy`, `.editorconfig`,
  `.yamllint`.
<!-- /claude:auto:build-and-test -->

## Layout rules the build actually enforces

<!-- claude:auto:code-layout -->
- **Source lists are explicit — `.cpp` files are never globbed.** A new `.cpp` must be added by hand
  to the `set(LMMS_SRCS ${LMMS_SRCS} … PARENT_SCOPE)` list in the owning `src/<dir>/CMakeLists.txt`
  (pattern: the list filling most of `src/core/CMakeLists.txt`, where the AI Composer's
  `core/ai/*.cpp` entries form their own blank-line-separated group after `core/StepRecorder.cpp`).
  A file not in that list is silently not compiled. Headers are the exception — see the `GLOB` note
  below.
- **Headers go in `include/`, flat.** The sole first-party header under `src/` is
  `src/core/UpgradeExtendedNoteRange.h`, and it is named in the source list explicitly
  (`src/core/CMakeLists.txt:87`).
- `AUTOMOC` and `AUTOUIC` are on (`src/CMakeLists.txt:20-21`); `.ui` files are searched for in
  `gui/modals` only (`AUTOUIC_SEARCH_PATHS`, `src/CMakeLists.txt:202`).
- A new `data/` subdirectory needs its own `CMakeLists.txt` plus an `ADD_SUBDIRECTORY` line in
  `data/CMakeLists.txt`, or it is not installed. `data/CMakeLists.txt` is `ADD_SUBDIRECTORY(…)` lines
  only; each subdirectory owns its install rule. Most use `INCLUDE(InstallHelpers)` +
  `INSTALL_DATA_SUBDIRS(…)`, which globs *sub*directories; for loose files in the directory itself
  the working form is `data/ai/CMakeLists.txt` — `FILE(GLOB … *.md)` + `INSTALL(FILES …
  DESTINATION "${LMMS_DATA_DIR}/ai")`.
- Panel toggles are `Ctrl+1` … `Ctrl+7`, bound to toolbar buttons in `MainWindow.cpp:432-458`
  (Song Editor, Pattern Editor, Piano Roll, Automation Editor, Mixer, Controller Rack, Project
  Notes). `Ctrl+8` has no window-level binding. The AI Composer panel is `Ctrl+Alt+A`
  (toolbar button `MainWindow.cpp:461-463`, view-menu entry `:1089`), alongside the pre-existing
  `Ctrl+Alt+S` (save as new version, `MainWindow.cpp:302`).
- **Checking whether a shortcut is free means grepping the editors' `keyPressEvent` switches, not
  just existing `setShortcut` calls.** A `MainWindow` toolbar-button/`QAction` shortcut has
  `Qt::WindowShortcut` context, so it fires before any MDI child editor sees the key. `Ctrl+Shift+A`
  is *not* free for that reason: `PianoRoll.cpp:1453-1461` uses it for "deselect all notes"
  (`clearSelectedNotes()`) and `SongEditor.cpp:511-513` for deselect-all-clips
  (`selectAllClips(!isShiftPressed)`). Both test `modifiers() & Qt::ControlModifier`
  non-exclusively, so *any* `Ctrl`+extra+`A` combination lands in their select-all branch unless a
  window-level shortcut claims it first — which is what `Ctrl+Alt+A` now does.
- Subwindow panels register through `MainWindow::addWindowedWidget` (`include/MainWindow.h:69`);
  `src/gui/ControllerRackView.cpp:82` is the reference example.
- **`include/*.h` is globbed at configure time** — `FILE(GLOB LMMS_INCLUDES …)`
  (`CMakeLists.txt:731`), compiled into `lmmsobjs` through `${LMMS_INCLUDES}`
  (`src/CMakeLists.txt:106`). A new header is invisible until CMake re-runs, and for a `Q_OBJECT`
  header AUTOMOC must also re-scan, which did not happen on its own in this tree (the link failed
  with `undefined symbol: lmms::AiClient::textDelta`). Working sequence: `cmake build`, then
  `rm -f build/src/lmmsobjs_autogen/timestamp`, then `cmake --build build`.
- The Settings dialog is opened by the existing slot `MainWindow::showSettingsDialog()`
  (`include/MainWindow.h:156`, defined `src/gui/MainWindow.cpp:887`) — reuse it rather than adding
  another entry point.
<!-- /claude:auto:code-layout -->

## Active design work

<!-- claude:auto:active-specs -->
| Doc | Status | Scope |
|---|---|---|
| `docs/superpowers/specs/2026-09-29-agent-harness-design.md` | **Governing design**, 269 lines, added by `a929195ee` (the whole range; nothing else changed). Approved, *no implementation plan yet and no code written* — `AiAgentServer`, `AiProjectSnapshot` and `registerAiMetaTools` do not exist anywhere under `include/`, `src/` or `tests/`. Its own header (`:5`) declares it supersedes the 2026-09-16 design. | Drop the in-process LLM loop, chat panel and provider settings; keep `AiToolRegistry` and the 29 tools and expose them over newline-delimited JSON on a loopback `QTcpServer` with a per-launch token file. Adds 6 tools (`ping`, `list_tools`, `checkpoint`, `revert`, `commit`, `add_sf2_track`) for 35 total, a GeneralUser GS SoundFont palette, and a repo-local skill `.claude/skills/lmms-composer/`. |
| `docs/superpowers/specs/2026-09-16-ai-composer-design.md` | **Superseded** by the above — but only in the new doc's text; this file still reads `Status: approved design, pending implementation plan` at `:4` and carries no supersede marker, which the new spec's §8 (`:265-266`) asks for. Implemented and its plan closed out. Accurate about the code it describes (reconciled through `c2eef8105`); treat it as history, not as a build target. | In-app AI chat panel driving the open project through the `.mmp` XML surface; OpenAI-compatible chat-completions with tool calling, agent loop in-process, code under `src/core/ai/`, `src/gui/ai/`, `data/ai/`. |
| `docs/superpowers/plans/2026-09-16-ai-composer.md` | Implementation plan, 13 tasks / 60 step boxes, **all 60 `[x]`** since `c2eef8105`. Closed, and now plans a design that has been superseded. It never described all the shipped work either: the FixWave (snapshot revert) and PersonaWave (batch tools, 150-call cap, prompt rewrite) were mid-run user requirements with no task boxes at all. | Build order: Qt `Network` + `AiConfig` (1), SSE parser and client (2), tool registry (3), agent loop (4), project/convenience tools (5-7), path policy and discovery (8), action tools (9), system prompt (10), settings page (11), chat panel and main-window wiring (12), mock-server smoke plus regression (13). |

**The code is exactly as it was at `5fb7d8549`.** `c2eef8105` and `a929195ee` are both docs-only, so
everything below still describes the tree, and the harness design describes a tree that does not
exist yet.

Landed AI surface: **29 registered tools** across `AiProjectTools.cpp` / `AiDiscoveryTools.cpp` /
`AiActionTools.cpp`; `AiSession` with a **150 tool call** per-turn cap (`include/AiSession.h:47`), a
5-consecutive-error cap, ~120 k-char history elision, and per-turn revert by **whole-project
snapshot** — `Song::saveProjectData(DataFile&)` (`include/Song.h:256`, extracted from
`saveProjectFile`) into an in-memory buffer, journalling off for the turn, and `Song::loadProject` on
a temp file to restore (`src/core/ai/AiSession.cpp:196-237`). The model-facing `undo` tool was
dropped with that change: `ProjectJournal` cannot restore a whole song. `AiPathPolicy` (canonical
compare, `..` refused, roots + user-named files), `buildAiSystemPrompt()` over
`data/ai/system_prompt.md` plus a generated plugin-name appendix, the `AiSettings` page in
`SetupDialog`, and `gui::AiChatView` on `Ctrl+Alt+A` (it owns the registry, session, policy and
prompt: `src/gui/ai/AiChatView.cpp:63-69`). XML tool results cap at 64 KB
(`MaxXmlBytes`, `src/core/ai/AiProjectTools.cpp:308`).

What the harness design keeps, and what it deletes, matters before touching any of it: **kept** —
`AiToolRegistry`, `AiProjectTools`, `AiDiscoveryTools`, `AiActionTools`, `AiToolHelpers.h`,
`AiPathPolicy` minus `allowFromUserText`, and their tests; **deleted** — `OpenAiClient`,
`OpenAiStreamParser`, `AiClient.h`, `AiSession` (its snapshot half is extracted, not kept),
`AiPromptBuilder`, all of `data/ai/`, `AiChatView` with its `Ctrl+Alt+A` binding, the four matching
tests and `tests/scripted/ai_mock_server.py` (`…-harness-design.md:62-80`). Qt `Network` stays —
`QTcpServer` needs it.

Two engine-wide changes came out of this work and are not AI-specific: `EffectChain::effects()`
(`include/EffectChain.h:70`), and `DataFile::findProblematicLadspaPlugins()` raising its modal
warning only when a GUI exists *and* the `DataFile` came from a file
(`src/core/DataFile.cpp:2041-2043`), so in-memory parses cannot block on a dialog.

Where the code departs from the 2026-09-16 design, now that it is superseded:

- `render` blocking in a nested `loop.exec(QEventLoop::ExcludeUserInputEvents)`
  (`src/core/ai/AiActionTools.cpp:130-134`) instead of the old §5's suspend-and-resume is **no longer
  a deviation** — the harness design adopts the blocking form deliberately (`§6:229-230`, "`render`
  still blocks in its nested event loop; the dispatch guard in §2 keeps other requests queued").
- The old §4's "tools that add a track scroll the Song Editor to it" was never implemented (the only
  `SongEditor` use under `src/core/ai/` is `moveTrackView` for `replace_track` positioning,
  `src/core/ai/AiProjectTools.cpp:386`) and the harness design does not carry it forward. It has
  lapsed rather than been decided.

Header placement follows this repo's convention rather than the 2026-09-16 spec's `src/core/ai/*.{h,cpp}`
sketch: headers are flat `include/Ai*.h` / `include/OpenAi*.h`, sources under `src/core/ai/`
(plan § Global Constraints); the harness design's New list (`:84-85`) follows the same convention.
`ConfigManager` keys are class `"ai"` with lowercase attributes `baseurl`, `apikey`, `model`
(`src/core/ai/AiConfig.cpp:35-49`); the harness replaces them with a single `ai/agentserver`.
A new `SetupDialog` page needs an entry in the `ConfigTab` enum (`include/SetupDialog.h:55-63`);
the AI page added `AiSettings` there, and the harness design keeps that tab.
<!-- /claude:auto:active-specs -->
