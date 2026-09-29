# PROJECT_CONTEXT — lmms

Agent-facing repo state. Derived: regenerated wholesale each context sync, 300-line budget. Nothing
here is authoritative — durable decisions get promoted into `CLAUDE.md` or the governing spec.

Synced at commit `2fd75de3e` (2026-09-30), dispatched range
`b6e85ef69..2fd75de3e` — one commit, **4 files changed, +78/−4**, all modified, none added or
deleted. Fork: `origin` → `JakeyHwang/lmms`, `upstream` → `lmms/lmms`. Branch: `ai-composer`.

This range adds one tool. The user asked for "an exportable file for GarageBand on iPad", and
`export_midi` (`src/core/ai/AiActionTools.cpp:171-189`) is the answer: a Standard MIDI File written
by instantiating the `midiexport` plugin (`plugins/MidiExport`) and calling
`ExportFilter::tryExport` over `Song::tracks()` plus `Engine::patternStore()->tracks()`. It does
**not** go through `Song::exportProjectMidi()`, which swallows failures and so cannot report one to
an agent. Path handling is the same `outputPathError` + `AiPathPolicy` gate as `save` and `render`,
`.mid` is appended when missing, and the result is `{path, bytes}`. No file was added, deleted or
renamed, so no managed reference could go dead; the proactive path sweep re-resolved every path
named in `CLAUDE.md` and in this file against the tree at `2fd75de3e` and all of them still exist.

## Entry points

| Thing | Where |
|---|---|
| `lmms` executable | `src/core/main.cpp`, target at `src/CMakeLists.txt:114` |
| Shared object library | `lmmsobjs` (`src/CMakeLists.txt:104`) — links into `lmms` and every test |
| CLI parsing | `src/core/main.cpp:260-660`; `render`/`--render`/`-r` sets `coreOnly` (`main.cpp:273`) |
| Headless render | `lmms render <project.mmp>` — the no-GUI path, used as the regression smoke |
| GUI bootstrap | `src/gui/MainApplication.cpp` → `GuiApplication.cpp` → `MainWindow.cpp` |
| Settings dialog | `MainWindow::showSettingsDialog()` (`include/MainWindow.h:156`, `src/gui/MainWindow.cpp:887`); page ids in the `ConfigTab` enum (`include/SetupDialog.h:55-63`, ending `AiSettings`) |
| Subwindow panels | `MainWindow::addWindowedWidget` (`include/MainWindow.h:69`); example `src/gui/ControllerRackView.cpp:82` |
| **Agent server** | `lmms::AiAgentServer` (`include/AiAgentServer.h`, `src/core/ai/AiAgentServer.cpp`), started by `GuiApplication::startAgentServer()` from `src/core/main.cpp:915`, after the initial project exists — not from the constructor. Skipped when `ai/agentserver` is `"0"`; on by default, an absent key means enabled (`src/core/ai/AiConfig.cpp:32-39`) |
| Agent client | `.claude/skills/lmms-composer/scripts/lmmsctl.py` (`tools`, `summary`, `call <tool>`) |
| Tool tables | `registerAiProjectTools` / `…Discovery` / `…Action` / `…Meta` (`include/AiTools.h`) into one `AiToolRegistry` owned by `GuiApplication` (`src/gui/GuiApplication.cpp:232-235`) |
| Path safety | `lmms::AiPathPolicy` (`include/AiPathPolicy.h`) — roots only; `allowFromUserText` is gone |
| Undo surface | `lmms::AiProjectSnapshot` (`include/AiProjectSnapshot.h:43-57`) over `Song::saveProjectData(DataFile&)` (`include/Song.h:256`) |
| **File hand-off** | `render` → audio (`AiActionTools.cpp:138` is the approved nested `loop.exec`); `export_midi` → `.mid` (`AiActionTools.cpp:171-189`); `save` → `.mmp` |

There is no in-app AI UI any more beyond one checkbox: no `AiChatView`, no `Ctrl+Alt+A`, no
provider settings, no `data/ai/system_prompt.md`. The model-facing knowledge lives in
`.claude/skills/lmms-composer/SKILL.md` and `references/{theory,tools}.md`.

## Module map

- `src/core/ai/` — 10 entries: 9 sources (`AiActionTools`, `AiAgentServer`, `AiConfig`,
  `AiDiscoveryTools`, `AiMetaTools`, `AiPathPolicy`, `AiProjectSnapshot`, `AiProjectTools`,
  `AiToolRegistry`) plus the directory-local, include-only `AiToolHelpers.h`.
- `include/` — every first-party header, flat. Harness set: `AiAgentServer.h`, `AiProjectSnapshot.h`,
  `AiToolRegistry.h`, `AiTools.h`, `AiPathPolicy.h`, `AiConfig.h`. Only exception under `src/` is
  `src/core/UpgradeExtendedNoteRange.h` (`src/core/CMakeLists.txt:87`).
- `src/gui/` — widgets, plus `editors/`, `modals/`, `widgets/`, `tracks/`, `clips/`, `instrument/`,
  `menus/`. **`src/gui/ai/` no longer exists.**
- `plugins/` — 59 plugin directories. `plugins/MidiExport` is now reached from core code as well as
  from the File menu, through `include/ExportFilter.h`.
- `data/` — **7** installed asset directories; `data/ai/` is gone and `data/CMakeLists.txt` is 7
  `ADD_SUBDIRECTORY` lines.
- `tests/` — **13** QTest executables (`tests/CMakeLists.txt:6-20`), `emptyproject.mmp`, and
  `scripted/` holding only upstream's `README.md`, `check-namespace`, `check-strings`, `verify`.
- `.claude/skills/lmms-composer/` — `SKILL.md`, `references/theory.md`, `references/tools.md`
  (**36** `###` sections, one per tool), `scripts/{lmmsctl,check_render,fetch_soundfont}.py` with
  `test_lmmsctl.py` and `test_check_render.py`. Stdlib only, Python 3.12.
- `docs/superpowers/` — `specs/` (two designs) and `plans/` (two plans).
- Qt `Network` still required: component list `CMakeLists.txt:278`, include dirs `:288`,
  `QT_LIBRARIES` `:291-298`. `AiAgentServer` is a `QTcpServer`.

## Which spec governs

`docs/superpowers/specs/2026-09-29-agent-harness-design.md` governs and is **implemented** (`:4`).
Its plan, `docs/superpowers/plans/2026-09-29-agent-harness.md`, is at **59 of 59** step boxes; the
single remaining `- [ ]` in that file is the boilerplate checkbox example in the for-agentic-workers
preamble (`:3`), not a task. Nothing in either file was ticked this range — `export_midi` was a
post-plan user request, and no plan box covers it.

`docs/superpowers/specs/2026-09-16-ai-composer-design.md` is marked superseded in the file itself.

### Registered tools — 36

`add_automation`, `add_clips`, `add_effect`, `add_instrument_track`, `add_notes`, `add_sample_clip`,
`add_sf2_track`, `add_track`, `checkpoint`, `commit`, `describe_model_tree`, **`export_midi`**,
`get_head`, `get_mixer_xml`, `get_preset_xml`, `get_project_summary`, `get_track_xml`,
`list_effects`, `list_instruments`, `list_presets`, `list_samples`, `list_tools`, `new_project`,
`ping`, `play`, `remove_clip`, `remove_track`, `render`, `replace_track`, `revert`, `save`,
`set_head`, `set_mixer_xml`, `set_params`, `set_track`, `stop`.

Counted from `r.add({"` in the four tables: project 20, discovery 5, **action 6**, meta 5. XML
results cap at 64 KB (`MaxXmlBytes`, `src/core/ai/AiProjectTools.cpp:350`).

## How this repo is verified

- `ctest` over the 13 `set(LMMS_TESTS …)` entries — 8 upstream (7 under `src/core/`, plus
  `src/tracks/AutomationTrackTest.cpp`) and 5 under `src/core/ai/`.
- `AiActionToolsTest` now holds 11 test slots plus `initTestCase`/`cleanupTestCase`, i.e. **13
  functions in QTest's totals**, of which `exportMidiWritesFile` `QSKIP`s when
  `PluginFactory::pluginInfo("midiexport")` is null — which it is in a headless build
  (`tests/src/core/ai/AiActionToolsTest.cpp:205`). The dispatch reports that run as green.
- Python: `python -m unittest discover -s .claude/skills/lmms-composer/scripts -p 'test_*.py'` —
  15 tests, OK, last run by this steward at `c5bb4e5a9`; unchanged by this range.
- Build loop: MSYS2 CLANG64 + Ninja,
  `bash -lc 'export MSYSTEM=CLANG64 PATH=/clang64/bin:$PATH; cd /c/git_repos/lmms && cmake --build build'`
  then `build/tests/<Name>.exe -o <file>,txt` (the exes print nothing to the MSYS console).
- Headless smoke: `lmms render tests/emptyproject.mmp`.
- Live smoke: the agent server is on by default; start `build/lmms.exe`, wait for
  `AiAgentServer: listening on 127.0.0.1:<port>` — which appears **after** the project window is
  up — then `lmmsctl.py tools` → **36**.
- Headless tests cannot load plugin DLLs in this build (plugins import from `lmms.exe`), so
  plugin-dependent tool cases `QSKIP`; those paths are proven only by the live smoke. `export_midi`
  is now in that category: its rejection cases run headless, its success case does not.
- Last recorded full run: **13/13 C++ green, 13 Python green, headless render rc=0**, recorded by
  the controller at `.superpowers/sdd/2026-09-29-agent-harness/progress.md:52`.
- Style config present and unenforced by any hook here: `.clang-format`, `.clang-tidy`,
  `.editorconfig`, `.yamllint`.

## Recent changes

- 2026-09-30 — **`export_midi` tool added**, `b6e85ef69..2fd75de3e` (4 files, +78/−4), from the user
  request for a file GarageBand on iPad can open. `src/core/ai/AiActionTools.cpp:171-189` drives the
  `midiexport` plugin through `ExportFilter::tryExport` instead of `Song::exportProjectMidi()`,
  which swallows failures; returns `{path, bytes}`. Two cases added to `AiActionToolsTest`
  (`:193-216`), the success one `QSKIP`ping headless. Tool count **35 → 36**, action table 5 → 6;
  `SKILL.md` and `references/tools.md` were updated by the implementing agent in the same commit.
- 2026-09-30 — `CLAUDE.md` § `active-specs` line anchor corrected: the approved render-blocking
  `loop.exec(QEventLoop::ExcludeUserInputEvents)` moved from `AiActionTools.cpp:130-134` to `:138`
  (four `#include` lines were added at the top of the file this range).
- 2026-09-30 — the 2026-09-29 spec § 3 tool table gained an `export_midi` row (`:144`), on explicit
  dispatch instruction. That table is outside any fence; the row is marked "added after this design
  landed" so the file still reads as the design that was approved.
- 2026-09-30 — **agent server start moved out of the `GuiApplication` constructor**,
  `554d1d545..daf34b47b`: `GuiApplication::startAgentServer()` (`include/GuiApplication.h:95`) is
  called from `src/core/main.cpp:915`, after the recovery prompt and after
  `loadProject`/`createNewProject`. Re-entrant-safe (`m_agentServer` early return).
- 2026-09-30 — **`deleteTrack` deletes the view first when a GUI exists**
  (`src/core/ai/AiProjectTools.cpp:441-458`), through `TrackContainerView::deleteTrackView`. The
  rule was **promoted into `CLAUDE.md` § `code-layout`** ("with a GUI, never `delete` a `Track`
  before its `TrackView`") — `~InstrumentTrackView` reaches through `model()`.
- 2026-09-30 — open question "the startup race is mitigated only by convention" **answered** and
  deleted: the race is closed in code by the `main.cpp` call site.
- 2026-09-30 — agent server is **on by default**, `2f7dbf314`: `AiConfig::load` only overrides the
  default when the `ai/agentserver` key is present, so an absent key means enabled and `"0"`
  disables (`src/core/ai/AiConfig.cpp:32-39`).
- 2026-09-30 — harness fix wave, `4e0f15a1c..805c31c5c`: spec status → `implemented`; "AI Composer"
  → "agent harness" comment sweep; `AiProjectSnapshot::restore()` fails on a failed temp-file
  flush; the agent token file is narrowed to `ReadOwner | WriteOwner` after writing.
- 2026-09-30 — open question "the governing spec's own status line is stale" **answered** and
  deleted (`801be6760`); plan `2026-09-29-agent-harness.md` reached **59/59**.
- 2026-09-30 — agent-harness cutover landed, `a929195ee..c5bb4e5a9` (20 commits). Deleted the
  in-app LLM path (`OpenAiClient`, `OpenAiStreamParser`, `AiClient.h`, `AiSession`,
  `AiPromptBuilder`, `AiChatView`, `data/ai/`, `ai_mock_server.py`, 4 tests); added
  `AiAgentServer`, `AiProjectSnapshot`, `AiMetaTools`, `AiAgentServerTest` and the
  `.claude/skills/lmms-composer/` skill. 29 tools → 35; 16 tests → 13. All refs to the deleted
  files were pruned from the four `CLAUDE.md` auto blocks in that sync.

## Open questions

1. **Song-Editor scroll-to-track lapsed without a decision.** Above floor, carried from five syncs.
   The 2026-09-16 design §4 (`:197-198`) promised "tools that add a track scroll the Song Editor to
   it"; no tool does (`src/core/ai/AiProjectTools.cpp:428` is `moveTrackView` for `replace_track`
   positioning only), and the harness design does not carry the requirement forward. The Song Editor
   survived the cutover, so it is a dropped agreed requirement rather than a moot one.
2. **The live smoke's success criterion is unmet, not failed.** Task 10 rendered a 56-bar five-track
   arrangement, `check_render` clean, and the `.ogg` was shared; the plan's criterion is the user's
   ear and that verdict has not come back (`progress.md:53`). The `export_midi` request suggests the
   user is now working with the output in another DAW, but that is not the verdict.
3. **Is the fork's agent surface ever intended for upstream?** `README.md:59` asks contributors to
   file an issue upstream before a big feature. Still unanswered, and `.claude/` and `CLAUDE.md`
   being tracked means they appear in any upstream-facing diff.
4. **Below floor, recorded.** `export_midi`'s success path has no headless coverage — the one case
   that writes a file `QSKIP`s without the plugin, so only the rejection paths are proven by
   `ctest`, and the GarageBand import itself is unverifiable from this repo at all. Older deferred
   minors live in `.superpowers/sdd/2026-09-29-agent-harness/progress.md:25-50`;
   `GuiApplication.h:121-124` declares the registry before the server it is passed to (benign).

## Standing above-floor triggers

Each names the observable condition that trips it.

1. A new `Q_OBJECT` header lands under `include/` and the build fails with `undefined symbol` on one
   of its signals or `vtable for lmms::<Class>` → AUTOMOC did not re-scan the globbed header list
   (`CMakeLists.txt:731`); reconfigure and delete `build/src/lmmsobjs_autogen/timestamp`.
2. A new `.cpp` under `src/` absent from its directory's `LMMS_SRCS` list → silently never compiled,
   and tests pass while the code is not in the binary.
3. A token, API key or default key value appears in a tracked file, or `AiAgentServer` binds
   anything but `127.0.0.1` → violates the harness design §6. The token belongs only in
   `.lmms-agent.json` under the working directory; that file must still be removed on quit and
   still be narrowed to `ReadOwner | WriteOwner` after writing
   (`src/core/ai/AiAgentServer.cpp:75-77`).
4. A window-level `setShortcut`/`QAction` duplicates a combination an editor handles in
   `keyPressEvent` (the editors test `modifiers() & Qt::ControlModifier` non-exclusively, so every
   `Ctrl`+extra+`A` variant is in scope) → the editor silently stops receiving it. `Ctrl+Alt+A` is
   free again; `Ctrl+Shift+A` is still owned by `PianoRoll.cpp:1453` and `SongEditor.cpp:511`.
5. The registered tool count reported by `lmmsctl.py tools` stops matching the `###` heading count in
   `.claude/skills/lmms-composer/references/tools.md` → the skill's tool reference has drifted from
   the registry. Both are 36 today.
