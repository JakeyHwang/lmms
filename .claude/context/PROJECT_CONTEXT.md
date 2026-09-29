# PROJECT_CONTEXT — lmms

Agent-facing repo state. Derived: regenerated wholesale each context sync, 300-line budget. Nothing
here is authoritative — durable decisions get promoted into `CLAUDE.md` or the governing spec.

Synced at commit `a929195ee` (2026-09-29), dispatched range `c2eef8105..a929195ee` — one commit,
**docs only**: 1 file added, `docs/superpowers/specs/2026-09-29-agent-harness-design.md`, +269/-0.
No code, build, test or asset file changed *in the range*, so every code line reference below is the
one verified at the `c2eef8105` sync and re-spot-checked this run.
Fork: `origin` → `JakeyHwang/lmms`, `upstream` → `lmms/lmms`. Branch: `ai-composer`.

**The working tree is NOT clean, and the last sync's claim that it was no longer holds.** Beside the
untracked `CLAUDE.md` and `.claude/`, `git status --porcelain` lists **13 modified tracked files**,
`git diff --stat HEAD` = +212/-13:

| File | Uncommitted change |
|---|---|
| `include/AiConfig.h`, `src/core/ai/AiConfig.cpp` | new `maxTokens` / `disableThinking` fields, `ai/maxtokens` + `ai/disablethinking` keys |
| `include/SetupDialog.h`, `src/gui/modals/SetupDialog.cpp` | widgets for the two new settings (+40 lines) |
| `include/OpenAiStreamParser.h`, `src/core/ai/OpenAiStreamParser.cpp`, `src/core/ai/OpenAiClient.cpp`, `src/core/ai/AiSession.cpp`, `src/gui/ai/AiChatView.cpp` | wiring for the above |
| `tests/scripted/ai_mock_server.py` (+58) and `AiSessionTest`, `OpenAiClientTest`, `OpenAiStreamParserTest` | matching test/mock coverage |

None of this is in any commit. The ledger attributes none of it (its last code entries are
2026-09-17; on 2026-09-29 it records only the three writes that produced the new spec), so it is the
user's own work or an unledgered session. **Nine of the thirteen are files the approved harness
design deletes outright** — see Open question 1. The steward never commits; these edits are not
mine and were left untouched.

## Entry points

| Thing | Where |
|---|---|
| `lmms` executable | `src/core/main.cpp`, target at `src/CMakeLists.txt:114` (`ADD_EXECUTABLE(lmms …)`) |
| Shared object library | `lmmsobjs` (`src/CMakeLists.txt:104`) — links into `lmms` and every test |
| CLI parsing | `src/core/main.cpp:260-660`; `render`/`--render`/`-r` sets `coreOnly` (`main.cpp:273`) |
| Headless render | `lmms render <project.mmp>` — the no-GUI path, used as the regression smoke |
| GUI bootstrap | `src/gui/MainApplication.cpp` → `GuiApplication.cpp` → `MainWindow.cpp` |
| Settings dialog | `MainWindow::showSettingsDialog()` (`include/MainWindow.h:156`); pages in `src/gui/modals/SetupDialog.cpp`, page ids in the `ConfigTab` enum (`include/SetupDialog.h:55-63`, ending `AiSettings`) |
| Subwindow panels | `MainWindow::addWindowedWidget` (`include/MainWindow.h:69`); example `src/gui/ControllerRackView.cpp:82` |
| AI chat panel | `lmms::gui::AiChatView` (`include/AiChatView.h`, `src/gui/ai/AiChatView.cpp`), toggled `Ctrl+Alt+A` (toolbar `MainWindow.cpp:461-463`, view menu `:1089`, slot `:1014`) — **slated for deletion** by the harness design |
| AI transport | `include/AiClient.h` (abstract) → `include/OpenAiClient.h` + `src/core/ai/OpenAiClient.cpp` — **slated for deletion** |
| AI agent loop | `lmms::AiSession` (`include/AiSession.h`, `src/core/ai/AiSession.cpp`) — **slated for deletion**, its snapshot half extracted to `AiProjectSnapshot` |
| AI tool tables | `registerAiProjectTools` / `registerAiDiscoveryTools` / `registerAiActionTools` (`include/AiTools.h:43,46,50`) into an `AiToolRegistry` (`include/AiToolRegistry.h`) — **retained** |
| AI path safety | `lmms::AiPathPolicy` (`include/AiPathPolicy.h`) — retained minus `allowFromUserText` (`AiPathPolicy.h:48`, `AiPathPolicy.cpp:60`) |
| AI system prompt | `lmms::buildAiSystemPrompt()` (`include/AiPromptBuilder.h:37`) over `data/ai/system_prompt.md` — **slated for deletion**; the text moves into the skill, corrected |
| Project snapshot API | `Song::saveProjectData(DataFile&)` (`include/Song.h:256`) — extracted from `saveProjectFile`, becomes the basis of `AiProjectSnapshot` |

`AiChatView`'s constructor registers all three tool tables and the system prompt
(`src/gui/ai/AiChatView.cpp:63-69`); it is the only caller of `register*Tools` outside `tests/`. Once
it is deleted, that wiring has to move to whatever owns `AiAgentServer` (the design puts it on
`GuiApplication`, after `MainWindow` exists).

## Module map

- `src/core/ai/` — 11 entries: 10 sources (`AiConfig`, `OpenAiStreamParser`, `OpenAiClient`,
  `AiToolRegistry`, `AiSession`, `AiProjectTools`, `AiDiscoveryTools`, `AiActionTools`,
  `AiPathPolicy`, `AiPromptBuilder`) plus the directory-local `AiToolHelpers.h` — the one header
  under `src/` that is not in a source list, because it is include-only.
- `include/` — every first-party header, flat. Only exception under `src/` is
  `src/core/UpgradeExtendedNoteRange.h` (`src/core/CMakeLists.txt:87`).
- `src/gui/` — widgets, plus `editors/`, `modals/`, `widgets/`, `tracks/`, `clips/`, `instrument/`,
  `menus/`, and `ai/` (`AiChatView.cpp`).
- `plugins/` — 59 plugin directories. `data/` — 8 installed asset directories (incl. `ai/`, which
  holds `CMakeLists.txt` + `system_prompt.md`). `docs/superpowers/` — `specs/` (now **two** designs)
  and `plans/` (one).
- `tests/` — 16 QTest executables (`tests/CMakeLists.txt:6-24`), `emptyproject.mmp`, and `scripted/`:
  `ai_mock_server.py` beside upstream's `README.md`, `check-namespace`, `check-strings`, `verify`.
- Build: CMake, C++20 (`src/CMakeLists.txt:25`), Qt5-or-Qt6 (`WANT_QT6`). `.cpp` lists are
  **explicit per directory** with `PARENT_SCOPE`; `include/*.h` **is** globbed
  (`CMakeLists.txt:731`), which is why a new `Q_OBJECT` header needs a reconfigure plus
  `rm -f build/src/lmmsobjs_autogen/timestamp` (in `CLAUDE.md` § `code-layout`).
- Qt `Network`: component list `CMakeLists.txt:278`, include dirs `:288`, `QT_LIBRARIES` `:291-298`.
  It stays after the harness cutover — `QTcpServer` needs it.

## Which spec governs

`docs/superpowers/specs/2026-09-29-agent-harness-design.md` (269 lines, added this range) is the
governing design. Its header `:4-5` reads `Status: approved design, pending implementation plan` and
`Supersedes: 2026-09-16-ai-composer-design.md`. **Implementation has not started**, confirmed by
absence rather than by assumption: `AiAgentServer`, `AiProjectSnapshot` and `registerAiMetaTools`
appear nowhere under `include/`, `src/` or `tests/`, and `.claude/skills/lmms-composer/` does not
exist. There is no plan doc derived from it yet.

Its stated cause is checkable and checks out: `data/ai/system_prompt.md:18,66` do tell the model
`basenote="57"`, while `include/Note.h:93` defines `DefaultBaseKey = Octave_4 + Key::A` (= 69 in MIDI
numbering) — so XML-built tracks really did play an octave high.

Shape of the cutover: keep `AiToolRegistry` + the 29 tools + `AiPathPolicy`; delete the OpenAI
client, SSE parser, `AiSession`, `AiPromptBuilder`, `data/ai/`, `AiChatView` and four tests; add
`AiAgentServer` (NDJSON over loopback TCP, ephemeral port, token file), `AiProjectSnapshot`, six new
tools (`ping`, `list_tools`, `checkpoint`, `revert`, `commit`, `add_sf2_track`) for 35 total, and the
`.claude/skills/lmms-composer/` skill with `lmmsctl.py`, `fetch_soundfont.py`, `check_render.py`.

`docs/superpowers/specs/2026-09-16-ai-composer-design.md` (253 lines) is now history: implemented,
accurate about the code it describes, and closed out by its plan
`docs/superpowers/plans/2026-09-16-ai-composer.md` at **60/60** step boxes. No task state changed this
range — the range touched neither file, and the new design has no checkboxes or `status:` lines to
flip. Dispatches have repeatedly named `docs/superpowers/specs/2026-09-16-ai-composer.md`, which has
never existed; the `-design` suffix is the real path.

### Registered tools — 29, unchanged this range

`add_automation`, `add_clips`, `add_effect`, `add_instrument_track`, `add_notes`, `add_sample_clip`,
`add_track`, `describe_model_tree`, `get_head`, `get_mixer_xml`, `get_preset_xml`,
`get_project_summary`, `get_track_xml`, `list_effects`, `list_instruments`, `list_presets`,
`list_samples`, `new_project`, `play`, `remove_clip`, `remove_track`, `render`, `replace_track`,
`save`, `set_head`, `set_mixer_xml`, `set_params`, `set_track`, `stop`. XML tool results cap at 64 KB
(`MaxXmlBytes`, `AiProjectTools.cpp:308`). Every description follows the
`WHAT: / WHEN: / UNITS: / RETURNS: / Gotcha:` form. 29 + the design's 6 meta-tools = the 35 its §7
smoke expects.

## How this repo is verified

- `ctest` over the 16 `set(LMMS_TESTS …)` entries (`tests/CMakeLists.txt:6-24`) — 8 upstream
  (7 under `src/core/`, plus `src/tracks/AutomationTrackTest.cpp`) and 8 under `src/core/ai/`.
  The harness design deletes 4 of the AI ones and adds `AiAgentServerTest`, netting 13.
- Per-task loop used by the implementers: MSYS2 CLANG64 + Ninja,
  `bash -lc 'export MSYSTEM=CLANG64 PATH=/clang64/bin:$PATH; cd /c/git_repos/lmms && cmake --build build'`
  then `build/tests/<Name>.exe -o <file>,txt` (the exes print nothing to the MSYS console).
- Headless smoke: `lmms render tests/emptyproject.mmp`; `build/lmms.exe --version` as the link check.
- In-app smoke today: `AI_MOCK_SCRIPT=song python tests/scripted/ai_mock_server.py`, then the panel
  on `Ctrl+Alt+A`. After the cutover it becomes `lmmsctl.py tools` against a running LMMS.
- Headless tests cannot load plugin DLLs in this build (plugins import from `lmms.exe`), so
  plugin-dependent tool cases `QSKIP`; those paths are proven only by the in-app smoke.
- **Last recorded full run is still 16/16 at the FixWave** (`progress.md:73,76,78`). The PersonaWave
  left screenshots but no `*-report.md`, so its "16 ctest tests pass" stays unverifiable from the
  repo. This range is docs-only and needs no run; the uncommitted tree changes have no recorded run
  at all.
- Style config present and unenforced by any hook here: `.clang-format`, `.clang-tidy`,
  `.editorconfig`, `.yamllint`.

## Recent changes

- 2026-09-29 — `a929195ee` added `docs/superpowers/specs/2026-09-29-agent-harness-design.md` (269
  lines, the entire range). External agent drives LMMS over loopback NDJSON; in-app chat, OpenAI
  client and provider settings are removed; tool layer kept. It declares itself superseding the
  2026-09-16 design.
- 2026-09-29 — `CLAUDE.md` § `active-specs` rewritten: three-row table (harness governing,
  composer superseded, plan closed), a kept-vs-deleted list for the cutover, and the note that code
  is unchanged since `5fb7d8549`.
- 2026-09-29 — open question "`render` blocks with a nested `QEventLoop` instead of the agreed
  suspend-and-resume" **answered** and deleted: the harness design adopts the blocking form on
  purpose (`…-harness-design.md:229-230`). The code never changed; the approved design moved to it.
- 2026-09-29 — proactive path sweep over `CLAUDE.md` and this file: `src/core/main.cpp`,
  `src/gui/ai/AiChatView.cpp`, `data/ai/system_prompt.md`, `data/ai/CMakeLists.txt`,
  `tests/scripted/ai_mock_server.py`, `src/core/UpgradeExtendedNoteRange.h`,
  `include/AiToolRegistry.h`, `src/core/ai/AiToolHelpers.h` and both `docs/superpowers/` docs all
  resolve. Nothing pruned. `.claude/skills/lmms-composer/` is a forward reference in the new spec,
  correctly absent.
- 2026-09-29 — recorded that the working tree carries 13 modified tracked files (+212/-13) that
  predate this sync and appear in no commit.
- 2026-09-17 — `c2eef8105` (docs only, +21/-17): the 2026-09-16 design's last three stale lines
  repaired — §2 tool table, the `DataFile` snapshot turn, and the cap 40 → 150. All verified
  faithful against code at that sync.
- 2026-09-17 — plan closed at **60/60**: task 13 step 4 flipped, and the previous sync's 11
  uncommitted flips swept into the same commit. No task text altered anywhere.
- 2026-09-17 — PersonaWave landed as `5fb7d8549`: batch/mix tools, `add_notes` manual sizing, cap
  40 → 150, tool descriptions rewritten skill-style, `data/ai/system_prompt.md` restructured, mock
  server `AI_MOCK_SCRIPT=song`, 5 new `AiProjectToolsTest` cases.
- 2026-09-17 — per-turn revert switched from `ProjectJournal` to a whole-project snapshot
  (`cc47a4793`): new `Song::saveProjectData(DataFile&)`, journalling off during a turn, model-facing
  `undo` tool dropped. `ProjectJournal::undo` segfaulted and never restored `<head>`.
- 2026-09-17 — promoted into `CLAUDE.md` § `active-specs`: the 29-tool surface, the 150-call cap, the
  snapshot-revert mechanism and `Song::saveProjectData`.

## Open questions

1. **13 files carry uncommitted work, and the approved design deletes nine of them.** Above floor.
   `src/core/ai/AiSession.cpp`, `OpenAiClient.cpp`, `OpenAiStreamParser.cpp`,
   `include/OpenAiStreamParser.h`, `src/gui/ai/AiChatView.cpp`, `tests/scripted/ai_mock_server.py`,
   `tests/src/core/ai/{AiSession,OpenAiClient,OpenAiStreamParser}Test.cpp` are all modified in the
   working tree *and* on the harness design's Removed list (`…-harness-design.md:62-73`). Starting
   the cutover destroys that work silently. Commit it, stash it, or explicitly discard it first.
2. **The superseded design is not marked superseded.** Above floor, cheap.
   `…-harness-design.md:265-266` asks for `Status: superseded by 2026-09-29-agent-harness-design.md`
   at the top of the 2026-09-16 file; that file still reads
   `Status: approved design, pending implementation plan` (`2026-09-16-ai-composer-design.md:4`).
   Fence discipline bars the steward from writing prose outside a fence in a spec doc, so this needs
   the doc's author. Until then a dispatch pointed at the old spec reads it as current — and
   dispatches have pointed there four times now.
3. **Song-Editor scroll-to-track lapsed without a decision.** Above floor. The 2026-09-16 design §4
   (`:197-198`) promised "tools that add a track scroll the Song Editor to it"; it was never built
   (the only `SongEditor` use under `src/core/ai/` is `moveTrackView` for `replace_track`
   positioning, `AiProjectTools.cpp:386`) and the harness design does not mention scrolling at all.
   The Song Editor survives the cutover, so this is a dropped agreed requirement, not a moot one.
4. **Is AI Composer — now the agent harness — ever intended for upstream?** `README.md:59` asks
   contributors to file an issue upstream before a big feature. Unanswered; it decides how far the
   fork bends to upstream conventions, and the harness makes the fork's surface larger, not smaller.
5. **Below floor, recorded.** The harness design's `AiSession.cpp:206-249` anchor (`:145-146`) is
   working-tree-relative: at `HEAD` those functions start at `:197` and `:219`, the +9 offset coming
   from the uncommitted edits — so the spec was written against the dirty tree. §7 `:260` says "the 5
   retained AI tests stay green" where 4 AI tests are retained (`AiPathPolicy`, `AiActionTools`,
   `AiProjectTools`, `AiToolRegistry`); the fifth is the new `AiAgentServerTest`. The plan's
   self-review still counts 22 tools (`plan:2298`) and points at a "Task 13 step 5" it does not have
   (`plan:2304`) — plan prose, not the steward's to edit. `CMakeLists.txt:278` still carries task 1's
   cosmetic leading tab.

## Standing above-floor triggers

Each names the observable condition that trips it.

1. A new `Q_OBJECT` header lands under `include/` and the build fails with
   `undefined symbol: lmms::<Class>::<signal>` or `vtable for lmms::<Class>` → AUTOMOC did not
   re-scan the globbed header list; reconfigure and delete `build/src/lmmsobjs_autogen/timestamp`.
   `AiAgentServer` is exactly such a header.
2. A new `.cpp` under `src/` that is absent from its directory's `LMMS_SRCS` list → silently never
   compiled, and tests pass while the code is not in the binary.
3. A token, API key or default key value appears in a tracked file, or `AiAgentServer` binds anything
   but `127.0.0.1` → violates the harness design §6 (`:231-233`); the token belongs only in the
   runtime file under the working directory.
4. A window-level `setShortcut`/`QAction` duplicates a combination an editor handles in
   `keyPressEvent` (the editors test `modifiers() & Qt::ControlModifier` non-exclusively, so every
   `Ctrl`+extra+`A` variant is in scope) → the editor silently stops receiving it. `Ctrl+Alt+A`
   frees up when `AiChatView` goes; `Ctrl+Shift+A` is still owned by `PianoRoll.cpp:1453` and
   `SongEditor.cpp:511`.
5. `set(LMMS_TESTS …)` drops entries without the harness cutover landing → the 4 AI tests scheduled
   for deletion (`OpenAiClient`, `OpenAiStreamParser`, `AiSession`, `AiPromptBuilder`) may only go
   when their sources do; anything else breaks the §7 promise that the rest stay green.
