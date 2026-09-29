# Agent Harness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development (recommended) or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the in-app AI Composer with a loopback control server plus a repo-local skill, so an external coding agent drives LMMS's existing tool layer directly and builds music from SoundFont instruments.

**Architecture:** `AiAgentServer` (QTcpServer on 127.0.0.1, NDJSON + per-launch token file) dispatches into the retained `AiToolRegistry`; five meta-tools and `add_sf2_track` join the registry; everything that talked to a model is deleted. A Python client and skill under `.claude/skills/lmms-composer/` are the agent-side half.

**Tech Stack:** C++20 / Qt5 (`QtNetwork`, `QtTest`), CMake + Ninja under MSYS2 CLANG64, Python 3.12 stdlib only for the skill scripts.

**Spec:** `docs/superpowers/specs/2026-09-29-agent-harness-design.md`

## Global Constraints

- Build: `C:/msys64/usr/bin/bash.exe -lc 'export MSYSTEM=CLANG64 PATH=/clang64/bin:$PATH; cd <checkout> && cmake --build build'`. A new `include/*.h` needs `cmake build` first, then `rm -f build/src/lmmsobjs_autogen/timestamp`, then the build. Tests print nothing to the console: run `build/tests/<Name>.exe -o <file>,txt` and read the file.
- Source lists are explicit: every new `.cpp` goes into `set(LMMS_SRCS …)` in `src/core/CMakeLists.txt` (the `core/ai/` group) or `src/gui/CMakeLists.txt`; every new test into `set(LMMS_TESTS …)` in `tests/CMakeLists.txt`. Headers are flat in `include/`.
- Keep the `Ai*` prefix. Licence header block as in `include/AiToolRegistry.h`. Tabs for indentation in C++.
- Server binds `127.0.0.1` only. Token file path: `ConfigManager::inst()->workingDir() + ".lmms-agent.json"`. Line limit `4 * 1024 * 1024` bytes.
- Wire format: request `{"id", "token", "tool", "args"}`; response `{"id", "result"}` or `{"id", "error"}` — `error` only for transport problems (bad JSON, missing/non-string `tool`, unknown tool, bad token, over-long line). Tool failures stay `result.ok=false`.
- Key numbers are MIDI: A4 = 69; the instrument base note defaults to 69 (`include/Note.h:93`). Never write `basenote="57"` anywhere.
- Python: stdlib only; scripts runnable as `python <script>`; tests via `python -m unittest`.
- Do not run formatters, linters or the full test suite inside a task; the controller runs the suite at integration.
- Lanes: Lane A (C++ additions, main checkout `C:/git_repos/lmms`, branch `ai-composer`), Lane B (C++ removal, worktree `C:/git_repos/lmms-wt/t10`, branch `harness-removal`), Lane C (Python + skill, main checkout, no build). Lanes only touch the files their task lists; Lane B and Lane A both edit `src/core/CMakeLists.txt` and `tests/CMakeLists.txt` on different lines — Lane A only adds lines, Lane B only removes lines.

---

## File map

| File | Responsibility | Task |
|---|---|---|
| `include/AiProjectSnapshot.h`, `src/core/ai/AiProjectSnapshot.cpp` | in-memory whole-project snapshot: `take / restore / drop / held` | 1 |
| `src/core/ai/AiMetaTools.cpp` | `ping`, `list_tools`, `checkpoint`, `revert`, `commit` | 1 |
| `src/core/ai/AiProjectTools.cpp` | `+ add_sf2_track` | 1 |
| `include/AiTools.h` | `+ registerAiMetaTools` | 1 |
| `tests/src/core/ai/AiProjectToolsTest.cpp` | `+ snapshot round-trip, add_sf2_track validation` | 1 |
| `include/AiAgentServer.h`, `src/core/ai/AiAgentServer.cpp` | loopback NDJSON server | 2 |
| `tests/src/core/ai/AiAgentServerTest.cpp` | server protocol tests | 2 |
| removals (see Task 3) | chat panel, LLM client, prompt, settings widgets | 3 |
| `include/AiConfig.h`, `src/core/ai/AiConfig.cpp` | `{bool agentServer}` | 3 |
| `src/gui/modals/SetupDialog.cpp`, `include/SetupDialog.h` | AI page: checkbox + path label | 4 |
| `src/gui/GuiApplication.cpp`, `include/GuiApplication.h` | owns registry, policy, snapshot, server | 4 |
| `.claude/skills/lmms-composer/scripts/lmmsctl.py`, `test_lmmsctl.py` | client CLI + `Lmms` class | 5 |
| `.claude/skills/lmms-composer/scripts/check_render.py`, `test_check_render.py` | WAV sanity check | 6 |
| `.claude/skills/lmms-composer/scripts/fetch_soundfont.py` | GeneralUser GS download | 7 |
| `.claude/skills/lmms-composer/SKILL.md`, `references/theory.md`, `references/tools.md` | agent knowledge | 8 |
| `docs/superpowers/specs/2026-09-16-ai-composer-design.md` | superseded status line | 9 |

Dependency graph: Task 1 → Task 2 → (merge Task 3) → Task 4 → Task 9 (integration) → Task 10 (smoke). Task 3 runs in parallel with 1–2. Tasks 5–8 run in parallel with everything; Task 8 consumes the tool descriptions in Task 1 (given in its brief) and the CLI shape of Task 5 (given in its brief).

---

### Task 1: AiProjectSnapshot, meta-tools, add_sf2_track (Lane A)

**Files:**
- Create: `include/AiProjectSnapshot.h`, `src/core/ai/AiProjectSnapshot.cpp`, `src/core/ai/AiMetaTools.cpp`
- Modify: `include/AiTools.h:47-50`, `src/core/ai/AiProjectTools.cpp:146-167` (add handler after `addInstrumentTrack`), `:888-892` (register after `add_instrument_track`), `src/core/CMakeLists.txt` (core/ai group, add two `.cpp`)
- Test: `tests/src/core/ai/AiProjectToolsTest.cpp`

**Interfaces:**
- Consumes: `AiToolRegistry` (`add`, `specs`, `ok`, `error`), `Song::saveProjectData(DataFile&)`, `Song::loadProject`, `Engine::projectJournal()->setJournalling`, `aitools::prop/schema` from `src/core/ai/AiToolHelpers.h`, `addTrackXml` (static in `AiProjectTools.cpp:417`), `elementToString` (`:318`).
- Produces:
  ```cpp
  class LMMS_EXPORT AiProjectSnapshot { public: void take(); bool restore(); void drop(); bool held() const; };
  LMMS_EXPORT void registerAiMetaTools(AiToolRegistry& r, AiProjectSnapshot& snapshot);
  ```
  Tools `ping → {ok, version}`, `list_tools → {ok, tools:[…specs…]}`, `checkpoint → {ok}`, `revert → {ok}` / `{ok:false, error:"No checkpoint held"}`, `commit → {ok}` / same error, `add_sf2_track(name, file, bank?=0, patch?=0, mixerChannel?) → {ok, index}`.

- [x] **Step 1: Write the failing tests** — append to `AiProjectToolsTest` (the class registers only project tools in `initTestCase`; extend it):

```cpp
// in initTestCase(): lmms::Engine::init(true); lmms::registerAiProjectTools(reg); lmms::registerAiMetaTools(reg, snap);
// member: lmms::AiProjectSnapshot snap;   (include "AiProjectSnapshot.h")

	void checkpointRevertRestoresProject()
	{
		reg.call("set_head", {{"bpm", 100}});
		QVERIFY(reg.call("checkpoint", {})["ok"].toBool());
		addBareInstrumentTrack("Added");
		reg.call("set_head", {{"bpm", 140}});
		QCOMPARE(int(lmms::Engine::getSong()->tracks().size()), 1);
		auto r = reg.call("revert", {});
		QVERIFY2(r["ok"].toBool(), qPrintable(r["error"].toString()));
		QCOMPARE(int(lmms::Engine::getSong()->tracks().size()), 0);
		QCOMPARE(int(lmms::Engine::getSong()->getTempo()), 100);
		QVERIFY(lmms::Engine::projectJournal()->isJournalling());
	}
	void revertWithoutCheckpointErrors()
	{
		QVERIFY(!reg.call("revert", {})["ok"].toBool());
		QVERIFY(!reg.call("commit", {})["ok"].toBool());
		QVERIFY(reg.call("checkpoint", {})["ok"].toBool());
		QVERIFY(reg.call("commit", {})["ok"].toBool());
		QVERIFY(!reg.call("revert", {})["ok"].toBool());
	}
	void pingAndListTools()
	{
		auto p = reg.call("ping", {});
		QVERIFY(p["ok"].toBool());
		QVERIFY(!p["version"].toString().isEmpty());
		auto t = reg.call("list_tools", {});
		QVERIFY(t["ok"].toBool());
		QStringList names;
		for (auto v : t["tools"].toArray()) { names << v.toObject()["function"].toObject()["name"].toString(); }
		QVERIFY(names.contains("add_notes"));
		QVERIFY(names.contains("list_tools"));
	}
	void addSf2TrackValidation()
	{
		QVERIFY(!reg.call("add_sf2_track", {{"name", "Piano"}})["ok"].toBool());                      // file required
		QVERIFY(!reg.call("add_sf2_track", {{"name", "Piano"}, {"file", "C:/nope/x.sf2"}})["ok"].toBool()); // missing file
		QTemporaryDir dir;
		QFile f(dir.path() + "/k.sf2"); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("x"); f.close();
		QVERIFY(!reg.call("add_sf2_track", {{"name", "P"}, {"file", f.fileName()}, {"bank", 200}})["ok"].toBool()); // bank range
		QCOMPARE(int(lmms::Engine::getSong()->tracks().size()), 0);
		if (!hasPlugin("sf2player")) { QSKIP("sf2player plugin not loadable in this test process"); }
		auto r = reg.call("add_sf2_track", {{"name", "P"}, {"file", f.fileName()}, {"bank", 128}, {"patch", 0}});
		QVERIFY2(r["ok"].toBool(), qPrintable(r["error"].toString()));
	}
	void addSf2TrackRespectsPathPolicy()
	{
		lmms::AiToolRegistry gated;
		lmms::registerAiProjectTools(gated, [](const QString&) { return false; });
		QTemporaryDir dir;
		QFile f(dir.path() + "/k.sf2"); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("x"); f.close();
		auto r = gated.call("add_sf2_track", {{"name", "P"}, {"file", f.fileName()}});
		QVERIFY(!r["ok"].toBool());
		QVERIFY(r["error"].toString().contains("not allowed"));
	}
```

- [x] **Step 2: Build and run to see them fail** — `cmake --build build --target AiProjectToolsTest` fails to compile (`registerAiMetaTools` undeclared). Expected.

- [x] **Step 3: `AiProjectSnapshot`** — `include/AiProjectSnapshot.h`:

```cpp
#ifndef LMMS_AI_PROJECT_SNAPSHOT_H
#define LMMS_AI_PROJECT_SNAPSHOT_H

#include <QByteArray>
#include <QString>

#include "lmms_export.h"

namespace lmms
{

/*! Whole-project snapshot held in memory, used by the agent's checkpoint / revert / commit tools.
 *
 * The project journal cannot restore a whole song (Song::restoreState tears tracks out from under
 * their views and never covers <head>), so a checkpoint serialises the project the way File > Save
 * does and restore() reloads it the way File > Open does. Journalling is off while a snapshot is
 * held so the agent's edits do not pile into the undo history.
 */
class LMMS_EXPORT AiProjectSnapshot
{
public:
	//! Serialise the open project into memory; replaces any held snapshot. Journalling off.
	void take();
	//! Reload the project from the snapshot. False when none is held or the temp file failed. Journalling on.
	bool restore();
	//! Forget the snapshot. Journalling on.
	void drop();
	bool held() const { return !m_data.isEmpty(); }

private:
	QByteArray m_data;
	QString m_fileName;        // Song::projectFileName() at take()
	bool m_modified = false;   // Song::isModified() at take()
};

} // namespace lmms

#endif // LMMS_AI_PROJECT_SNAPSHOT_H
```

`src/core/ai/AiProjectSnapshot.cpp` — the bodies are `AiSession::beginTurnCheckpoint` / `revertLastTurn` (`src/core/ai/AiSession.cpp:206-249`) moved over:

```cpp
#include "AiProjectSnapshot.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QTextStream>

#include "DataFile.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "Song.h"

namespace lmms
{

void AiProjectSnapshot::take()
{
	m_data.clear();
	Song* song = Engine::getSong();
	if (!song) { return; }
	m_fileName = song->projectFileName();
	m_modified = song->isModified();
	DataFile df(DataFile::Type::SongProject);
	song->saveProjectData(df);
	QTextStream ts(&m_data, QIODevice::WriteOnly);
	df.write(ts);
	ts.flush();
	Engine::projectJournal()->setJournalling(false);
}

bool AiProjectSnapshot::restore()
{
	Song* song = Engine::getSong();
	if (!held() || !song) { return false; }
	// Next to the project file so "local:" resource paths resolve; no .mmp suffix so the
	// temporary file stays out of the recent-projects list.
	const QString dir = m_fileName.isEmpty() ? QDir::tempPath() : QFileInfo(m_fileName).absolutePath();
	QTemporaryFile tmp(dir + "/lmms-agent-checkpoint-XXXXXX");
	if (!tmp.open())
	{
		tmp.setFileTemplate(QDir::tempPath() + "/lmms-agent-checkpoint-XXXXXX");
		if (!tmp.open()) { return false; }
	}
	tmp.write(m_data);
	tmp.close(); // still auto-removed on destruction
	song->loadProject(tmp.fileName()); // leaves the song unmodified
	song->setProjectFileName(m_fileName);
	if (m_modified) { song->setModified(); }
	drop();
	return true;
}

void AiProjectSnapshot::drop()
{
	m_data.clear();
	if (Engine::getSong()) { Engine::projectJournal()->setJournalling(true); }
}

} // namespace lmms
```

- [x] **Step 4: meta-tools** — `src/core/ai/AiMetaTools.cpp`:

```cpp
#include "AiProjectSnapshot.h"
#include "AiToolHelpers.h"
#include "AiTools.h"
#include "lmmsversion.h"

namespace lmms
{

using namespace aitools;
using R = AiToolRegistry;

void registerAiMetaTools(AiToolRegistry& r, AiProjectSnapshot& snapshot)
{
	r.add({"ping", "WHAT: liveness check. RETURNS {version}.", schema({}),
		[](const QJsonObject&) { return R::ok({{"version", LMMS_VERSION}}); }});
	r.add({"list_tools", "WHAT: every tool with its JSON schema, in OpenAI function format. RETURNS {tools:[{type,function:{name,description,parameters}}]}.",
		schema({}), [&r](const QJsonObject&) { return R::ok({{"tools", r.specs()}}); }});
	r.add({"checkpoint",
		"WHAT: snapshot the whole project in memory so revert can restore it; replaces an earlier checkpoint. "
		"WHEN: before a batch of edits. Undo history is paused until revert or commit.",
		schema({}), [&snapshot](const QJsonObject&) { snapshot.take(); return R::ok(); }});
	r.add({"revert", "WHAT: reload the project from the last checkpoint and drop it (also discards edits made since). RETURNS ok, or error when none is held.",
		schema({}), [&snapshot](const QJsonObject&) {
			if (!snapshot.held()) { return R::error("No checkpoint held"); }
			return snapshot.restore() ? R::ok() : R::error("Could not restore the checkpoint");
		}});
	r.add({"commit", "WHAT: keep the current project state and drop the checkpoint; undo history resumes. RETURNS ok, or error when none is held.",
		schema({}), [&snapshot](const QJsonObject&) {
			if (!snapshot.held()) { return R::error("No checkpoint held"); }
			snapshot.drop();
			return R::ok();
		}});
}

} // namespace lmms
```

Check the version header name: `grep -l LMMS_VERSION include/*.h build/include/*.h` — `main.cpp` uses `LMMS_VERSION`; include whatever header defines it (`lmmsversion.h` is generated into the build include dir).

Declare in `include/AiTools.h` after `registerAiActionTools`:

```cpp
class AiProjectSnapshot;
//! Meta tools: ping, list_tools, checkpoint / revert / commit over `snapshot`. `r` and `snapshot` must outlive the registry's use.
LMMS_EXPORT void registerAiMetaTools(AiToolRegistry& r, AiProjectSnapshot& snapshot);
```

- [x] **Step 5: `add_sf2_track`** — in `AiProjectTools.cpp` after `addInstrumentTrack` (needs `#include <QFileInfo>` and `#include <QDomDocument>`; `addTrackXml` is defined later in the file, so add a forward declaration `static QJsonObject addTrackXml(const QJsonObject& a);` above):

```cpp
//! Instrument track playing a SoundFont preset through sf2player, built as <track> XML so it goes
//! through the same validation as add_track. Checks that need no plugin (file, path policy, ranges)
//! run first so they hold in headless builds.
static QJsonObject addSf2Track(const QJsonObject& a, const std::function<bool(const QString&)>& pathAllowed)
{
	const QString file = a["file"].toString();
	if (file.isEmpty()) { return R::error("file is required (absolute path to a .sf2)"); }
	if (pathAllowed && !pathAllowed(file)) { return R::error("Path not allowed: " + file); }
	if (!QFileInfo(file).isFile()) { return R::error("No such file: " + file); }
	const int bank = a["bank"].toInt(0);
	const int patch = a["patch"].toInt(0);
	if (bank < 0 || bank > 128 || patch < 0 || patch > 127) { return R::error("bank must be 0..128 (128 = drum kits) and patch 0..127"); }
	const int mixerChannel = a["mixerChannel"].toInt(0);
	if (mixerChannel < 0 || mixerChannel >= Engine::mixer()->numChannels())
	{
		return R::error(QString("mixerChannel must be 0..%1").arg(Engine::mixer()->numChannels() - 1));
	}
	if (PluginFactory::instance()->pluginInfo("sf2player").isNull()) { return R::error("sf2player plugin is not available in this build"); }

	QDomDocument doc;
	QDomElement track = doc.createElement("track");
	track.setAttribute("type", int(Track::Type::Instrument));
	track.setAttribute("name", a["name"].toString(QFileInfo(file).completeBaseName()));
	QDomElement it = doc.createElement("instrumenttrack");
	it.setAttribute("mixch", mixerChannel);
	QDomElement inst = doc.createElement("instrument");
	inst.setAttribute("name", "sf2player");
	QDomElement sf2 = doc.createElement("sf2player");
	sf2.setAttribute("src", file);
	sf2.setAttribute("bank", bank);
	sf2.setAttribute("patch", patch);
	inst.appendChild(sf2);
	it.appendChild(inst);
	track.appendChild(it);
	doc.appendChild(track);
	return addTrackXml({{"xml", elementToString(track)}});
}
```

Register right after `add_instrument_track` (`:888-892`):

```cpp
	r.add({"add_sf2_track",
		"WHAT: append an instrument track playing one SoundFont preset (sf2player). WHEN: realistic instruments: piano, guitars, bass, drum kits, strings, brass — the default palette. "
		"bank 0 = melodic GM patches (0 piano, 33 finger bass, 27 clean guitar, 29 overdriven, 30 distortion, 48 strings), bank 128 = drum kits (patch 0 standard; GM drum map: 36 kick, 38 snare, 42 closed hat, 46 open hat, 49 crash, 51 ride). "
		"RETURNS {index}. Gotcha: file must be an absolute path inside the allowed roots (the LMMS working directory's samples/soundfonts/ is).",
		schema({{"name", prop("string", "track name")}, {"file", prop("string", "absolute path to the .sf2")},
			{"bank", prop("integer", "0..128, default 0; 128 = drum kits")}, {"patch", prop("integer", "0..127 GM program, default 0")},
			{"mixerChannel", mixerIndex}}, {"file"}),
		[pathAllowed](const QJsonObject& a) { return addSf2Track(a, pathAllowed); }});
```

(`mixerIndex` is the schema piece `add_instrument_track` already uses; keep the same object.)

- [x] **Step 6: CMake** — add `core/ai/AiMetaTools.cpp` and `core/ai/AiProjectSnapshot.cpp` to the `core/ai/` group in `src/core/CMakeLists.txt`, alphabetical. Re-run `cmake build`, remove `build/src/lmmsobjs_autogen/timestamp`, build `AiProjectToolsTest`.

- [x] **Step 7: Run** — `build/tests/AiProjectToolsTest.exe -o /tmp/t1.txt,txt`; all cases pass (plugin cases `SKIP`).

- [x] **Step 8: Commit** — `git add include/AiProjectSnapshot.h include/AiTools.h src/core/ai/AiProjectSnapshot.cpp src/core/ai/AiMetaTools.cpp src/core/ai/AiProjectTools.cpp src/core/CMakeLists.txt tests/src/core/ai/AiProjectToolsTest.cpp && git commit -m "feat(ai): project snapshot, meta tools, add_sf2_track"`.

---

### Task 2: AiAgentServer (Lane A)

**Files:**
- Create: `include/AiAgentServer.h`, `src/core/ai/AiAgentServer.cpp`, `tests/src/core/ai/AiAgentServerTest.cpp`
- Modify: `src/core/CMakeLists.txt` (add `core/ai/AiAgentServer.cpp`), `tests/CMakeLists.txt` (add `src/core/ai/AiAgentServerTest.cpp` after `AiActionToolsTest.cpp`)

**Interfaces:**
- Consumes: `AiToolRegistry::has / call`, `ConfigManager::inst()->workingDir()`.
- Produces:
  ```cpp
  class LMMS_EXPORT AiAgentServer : public QObject {
  public:
      static constexpr int MaxLineBytes = 4 * 1024 * 1024;
      explicit AiAgentServer(QObject* parent = nullptr);
      ~AiAgentServer() override;              // calls stop()
      bool start(AiToolRegistry* registry, const QString& tokenFilePath, quint16 port = 0);
      void stop();
      bool running() const;
      quint16 port() const;
      QString token() const;
      static QString defaultTokenFilePath();  // ConfigManager::inst()->workingDir() + ".lmms-agent.json"
  };
  ```

- [x] **Step 1: Write the failing test** — `tests/src/core/ai/AiAgentServerTest.cpp`:

```cpp
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

#include "AiAgentServer.h"
#include "AiToolRegistry.h"

class AiAgentServerTest : public QObject
{
	Q_OBJECT
	lmms::AiToolRegistry reg;

	static QByteArray line(QJsonObject o) { return QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n'; }
	//! Sends `req` and returns the next response line, or an empty array on timeout.
	static QJsonObject roundTrip(QTcpSocket& s, const QByteArray& req)
	{
		s.write(req);
		if (!QTest::qWaitFor([&] { return s.canReadLine(); }, 3000)) { return {}; }
		return QJsonDocument::fromJson(s.readLine()).object();
	}
	QJsonObject request(const lmms::AiAgentServer& srv, const QString& tool, QJsonValue id = 1, QJsonObject args = {}, QString token = {})
	{
		return {{"id", id}, {"token", token.isNull() ? srv.token() : token}, {"tool", tool}, {"args", args}};
	}
	bool connectClient(QTcpSocket& s, const lmms::AiAgentServer& srv)
	{
		s.connectToHost("127.0.0.1", srv.port());
		return QTest::qWaitFor([&] { return s.state() == QAbstractSocket::ConnectedState; }, 3000);
	}

private slots:
	void initTestCase()
	{
		reg.add({"ping", "", {{"type", "object"}}, [](const QJsonObject&) { return lmms::AiToolRegistry::ok({{"pong", true}}); }});
		reg.add({"echo", "", {{"type", "object"}}, [](const QJsonObject& a) { return lmms::AiToolRegistry::ok({{"args", a}}); }});
	}

	void startWritesTokenFileAndStopRemovesIt()
	{
		QTemporaryDir dir;
		const QString path = dir.path() + "/agent.json";
		lmms::AiAgentServer srv;
		QVERIFY(srv.start(&reg, path));
		QVERIFY(srv.running());
		QVERIFY(srv.port() > 0);
		QCOMPARE(srv.token().size(), 64);
		QFile f(path);
		QVERIFY(f.open(QIODevice::ReadOnly));
		auto o = QJsonDocument::fromJson(f.readAll()).object();
		QCOMPARE(o["port"].toInt(), int(srv.port()));
		QCOMPARE(o["token"].toString(), srv.token());
		srv.stop();
		QVERIFY(!srv.running());
		QVERIFY(!QFile::exists(path));
	}
	void pingRoundTripEchoesId()
	{
		QTemporaryDir dir; lmms::AiAgentServer srv; QVERIFY(srv.start(&reg, dir.path() + "/a.json"));
		QTcpSocket s; QVERIFY(connectClient(s, srv));
		auto r = roundTrip(s, line(request(srv, "ping", "abc")));
		QCOMPARE(r["id"].toString(), QString("abc"));
		QVERIFY(r["result"].toObject()["ok"].toBool());
		QVERIFY(r["result"].toObject()["pong"].toBool());
		auto e = roundTrip(s, line(request(srv, "echo", 2, {{"x", 5}})));
		QCOMPARE(e["result"].toObject()["args"].toObject()["x"].toInt(), 5);
	}
	void wrongTokenErrorsAndDisconnects()
	{
		QTemporaryDir dir; lmms::AiAgentServer srv; QVERIFY(srv.start(&reg, dir.path() + "/a.json"));
		QTcpSocket s; QVERIFY(connectClient(s, srv));
		auto r = roundTrip(s, line(request(srv, "ping", 1, {}, "nope")));
		QVERIFY(r["error"].toString().contains("token"));
		QVERIFY(QTest::qWaitFor([&] { return s.state() == QAbstractSocket::UnconnectedState; }, 3000));
	}
	void malformedJsonErrorsButKeepsConnection()
	{
		QTemporaryDir dir; lmms::AiAgentServer srv; QVERIFY(srv.start(&reg, dir.path() + "/a.json"));
		QTcpSocket s; QVERIFY(connectClient(s, srv));
		auto r = roundTrip(s, "{not json\n");
		QVERIFY(r.contains("error"));
		QVERIFY(r["id"].isNull());
		auto ok = roundTrip(s, line(request(srv, "ping", 3)));
		QVERIFY(ok["result"].toObject()["ok"].toBool());
	}
	void unknownToolAndMissingToolError()
	{
		QTemporaryDir dir; lmms::AiAgentServer srv; QVERIFY(srv.start(&reg, dir.path() + "/a.json"));
		QTcpSocket s; QVERIFY(connectClient(s, srv));
		QVERIFY(roundTrip(s, line(request(srv, "nope", 1)))["error"].toString().contains("nope"));
		QVERIFY(roundTrip(s, line({{"id", 2}, {"token", srv.token()}}))["error"].toString().contains("tool"));
	}
	void twoRequestsInOneWriteAnsweredInOrder()
	{
		QTemporaryDir dir; lmms::AiAgentServer srv; QVERIFY(srv.start(&reg, dir.path() + "/a.json"));
		QTcpSocket s; QVERIFY(connectClient(s, srv));
		s.write(line(request(srv, "ping", 3)) + line(request(srv, "ping", 4)));
		QStringList ids;
		QVERIFY(QTest::qWaitFor([&] {
			while (s.canReadLine()) { ids << QString::number(QJsonDocument::fromJson(s.readLine()).object()["id"].toInt()); }
			return ids.size() == 2;
		}, 3000));
		QCOMPARE(ids, QStringList({"3", "4"}));
	}
	void overlongLineErrorsAndDisconnects()
	{
		QTemporaryDir dir; lmms::AiAgentServer srv; QVERIFY(srv.start(&reg, dir.path() + "/a.json"));
		QTcpSocket s; QVERIFY(connectClient(s, srv));
		s.write(QByteArray(lmms::AiAgentServer::MaxLineBytes + 1, 'a'));
		QVERIFY(QTest::qWaitFor([&] { return s.canReadLine(); }, 5000));
		QVERIFY(QJsonDocument::fromJson(s.readLine()).object()["error"].toString().contains("long"));
		QVERIFY(QTest::qWaitFor([&] { return s.state() == QAbstractSocket::UnconnectedState; }, 3000));
	}
};

QTEST_MAIN(AiAgentServerTest)
#include "AiAgentServerTest.moc"
```

- [x] **Step 2: Build to see it fail** — add the test to `tests/CMakeLists.txt`; `cmake --build build --target AiAgentServerTest` fails on the missing header. Expected.

- [x] **Step 3: Header** — `include/AiAgentServer.h`:

```cpp
#ifndef LMMS_AI_AGENT_SERVER_H
#define LMMS_AI_AGENT_SERVER_H

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>

#include "lmms_export.h"

class QTcpServer;
class QTcpSocket;

namespace lmms
{

class AiToolRegistry;

/*! Loopback control server: newline-delimited JSON requests dispatched into an AiToolRegistry.
 *
 * Request  {"id": any, "token": "...", "tool": "name", "args": {...}}
 * Response {"id": any, "result": {...}} or, for transport problems only, {"id": any, "error": "..."}.
 * Binds 127.0.0.1 only. The port and a per-launch token are written to `tokenFilePath` while
 * running. Handlers run on this object's thread (the GUI thread), one request at a time, in
 * arrival order; bytes that arrive while a handler runs are dispatched after it returns.
 */
class LMMS_EXPORT AiAgentServer : public QObject
{
	Q_OBJECT
public:
	static constexpr int MaxLineBytes = 4 * 1024 * 1024;

	explicit AiAgentServer(QObject* parent = nullptr);
	~AiAgentServer() override;

	//! Listens on 127.0.0.1 (ephemeral port unless `port` > 0) and writes the token file.
	//! False, listening on nothing, when the bind or the file write fails.
	bool start(AiToolRegistry* registry, const QString& tokenFilePath, quint16 port = 0);
	//! Closes every socket and removes the token file.
	void stop();
	bool running() const;
	quint16 port() const;
	QString token() const { return m_token; }
	static QString defaultTokenFilePath();

private:
	void onNewConnection();
	void onReadyRead(QTcpSocket* socket);
	void drainAll();
	//! Handles one complete line; sets `*closeAfter` when the socket must be dropped afterwards.
	QByteArray handleLine(const QByteArray& line, bool* closeAfter);

	QTcpServer* m_server = nullptr;
	AiToolRegistry* m_registry = nullptr;
	QString m_token;
	QString m_tokenFile;
	QHash<QTcpSocket*, QByteArray> m_buffers;
	bool m_dispatching = false;
};

} // namespace lmms

#endif // LMMS_AI_AGENT_SERVER_H
```

- [x] **Step 4: Implementation** — `src/core/ai/AiAgentServer.cpp`:

```cpp
#include "AiAgentServer.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QTcpServer>
#include <QTcpSocket>

#include "AiToolRegistry.h"
#include "ConfigManager.h"

namespace lmms
{

AiAgentServer::AiAgentServer(QObject* parent) : QObject(parent) {}

AiAgentServer::~AiAgentServer() { stop(); }

QString AiAgentServer::defaultTokenFilePath()
{
	return ConfigManager::inst()->workingDir() + ".lmms-agent.json";
}

bool AiAgentServer::start(AiToolRegistry* registry, const QString& tokenFilePath, quint16 port)
{
	stop();
	m_registry = registry;
	m_server = new QTcpServer(this);
	if (!m_server->listen(QHostAddress::LocalHost, port))
	{
		qWarning("AiAgentServer: could not listen on 127.0.0.1:%u: %s", port, qPrintable(m_server->errorString()));
		stop();
		return false;
	}
	QByteArray raw(32, Qt::Uninitialized);
	QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(raw.data()), raw.size() / int(sizeof(quint32)));
	m_token = QString::fromLatin1(raw.toHex());
	m_tokenFile = tokenFilePath;
	QSaveFile f(m_tokenFile);
	const QJsonObject info{{"port", int(m_server->serverPort())}, {"token", m_token}};
	if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(info).toJson(QJsonDocument::Compact) + '\n') < 0 || !f.commit())
	{
		qWarning("AiAgentServer: could not write %s: %s", qPrintable(m_tokenFile), qPrintable(f.errorString()));
		stop();
		return false;
	}
	connect(m_server, &QTcpServer::newConnection, this, &AiAgentServer::onNewConnection);
	qInfo("AiAgentServer: listening on 127.0.0.1:%u, token file %s", m_server->serverPort(), qPrintable(m_tokenFile));
	return true;
}

void AiAgentServer::stop()
{
	for (auto it = m_buffers.begin(); it != m_buffers.end(); ++it)
	{
		it.key()->disconnect(this);
		it.key()->abort();
		it.key()->deleteLater();
	}
	m_buffers.clear();
	if (m_server)
	{
		m_server->close();
		m_server->deleteLater();
		m_server = nullptr;
	}
	if (!m_tokenFile.isEmpty()) { QFile::remove(m_tokenFile); m_tokenFile.clear(); }
	m_token.clear();
	m_registry = nullptr;
}

bool AiAgentServer::running() const { return m_server && m_server->isListening(); }

quint16 AiAgentServer::port() const { return running() ? m_server->serverPort() : 0; }

void AiAgentServer::onNewConnection()
{
	while (auto socket = m_server->nextPendingConnection())
	{
		m_buffers.insert(socket, {});
		connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
		connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
			m_buffers.remove(socket);
			socket->deleteLater();
		});
	}
}

void AiAgentServer::onReadyRead(QTcpSocket* socket)
{
	auto it = m_buffers.find(socket);
	if (it == m_buffers.end()) { return; }
	it->append(socket->readAll());
	if (m_dispatching) { return; } // a handler is running (e.g. render's nested loop); drained afterwards
	drainAll();
}

void AiAgentServer::drainAll()
{
	m_dispatching = true;
	bool progressed = true;
	while (progressed)
	{
		progressed = false;
		for (auto socket : m_buffers.keys())
		{
			auto it = m_buffers.find(socket);
			if (it == m_buffers.end()) { continue; }
			const int nl = it->indexOf('\n');
			if (nl < 0)
			{
				if (it->size() > MaxLineBytes)
				{
					socket->write(QJsonDocument(QJsonObject{{"id", QJsonValue::Null}, {"error", "line too long"}}).toJson(QJsonDocument::Compact) + '\n');
					socket->disconnectFromHost();
					m_buffers.remove(socket);
					progressed = true;
				}
				continue;
			}
			const QByteArray line = it->left(nl);
			it->remove(0, nl + 1);
			bool closeAfter = false;
			const QByteArray reply = handleLine(line, &closeAfter);  // may run a nested event loop
			if (!m_buffers.contains(socket)) { continue; }           // client went away meanwhile
			socket->write(reply);
			if (closeAfter) { socket->disconnectFromHost(); m_buffers.remove(socket); }
			progressed = true;
		}
	}
	m_dispatching = false;
}

QByteArray AiAgentServer::handleLine(const QByteArray& line, bool* closeAfter)
{
	auto respond = [](const QJsonValue& id, const char* key, const QJsonValue& v) {
		return QJsonDocument(QJsonObject{{"id", id}, {key, v}}).toJson(QJsonDocument::Compact) + '\n';
	};
	if (line.size() > MaxLineBytes) { *closeAfter = true; return respond(QJsonValue::Null, "error", "line too long"); }
	QJsonParseError perr;
	const auto doc = QJsonDocument::fromJson(line, &perr);
	if (!doc.isObject()) { return respond(QJsonValue::Null, "error", "request is not a JSON object: " + perr.errorString()); }
	const QJsonObject req = doc.object();
	const QJsonValue id = req.contains("id") ? req["id"] : QJsonValue::Null;
	if (req["token"].toString() != m_token) { *closeAfter = true; return respond(id, "error", "bad token"); }
	if (!req["tool"].isString() || req["tool"].toString().isEmpty()) { return respond(id, "error", "missing string field 'tool'"); }
	const QString tool = req["tool"].toString();
	if (!m_registry || !m_registry->has(tool)) { return respond(id, "error", "unknown tool '" + tool + "'"); }
	if (req.contains("args") && !req["args"].isObject()) { return respond(id, "error", "'args' must be an object"); }
	return respond(id, "result", m_registry->call(tool, req["args"].toObject()));
}

} // namespace lmms
```

Note on `stop()` inside `start()` failure paths: `stop()` resets `m_registry`; the early `stop()` call at the top of `start()` runs before `m_registry` is assigned, so order is correct.

- [x] **Step 5: CMake + build** — add `core/ai/AiAgentServer.cpp` to `src/core/CMakeLists.txt`; `cmake build`; delete `build/src/lmmsobjs_autogen/timestamp`; `cmake --build build --target AiAgentServerTest`.

- [x] **Step 6: Run** — `build/tests/AiAgentServerTest.exe -o /tmp/t2.txt,txt`; 7 cases pass.

- [x] **Step 7: Commit** — `git add include/AiAgentServer.h src/core/ai/AiAgentServer.cpp src/core/CMakeLists.txt tests/CMakeLists.txt tests/src/core/ai/AiAgentServerTest.cpp && git commit -m "feat(ai): loopback agent server"`.

---

### Task 3: Remove the in-app LLM path (Lane B, worktree)

**Files:**
- Delete: `src/core/ai/OpenAiClient.cpp`, `src/core/ai/OpenAiStreamParser.cpp`, `src/core/ai/AiSession.cpp`, `src/core/ai/AiPromptBuilder.cpp`, `include/OpenAiClient.h`, `include/OpenAiStreamParser.h`, `include/AiClient.h`, `include/AiSession.h`, `include/AiPromptBuilder.h`, `include/AiChatView.h`, `src/gui/ai/AiChatView.cpp` (and the now-empty `src/gui/ai/` directory), `data/ai/system_prompt.md`, `data/ai/CMakeLists.txt`, `tests/scripted/ai_mock_server.py`, `tests/src/core/ai/OpenAiClientTest.cpp`, `tests/src/core/ai/OpenAiStreamParserTest.cpp`, `tests/src/core/ai/AiSessionTest.cpp`, `tests/src/core/ai/AiPromptBuilderTest.cpp`
- Modify: `src/core/CMakeLists.txt` (remove the four `.cpp`), `src/gui/CMakeLists.txt:47` (remove `gui/ai/AiChatView.cpp`), `tests/CMakeLists.txt` (remove the four test entries), `data/CMakeLists.txt:1` (remove `ADD_SUBDIRECTORY(ai)`), `src/gui/MainWindow.cpp:39,461-463,480-481,493-494,895-896,1014-1017,1085-1091`, `include/MainWindow.h` (the `toggleAiChatWin` slot), `src/gui/GuiApplication.cpp:32,176-178,262-265`, `include/GuiApplication.h:39,84,108`, `src/gui/modals/SetupDialog.cpp:38,46,876-961,1147,1590-1653` and `include/SetupDialog.h:134-141,235-242`, `include/AiConfig.h`, `src/core/ai/AiConfig.cpp`, `include/AiPathPolicy.h:47-48,55`, `src/core/ai/AiPathPolicy.cpp` (`allowFromUserText` and `m_files` use inside `allows`), `tests/src/core/ai/AiPathPolicyTest.cpp` (cases that call `allowFromUserText`)

**Interfaces:**
- Produces: `struct LMMS_EXPORT AiConfig { bool agentServer = false; static AiConfig load(); static void save(const AiConfig&); };` with config key class `"ai"`, attribute `"agentserver"`, value `"1"`/`"0"`. The AI settings page stays registered as `ConfigTab::AiSettings` and is reduced to `labelWidget(ai_w, tr("Agent control"))` plus an empty `QVBoxLayout` with a stretch — Task 4 fills it. `SetupDialog::accept()`'s AI line becomes `AiConfig::save({m_agentServer});` with a new `bool m_agentServer` member loaded from `AiConfig::load().agentServer` where the old `m_aiBaseUrl…` were loaded.
- Removes: `GuiApplication::aiChatView()`, `MainWindow::toggleAiChatWin`, `Ctrl+Alt+A`.

- [x] **Step 1: Worktree** — `cd C:/git_repos/lmms-wt/t10 && git checkout -B harness-removal <BASE>` where `<BASE>` is the commit the controller names in the dispatch (the `ai-composer` head at dispatch time). Build cache in `t10/build` is reused.

- [x] **Step 2: Delete files** — `git rm` every path in the Delete list. `git rm -r data/ai tests/scripted/ai_mock_server.py`.

- [x] **Step 3: CMake lists** — remove the entries named above. `data/CMakeLists.txt` loses line 1 only.

- [x] **Step 4: GUI wiring** — in `MainWindow.cpp` remove the include, the `ai_chat_window` ToolButton block (`:461-463`), the two `if (sd.exec() == QDialog::Accepted) { getGUI()->aiChatView()->reloadConfig(); }` bodies become `sd.exec();`, `showSettingsDialog()` likewise, delete `toggleAiChatWin()` and the View-menu `addAction` for "AI Composer" (`:1085-1091` — keep the following `addSeparator` only if there is still an item before it; check the result reads as upstream's menu). Remove the slot declaration from `include/MainWindow.h`. In `GuiApplication.cpp` remove the include, the "Preparing AI composer" block, the `childDestroyed` branch; in `GuiApplication.h` the forward declaration, accessor and member.

- [x] **Step 5: Settings page** — `SetupDialog.cpp`: remove `#include "AiConfig.h"`? No — keep it (Task 4 and `accept()` use it); remove `#include "OpenAiClient.h"`. Replace the AI page body (`:885-961`) with:

```cpp
	m_agentServer = AiConfig::load().agentServer;
	ai_layout->addStretch();
```

Replace `AiConfig::save({m_aiBaseUrl, m_aiApiKey, m_aiModel, m_aiMaxTokens, m_aiDisableThinking});` with `AiConfig::save({m_agentServer});`. Delete the slot bodies `setAiBaseUrl`, `setAiApiKey`, `setAiModel`, `setAiMaxTokens`, `toggleAiDisableThinking`, `toggleAiKeyVisible`, `testAiConnection` and their declarations; delete the `m_ai*` members; add `bool m_agentServer;` under the `// AI settings widgets.` comment.

- [x] **Step 6: AiConfig** — `include/AiConfig.h` struct body becomes:

```cpp
struct LMMS_EXPORT AiConfig
{
	//! Start the loopback agent server at launch (Settings > AI). Read once at startup.
	bool agentServer = false;
	static AiConfig load();
	static void save(const AiConfig& c);
};
```

`AiConfig.cpp`:

```cpp
AiConfig AiConfig::load()
{
	AiConfig c;
	c.agentServer = ConfigManager::inst()->value("ai", "agentserver").toInt() != 0;
	return c;
}

void AiConfig::save(const AiConfig& c)
{
	ConfigManager::inst()->setValue("ai", "agentserver", QString::number(c.agentServer ? 1 : 0));
}
```

Remove `DefaultBaseUrl` and `configured()` if present.

- [x] **Step 7: AiPathPolicy** — remove `allowFromUserText`, `m_files`, and the `m_files` check in `allows()`; update the class comment ("Allowed are files under any root directory"). Delete the test cases in `AiPathPolicyTest.cpp` that call `allowFromUserText`; keep root/`..`/canonical-form cases.

- [x] **Step 8: Build** — `cmake build` (source lists changed), `rm -f build/src/lmmsobjs_autogen/timestamp`, `cmake --build build`. Fix any remaining reference the compiler finds (grep `AiChatView|OpenAiClient|AiSession|AiPromptBuilder|aiChatView` across `src include tests` must return nothing).

- [x] **Step 9: Run retained tests** — `AiPathPolicyTest`, `AiToolRegistryTest`, `AiProjectToolsTest`, `AiActionToolsTest` via `build/tests/<Name>.exe -o /tmp/<Name>.txt,txt`; all pass. Launch `build/lmms.exe` once, open Settings: the AI tab exists and is empty apart from its title; there is no AI Composer toolbar button or View-menu entry; `Ctrl+Alt+A` does nothing. Close.

- [x] **Step 10: Commit** — one commit: `git add -A && git commit -m "refactor(ai): remove in-app LLM composer; keep tool layer"`.

---

### Task 4: Settings checkbox and server start-up (Lane A, after Task 3 is merged)

**Files:**
- Modify: `src/gui/modals/SetupDialog.cpp` (AI page body), `include/SetupDialog.h` (`toggleAgentServer` slot), `src/gui/GuiApplication.cpp`, `include/GuiApplication.h`

**Interfaces:**
- Consumes: `AiAgentServer` (Task 2), `registerAiMetaTools` + `AiProjectSnapshot` (Task 1), `AiConfig::agentServer` (Task 3), `registerAiProjectTools/DiscoveryTools/ActionTools` (`include/AiTools.h`), `AiPathPolicy::setRoots`.

- [x] **Step 1: Settings page body** — replace the two lines left by Task 3 with:

```cpp
	m_agentServer = AiConfig::load().agentServer;
	auto agentBox = new QGroupBox(tr("External agent"), ai_w);
	auto agentLayout = new QVBoxLayout(agentBox);
	auto agentCheckBox = new QCheckBox(tr("Allow an external agent to control LMMS over localhost (takes effect after restart)"), agentBox);
	agentCheckBox->setChecked(m_agentServer);
	connect(agentCheckBox, &QCheckBox::toggled, this, &SetupDialog::toggleAgentServer);
	agentLayout->addWidget(agentCheckBox);
	auto agentPathLbl = new QLabel(tr("While LMMS runs, the port and access token are written to:<br><code>%1</code>")
		.arg(AiAgentServer::defaultTokenFilePath().toHtmlEscaped()), agentBox);
	agentPathLbl->setWordWrap(true);
	agentPathLbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
	agentLayout->addWidget(agentPathLbl);
	auto agentWarnLbl = new QLabel(tr("Any local program that can read that file gets full control of the open project."), agentBox);
	agentWarnLbl->setWordWrap(true);
	agentLayout->addWidget(agentWarnLbl);
	ai_layout->addWidget(agentBox);
	ai_layout->addStretch();
```

Add `#include "AiAgentServer.h"`; slot `void toggleAgentServer(bool enabled) { m_agentServer = enabled; }` declared under the AI comment in the header.

- [x] **Step 2: GuiApplication** — header: forward-declare `class AiAgentServer;` and add members

```cpp
	AiToolRegistry m_agentRegistry;
	AiPathPolicy m_agentPolicy;
	AiProjectSnapshot m_agentSnapshot;
	AiAgentServer* m_agentServer = nullptr;
```

(`#include "AiPathPolicy.h"`, `"AiProjectSnapshot.h"`, `"AiToolRegistry.h"` — these are by-value members, so includes not forward declarations.) In `GuiApplication.cpp`, after the last `displayInitProgress`/subwindow creation and before the main window is shown, add:

```cpp
	if (AiConfig::load().agentServer)
	{
		displayInitProgress(tr("Starting agent server"));
		auto cm = ConfigManager::inst();
		QStringList roots;
		for (const auto& dir : {cm->dataDir(), cm->workingDir(), cm->factoryPresetsDir(), cm->userPresetsDir(),
				cm->factorySamplesDir(), cm->userSamplesDir()})
		{
			if (!dir.isEmpty()) { roots << QDir(dir).absolutePath(); }
		}
		m_agentPolicy.setRoots(roots);
		auto pathAllowed = [this](const QString& p) {
			// The project directory can change with every open/save, so it is checked live.
			const QString& projectFile = Engine::getSong()->projectFileName();
			if (!projectFile.isEmpty())
			{
				AiPathPolicy withProject = m_agentPolicy;
				withProject.setRoots(m_agentPolicy.roots() << QFileInfo(projectFile).absolutePath());
				return withProject.allows(p);
			}
			return m_agentPolicy.allows(p);
		};
		registerAiProjectTools(m_agentRegistry, pathAllowed);
		registerAiDiscoveryTools(m_agentRegistry, m_agentPolicy);
		registerAiActionTools(m_agentRegistry, pathAllowed);
		registerAiMetaTools(m_agentRegistry, m_agentSnapshot);
		m_agentServer = new AiAgentServer(this);
		m_agentServer->start(&m_agentRegistry, AiAgentServer::defaultTokenFilePath());
	}
```

Includes: `AiAgentServer.h`, `AiConfig.h`, `AiTools.h`, `Engine.h`, `Song.h`, `<QDir>`, `<QFileInfo>`. `registerAiDiscoveryTools` takes the policy by reference and reads roots at call time, so it sees the static roots only; `get_preset_xml` on a preset next to the project file is the only case that loses, acceptable.

- [x] **Step 3: Build and check** — full `cmake --build build`. Launch `build/lmms.exe`; Settings → AI shows the checkbox and path; tick it, OK, quit, relaunch; the console prints `AiAgentServer: listening on 127.0.0.1:<port>` and the token file exists at the printed path with `port` and `token`. From bash: `printf '{"id":1,"token":"%s","tool":"ping"}\n' "$(python -c "import json;print(json.load(open(r'<path>'))['token'])")" | python -c "import socket,sys,json;f=json.load(open(r'<path>'));s=socket.create_connection(('127.0.0.1',f['port']));s.sendall(sys.stdin.buffer.read());print(s.recv(4096))"` prints `{"id":1,"result":{"ok":true,"version":"…"}}`. Quit LMMS; the token file is gone.

- [x] **Step 4: Commit** — `git add src/gui/modals/SetupDialog.cpp include/SetupDialog.h src/gui/GuiApplication.cpp include/GuiApplication.h && git commit -m "feat(ai): agent server setting and start-up"`.

---

### Task 5: `lmmsctl.py` client (Lane C)

**Files:**
- Create: `.claude/skills/lmms-composer/scripts/lmmsctl.py`, `.claude/skills/lmms-composer/scripts/test_lmmsctl.py`

**Interfaces:**
- Consumes: wire format from Global Constraints; token file `{"port", "token"}`.
- Produces:
  ```python
  class LmmsError(Exception): ...            # transport-level: cannot connect, {"error": ...} reply
  class LmmsToolError(LmmsError): ...        # result.ok == False (raised by ok() only)
  def token_file_path() -> pathlib.Path     # $LMMS_AGENT_FILE, else <workingdir>/.lmms-agent.json
  class Lmms:
      def __init__(self, path: Path | None = None, timeout: float = 600.0)
      def call(self, tool: str, args: dict | None = None) -> dict   # returns result object
      def ok(self, tool: str, args: dict | None = None) -> dict     # raises LmmsToolError when ok is False
      def tools(self) -> list[dict]                                 # list_tools → tools array
      def close(self)
  ```
  CLI: `lmmsctl.py tools [--schema]`, `lmmsctl.py summary`, `lmmsctl.py call <tool> [<json>] [--args-file F] [-]`. Exit 0 on `ok:true`, 1 on `ok:false`, 2 on transport error; prints the result JSON (indent 2) to stdout, errors to stderr.

- [x] **Step 1: Failing tests** — `test_lmmsctl.py` with a stub NDJSON server in a thread:

```python
import json, os, socket, socketserver, tempfile, threading, unittest
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

if __name__ == "__main__": unittest.main()
```

- [x] **Step 2: Run to see it fail** — `cd .claude/skills/lmms-composer/scripts && python -m unittest test_lmmsctl -v` → `ModuleNotFoundError: lmmsctl`.

- [x] **Step 3: Implement `lmmsctl.py`** — module docstring with the CLI usage; `token_file_path()` reads `LMMS_AGENT_FILE`, else parses `~/.lmmsrc.xml` (`<paths workingdir="…">`, `xml.etree`) when present, else `Path.home() / "lmms"`, and returns `<workingdir>/.lmms-agent.json`. `Lmms.__init__` stores the path, connects lazily on first `call` (`socket.create_connection(("127.0.0.1", port), timeout)`, `makefile("rb")` for line reads); a missing token file raises `LmmsError("No agent connection file at <path>. In LMMS enable Settings > AI > 'Allow an external agent…' and restart LMMS.")`; connection refused raises `LmmsError` naming the port. `call` sends `{"id": n, "token", "tool", "args"}` with an incrementing id, reads one line, raises `LmmsError(reply["error"])` on `error`, returns `reply["result"]`. `ok` calls `call` and raises `LmmsToolError(result.get("error", "tool failed"))` when `not result.get("ok")`. Context manager support (`__enter__`/`__exit__` → `close`). CLI in `main(argv=None) -> int` with `argparse` subcommands as specified; `call` merges the inline JSON string, `--args-file`, and `-` (stdin) — exactly one source, or none for `{}`; `tools` prints `name — first sentence of description` per line, `--schema` dumps the full array; `summary` prints `get_project_summary`. `if __name__ == "__main__": sys.exit(main())`.

- [x] **Step 4: Run tests** — all 7 pass.

- [x] **Step 5: Commit** — `git add .claude/skills/lmms-composer/scripts/lmmsctl.py .claude/skills/lmms-composer/scripts/test_lmmsctl.py && git commit -m "feat(skill): lmmsctl agent client"`.

---

### Task 6: `check_render.py` (Lane C)

**Files:**
- Create: `.claude/skills/lmms-composer/scripts/check_render.py`, `.claude/skills/lmms-composer/scripts/test_check_render.py`

**Interfaces:**
- Produces: `analyze(path: Path, bpm: float | None = None, ticks_per_bar: int = 192, bars: int | None = None) -> dict` with keys `channels, sample_rate, duration_s, peak_dbfs, rms_dbfs, clipped_samples, silent_bars (list[int], 1-based, only when bpm given)`; CLI `check_render.py <wav> [--bpm B] [--ticks-per-bar 192] [--bars N]` prints the dict as JSON and exits 1 when `clipped_samples > 0` or the whole file is below −60 dBFS RMS, else 0. Bar length in seconds = `4 * 60 / bpm * (ticks_per_bar / 192)`.

- [x] **Step 1: Failing tests** — synthesize WAVs with `wave` + `array`/`struct`: (a) 2 s 440 Hz sine at −6 dBFS, 44.1 kHz, 16-bit stereo → `peak_dbfs ≈ −6 ± 0.2`, `clipped_samples == 0`, exit 0; (b) same with 1 000 samples at +32767 → `clipped_samples >= 1000`, exit 1; (c) 4 bars at 120 BPM where bar 3 is silence → `silent_bars == [3]`; (d) all-zero file → exit 1; (e) 24-bit mono file reads without error and reports `channels == 1`.

- [x] **Step 2: Run to see failure** — `python -m unittest test_check_render -v` → import error.

- [x] **Step 3: Implement** — stdlib `wave` for 16-bit; for 24-bit unpack 3-byte little-endian manually; 32-bit via `array('i')`. RMS over the whole file and per bar; `−inf` reported as `-120.0`. Clipped = samples equal to the type's max or min. `main(argv) -> int`.

- [x] **Step 4: Run tests** — pass.

- [x] **Step 5: Commit** — `git add .claude/skills/lmms-composer/scripts/check_render.py .claude/skills/lmms-composer/scripts/test_check_render.py && git commit -m "feat(skill): render sanity check"`.

---

### Task 7: `fetch_soundfont.py` (Lane C)

**Files:**
- Create: `.claude/skills/lmms-composer/scripts/fetch_soundfont.py`

**Interfaces:**
- Produces: CLI `fetch_soundfont.py [--dest DIR] [--force]`; prints the absolute `.sf2` path on success; exit 0. Default dest: `<workingdir>/samples/soundfonts/` with the working directory resolved like `lmmsctl.token_file_path()` (import `lmmsctl` from the same directory).

- [x] **Step 1: Research the download** — the official source is S. Christian Collins's GitHub repository `mrbumpy409/GeneralUser-GS` (current release 2.0.3). Find the direct URL of the `.sf2` (a GitHub release asset or the raw file), download it once by hand, and record its SHA-256 and byte size in the script as constants `URL`, `SHA256`, `SIZE`, `VERSION`, `FILENAME = "GeneralUser-GS.sf2"`. Also locate the licence text in the repository (`documentation/LICENSE.txt` or equivalent) and record its URL as `LICENSE_URL`.

- [x] **Step 2: Implement** — `urllib.request` with a progress line every 5 %; download to `<dest>/<FILENAME>.part`, verify SHA-256, rename; fetch the licence to `<dest>/GeneralUser-GS-LICENSE.txt`; if the target exists and its SHA-256 matches, print the path and exit without downloading (unless `--force`). Non-matching checksum → delete the part file, exit 3 with a message. `main(argv) -> int`.

- [x] **Step 3: Verify** — run it into a temp `--dest`; it downloads, verifies, prints the path; run again: "already present", no download. Run `fluidsynth`-free sanity: the file starts with bytes `RIFF` and contains `sfbk` at offset 8.

- [x] **Step 4: Commit** — `git add .claude/skills/lmms-composer/scripts/fetch_soundfont.py && git commit -m "feat(skill): GeneralUser GS fetcher"`.

---

### Task 8: Skill document and references (Lane C)

**Files:**
- Create: `.claude/skills/lmms-composer/SKILL.md`, `.claude/skills/lmms-composer/references/theory.md`, `.claude/skills/lmms-composer/references/tools.md`

**Interfaces:**
- Consumes: the tool list — the 29 registered in `src/core/ai/AiProjectTools.cpp`, `AiDiscoveryTools.cpp`, `AiActionTools.cpp` (read their `r.add({…})` descriptions and schemas), plus `ping`, `list_tools`, `checkpoint`, `revert`, `commit`, `add_sf2_track` as specified in Task 1's Interfaces; the CLI shape from Task 5's Interfaces; `check_render.py` and `fetch_soundfont.py` CLIs from Tasks 6–7.

- [x] **Step 1: `SKILL.md`** — frontmatter:

```yaml
---
name: lmms-composer
description: Use whenever the user asks to create, edit, arrange, mix, or render music in LMMS, or to recreate a song, artist or genre — "make a beat", "write a chorus", "build Creep by Radiohead", "add a bass line", "make it less echoey". Drives the running LMMS through the agent server with scripts/lmmsctl.py. Not for changing LMMS's own source code.
---
```

Body sections, in this order, each concrete (commands, tool names, numbers): **Connect** (token file, `lmmsctl.py tools`, what to tell the user when it is missing, how to start LMMS with the process supervisor: `hub start name=lmms application=C:/git_repos/lmms/build/lmms.exe ready.log="AiAgentServer: listening"`); **Research first** (for a named song/artist: tempo, key, progression per section, form with bar counts, instrumentation, feel — via `web_search`, at least two sources, state the findings with sources before touching the project; for a genre/mood: pick from `theory.md`'s recipes and say so; never guess a named song's tempo or key; melodies are written from that research and by ear, not transcribed note-for-note); **Build order** (`checkpoint` → `get_project_summary` → `set_head` → palette → sections with `add_clips` → mix → verify); **Palette** (SoundFont first: `fetch_soundfont.py` once, `add_sf2_track` with patches from `theory.md`; LMMS synths when the style is synth-native; one track per role; kit on bank 128); **Writing parts** (per-song build script pattern using `from lmmsctl import Lmms` — a 20-line example that builds a 4-bar drum pattern and a bass line with helper functions `bar(n)`, `beat(b)`, `chord(root, kind)`); **Mix discipline** (levels: drums 100, bass 90, chords 70, lead 85; pan hats/keys slightly; reverb only on pads/keys/leads and ≤ 25 % wet — check the ReverbSC parameter names with `describe_model_tree` first; drums and bass dry; no effect without a stated purpose; `add_effect` compressor on channel 0 only if peaks clip); **Verify** (`render` to `<workingdir>/renders/<name>.wav`, `check_render.py --bpm`, fix silent bars/clipping, `get_project_summary`); **Hand over** (`commit` or `revert`; `save` when asked; report the section map and one line per track; offer to share the render). Keep the whole file under 250 lines.

- [x] **Step 2: `references/theory.md`** — migrate the music-theory and genre-recipe content from the former `data/ai/system_prompt.md` (available in git history at `git show a929195ee:data/ai/system_prompt.md`) with these corrections: keys are MIDI numbers (A4 = 69, C4 = 60); instrument base note defaults to 69 — never write `basenote="57"`; remove every mention of "basenote 57". Add a **GM patch table** for `add_sf2_track` (bank 0): 0 Acoustic Grand, 1 Bright Piano, 4 Electric Piano 1, 5 Electric Piano 2, 16 Drawbar Organ, 24 Nylon Guitar, 25 Steel Guitar, 26 Jazz Guitar, 27 Clean Guitar, 28 Muted Guitar, 29 Overdriven Guitar, 30 Distortion Guitar, 32 Acoustic Bass, 33 Finger Bass, 34 Pick Bass, 35 Fretless, 38 Synth Bass 1, 40 Violin, 42 Cello, 48 String Ensemble, 49 Slow Strings, 52 Choir Aahs, 56 Trumpet, 61 Brass Section, 65 Alto Sax, 73 Flute, 80 Square Lead, 81 Saw Lead, 88 New Age Pad, 89 Warm Pad; bank 128: patch 0 Standard Kit, 8 Room, 16 Power, 24 Electronic, 25 TR-808, 32 Jazz, 40 Brush. **GM drum map**: 35/36 kick, 37 side stick, 38 snare, 39 clap, 40 snare 2, 41 low tom, 42 closed hat, 44 pedal hat, 45 mid tom, 46 open hat, 48 high tom, 49 crash, 51 ride, 53 ride bell, 54 tambourine, 56 cowbell. Ticks table, scale/chord/progression tables, velocity guidance, and the genre recipes rewritten to name SoundFont patches where a real instrument fits.

- [x] **Step 3: `references/tools.md`** — one `###` section per tool (35), grouped as Meta / Project / Convenience / Discovery / Actions, each: purpose, args (name, type, required?, meaning), result fields, gotchas taken from the descriptions in the source. State at the top that `lmmsctl.py tools --schema` is the authoritative schema and this file is the readable companion.

- [x] **Step 4: Check** — `python - <<'EOF'` that parses the frontmatter of `SKILL.md` (starts with `---`, has `name:` and `description:`); count of `###` headings in `tools.md` is 35; `grep -c 'basenote="57"' references/*.md SKILL.md` is 0.

- [x] **Step 5: Commit** — `git add .claude/skills/lmms-composer && git commit -m "feat(skill): lmms-composer skill and references"`.

---

### Task 9: Integration (controller)

- [x] **Step 1: Merge Lane B** — on `ai-composer`: `git merge harness-removal`. Resolve conflicts in `src/core/CMakeLists.txt` / `tests/CMakeLists.txt` by keeping Lane A's additions and Lane B's removals; `GuiApplication.cpp` keep both hunks.
- [x] **Step 2: Dispatch Task 4.**
- [x] **Step 3: Full build** — `cmake build && rm -f build/src/lmmsobjs_autogen/timestamp && cmake --build build`.
- [x] **Step 4: All tests** — every `build/tests/*.exe -o <file>,txt`; 8 upstream + `AiPathPolicyTest AiToolRegistryTest AiProjectToolsTest AiActionToolsTest AiAgentServerTest` pass. `build/lmms.exe render tests/emptyproject.mmp -o /tmp/empty.wav` exits 0.
- [x] **Step 5: Python tests** — `python -m unittest discover -s .claude/skills/lmms-composer/scripts -p 'test_*.py'`.
- [x] **Step 6: Spec status** — add `Status: superseded by 2026-09-29-agent-harness-design.md` under the date line of the 2026-09-16 spec; commit `docs: mark AI composer spec superseded`.

### Task 10: Smoke — make music the user can hear (controller, using the skill)

- [x] **Step 1** — Enable the setting (or write `<ai agentserver="1"/>` into `.lmmsrc.xml`), start `build/lmms.exe` via `hub start` with `ready.log "AiAgentServer: listening"`.
- [x] **Step 2** — `fetch_soundfont.py`; `lmmsctl.py tools` lists 35 tools.
- [x] **Step 3** — Following `SKILL.md`: `checkpoint`; 16 bars at a researched tempo/key of a simple, well-known progression; tracks: drum kit (bank 128), finger bass, clean guitar or piano chords, a lead; `render` to `<workingdir>/renders/agent-smoke.ogg` (and `.wav` for `check_render.py`); `check_render.py --bpm` clean; `save` to `<workingdir>/projects/agent-smoke.mmp`; `commit`.
- [x] **Step 4** — Hand the render to the user (`xd://share_to_telegram` with the `.ogg`, plus the local path). Success criterion is the user's ear.

### Task 11: Docs sync (controller)

- [ ] Dispatch the context-steward over the full range; relay its report. `CLAUDE.md` auto blocks must no longer mention `data/ai`, `Ctrl+Alt+A`, `OpenAi*`, `AiSession`, `AiChatView`, or the 8-test AI count.
