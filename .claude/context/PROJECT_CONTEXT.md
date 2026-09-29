# PROJECT_CONTEXT — lmms

Agent-facing repo state. Derived: regenerated wholesale each context sync, 300-line budget. Nothing
here is authoritative — durable decisions get promoted into `CLAUDE.md` or the governing spec.

Synced at commit `805c31c5c` (2026-09-30), dispatched range
`4e0f15a1c..805c31c5c` — **17 files changed, +20/−16**, all modified, none added or deleted.
Fork: `origin` → `JakeyHwang/lmms`, `upstream` → `lmms/lmms`. Branch: `ai-composer`.

This range is the **harness fix wave** that follows the cutover: the governing spec's status line
moved to `implemented` (`801be6760`), the last "AI Composer" comments across `include/`, `src/` and
`tests/` were swept to "agent harness" (`08a07600a`, `805c31c5c`), `AiProjectSnapshot::restore()`
gained a flush check, and the agent token file is now narrowed to owner-only after it is written.
No file was added, deleted or renamed, so no managed reference could go dead this range; the
proactive path sweep re-resolved every path named in `CLAUDE.md` and in this file against the tree
at `805c31c5c` and all of them still exist.

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
| **Agent server** | `lmms::AiAgentServer` (`include/AiAgentServer.h`, `src/core/ai/AiAgentServer.cpp`), started by `GuiApplication::init` when `ai/agentserver` is set (`src/gui/GuiApplication.cpp:201-232`) |
| Agent client | `.claude/skills/lmms-composer/scripts/lmmsctl.py` (`tools`, `summary`, `call <tool>`) |
| Tool tables | `registerAiProjectTools` / `…Discovery` / `…Action` / `…Meta` (`include/AiTools.h`) into one `AiToolRegistry` owned by `GuiApplication` |
| Path safety | `lmms::AiPathPolicy` (`include/AiPathPolicy.h`) — roots only; `allowFromUserText` is gone |
| Undo surface | `lmms::AiProjectSnapshot` (`include/AiProjectSnapshot.h:43-57`) over `Song::saveProjectData(DataFile&)` (`include/Song.h:256`) |

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
- `plugins/` — 59 plugin directories. `data/` — **7** installed asset directories; `data/ai/` is
  gone and `data/CMakeLists.txt` is 7 `ADD_SUBDIRECTORY` lines.
- `tests/` — **13** QTest executables (`tests/CMakeLists.txt:6-20`), `emptyproject.mmp`, and
  `scripted/` holding only upstream's `README.md`, `check-namespace`, `check-strings`, `verify`.
- `.claude/skills/lmms-composer/` — `SKILL.md`, `references/theory.md`, `references/tools.md`
  (35 `###` sections, one per tool), `scripts/{lmmsctl,check_render,fetch_soundfont}.py` with
  `test_lmmsctl.py` and `test_check_render.py`. Stdlib only, Python 3.12.
- `docs/superpowers/` — `specs/` (two designs) and `plans/` (two plans).
- Qt `Network` still required: component list `CMakeLists.txt:278`, include dirs `:288`,
  `QT_LIBRARIES` `:291-298`. `AiAgentServer` is a `QTcpServer`.

## Which spec governs

`docs/superpowers/specs/2026-09-29-agent-harness-design.md` governs, is **implemented**, and now
says so in its own header (`:4`, `801be6760`). Its plan,
`docs/superpowers/plans/2026-09-29-agent-harness.md`, is at **59 of 59** step boxes; the single
remaining `- [ ]` in that file is the boilerplate checkbox example in the for-agentic-workers
preamble (`:3`), not a task.

`docs/superpowers/specs/2026-09-16-ai-composer-design.md` is marked superseded in the file itself as
of `835de8f83` — the open question the last two syncs carried is answered and closed.

### Registered tools — 35

`add_automation`, `add_clips`, `add_effect`, `add_instrument_track`, `add_notes`, `add_sample_clip`,
`add_sf2_track`, `add_track`, `checkpoint`, `commit`, `describe_model_tree`, `get_head`,
`get_mixer_xml`, `get_preset_xml`, `get_project_summary`, `get_track_xml`, `list_effects`,
`list_instruments`, `list_presets`, `list_samples`, `list_tools`, `new_project`, `ping`, `play`,
`remove_clip`, `remove_track`, `render`, `replace_track`, `revert`, `save`, `set_head`,
`set_mixer_xml`, `set_params`, `set_track`, `stop`.

Counted from `r.add({"` in the four tables: project 20, discovery 5, action 5, meta 5. XML results
cap at 64 KB (`MaxXmlBytes`, `src/core/ai/AiProjectTools.cpp:350`).

## How this repo is verified

- `ctest` over the 13 `set(LMMS_TESTS …)` entries — 8 upstream (7 under `src/core/`, plus
  `src/tracks/AutomationTrackTest.cpp`) and 5 under `src/core/ai/`.
- Python: `python -m unittest discover -s .claude/skills/lmms-composer/scripts -p 'test_*.py'` —
  **15 tests, OK, run by this steward at `c5bb4e5a9`**.
- Build loop: MSYS2 CLANG64 + Ninja,
  `bash -lc 'export MSYSTEM=CLANG64 PATH=/clang64/bin:$PATH; cd /c/git_repos/lmms && cmake --build build'`
  then `build/tests/<Name>.exe -o <file>,txt` (the exes print nothing to the MSYS console).
- Headless smoke: `lmms render tests/emptyproject.mmp`.
- Live smoke: enable the setting, start `build/lmms.exe`, wait for
  `AiAgentServer: listening on 127.0.0.1:<port>`, then `lmmsctl.py tools` → 35.
- Headless tests cannot load plugin DLLs in this build (plugins import from `lmms.exe`), so
  plugin-dependent tool cases `QSKIP`; those paths are proven only by the live smoke.
- Last recorded full run: **13/13 C++ green, 13 Python green, headless render rc=0**, recorded by
  the controller at `.superpowers/sdd/2026-09-29-agent-harness/progress.md:52`.
- Style config present and unenforced by any hook here: `.clang-format`, `.clang-tidy`,
  `.editorconfig`, `.yamllint`.

## Recent changes

- 2026-09-30 — harness fix wave landed, `4e0f15a1c..805c31c5c` (3 commits, 17 files, +20/−16):
  spec status → `implemented`; every remaining "AI Composer" comment across `include/`,
  `src/core/ai/`, `src/core/DataFile.cpp` and `tests/src/core/ai/` swept to "agent harness";
  `AiProjectSnapshot::restore()` now fails on a failed temp-file flush; the agent token file is
  narrowed to `ReadOwner | WriteOwner` after writing. No file added, deleted or renamed.
- 2026-09-30 — open question "the governing spec's own status line is stale" **answered** and
  deleted: `801be6760` set `2026-09-29-agent-harness-design.md:4` to
  `Status: implemented (plan …, all tasks landed)`.
- 2026-09-30 — plan `2026-09-29-agent-harness.md`: Task 11's own docs-sync box flipped to `[x]` by
  its author; the plan is now **59/59**. `CLAUDE.md` § `active-specs` re-stated from 58/59, and the
  token-file permission and snapshot-flush behaviours recorded there.
- 2026-09-30 — line-anchor sweep after the two insertions this range: `AiAgentServer.cpp:47-50`
  (token path) and `DataFile.cpp:2041-2043` re-verified unshifted; new anchor
  `AiAgentServer.cpp:75-77` added for the permission call.
- 2026-09-30 — agent-harness cutover landed, `a929195ee..c5bb4e5a9` (20 commits). Deleted the
  in-app LLM path (`OpenAiClient`, `OpenAiStreamParser`, `AiClient.h`, `AiSession`,
  `AiPromptBuilder`, `AiChatView`, `data/ai/`, `ai_mock_server.py`, 4 tests); added
  `AiAgentServer`, `AiProjectSnapshot`, `AiMetaTools`, `AiAgentServerTest` and the
  `.claude/skills/lmms-composer/` skill. 29 tools → 35; 16 tests → 13.
- 2026-09-30 — `CLAUDE.md` all four auto blocks rewritten: `src/gui/ai`, `data/ai`,
  `ai_mock_server.py`, the 8-test AI count, `OpenAi*`/`AiSession`/`AiPromptBuilder`/`AiChatView`
  and the `Ctrl+Alt+A` binding are gone from every block; the wire protocol, the 35-tool list, the
  snapshot/checkpoint model and the dev-build token-file rule are in.
- 2026-09-30 — pruned refs to `src/gui/ai/AiChatView.cpp`, `include/AiChatView.h`,
  `include/AiClient.h`, `include/AiSession.h`, `include/AiPromptBuilder.h`,
  `include/OpenAiClient.h`, `include/OpenAiStreamParser.h`, `data/ai/system_prompt.md`,
  `data/ai/CMakeLists.txt`, `tests/scripted/ai_mock_server.py` and the four deleted AI tests
  (all deleted this range) from `CLAUDE.md` § `project-structure`, § `build-and-test`,
  § `code-layout`, § `active-specs`.
- 2026-09-30 — re-anchored stale line references: `MaxXmlBytes` `AiProjectTools.cpp:308` → `:350`,
  `moveTrackView` `:386` → `:428`, `Ctrl+Alt+S` `MainWindow.cpp:302` → `:300-301`, panel toggles
  `:432-458` → `:431-458`.
- 2026-09-30 — plan `2026-09-29-agent-harness.md`: **58 step boxes flipped to `[x]`** on commit,
  file and controller-ledger evidence. No task text altered.
- 2026-09-30 — open question "the superseded design is not marked superseded" **answered** and
  deleted: `835de8f83` added the `Status: superseded by …` line the new spec's §8 asked for.

## Open questions

1. **Song-Editor scroll-to-track lapsed without a decision.** Above floor, carried from four syncs.
   The 2026-09-16 design §4 (`:197-198`) promised "tools that add a track scroll the Song Editor to
   it"; no tool does (`src/core/ai/AiProjectTools.cpp:428` is `moveTrackView` for `replace_track`
   positioning only), and the harness design does not carry the requirement forward. The Song Editor
   survived the cutover, so it is a dropped agreed requirement rather than a moot one.
2. **The live smoke's success criterion is unmet, not failed.** Task 10 rendered a 56-bar five-track
   arrangement, `check_render` clean (peak −10.9 dBFS, 0 clipped, no silent bars), and the `.ogg`
   was shared to the user; the plan's criterion is the user's ear and that verdict has not come back
   (`progress.md:53`). The plan box is ticked for the work done; the judgement is outstanding.
3. **Is the fork's agent surface ever intended for upstream?** `README.md:59` asks contributors to
   file an issue upstream before a big feature. Still unanswered, and the harness makes the fork's
   divergence larger, not smaller — `.claude/` and `CLAUDE.md` are now tracked, so they will appear
   in any upstream-facing diff.
4. **Below floor, recorded.** Deferred minors from the task reviews live in
   `.superpowers/sdd/2026-09-29-agent-harness/progress.md:25-50` and are not repeated here.
   `GuiApplication.h:118-121` declares the registry before the server it is passed to (benign: the
   server is a QObject child that is never deleted). The startup race — the server accepts before
   `main.cpp` finishes `loadProject` — is mitigated only by convention (call `get_project_summary`
   first). `AiAgentServerTest.cpp:179`'s nested-loop case was re-armed via `QTimer::singleShot` in
   `8c24c6a94` and mutation-verified, so it is no longer vacuous.

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
   the registry. Both are 35 today.
