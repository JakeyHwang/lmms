# PROJECT_CONTEXT — lmms

Agent-facing repo state. Derived: regenerated wholesale each context sync, 300-line budget. Nothing
here is authoritative — durable decisions get promoted into `CLAUDE.md` or the governing spec.

Synced at commit `c15632ec4` (2026-09-30), dispatched range
`55c360ebf..c15632ec4` — **4 files changed, +111/−8**, all modified, none added or deleted. Fork:
`origin` → `JakeyHwang/lmms`, `upstream` → `lmms/lmms`. Branch: `ai-composer`.

This range answers "recreate the real song, don't approximate it". `import_midi`
(`src/core/ai/AiActionTools.cpp:198-230`) loads a Standard MIDI File through the `midiimport`
plugin, so a found transcription becomes tracks instead of a hand-written pastiche; `SKILL.md`'s
research section was reordered around it (MIDI first, tabs and scores second) and gained a "Making
it flow" section on legato, strums, velocity contour and ring-out. A third, unrelated change rides
along: `RenderManager::renderNextTrack()` now joins the render thread before destroying it — an
engine-wide crash fix, not an agent one. No file was added, deleted or renamed, so no managed
reference could go dead; the proactive sweep re-resolved every path named in `CLAUDE.md` and in this
file against the tree at `c15632ec4` and all of them still exist.

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
| **File hand-off** | out: `render` → audio (`AiActionTools.cpp:141` is the approved nested `loop.exec`), `export_midi` → `.mid` (`:174-192`), `save` → `.mmp`. In: **`import_midi`** ← `.mid` (`:198-230`) |

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
- `plugins/` — 59 plugin directories. Two are now reached from core code as well as from the File
  menu: `plugins/MidiExport` through `include/ExportFilter.h`, and **`plugins/MidiImport`** through
  `include/ImportFilter.h`.
- `data/` — **7** installed asset directories; `data/ai/` is gone and `data/CMakeLists.txt` is 7
  `ADD_SUBDIRECTORY` lines.
- `tests/` — **13** QTest executables (`tests/CMakeLists.txt:6-20`), `emptyproject.mmp`, and
  `scripted/` holding only upstream's `README.md`, `check-namespace`, `check-strings`, `verify`.
- `.claude/skills/lmms-composer/` — `SKILL.md`, `references/theory.md`, `references/tools.md`
  (**37** `###` sections, one per tool), `scripts/{lmmsctl,check_render,fetch_soundfont}.py` with
  `test_lmmsctl.py` and `test_check_render.py`. Stdlib only, Python 3.12.
- `docs/superpowers/` — `specs/` (two designs) and `plans/` (two plans).
- Qt `Network` still required: component list `CMakeLists.txt:278`, include dirs `:288`,
  `QT_LIBRARIES` `:291-298`. `AiAgentServer` is a `QTcpServer`.

## Which spec governs

`docs/superpowers/specs/2026-09-29-agent-harness-design.md` governs and is **implemented** (`:4`).
Its plan, `docs/superpowers/plans/2026-09-29-agent-harness.md`, is at **59 of 59** step boxes; the
single remaining `- [ ]` in that file is the boilerplate checkbox example in the for-agentic-workers
preamble (`:3`), not a task. Nothing in either file was ticked this range — `import_midi`, like
`export_midi` before it, was a post-plan user request and no plan box covers it.

`docs/superpowers/specs/2026-09-16-ai-composer-design.md` is marked superseded in the file itself.

### Registered tools — 37

`add_automation`, `add_clips`, `add_effect`, `add_instrument_track`, `add_notes`, `add_sample_clip`,
`add_sf2_track`, `add_track`, `checkpoint`, `commit`, `describe_model_tree`, `export_midi`,
`get_head`, `get_mixer_xml`, `get_preset_xml`, `get_project_summary`, `get_track_xml`,
**`import_midi`**, `list_effects`, `list_instruments`, `list_presets`, `list_samples`, `list_tools`,
`new_project`, `ping`, `play`, `remove_clip`, `remove_track`, `render`, `replace_track`, `revert`,
`save`, `set_head`, `set_mixer_xml`, `set_params`, `set_track`, `stop`.

Counted from `r.add({"` in the four tables: project 20, discovery 5, **action 7**, meta 5. XML
results cap at 64 KB (`MaxXmlBytes`, `src/core/ai/AiProjectTools.cpp:350`).

## How this repo is verified

- `ctest` over the 13 `set(LMMS_TESTS …)` entries — 8 upstream (7 under `src/core/`, plus
  `src/tracks/AutomationTrackTest.cpp`) and 5 under `src/core/ai/`.
- `AiActionToolsTest` now holds **12** test slots plus `initTestCase`/`cleanupTestCase`, i.e. **14
  functions in QTest's totals** (`init()` is a fixture, not a case). `exportMidiWritesFile`
  `QSKIP`s when `PluginFactory::pluginInfo("midiexport")` is null, which it is in a headless build.
  `importMidiRejectsBadInput` (`tests/src/core/ai/AiActionToolsTest.cpp:217-233`) runs in full
  headless: its four rejections (no path, forbidden path, missing file, junk magic) are all decided
  before the plugin lookup.
- Python: `python -m unittest discover -s .claude/skills/lmms-composer/scripts -p 'test_*.py'` —
  15 tests, OK, last run by this steward at `c5bb4e5a9`; unchanged by this range.
- Build loop: MSYS2 CLANG64 + Ninja,
  `bash -lc 'export MSYSTEM=CLANG64 PATH=/clang64/bin:$PATH; cd /c/git_repos/lmms && cmake --build build'`
  then `build/tests/<Name>.exe -o <file>,txt` (the exes print nothing to the MSYS console).
- Headless smoke: `lmms render tests/emptyproject.mmp`.
- Live smoke: the agent server is on by default; start `build/lmms.exe`, wait for
  `AiAgentServer: listening on 127.0.0.1:<port>` — which appears **after** the project window is
  up — then `lmmsctl.py tools` → **37**.
- Headless tests cannot load plugin DLLs in this build (plugins import from `lmms.exe`), so
  plugin-dependent tool cases `QSKIP` or stop at the rejection paths; the success paths of
  `render`, `export_midi` and `import_midi` are proven only by the live smoke.
- Last recorded full run: **13/13 C++ green, 13 Python green, headless render rc=0**, reported by
  the harness controller. Its run directory `.superpowers/sdd/2026-09-29-agent-harness/` has since
  been deleted, so that figure is no longer citable — re-run `ctest` rather than trusting it.
- Style config present and unenforced by any hook here: `.clang-format`, `.clang-tidy`,
  `.editorconfig`, `.yamllint`.

## Recent changes

- 2026-09-30 — **`import_midi` tool added**, `55c360ebf..c15632ec4` (4 files, +111/−8), from the
  user's ask for a faithful recreation rather than a stylistic one.
  `src/core/ai/AiActionTools.cpp:198-230` drives the `midiimport` plugin through
  `ImportFilter::import`; one `sf2player` track per MIDI channel on the default SoundFont, channel
  10 on drum bank 128, plus tempo/time-signature automation. Returns
  `{tracksAdded, tracks:[{index,name}]}`. Every modal-dialog condition the plugin would raise is
  pre-checked, because a blocking handler hangs the server. Tool count **36 → 37**, action table
  6 → 7.
- 2026-09-30 — **engine-wide render crash fixed**, `src/core/RenderManager.cpp:64-66`:
  `renderNextTrack()` `wait()`s on the active `ProjectRenderer` before `m_activeRenderer.reset()`.
  `QThread::finished` is emitted *from* the render thread just before it exits, so the slot ran
  while the thread was still winding down and destroying it there was fatal — an intermittent crash
  on back-to-back renders, in the File → Export path as much as in the `render` tool. Recorded in
  `CLAUDE.md` § `active-specs` as the third non-agent-specific engine change.
- 2026-09-30 — `SKILL.md` research guidance reordered for recreation work: **MIDI transcription
  first** (search, download to `<workingdir>/samples/midi/`, inspect tracks/channels/programs before
  importing, then `import_midi` and re-voice), tabs and scores second, facts third. New **"Making it
  flow"** section: let-ring, offset strums, connected `len`, legato bass with approach notes,
  velocity contour, glue pad, legato melody.
- 2026-09-30 — `references/tools.md` had **not** been updated by the implementing agent, so standing
  trigger 5 tripped (registry 37 vs doc 36). This sync added the `### import_midi` section and bumped
  every "36 tools" mention in `SKILL.md`, `tools.md` and `CLAUDE.md`.
- 2026-09-30 — pruned refs to `.superpowers/sdd/2026-09-29-agent-harness/progress.md` (directory
  deleted outside the diff): from `CLAUDE.md` § `active-specs` (the controller-ruling citation) and
  from three places in this file. The rulings' substance already lives in `CLAUDE.md`, so only the
  dead pointers went; the 13/13 test figure is now marked as no longer citable.
- 2026-09-30 — the 2026-09-29 spec § 3 tool table gained an `import_midi` row (`:145`), on explicit
  dispatch instruction, alongside the `export_midi` row added last sync. Both are marked "added
  after this design landed" so the file still reads as the design that was approved.
- 2026-09-30 — `CLAUDE.md` § `active-specs` anchor corrected: the approved render-blocking
  `loop.exec(QEventLoop::ExcludeUserInputEvents)` moved from `AiActionTools.cpp:138` to `:141`
  (three `#include` lines added at the top of the file this range).
- 2026-09-30 — **`export_midi` tool added**, `b6e85ef69..2fd75de3e`: the `midiexport` plugin driven
  through `ExportFilter::tryExport` rather than `Song::exportProjectMidi()`, which swallows
  failures; returns `{path, bytes}`. Tool count 35 → 36.
- 2026-09-30 — **agent server start moved out of the `GuiApplication` constructor**,
  `554d1d545..daf34b47b`: `GuiApplication::startAgentServer()` (`include/GuiApplication.h:95`) is
  called from `src/core/main.cpp:915`, after the recovery prompt and after
  `loadProject`/`createNewProject`. Re-entrant-safe (`m_agentServer` early return).
- 2026-09-30 — **`deleteTrack` deletes the view first when a GUI exists**
  (`src/core/ai/AiProjectTools.cpp:441-458`), through `TrackContainerView::deleteTrackView`. The
  rule was **promoted into `CLAUDE.md` § `code-layout`** ("with a GUI, never `delete` a `Track`
  before its `TrackView`") — `~InstrumentTrackView` reaches through `model()`.

## Open questions

1. **Song-Editor scroll-to-track lapsed without a decision.** Above floor, carried from six syncs.
   The 2026-09-16 design §4 (`:197-198`) promised "tools that add a track scroll the Song Editor to
   it"; no tool does (`src/core/ai/AiProjectTools.cpp:428` is `moveTrackView` for `replace_track`
   positioning only), and the harness design does not carry the requirement forward. The Song Editor
   survived the cutover, so it is a dropped agreed requirement rather than a moot one.
2. **The live smoke's success criterion is unmet, not failed.** The harness run rendered a 56-bar
   five-track arrangement, `check_render` clean, and the `.ogg` was shared; the plan's criterion is
   the user's ear and that verdict has not come back. `export_midi` and now `import_midi` both
   suggest the user is iterating on fidelity, but neither is the verdict.
3. **Is the fork's agent surface ever intended for upstream?** `README.md:59` asks contributors to
   file an issue upstream before a big feature. Still unanswered, and `.claude/` and `CLAUDE.md`
   being tracked means they appear in any upstream-facing diff.
4. **Below floor, recorded.** The `RenderManager` join has **no test** — it fixes an intermittent
   crash and nothing in `tests/` exercises `renderNextTrack()`'s teardown, so a future edit can undo
   it silently. `import_midi`'s success path likewise has no headless coverage (the plugin cannot
   load in this build), and the copyright posture of importing a third-party transcription is the
   user's call, not the repo's. The deferred-minors list that used to be citable is gone with
   `.superpowers/sdd/2026-09-29-agent-harness/`; what survives from it is
   `GuiApplication.h:121-124` declaring the registry before the server it is passed to (benign).

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
4. A tool handler can reach a **modal dialog** — `QMessageBox`, a plugin's own error popup, a file
   dialog — on the GUI thread → the server stops answering, because handlers run on that thread one
   at a time. `import_midi` is the worked example of pre-checking instead
   (`src/core/ai/AiActionTools.cpp:200-220`); `DataFile.cpp:2041-2043` is the other half.
5. The registered tool count reported by `lmmsctl.py tools` stops matching the `###` heading count in
   `.claude/skills/lmms-composer/references/tools.md` → the skill's tool reference has drifted from
   the registry. Both are 37 today. This tripped twice now; a tool added without its `tools.md`
   section is the normal cause.
