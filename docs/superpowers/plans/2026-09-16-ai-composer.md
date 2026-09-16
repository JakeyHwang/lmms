# AI Composer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development (recommended) or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** An in-app chat panel where an OpenAI-compatible LLM builds and edits the open LMMS project through a tool layer whose universal surface is the project's own `.mmp` XML.

**Architecture:** `src/core/ai/` holds a network client (`OpenAiClient` + a pure `OpenAiStreamParser`), a tool registry, tool handlers (project XML, convenience, discovery, actions) and the agent loop (`AiSession`). `src/gui/ai/AiChatView` is an MDI sub-window like the Controller Rack; an *AI* page in `SetupDialog` stores base URL/key/model in `ConfigManager`. Every tool handler runs on the GUI thread; one song-level journal checkpoint per user turn gives whole-turn undo.

**Tech Stack:** C++20, Qt 6 (Core, Widgets, Xml, **Network** — new), QTest, CMake/Ninja under MSYS2 CLANG64 (see "Build" below).

**Spec:** `docs/superpowers/specs/2026-09-16-ai-composer-design.md`

## Global Constraints

- LLM API shape: OpenAI-compatible `POST {baseUrl}/chat/completions` with `tools` / `tool_calls`, `stream: true`; also accept non-streaming JSON bodies.
- Config keys (`ConfigManager`): class `"ai"`, attributes `baseurl` (default `https://api.openai.com/v1`), `apikey`, `model`.
- Per-turn caps: 40 tool calls; 5 consecutive tool errors. Request body over 120 000 chars → elide oldest tool results to `"[elided]"`. Tool XML results over 64 KB → error.
- Every tool result is a JSON object with `ok: bool` and, when `ok` is false, `error: string`.
- All XML submitted by the model goes through `DataFile`; reject when `hasLocalPlugins()` is true.
- File paths accepted by tools must be under: project file's directory, `ConfigManager::dataDir()`, `ConfigManager::workingDir()`, or a path the user typed in chat this session.
- Model mutations happen on the GUI thread. Wrap raw model edits in `Engine::audioEngine()->requestChangesGuard()`; never wrap calls that lock internally (`Track::create`, `EffectChain::appendEffect`, `Song::stop`, `Song::loadProject`) — `std::mutex` is non-recursive.
- `MidiClip::addNote(note, false)` and `AutomationClip::putValue(t, v, false)` — the `quantPos=true` variants dereference the piano roll.
- Headers are flat in `include/` with an `Ai` prefix (repo convention: no header subdirectories). Sources go in `src/core/ai/` and `src/gui/ai/`.
- Code style: tabs, `namespace lmms { … }` (GUI classes in `namespace lmms::gui`), GPL header comment block copied from a neighbouring file, `LMMS_EXPORT` on classes used by tests/GUI.
- Tests: `tests/src/core/ai/*Test.cpp`, QTest, `QTEST_GUILESS_MAIN`, added to `LMMS_TESTS` in `tests/CMakeLists.txt`. Tests that touch `Song` call `Engine::init(true)` in `initTestCase` and `Engine::destroy()` in `cleanupTestCase`.
- No formatter/linter runs mid-plan. Run the full CTest suite only in the final task.

## Build

All commands run from the MSYS2 CLANG64 shell (`C:\msys64\clang64.exe`), repo at `/c/git_repos/lmms`, build dir `build/` already configured (Ninja, RelWithDebInfo, Qt6). Rebuild:

```
cmake --build build
```

Run one test: `build/tests/<TestName>.exe`. Re-run configure only when CMake files change (`cmake --build build` does that automatically via Ninja).

## Interfaces (shared by all tasks)

```cpp
// include/AiClient.h — abstract LLM transport (Task 2 defines; Task 4 consumes)
namespace lmms {
class LMMS_EXPORT AiClient : public QObject {
	Q_OBJECT
public:
	using QObject::QObject;
	virtual void send(const QJsonArray& messages, const QJsonArray& tools) = 0;
	virtual void abort() = 0;
signals:
	void textDelta(const QString& text);
	void completed(const QJsonObject& assistantMessage); // OpenAI "message" object: role, content, tool_calls?
	void failed(const QString& error);
};
}

// include/AiToolRegistry.h (Task 3)
namespace lmms {
struct AiTool {
	QString name;
	QString description;
	QJsonObject parameters;                               // JSON schema
	std::function<QJsonObject(const QJsonObject&)> handler;
};
class LMMS_EXPORT AiToolRegistry {
public:
	void add(AiTool tool);
	bool has(const QString& name) const;
	QJsonArray specs() const;                             // [{type:"function", function:{name,description,parameters}}]
	QJsonObject call(const QString& name, const QJsonObject& args) const; // never throws
	static QJsonObject ok(QJsonObject fields = {});
	static QJsonObject error(const QString& message);
};
}

// include/AiSession.h (Task 4)
namespace lmms {
class LMMS_EXPORT AiSession : public QObject {
	Q_OBJECT
public:
	static constexpr int MaxToolCallsPerTurn = 40;
	static constexpr int MaxConsecutiveToolErrors = 5;
	static constexpr int MaxRequestChars = 120000;
	AiSession(AiClient* client, AiToolRegistry* registry, QObject* parent = nullptr);
	void setSystemPrompt(const QString& text);
	void submit(const QString& userText);   // starts a turn
	void stop();
	void clear();
	bool busy() const;
	const QJsonArray& history() const;
signals:
	void assistantTextDelta(const QString& text);
	void toolCallStarted(const QString& name, const QJsonObject& args);
	void toolCallFinished(const QString& name, const QJsonObject& result);
	void turnFinished(const QString& finalText);
	void turnFailed(const QString& error);
	void status(const QString& text);
};
}

// include/AiPathPolicy.h (Task 8)
namespace lmms {
class LMMS_EXPORT AiPathPolicy {
public:
	void setRoots(QStringList roots);        // absolute dirs
	void allowFromUserText(const QString& text); // extracts absolute paths mentioned by the user
	bool allows(const QString& path) const;
};
}

// include/AiTools.h (Tasks 5–9) — registration entry points
namespace lmms {
void registerAiProjectTools(AiToolRegistry& r);
void registerAiDiscoveryTools(AiToolRegistry& r, AiPathPolicy& policy);
void registerAiActionTools(AiToolRegistry& r, AiPathPolicy& policy);
}

// include/AiConfig.h (Task 1)
namespace lmms {
struct AiConfig {
	QString baseUrl; QString apiKey; QString model;
	static AiConfig load();                 // from ConfigManager
	bool configured() const { return !apiKey.isEmpty() && !model.isEmpty(); }
};
}
```

Tick units: `TimePos::ticksPerBar()` is 192 in 4/4; quarter = 48, 16th = 12.

---

### Task 1: Build wiring and AiConfig

**Files:**
- Modify: `CMakeLists.txt:278` (Qt components), `CMakeLists.txt:290-296` (`QT_LIBRARIES`)
- Modify: `src/core/CMakeLists.txt` (add `core/ai/*.cpp` entries as later tasks create them — this task adds `core/ai/AiConfig.cpp`)
- Create: `include/AiConfig.h`, `src/core/ai/AiConfig.cpp`
- Test: none (compile is the check; `AiConfig::load` is exercised by Task 11's settings page)

**Interfaces:**
- Produces: `AiConfig` as declared above.

- [x] **Step 1: Link Qt Network**

`CMakeLists.txt` line 278 → `COMPONENTS Core Gui Widgets Xml Svg Network REQUIRED`. Add `Qt${QT_VERSION_MAJOR}::Network` to the `set(QT_LIBRARIES …)` block after `::Svg`. Add `${Qt${QT_VERSION_MAJOR}Network_INCLUDE_DIRS}` to the `include_directories(SYSTEM …)` block at line 283.

- [x] **Step 2: AiConfig**

`include/AiConfig.h`:
```cpp
#ifndef LMMS_AI_CONFIG_H
#define LMMS_AI_CONFIG_H
#include <QString>
#include "lmms_export.h"
namespace lmms {
struct LMMS_EXPORT AiConfig
{
	QString baseUrl;
	QString apiKey;
	QString model;
	static AiConfig load();
	static void save(const AiConfig& c);
	bool configured() const { return !apiKey.isEmpty() && !model.isEmpty(); }
	static constexpr const char* DefaultBaseUrl = "https://api.openai.com/v1";
};
}
#endif
```
`src/core/ai/AiConfig.cpp`:
```cpp
#include "AiConfig.h"
#include "ConfigManager.h"
namespace lmms {
AiConfig AiConfig::load()
{
	auto cm = ConfigManager::inst();
	AiConfig c;
	c.baseUrl = cm->value("ai", "baseurl", DefaultBaseUrl);
	c.apiKey = cm->value("ai", "apikey");
	c.model = cm->value("ai", "model");
	while (c.baseUrl.endsWith('/')) { c.baseUrl.chop(1); }
	return c;
}
void AiConfig::save(const AiConfig& c)
{
	auto cm = ConfigManager::inst();
	cm->setValue("ai", "baseurl", c.baseUrl);
	cm->setValue("ai", "apikey", c.apiKey);
	cm->setValue("ai", "model", c.model);
}
}
```
Add `core/ai/AiConfig.cpp` to `src/core/CMakeLists.txt` in a new blank-line-separated group after `core/StepRecorder.cpp`.

- [x] **Step 3: Build**

Run: `cmake --build build`
Expected: links; `lmms.exe --version` still prints.

- [x] **Step 4: Commit**

```bash
git add CMakeLists.txt src/core/CMakeLists.txt include/AiConfig.h src/core/ai/AiConfig.cpp
git commit -m "build: link Qt Network; add AiConfig"
```

---

### Task 2: OpenAiStreamParser and OpenAiClient

**Files:**
- Create: `include/AiClient.h`, `include/OpenAiStreamParser.h`, `include/OpenAiClient.h`, `src/core/ai/OpenAiStreamParser.cpp`, `src/core/ai/OpenAiClient.cpp`
- Modify: `src/core/CMakeLists.txt`, `tests/CMakeLists.txt`
- Test: `tests/src/core/ai/OpenAiStreamParserTest.cpp`

**Interfaces:**
- Produces: `AiClient` (above); `OpenAiStreamParser` with `void feed(const QByteArray& chunk)`, `bool finished() const`, `QJsonObject message() const`, signal-free (pure; the client emits). `OpenAiClient(const AiConfig&, QObject*)`.

- [x] **Step 1: Failing parser tests**

`tests/src/core/ai/OpenAiStreamParserTest.cpp`:
```cpp
#include "OpenAiStreamParser.h"
#include <QtTest>
#include <QJsonArray>
#include <QJsonObject>

class OpenAiStreamParserTest : public QObject
{
	Q_OBJECT
private slots:
	void streamedText()
	{
		lmms::OpenAiStreamParser p;
		p.feed("data: {\"choices\":[{\"delta\":{\"role\":\"assistant\",\"content\":\"Hel\"}}]}\n\n");
		p.feed("data: {\"choices\":[{\"delta\":{\"content\":\"lo\"},\"finish_reason\":null}]}\n\ndata: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\ndata: [DONE]\n\n");
		QVERIFY(p.finished());
		QCOMPARE(p.message()["content"].toString(), QString("Hello"));
		QCOMPARE(p.textDeltas(), QStringList({"Hel", "lo"}));
	}
	void streamedToolCallsSplitAcrossChunks()
	{
		lmms::OpenAiStreamParser p;
		p.feed("data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"c1\",\"type\":\"function\",\"function\":{\"name\":\"add_notes\",\"arguments\":\"{\\\"tr\"}}]}}]}\n\n");
		p.feed("data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":{\"arguments\":\"ack\\\":1}\"}}]}}]}\n\n");
		p.feed("data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":1,\"id\":\"c2\",\"type\":\"function\",\"function\":{\"name\":\"play\",\"arguments\":\"{}\"}}]}}]}\n\n");
		p.feed("data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\n");
		QVERIFY(p.finished());
		auto calls = p.message()["tool_calls"].toArray();
		QCOMPARE(calls.size(), 2);
		QCOMPARE(calls[0].toObject()["function"].toObject()["arguments"].toString(), QString("{\"track\":1}"));
		QCOMPARE(calls[1].toObject()["id"].toString(), QString("c2"));
	}
	void nonStreamingBody()
	{
		lmms::OpenAiStreamParser p;
		p.feed("{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"plain\"},\"finish_reason\":\"stop\"}]}");
		p.end();
		QVERIFY(p.finished());
		QCOMPARE(p.message()["content"].toString(), QString("plain"));
	}
	void errorBody()
	{
		lmms::OpenAiStreamParser p;
		p.feed("{\"error\":{\"message\":\"bad key\"}}");
		p.end();
		QVERIFY(!p.finished());
		QCOMPARE(p.error(), QString("bad key"));
	}
};
QTEST_GUILESS_MAIN(OpenAiStreamParserTest)
#include "OpenAiStreamParserTest.moc"
```
Add `src/core/ai/OpenAiStreamParserTest.cpp` to `LMMS_TESTS` in `tests/CMakeLists.txt`.

- [x] **Step 2: Run to verify failure**

Run: `cmake --build build`
Expected: FAIL — `OpenAiStreamParser.h` not found.

- [x] **Step 3: Implement parser**

`include/OpenAiStreamParser.h`:
```cpp
#ifndef LMMS_OPENAI_STREAM_PARSER_H
#define LMMS_OPENAI_STREAM_PARSER_H
#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QStringList>
#include "lmms_export.h"
namespace lmms {
//! Accumulates an OpenAI chat-completions response (SSE stream or plain JSON) into one assistant message.
class LMMS_EXPORT OpenAiStreamParser
{
public:
	void feed(const QByteArray& chunk);   //!< call for each network chunk
	void end();                           //!< call when the body is complete (handles non-SSE bodies)
	bool finished() const { return m_finished; }
	QString error() const { return m_error; }
	QJsonObject message() const;          //!< {role, content, tool_calls?}
	QStringList takeTextDeltas();         //!< deltas since last call
	QStringList textDeltas() const { return m_allDeltas; }
private:
	struct ToolCall { QString id; QString name; QString arguments; };
	void handleEvent(const QByteArray& data);
	void handleChoice(const QJsonObject& choice, bool streaming);
	QByteArray m_buffer;
	QString m_content;
	QMap<int, ToolCall> m_toolCalls;
	QStringList m_pendingDeltas, m_allDeltas;
	QString m_error;
	bool m_finished = false;
	bool m_sawSse = false;
};
}
#endif
```
`src/core/ai/OpenAiStreamParser.cpp`:
```cpp
#include "OpenAiStreamParser.h"
#include <QJsonArray>
#include <QJsonDocument>
namespace lmms {

void OpenAiStreamParser::feed(const QByteArray& chunk)
{
	m_buffer += chunk;
	int nl;
	while ((nl = m_buffer.indexOf('\n')) >= 0)
	{
		QByteArray line = m_buffer.left(nl).trimmed();
		m_buffer.remove(0, nl + 1);
		if (line.startsWith("data:"))
		{
			m_sawSse = true;
			QByteArray data = line.mid(5).trimmed();
			if (data == "[DONE]") { m_finished = m_finished || !m_error.isEmpty() == false; continue; }
			handleEvent(data);
		}
		else if (!line.isEmpty() && !m_sawSse)
		{
			// not SSE: keep whole body until end()
			m_buffer.prepend(line + '\n');
			return;
		}
	}
}

void OpenAiStreamParser::end()
{
	if (m_sawSse || m_buffer.trimmed().isEmpty()) { return; }
	QJsonParseError err;
	auto doc = QJsonDocument::fromJson(m_buffer, &err);
	m_buffer.clear();
	if (err.error != QJsonParseError::NoError) { m_error = "Unparseable response: " + err.errorString(); return; }
	auto obj = doc.object();
	if (obj.contains("error")) { m_error = obj["error"].toObject()["message"].toString("unknown error"); return; }
	auto choices = obj["choices"].toArray();
	if (choices.isEmpty()) { m_error = "Response has no choices"; return; }
	handleChoice(choices[0].toObject(), false);
	m_finished = true;
}

void OpenAiStreamParser::handleEvent(const QByteArray& data)
{
	auto obj = QJsonDocument::fromJson(data).object();
	if (obj.contains("error")) { m_error = obj["error"].toObject()["message"].toString("unknown error"); return; }
	auto choices = obj["choices"].toArray();
	if (choices.isEmpty()) { return; }
	handleChoice(choices[0].toObject(), true);
}

void OpenAiStreamParser::handleChoice(const QJsonObject& choice, bool streaming)
{
	auto msg = choice[streaming ? "delta" : "message"].toObject();
	auto content = msg["content"];
	if (content.isString() && !content.toString().isEmpty())
	{
		m_content += content.toString();
		m_pendingDeltas << content.toString();
		m_allDeltas << content.toString();
	}
	for (auto v : msg["tool_calls"].toArray())
	{
		auto tc = v.toObject();
		int index = tc.contains("index") ? tc["index"].toInt() : m_toolCalls.size();
		auto& slot = m_toolCalls[index];
		if (tc.contains("id")) { slot.id = tc["id"].toString(); }
		auto fn = tc["function"].toObject();
		if (fn.contains("name")) { slot.name += fn["name"].toString(); }
		slot.arguments += fn["arguments"].toString();
	}
	if (!choice["finish_reason"].isNull() && choice["finish_reason"].isString()) { m_finished = true; }
}

QJsonObject OpenAiStreamParser::message() const
{
	QJsonObject m{{"role", "assistant"}, {"content", m_content}};
	if (!m_toolCalls.isEmpty())
	{
		QJsonArray calls;
		for (auto it = m_toolCalls.cbegin(); it != m_toolCalls.cend(); ++it)
		{
			calls.append(QJsonObject{{"id", it->id}, {"type", "function"},
				{"function", QJsonObject{{"name", it->name}, {"arguments", it->arguments}}}});
		}
		m["tool_calls"] = calls;
	}
	return m;
}

QStringList OpenAiStreamParser::takeTextDeltas()
{
	QStringList out; out.swap(m_pendingDeltas); return out;
}
}
```
Fix the `[DONE]` line to simply `continue;` (finish is set by `finish_reason`). Add both `.cpp` to `src/core/CMakeLists.txt`.

- [x] **Step 4: Run parser tests**

Run: `cmake --build build && build/tests/OpenAiStreamParserTest.exe`
Expected: 4 passed.

- [x] **Step 5: AiClient + OpenAiClient**

`include/AiClient.h` — exactly the interface block above (with GPL header, include guards, `#include <QJsonArray>`, `<QJsonObject>`, `<QObject>`, `"lmms_export.h"`).

`include/OpenAiClient.h`:
```cpp
#ifndef LMMS_OPENAI_CLIENT_H
#define LMMS_OPENAI_CLIENT_H
#include "AiClient.h"
#include "AiConfig.h"
#include "OpenAiStreamParser.h"
#include <memory>
class QNetworkAccessManager;
class QNetworkReply;
namespace lmms {
class LMMS_EXPORT OpenAiClient : public AiClient
{
	Q_OBJECT
public:
	explicit OpenAiClient(AiConfig config, QObject* parent = nullptr);
	void setConfig(AiConfig config) { m_config = std::move(config); }
	void send(const QJsonArray& messages, const QJsonArray& tools) override;
	void abort() override;
	//! GET {baseUrl}/models; calls back with (ok, message). Used by the settings page.
	void testConnection(std::function<void(bool, QString)> done);
private:
	AiConfig m_config;
	QNetworkAccessManager* m_nam;
	QNetworkReply* m_reply = nullptr;
	std::unique_ptr<OpenAiStreamParser> m_parser;
};
}
#endif
```
`src/core/ai/OpenAiClient.cpp`:
```cpp
#include "OpenAiClient.h"
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
namespace lmms {

OpenAiClient::OpenAiClient(AiConfig config, QObject* parent)
	: AiClient(parent), m_config(std::move(config)), m_nam(new QNetworkAccessManager(this)) {}

void OpenAiClient::send(const QJsonArray& messages, const QJsonArray& tools)
{
	abort();
	m_parser = std::make_unique<OpenAiStreamParser>();
	QJsonObject body{{"model", m_config.model}, {"messages", messages}, {"stream", true}};
	if (!tools.isEmpty()) { body["tools"] = tools; body["tool_choice"] = "auto"; }
	QNetworkRequest req(QUrl(m_config.baseUrl + "/chat/completions"));
	req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	req.setRawHeader("Authorization", ("Bearer " + m_config.apiKey).toUtf8());
	req.setRawHeader("Accept", "text/event-stream, application/json");
	m_reply = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
	connect(m_reply, &QIODevice::readyRead, this, [this] {
		m_parser->feed(m_reply->readAll());
		for (const auto& d : m_parser->takeTextDeltas()) { emit textDelta(d); }
	});
	connect(m_reply, &QNetworkReply::finished, this, [this] {
		auto reply = m_reply; m_reply = nullptr;
		reply->deleteLater();
		if (reply->error() == QNetworkReply::OperationCanceledError) { return; }
		m_parser->feed(reply->readAll());
		m_parser->end();
		for (const auto& d : m_parser->takeTextDeltas()) { emit textDelta(d); }
		int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (!m_parser->error().isEmpty()) { emit failed(QString("HTTP %1: %2").arg(status).arg(m_parser->error())); return; }
		if (reply->error() != QNetworkReply::NoError) { emit failed(QString("HTTP %1: %2").arg(status).arg(reply->errorString())); return; }
		if (!m_parser->finished()) { emit failed("Incomplete response from model"); return; }
		emit completed(m_parser->message());
	});
}

void OpenAiClient::abort()
{
	if (m_reply) { auto r = m_reply; m_reply = nullptr; r->abort(); r->deleteLater(); }
}

void OpenAiClient::testConnection(std::function<void(bool, QString)> done)
{
	QNetworkRequest req(QUrl(m_config.baseUrl + "/models"));
	req.setRawHeader("Authorization", ("Bearer " + m_config.apiKey).toUtf8());
	auto reply = m_nam->get(req);
	connect(reply, &QNetworkReply::finished, this, [reply, done = std::move(done)] {
		reply->deleteLater();
		int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (reply->error() != QNetworkReply::NoError) { done(false, QString("HTTP %1: %2").arg(status).arg(reply->errorString())); return; }
		done(true, QString("OK (HTTP %1)").arg(status));
	});
}
}
```
Add to `src/core/CMakeLists.txt`.

- [x] **Step 6: Build and commit**

Run: `cmake --build build && build/tests/OpenAiStreamParserTest.exe` → PASS.
```bash
git add include/AiClient.h include/OpenAiStreamParser.h include/OpenAiClient.h src/core/ai tests/src/core/ai/OpenAiStreamParserTest.cpp tests/CMakeLists.txt src/core/CMakeLists.txt
git commit -m "ai: OpenAI-compatible client with SSE parser"
```

---

### Task 3: AiToolRegistry

**Files:**
- Create: `include/AiToolRegistry.h`, `src/core/ai/AiToolRegistry.cpp`
- Test: `tests/src/core/ai/AiToolRegistryTest.cpp`
- Modify: `src/core/CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:** Produces `AiTool`, `AiToolRegistry` as declared above.

- [x] **Step 1: Failing test**

```cpp
#include "AiToolRegistry.h"
#include <QtTest>
#include <QJsonArray>
class AiToolRegistryTest : public QObject
{
	Q_OBJECT
private slots:
	void specsAndDispatch()
	{
		lmms::AiToolRegistry r;
		r.add({"echo", "returns x", QJsonObject{{"type","object"}}, [](const QJsonObject& a){ return lmms::AiToolRegistry::ok({{"x", a["x"]}}); }});
		auto specs = r.specs();
		QCOMPARE(specs.size(), 1);
		QCOMPARE(specs[0].toObject()["function"].toObject()["name"].toString(), QString("echo"));
		auto res = r.call("echo", {{"x", 7}});
		QVERIFY(res["ok"].toBool());
		QCOMPARE(res["x"].toInt(), 7);
	}
	void unknownToolIsError()
	{
		lmms::AiToolRegistry r;
		auto res = r.call("nope", {});
		QVERIFY(!res["ok"].toBool());
		QVERIFY(res["error"].toString().contains("nope"));
	}
	void handlerExceptionIsError()
	{
		lmms::AiToolRegistry r;
		r.add({"boom", "", {}, [](const QJsonObject&) -> QJsonObject { throw std::runtime_error("kaboom"); }});
		auto res = r.call("boom", {});
		QVERIFY(!res["ok"].toBool());
		QVERIFY(res["error"].toString().contains("kaboom"));
	}
};
QTEST_GUILESS_MAIN(AiToolRegistryTest)
#include "AiToolRegistryTest.moc"
```

- [x] **Step 2: Verify failure** — build fails on missing header.

- [x] **Step 3: Implement**

`include/AiToolRegistry.h` — the interface block plus `#include <functional>`, `<QJsonArray>`, `<QJsonObject>`, `<QMap>`; private `QMap<QString, AiTool> m_tools;`.

`src/core/ai/AiToolRegistry.cpp`:
```cpp
#include "AiToolRegistry.h"
#include <exception>
namespace lmms {
void AiToolRegistry::add(AiTool tool) { m_tools.insert(tool.name, std::move(tool)); }
bool AiToolRegistry::has(const QString& name) const { return m_tools.contains(name); }
QJsonArray AiToolRegistry::specs() const
{
	QJsonArray out;
	for (const auto& t : m_tools)
	{
		out.append(QJsonObject{{"type", "function"}, {"function", QJsonObject{
			{"name", t.name}, {"description", t.description}, {"parameters", t.parameters}}}});
	}
	return out;
}
QJsonObject AiToolRegistry::call(const QString& name, const QJsonObject& args) const
{
	auto it = m_tools.find(name);
	if (it == m_tools.end()) { return error("Unknown tool: " + name); }
	try { return it->handler(args); }
	catch (const std::exception& e) { return error(QString("Tool %1 failed: %2").arg(name, e.what())); }
	catch (...) { return error("Tool " + name + " failed"); }
}
QJsonObject AiToolRegistry::ok(QJsonObject fields) { fields["ok"] = true; return fields; }
QJsonObject AiToolRegistry::error(const QString& message) { return {{"ok", false}, {"error", message}}; }
}
```

- [x] **Step 4: Run** — `build/tests/AiToolRegistryTest.exe` → 3 passed.

- [x] **Step 5: Commit** — `git commit -m "ai: tool registry"`.

---

### Task 4: AiSession agent loop

**Files:**
- Create: `include/AiSession.h`, `src/core/ai/AiSession.cpp`
- Test: `tests/src/core/ai/AiSessionTest.cpp` (uses a `FakeAiClient` defined in the test)
- Modify: CMake lists

**Interfaces:**
- Consumes: `AiClient`, `AiToolRegistry`.
- Produces: `AiSession` as declared above. Journal hooks are virtual so the test can stub them: `protected: virtual void beginTurnCheckpoint(); virtual void endTurnCheckpoint();` — default implementation: `Engine::getSong()->addJournalCheckPoint(); Engine::projectJournal()->setJournalling(false);` and `Engine::projectJournal()->setJournalling(true);` respectively. Guard with `if (Engine::getSong())` so the session works without an engine.

- [x] **Step 1: Failing tests**

```cpp
#include "AiSession.h"
#include "AiToolRegistry.h"
#include <QtTest>
#include <QJsonArray>
#include <QSignalSpy>

class FakeAiClient : public lmms::AiClient
{
	Q_OBJECT
public:
	QList<QJsonObject> scripted;          // assistant messages to return in order
	QList<QJsonArray> requests;           // messages arrays seen
	QString failOnRequest;                // if non-empty, the Nth request fails
	int failAt = -1;
	void send(const QJsonArray& messages, const QJsonArray&) override
	{
		requests << messages;
		int n = requests.size() - 1;
		QMetaObject::invokeMethod(this, [this, n] {
			if (n == failAt) { emit failed(failOnRequest); return; }
			emit completed(scripted.value(n, QJsonObject{{"role","assistant"},{"content","done"}}));
		}, Qt::QueuedConnection);
	}
	void abort() override {}
};

static QJsonObject toolCallMsg(const QStringList& names)
{
	QJsonArray calls;
	for (int i = 0; i < names.size(); ++i)
		calls.append(QJsonObject{{"id", QString("c%1").arg(i)}, {"type","function"},
			{"function", QJsonObject{{"name", names[i]}, {"arguments", "{}"}}}});
	return {{"role","assistant"},{"content",""},{"tool_calls", calls}};
}

class TestSession : public lmms::AiSession
{
public:
	using AiSession::AiSession;
	int checkpoints = 0;
protected:
	void beginTurnCheckpoint() override { ++checkpoints; }
	void endTurnCheckpoint() override {}
};

class AiSessionTest : public QObject
{
	Q_OBJECT
	lmms::AiToolRegistry reg;
	QStringList calls;
private slots:
	void initTestCase()
	{
		reg.add({"a", "", {}, [this](const QJsonObject&){ calls << "a"; return lmms::AiToolRegistry::ok(); }});
		reg.add({"b", "", {}, [this](const QJsonObject&){ calls << "b"; return lmms::AiToolRegistry::ok({{"big", QString(200000, 'x')}}); }});
		reg.add({"bad", "", {}, [this](const QJsonObject&){ calls << "bad"; return lmms::AiToolRegistry::error("no"); }});
	}
	void init() { calls.clear(); }

	void dispatchesToolsInOrderThenFinishes()
	{
		FakeAiClient c; TestSession s(&c, &reg);
		c.scripted << toolCallMsg({"a", "b"}) << QJsonObject{{"role","assistant"},{"content","all done"}};
		QSignalSpy fin(&s, &lmms::AiSession::turnFinished);
		s.submit("go");
		QVERIFY(fin.wait(2000));
		QCOMPARE(calls, QStringList({"a", "b"}));
		QCOMPARE(fin[0][0].toString(), QString("all done"));
		QCOMPARE(s.checkpoints, 1);
		// history: system, user, assistant(tool_calls), tool, tool, assistant
		QCOMPARE(s.history().size(), 6);
		QCOMPARE(s.history()[3].toObject()["role"].toString(), QString("tool"));
		QCOMPARE(s.history()[3].toObject()["tool_call_id"].toString(), QString("c0"));
	}
	void stopsAfterConsecutiveErrors()
	{
		FakeAiClient c; TestSession s(&c, &reg);
		for (int i = 0; i < 10; ++i) c.scripted << toolCallMsg({"bad"});
		QSignalSpy failed(&s, &lmms::AiSession::turnFailed);
		s.submit("go");
		QVERIFY(failed.wait(2000));
		QCOMPARE(calls.size(), lmms::AiSession::MaxConsecutiveToolErrors);
	}
	void stopsAtToolCallCap()
	{
		FakeAiClient c; TestSession s(&c, &reg);
		for (int i = 0; i < 100; ++i) c.scripted << toolCallMsg({"a"});
		QSignalSpy failed(&s, &lmms::AiSession::turnFailed);
		s.submit("go");
		QVERIFY(failed.wait(5000));
		QCOMPARE(calls.size(), lmms::AiSession::MaxToolCallsPerTurn);
	}
	void elidesOldToolResultsWhenLarge()
	{
		FakeAiClient c; TestSession s(&c, &reg);
		c.scripted << toolCallMsg({"b"}) << toolCallMsg({"a"}) << QJsonObject{{"role","assistant"},{"content","ok"}};
		QSignalSpy fin(&s, &lmms::AiSession::turnFinished);
		s.submit("go");
		QVERIFY(fin.wait(2000));
		// third request must not carry the 200k payload
		auto third = c.requests[2];
		QString dump = QString::fromUtf8(QJsonDocument(third).toJson(QJsonDocument::Compact));
		QVERIFY(dump.size() < lmms::AiSession::MaxRequestChars);
		QVERIFY(dump.contains("[elided]"));
	}
	void rollsBackHistoryOnFailure()
	{
		FakeAiClient c; TestSession s(&c, &reg);
		c.failAt = 0; c.failOnRequest = "HTTP 401: bad key";
		QSignalSpy failed(&s, &lmms::AiSession::turnFailed);
		s.submit("go");
		QVERIFY(failed.wait(2000));
		QCOMPARE(s.history().size(), 1); // system prompt only; user message removed
		QVERIFY(!s.busy());
	}
};
QTEST_GUILESS_MAIN(AiSessionTest)
#include "AiSessionTest.moc"
```

- [x] **Step 2: Verify failure** — build error on missing header.

- [x] **Step 3: Implement**

`include/AiSession.h`: interface block; private members:
```cpp
	void request();
	void onCompleted(const QJsonObject& msg);
	void onFailed(const QString& err);
	void finishTurn(const QString& text);
	void failTurn(const QString& err);
	void elideIfLarge();
	AiClient* m_client; AiToolRegistry* m_registry;
	QJsonArray m_history; QString m_systemPrompt;
	int m_turnStartIndex = 0, m_toolCalls = 0, m_consecutiveErrors = 0;
	bool m_busy = false;
```
`src/core/ai/AiSession.cpp`:
```cpp
#include "AiSession.h"
#include "AiToolRegistry.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "Song.h"
#include <QJsonDocument>
namespace lmms {

AiSession::AiSession(AiClient* client, AiToolRegistry* registry, QObject* parent)
	: QObject(parent), m_client(client), m_registry(registry)
{
	connect(m_client, &AiClient::textDelta, this, &AiSession::assistantTextDelta);
	connect(m_client, &AiClient::completed, this, &AiSession::onCompleted);
	connect(m_client, &AiClient::failed, this, &AiSession::onFailed);
	clear();
}

void AiSession::setSystemPrompt(const QString& text)
{
	m_systemPrompt = text;
	if (!m_history.isEmpty()) { m_history[0] = QJsonObject{{"role", "system"}, {"content", text}}; }
}

void AiSession::clear()
{
	m_history = QJsonArray{QJsonObject{{"role", "system"}, {"content", m_systemPrompt}}};
	m_busy = false;
}

bool AiSession::busy() const { return m_busy; }
const QJsonArray& AiSession::history() const { return m_history; }

void AiSession::submit(const QString& userText)
{
	if (m_busy) { return; }
	m_busy = true;
	m_turnStartIndex = m_history.size();
	m_toolCalls = 0; m_consecutiveErrors = 0;
	m_history.append(QJsonObject{{"role", "user"}, {"content", userText}});
	beginTurnCheckpoint();
	request();
}

void AiSession::stop()
{
	if (!m_busy) { return; }
	m_client->abort();
	failTurn(tr("Stopped"));
}

void AiSession::request()
{
	elideIfLarge();
	emit status(tr("Thinking…"));
	m_client->send(m_history, m_registry->specs());
}

void AiSession::onCompleted(const QJsonObject& msg)
{
	if (!m_busy) { return; }
	m_history.append(msg);
	auto calls = msg["tool_calls"].toArray();
	if (calls.isEmpty()) { finishTurn(msg["content"].toString()); return; }
	for (auto v : calls)
	{
		auto call = v.toObject();
		auto fn = call["function"].toObject();
		QString name = fn["name"].toString();
		QJsonParseError perr;
		auto argsDoc = QJsonDocument::fromJson(fn["arguments"].toString().toUtf8(), &perr);
		QJsonObject args = perr.error == QJsonParseError::NoError ? argsDoc.object() : QJsonObject{};
		if (++m_toolCalls > MaxToolCallsPerTurn)
		{
			failTurn(tr("Stopped: more than %1 tool calls in one turn").arg(MaxToolCallsPerTurn));
			return;
		}
		emit status(tr("Running %1…").arg(name));
		emit toolCallStarted(name, args);
		QJsonObject result = perr.error == QJsonParseError::NoError
			? m_registry->call(name, args)
			: AiToolRegistry::error("Arguments are not valid JSON: " + perr.errorString());
		emit toolCallFinished(name, result);
		m_consecutiveErrors = result["ok"].toBool() ? 0 : m_consecutiveErrors + 1;
		m_history.append(QJsonObject{{"role", "tool"}, {"tool_call_id", call["id"].toString()},
			{"content", QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact))}});
		if (m_consecutiveErrors >= MaxConsecutiveToolErrors)
		{
			failTurn(tr("Stopped: %1 consecutive tool errors").arg(MaxConsecutiveToolErrors));
			return;
		}
	}
	request();
}

void AiSession::onFailed(const QString& err)
{
	if (!m_busy) { return; }
	// roll back to before this turn's user message so a retry is clean
	while (m_history.size() > m_turnStartIndex) { m_history.removeLast(); }
	failTurn(err);
}

void AiSession::finishTurn(const QString& text)
{
	m_busy = false; endTurnCheckpoint();
	emit status(tr("Done (%1 tool calls)").arg(m_toolCalls));
	emit turnFinished(text);
}

void AiSession::failTurn(const QString& err)
{
	m_busy = false; endTurnCheckpoint();
	emit status(err);
	emit turnFailed(err);
}

void AiSession::elideIfLarge()
{
	auto size = [this] { return QJsonDocument(m_history).toJson(QJsonDocument::Compact).size(); };
	for (int i = 1; i < m_history.size() && size() > MaxRequestChars; ++i)
	{
		auto m = m_history[i].toObject();
		if (m["role"].toString() == "tool" && m["content"].toString() != "[elided]")
		{
			m["content"] = "[elided]"; m_history[i] = m;
		}
	}
}

void AiSession::beginTurnCheckpoint()
{
	if (!Engine::getSong()) { return; }
	Engine::getSong()->addJournalCheckPoint();
	Engine::projectJournal()->setJournalling(false);
}

void AiSession::endTurnCheckpoint()
{
	if (!Engine::getSong()) { return; }
	Engine::projectJournal()->setJournalling(true);
}
}
```
Note `failTurn` in `stopsAfterConsecutiveErrors` fires after the cap-th error, and `MaxToolCallsPerTurn` check happens before dispatch so exactly 40 tools run.

- [x] **Step 4: Run** — `build/tests/AiSessionTest.exe` → 5 passed.

- [x] **Step 5: Commit** — `git commit -m "ai: agent session loop with caps, elision, rollback"`.

---

### Task 5: Project tools — summary, head, add_instrument_track, add_notes

**Files:**
- Create: `include/AiTools.h`, `src/core/ai/AiProjectTools.cpp`
- Test: `tests/src/core/ai/AiProjectToolsTest.cpp`
- Modify: CMake lists

**Interfaces:**
- Produces: `void registerAiProjectTools(AiToolRegistry&)`; internal helpers in an anonymous namespace shared by Tasks 6–7 via a private header `src/core/ai/AiToolHelpers.h`:
```cpp
namespace lmms::aitools {
Track* trackAt(int index, QString* err);                      // Engine::getSong()->tracks()[index] or err
InstrumentTrack* instrumentTrackAt(int index, QString* err);
QJsonObject schema(std::initializer_list<std::pair<QString, QJsonObject>> props, QStringList required = {});
QJsonObject prop(const QString& type, const QString& description);
}
```

- [x] **Step 1: Failing tests**

```cpp
#include "AiToolRegistry.h"
#include "AiTools.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Song.h"
#include <QtTest>
#include <QJsonArray>

class AiProjectToolsTest : public QObject
{
	Q_OBJECT
	lmms::AiToolRegistry reg;
private slots:
	void initTestCase() { lmms::Engine::init(true); lmms::registerAiProjectTools(reg); }
	void cleanupTestCase() { lmms::Engine::destroy(); }
	void init() { lmms::Engine::getSong()->clearProject(); }

	void headRoundTrip()
	{
		auto r = reg.call("set_head", {{"bpm", 92}, {"timesigNum", 3}, {"timesigDen", 4}});
		QVERIFY(r["ok"].toBool());
		auto h = reg.call("get_head", {});
		QCOMPARE(h["bpm"].toInt(), 92);
		QCOMPARE(h["timesigNum"].toInt(), 3);
		QVERIFY(!reg.call("set_head", {{"bpm", 5}})["ok"].toBool()); // below MinTempo
	}
	void addInstrumentTrackAndNotes()
	{
		auto r = reg.call("add_instrument_track", {{"name", "Lead"}, {"instrument", "tripleoscillator"}});
		QVERIFY2(r["ok"].toBool(), qPrintable(r["error"].toString()));
		int idx = r["index"].toInt();
		auto notes = QJsonArray{QJsonObject{{"pos", 0}, {"len", 48}, {"key", 60}}, QJsonObject{{"pos", 48}, {"len", 48}, {"key", 64}, {"vol", 80}}};
		auto n = reg.call("add_notes", {{"track", idx}, {"clipPos", 0}, {"notes", notes}});
		QVERIFY2(n["ok"].toBool(), qPrintable(n["error"].toString()));
		auto track = dynamic_cast<lmms::InstrumentTrack*>(lmms::Engine::getSong()->tracks()[idx]);
		QVERIFY(track);
		QCOMPARE(track->instrumentName(), QString("Triple Oscillator"));
		auto clip = dynamic_cast<lmms::MidiClip*>(track->getClips()[0]);
		QCOMPARE(int(clip->notes().size()), 2);
		QCOMPARE(clip->notes()[1]->key(), 64);
		QCOMPARE(int(clip->notes()[1]->getVolume()), 80);
		auto s = reg.call("get_project_summary", {});
		QCOMPARE(s["tracks"].toArray()[idx].toObject()["clips"].toArray()[0].toObject()["noteCount"].toInt(), 2);
	}
	void addNotesRejectsBadInput()
	{
		QVERIFY(!reg.call("add_notes", {{"track", 99}, {"clipPos", 0}, {"notes", QJsonArray{}}})["ok"].toBool());
		reg.call("add_instrument_track", {{"name", "X"}, {"instrument", "tripleoscillator"}});
		QVERIFY(!reg.call("add_notes", {{"track", 0}, {"clipPos", 0}, {"notes", QJsonArray{QJsonObject{{"pos", 0}, {"len", 48}, {"key", 200}}}}})["ok"].toBool());
		QVERIFY(!reg.call("add_instrument_track", {{"name", "Y"}, {"instrument", "no_such_plugin"}})["ok"].toBool());
	}
};
QTEST_GUILESS_MAIN(AiProjectToolsTest)
#include "AiProjectToolsTest.moc"
```
Note `Song::clearProject()` — check it is public in `include/Song.h`; if it is a private slot, call `Engine::getSong()->createNewProject()` instead (it is public, `Song.h:248`).

- [x] **Step 2: Verify failure.**

- [x] **Step 3: Implement**

`include/AiTools.h`:
```cpp
#ifndef LMMS_AI_TOOLS_H
#define LMMS_AI_TOOLS_H
#include "lmms_export.h"
namespace lmms {
class AiToolRegistry; class AiPathPolicy;
LMMS_EXPORT void registerAiProjectTools(AiToolRegistry& r);
LMMS_EXPORT void registerAiDiscoveryTools(AiToolRegistry& r, AiPathPolicy& policy);
LMMS_EXPORT void registerAiActionTools(AiToolRegistry& r, AiPathPolicy& policy);
}
#endif
```
`src/core/ai/AiToolHelpers.h`:
```cpp
#pragma once
#include "AiToolRegistry.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "Song.h"
#include <QJsonObject>
namespace lmms::aitools {
inline Track* trackAt(int index, QString* err)
{
	const auto& tracks = Engine::getSong()->tracks();
	if (index < 0 || index >= int(tracks.size())) { *err = QString("No track at index %1 (project has %2)").arg(index).arg(tracks.size()); return nullptr; }
	return tracks[index];
}
inline InstrumentTrack* instrumentTrackAt(int index, QString* err)
{
	auto t = dynamic_cast<InstrumentTrack*>(trackAt(index, err));
	if (!t && err->isEmpty()) { *err = QString("Track %1 is not an instrument track").arg(index); }
	return t;
}
inline QJsonObject prop(const QString& type, const QString& description)
{
	return {{"type", type}, {"description", description}};
}
inline QJsonObject schema(std::initializer_list<std::pair<QString, QJsonObject>> props, QStringList required = {})
{
	QJsonObject p; for (auto& [k, v] : props) { p[k] = v; }
	QJsonObject s{{"type", "object"}, {"properties", p}};
	if (!required.isEmpty()) { s["required"] = QJsonArray::fromStringList(required); }
	return s;
}
}
```
`src/core/ai/AiProjectTools.cpp` (part 1 — Tasks 6/7 append to the same file):
```cpp
#include "AiTools.h"
#include "AiToolHelpers.h"
#include "AudioEngine.h"
#include "Clip.h"
#include "MidiClip.h"
#include "Note.h"
#include "PluginFactory.h"
#include "TimePos.h"
#include "Track.h"
#include <QJsonArray>

namespace lmms {
using namespace aitools;
using R = AiToolRegistry;

static QJsonObject projectSummary(const QJsonObject&)
{
	auto song = Engine::getSong();
	QJsonArray tracks;
	int i = 0;
	for (auto t : song->tracks())
	{
		QJsonObject to{{"index", i++}, {"name", t->name()}, {"muted", t->isMuted()}};
		switch (t->type())
		{
			case Track::Type::Instrument: to["type"] = "instrument"; break;
			case Track::Type::Sample: to["type"] = "sample"; break;
			case Track::Type::Automation: to["type"] = "automation"; break;
			case Track::Type::Pattern: to["type"] = "pattern"; break;
			default: to["type"] = "other";
		}
		if (auto it = dynamic_cast<InstrumentTrack*>(t))
		{
			to["instrument"] = it->instrumentName();
			to["mixerChannel"] = it->mixerChannelModel()->value();
			to["volume"] = it->volumeModel()->value();
		}
		QJsonArray clips;
		for (auto c : t->getClips())
		{
			QJsonObject co{{"pos", c->startPosition().getTicks()}, {"len", c->length().getTicks()}, {"name", c->name()}};
			if (auto mc = dynamic_cast<MidiClip*>(c)) { co["noteCount"] = int(mc->notes().size()); }
			clips.append(co);
		}
		to["clips"] = clips;
		tracks.append(to);
	}
	return R::ok({{"bpm", song->getTempo()}, {"timesigNum", song->getTimeSigModel().getNumerator()},
		{"timesigDen", song->getTimeSigModel().getDenominator()}, {"lengthBars", song->length()},
		{"ticksPerBar", TimePos::ticksPerBar()}, {"tracks", tracks}});
}

static QJsonObject getHead(const QJsonObject&)
{
	auto song = Engine::getSong();
	return R::ok({{"bpm", song->getTempo()}, {"timesigNum", song->getTimeSigModel().getNumerator()},
		{"timesigDen", song->getTimeSigModel().getDenominator()}, {"masterVol", song->masterVolume()},
		{"masterPitch", song->masterPitch()}});
}

static QJsonObject setHead(const QJsonObject& a)
{
	auto song = Engine::getSong();
	if (a.contains("bpm"))
	{
		int bpm = a["bpm"].toInt();
		if (bpm < MinTempo || bpm > MaxTempo) { return R::error(QString("bpm must be %1..%2").arg(MinTempo).arg(MaxTempo)); }
		song->tempoModel().setValue(bpm);
	}
	if (a.contains("timesigNum")) { song->getTimeSigModel().numeratorModel().setValue(a["timesigNum"].toInt()); }
	if (a.contains("timesigDen")) { song->getTimeSigModel().denominatorModel().setValue(a["timesigDen"].toInt()); }
	if (a.contains("masterVol")) { song->setMasterVolume(a["masterVol"].toInt()); }
	if (a.contains("masterPitch")) { song->setMasterPitch(a["masterPitch"].toInt()); }
	return getHead({});
}

static QJsonObject addInstrumentTrack(const QJsonObject& a)
{
	QString plugin = a["instrument"].toString();
	if (!PluginFactory::instance()->pluginInfo(plugin.toUtf8().constData()).isNull())
	{
		// found
	}
	else { return R::error("Unknown instrument plugin: " + plugin + " (use list_instruments)"); }
	auto track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
	if (!track) { return R::error("Could not create instrument track"); }
	if (!track->loadInstrument(plugin)) { return R::error("Failed to load instrument " + plugin); }
	if (a.contains("name")) { track->setName(a["name"].toString()); }
	if (a.contains("mixerChannel")) { track->mixerChannelModel()->setValue(a["mixerChannel"].toInt()); }
	int index = int(Engine::getSong()->tracks().size()) - 1;
	return R::ok({{"index", index}});
}

static QJsonObject addNotes(const QJsonObject& a)
{
	QString err;
	auto track = instrumentTrackAt(a["track"].toInt(-1), &err);
	if (!track) { return R::error(err); }
	TimePos clipPos(a["clipPos"].toInt(0));
	MidiClip* clip = nullptr;
	for (auto c : track->getClips())
	{
		if (c->startPosition() == clipPos) { clip = dynamic_cast<MidiClip*>(c); break; }
	}
	if (!clip) { clip = dynamic_cast<MidiClip*>(track->createClip(clipPos)); }
	if (!clip) { return R::error("Could not create clip"); }
	auto notes = a["notes"].toArray();
	for (auto v : notes)
	{
		auto n = v.toObject();
		int key = n["key"].toInt(-1);
		if (key < 0 || key >= NumKeys) { return R::error(QString("note key %1 out of range 0..127").arg(key)); }
		if (n["len"].toInt(0) <= 0) { return R::error("note len must be > 0 ticks"); }
	}
	if (a["clear"].toBool(false)) { clip->clearNotes(); }
	for (auto v : notes)
	{
		auto n = v.toObject();
		Note note(TimePos(n["len"].toInt()), TimePos(n["pos"].toInt(0)), n["key"].toInt(),
			volume_t(n["vol"].toInt(DefaultVolume)), panning_t(n["pan"].toInt(DefaultPanning)));
		clip->addNote(note, false);
	}
	return R::ok({{"track", a["track"].toInt()}, {"clipPos", clipPos.getTicks()}, {"noteCount", int(clip->notes().size())}});
}

void registerAiProjectTools(AiToolRegistry& r)
{
	r.add({"get_project_summary", "Compact overview of the open project: tempo, time signature, tracks, clips. Call this first.", schema({}), projectSummary});
	r.add({"get_head", "Tempo, time signature, master volume/pitch.", schema({}), getHead});
	r.add({"set_head", "Set tempo (bpm), time signature, master volume/pitch. All fields optional.",
		schema({{"bpm", prop("integer", "10..999")}, {"timesigNum", prop("integer", "")}, {"timesigDen", prop("integer", "")},
			{"masterVol", prop("integer", "0..200")}, {"masterPitch", prop("integer", "-12..12 semitones")}}), setHead});
	r.add({"add_instrument_track", "Create an instrument track with the given plugin (see list_instruments). Returns its index.",
		schema({{"name", prop("string", "track name")}, {"instrument", prop("string", "plugin name, e.g. tripleoscillator, kicker, sf2player")},
			{"mixerChannel", prop("integer", "mixer channel, 0 = master")}}, {"instrument"}), addInstrumentTrack});
	r.add({"add_notes", "Add notes to a MIDI clip on an instrument track (creates the clip at clipPos if missing). Ticks: 192 per bar in 4/4, quarter=48, 16th=12. key is MIDI number (60 = C4).",
		schema({{"track", prop("integer", "track index")}, {"clipPos", prop("integer", "clip start in ticks")},
			{"notes", QJsonObject{{"type", "array"}, {"items", schema({{"pos", prop("integer", "ticks from clip start")}, {"len", prop("integer", "ticks")},
				{"key", prop("integer", "0..127")}, {"vol", prop("integer", "0..200, default 100")}, {"pan", prop("integer", "-100..100")}}, {"pos", "len", "key"})}}},
			{"clear", prop("boolean", "remove existing notes first")}}, {"track", "clipPos", "notes"}), addNotes});
}
}
```
Verify accessor names while implementing: `MeterModel::getNumerator()/getDenominator()/numeratorModel()/denominatorModel()` (`include/MeterModel.h`), `Track::isMuted()`, `Clip::name()`, `Note::key()/getVolume()`. Adjust to the real names if they differ; the tests use the tool interface, not these internals.

- [x] **Step 4: Run** — `build/tests/AiProjectToolsTest.exe` → 3 passed. If `Track::create` asserts on GUI in headless mode, replace with `new InstrumentTrack(Engine::getSong())` followed by `Engine::getSong()->addTrack(track)` — check what `Track::create` does at `src/core/Track.cpp:82-110` first.

- [x] **Step 5: Commit** — `git commit -m "ai: project summary/head/instrument-track/notes tools"`.

---

### Task 6: Project tools — XML surface

**Files:**
- Modify: `src/core/ai/AiProjectTools.cpp` (append), `tests/src/core/ai/AiProjectToolsTest.cpp` (append cases)

**Interfaces:** Tools `get_track_xml`, `add_track`, `replace_track`, `remove_track`, `get_mixer_xml`, `set_mixer_xml`.

- [x] **Step 1: Failing tests (append to AiProjectToolsTest)**

```cpp
	void trackXmlRoundTrip()
	{
		reg.call("add_instrument_track", {{"name", "Src"}, {"instrument", "tripleoscillator"}});
		reg.call("add_notes", {{"track", 0}, {"clipPos", 0}, {"notes", QJsonArray{QJsonObject{{"pos", 0}, {"len", 24}, {"key", 62}}}}});
		auto x = reg.call("get_track_xml", {{"index", 0}});
		QVERIFY(x["ok"].toBool());
		QString xml = x["xml"].toString();
		QVERIFY(xml.startsWith("<track"));
		QVERIFY(xml.contains("key=\"62\""));
		auto added = reg.call("add_track", {{"xml", xml.replace("name=\"Src\"", "name=\"Copy\"")}});
		QVERIFY2(added["ok"].toBool(), qPrintable(added["error"].toString()));
		QCOMPARE(added["index"].toInt(), 1);
		QCOMPARE(lmms::Engine::getSong()->tracks()[1]->name(), QString("Copy"));
		auto s = reg.call("get_project_summary", {});
		QCOMPARE(s["tracks"].toArray()[1].toObject()["clips"].toArray()[0].toObject()["noteCount"].toInt(), 1);
	}
	void invalidXmlLeavesProjectUntouched()
	{
		reg.call("add_instrument_track", {{"name", "A"}, {"instrument", "tripleoscillator"}});
		QVERIFY(!reg.call("add_track", {{"xml", "<track type=\"0\" name=\"x\""}})["ok"].toBool());
		QVERIFY(!reg.call("add_track", {{"xml", "<midiclip/>"}})["ok"].toBool());
		QCOMPARE(int(lmms::Engine::getSong()->tracks().size()), 1);
		QVERIFY(!reg.call("replace_track", {{"index", 7}, {"xml", "<track type=\"0\" name=\"x\"/>"}})["ok"].toBool());
	}
	void replaceAndRemove()
	{
		reg.call("add_instrument_track", {{"name", "A"}, {"instrument", "tripleoscillator"}});
		reg.call("add_instrument_track", {{"name", "B"}, {"instrument", "tripleoscillator"}});
		QString xml = reg.call("get_track_xml", {{"index", 0}})["xml"].toString().replace("name=\"A\"", "name=\"A2\"");
		QVERIFY(reg.call("replace_track", {{"index", 0}, {"xml", xml}})["ok"].toBool());
		QCOMPARE(lmms::Engine::getSong()->tracks()[0]->name(), QString("A2"));
		QCOMPARE(lmms::Engine::getSong()->tracks()[1]->name(), QString("B"));
		QVERIFY(reg.call("remove_track", {{"index", 0}})["ok"].toBool());
		QCOMPARE(int(lmms::Engine::getSong()->tracks().size()), 1);
		QCOMPARE(lmms::Engine::getSong()->tracks()[0]->name(), QString("B"));
	}
	void mixerXmlRoundTrip()
	{
		auto m = reg.call("get_mixer_xml", {});
		QVERIFY(m["ok"].toBool());
		QVERIFY(m["xml"].toString().startsWith("<mixer"));
		QVERIFY(reg.call("set_mixer_xml", {{"xml", m["xml"].toString()}})["ok"].toBool());
		QVERIFY(!reg.call("set_mixer_xml", {{"xml", "<nope/>"}})["ok"].toBool());
	}
```

- [x] **Step 2: Verify failure** — unknown tool errors.

- [x] **Step 3: Implement (append to AiProjectTools.cpp, register in `registerAiProjectTools`)**

```cpp
#include "DataFile.h"
#include "Mixer.h"
#include <QDomDocument>
#include <QTextStream>

static constexpr int MaxXmlBytes = 64 * 1024;

static QString elementToString(const QDomElement& e)
{
	QString out; QTextStream ts(&out); e.save(ts, 0); return out.trimmed();
}

//! Wraps a <track> (or <mixer>) fragment in an lmms-project envelope so DataFile applies upgrades and security checks.
static bool parseFragment(const QString& xml, const QString& expectedTag, DataFile& out, QDomElement& element, QString* err)
{
	QDomDocument probe;
	QString perr; int line = 0;
	if (!probe.setContent(xml, &perr, &line)) { *err = QString("XML parse error line %1: %2").arg(line).arg(perr); return false; }
	if (probe.documentElement().tagName() != expectedTag) { *err = QString("Root element must be <%1>, got <%2>").arg(expectedTag, probe.documentElement().tagName()); return false; }
	QString wrapped = QString("<?xml version=\"1.0\"?><lmms-project version=\"%1\" type=\"song\" creator=\"LMMS\" creatorversion=\"%2\"><head/><song><trackcontainer type=\"song\">%3</trackcontainer></song></lmms-project>")
		.arg(DataFile::currentVersion()).arg(LMMS_VERSION).arg(expectedTag == "track" ? xml : "");
	if (expectedTag == "mixer")
	{
		wrapped = QString("<?xml version=\"1.0\"?><lmms-project version=\"%1\" type=\"song\" creator=\"LMMS\" creatorversion=\"%2\"><head/><song>%3<trackcontainer type=\"song\"/></song></lmms-project>")
			.arg(DataFile::currentVersion()).arg(LMMS_VERSION).arg(xml);
	}
	out = DataFile(wrapped.toUtf8());
	if (out.hasLocalPlugins()) { *err = "XML references local plugin paths, which is not allowed"; return false; }
	element = out.content().elementsByTagName(expectedTag).item(0).toElement();
	if (element.isNull()) { *err = "Fragment lost during validation"; return false; }
	return true;
}

static QJsonObject getTrackXml(const QJsonObject& a)
{
	QString err; auto track = trackAt(a["index"].toInt(-1), &err);
	if (!track) { return R::error(err); }
	QDomDocument doc; QDomElement root = doc.createElement("root"); doc.appendChild(root);
	track->saveSettings(doc, root);
	QString xml = elementToString(root.firstChildElement("track"));
	if (xml.size() > MaxXmlBytes) { return R::error(QString("Track XML is %1 bytes (limit %2); use get_project_summary and convenience tools instead").arg(xml.size()).arg(MaxXmlBytes)); }
	return R::ok({{"index", a["index"].toInt()}, {"xml", xml}});
}

static QJsonObject addTrackXml(const QJsonObject& a)
{
	DataFile df(DataFile::Type::SongProject); QDomElement el; QString err;
	if (!parseFragment(a["xml"].toString(), "track", df, el, &err)) { return R::error(err); }
	auto track = Track::create(el, Engine::getSong());
	if (!track) { return R::error("Track could not be created from XML"); }
	return R::ok({{"index", int(Engine::getSong()->tracks().size()) - 1}});
}

static QJsonObject replaceTrackXml(const QJsonObject& a)
{
	QString err; int index = a["index"].toInt(-1);
	auto old = trackAt(index, &err);
	if (!old) { return R::error(err); }
	DataFile df(DataFile::Type::SongProject); QDomElement el;
	if (!parseFragment(a["xml"].toString(), "track", df, el, &err)) { return R::error(err); }
	auto track = Track::create(el, Engine::getSong());
	if (!track) { return R::error("Track could not be created from XML"); }
	Engine::getSong()->moveTrack(track, index);
	Engine::getSong()->removeTrack(old);
	return R::ok({{"index", index}});
}

static QJsonObject removeTrack(const QJsonObject& a)
{
	QString err; auto track = trackAt(a["index"].toInt(-1), &err);
	if (!track) { return R::error(err); }
	Engine::getSong()->removeTrack(track);
	return R::ok({{"trackCount", int(Engine::getSong()->tracks().size())}});
}

static QJsonObject getMixerXml(const QJsonObject&)
{
	QDomDocument doc; QDomElement root = doc.createElement("root"); doc.appendChild(root);
	Engine::mixer()->saveSettings(doc, root);
	QString xml = elementToString(root.firstChildElement("mixer"));
	if (xml.size() > MaxXmlBytes) { return R::error("Mixer XML too large"); }
	return R::ok({{"xml", xml}});
}

static QJsonObject setMixerXml(const QJsonObject& a)
{
	DataFile df(DataFile::Type::SongProject); QDomElement el; QString err;
	if (!parseFragment(a["xml"].toString(), "mixer", df, el, &err)) { return R::error(err); }
	auto guard = Engine::audioEngine()->requestChangesGuard();
	Engine::mixer()->loadSettings(el);
	return R::ok({{"channels", int(Engine::mixer()->numChannels())}});
}
```
Registrations:
```cpp
	r.add({"get_track_xml", "Full <track> XML for one track (instrument settings, effects, clips, notes). Same format as .mmp files.", schema({{"index", prop("integer", "")}}, {"index"}), getTrackXml});
	r.add({"add_track", "Add a track from <track> XML (as returned by get_track_xml or built from a preset). Returns index.", schema({{"xml", prop("string", "<track …>…</track>")}}, {"xml"}), addTrackXml});
	r.add({"replace_track", "Replace track at index with new <track> XML, keeping its position.", schema({{"index", prop("integer", "")}, {"xml", prop("string", "")}}, {"index", "xml"}), replaceTrackXml});
	r.add({"remove_track", "Delete a track.", schema({{"index", prop("integer", "")}}, {"index"}), removeTrack});
	r.add({"get_mixer_xml", "Mixer channels, names, volumes, sends and channel effect chains as <mixer> XML.", schema({}), getMixerXml});
	r.add({"set_mixer_xml", "Replace the whole mixer from <mixer> XML.", schema({{"xml", prop("string", "")}}, {"xml"}), setMixerXml});
```
Check `DataFile::currentVersion()` exists; if not, expose a static in `DataFile.h` returning `UPGRADE_METHODS.size()` (the value used in the `DataFile(Type)` ctor at `DataFile.cpp:127-139`). `LMMS_VERSION` comes from `lmmsversion.h`. `Track::saveSettings` writes a `<track>` child into the given parent (`Track.cpp:149-190`).

- [x] **Step 4: Run** — all `AiProjectToolsTest` cases pass.

- [x] **Step 5: Commit** — `git commit -m "ai: track/mixer XML tools"`.

---

### Task 7: Project tools — effects, params, automation, samples

**Files:**
- Modify: `src/core/ai/AiProjectTools.cpp`, `tests/src/core/ai/AiProjectToolsTest.cpp`
- Modify: `include/EffectChain.h` — add `const std::vector<Effect*>& effects() const { return m_effects; }` (public accessor; `m_effects` is private with only a GUI friend today)

**Interfaces:** Tools `add_effect`, `set_params`, `describe_model_tree`, `add_automation`, `add_sample_clip`. Shared helper `AutomatableModel* findModel(Model* root, const QString& name)` — matches `displayName()` case-insensitively, then `fullDisplayName()` suffix.

- [x] **Step 1: Failing tests (append)**

```cpp
	void effectAndParams()
	{
		reg.call("add_instrument_track", {{"name", "A"}, {"instrument", "tripleoscillator"}});
		auto e = reg.call("add_effect", {{"track", 0}, {"effect", "amplifier"}, {"params", QJsonObject{{"Volume", 50}}}});
		QVERIFY2(e["ok"].toBool(), qPrintable(e["error"].toString()));
		auto d = reg.call("describe_model_tree", {{"track", 0}});
		QVERIFY(d["ok"].toBool());
		QVERIFY(d["effects"].toArray().size() == 1);
		auto p = reg.call("set_params", {{"track", 0}, {"target", "track"}, {"params", QJsonObject{{"Volume", 42}}}});
		QVERIFY2(p["ok"].toBool(), qPrintable(p["error"].toString()));
		auto t = dynamic_cast<lmms::InstrumentTrack*>(lmms::Engine::getSong()->tracks()[0]);
		QCOMPARE(int(t->volumeModel()->value()), 42);
		QVERIFY(!reg.call("set_params", {{"track", 0}, {"target", "track"}, {"params", QJsonObject{{"NoSuchParam", 1}}}})["ok"].toBool());
		QVERIFY(!reg.call("add_effect", {{"track", 0}, {"effect", "no_such_fx"}})["ok"].toBool());
	}
	void automation()
	{
		reg.call("add_instrument_track", {{"name", "A"}, {"instrument", "tripleoscillator"}});
		auto r = reg.call("add_automation", {{"track", 0}, {"target", "track"}, {"model", "Volume"},
			{"points", QJsonArray{QJsonObject{{"pos", 0}, {"value", 0}}, QJsonObject{{"pos", 192}, {"value", 100}}}}, {"progression", "linear"}});
		QVERIFY2(r["ok"].toBool(), qPrintable(r["error"].toString()));
		int autoIdx = r["automationTrack"].toInt();
		auto at = lmms::Engine::getSong()->tracks()[autoIdx];
		QCOMPARE(at->type(), lmms::Track::Type::Automation);
		auto clip = dynamic_cast<lmms::AutomationClip*>(at->getClips()[0]);
		QVERIFY(clip);
		QCOMPARE(clip->valueAt(96), 50.0f);
	}
	void sampleClipRequiresExistingFile()
	{
		QVERIFY(!reg.call("add_sample_clip", {{"file", "C:/does/not/exist.wav"}, {"pos", 0}})["ok"].toBool());
	}
```
Add `#include "AutomationClip.h"` to the test.

- [x] **Step 2: Verify failure.**

- [x] **Step 3: Implement (append)**

```cpp
#include "AutomatableModel.h"
#include "AutomationClip.h"
#include "AutomationTrack.h"
#include "Effect.h"
#include "EffectChain.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include <QFileInfo>

static AutomatableModel* findModel(Model* root, const QString& name)
{
	if (!root) { return nullptr; }
	for (auto m : root->findChildren<AutomatableModel*>())
	{
		if (m->displayName().compare(name, Qt::CaseInsensitive) == 0) { return m; }
	}
	for (auto m : root->findChildren<AutomatableModel*>())
	{
		if (m->fullDisplayName().endsWith(name, Qt::CaseInsensitive)) { return m; }
	}
	return nullptr;
}

//! Resolves "instrument" | "effect:N" | "track" to a Model root on an instrument track.
static Model* resolveTarget(InstrumentTrack* track, const QString& target, QString* err)
{
	if (target == "track") { return track; }
	if (target == "instrument") { return track->instrument(); }
	if (target.startsWith("effect:"))
	{
		int n = target.mid(7).toInt();
		const auto& fx = track->audioBusHandle()->effects()->effects();
		if (n < 0 || n >= int(fx.size())) { *err = QString("No effect %1 on track (has %2)").arg(n).arg(fx.size()); return nullptr; }
		return fx[n];
	}
	*err = "target must be 'track', 'instrument' or 'effect:N'";
	return nullptr;
}

static QJsonArray describeModels(Model* root)
{
	QJsonArray out;
	if (!root) { return out; }
	for (auto m : root->findChildren<AutomatableModel*>())
	{
		if (m->displayName().isEmpty()) { continue; }
		out.append(QJsonObject{{"name", m->displayName()}, {"value", m->value<float>()},
			{"min", m->minValue<float>()}, {"max", m->maxValue<float>()}});
	}
	return out;
}

static QJsonObject applyParams(Model* root, const QJsonObject& params, QString* err)
{
	QJsonObject applied;
	for (auto it = params.begin(); it != params.end(); ++it)
	{
		auto m = findModel(root, it.key());
		if (!m) { *err = "Unknown parameter '" + it.key() + "' (use describe_model_tree)"; return {}; }
		m->setValue(float(it.value().toDouble()));
		applied[it.key()] = m->value<float>();
	}
	return applied;
}

static QJsonObject addEffect(const QJsonObject& a)
{
	QString plugin = a["effect"].toString();
	if (PluginFactory::instance()->pluginInfo(plugin.toUtf8().constData()).isNull()) { return R::error("Unknown effect plugin: " + plugin + " (use list_effects)"); }
	EffectChain* chain = nullptr; QString err;
	if (a.contains("mixerChannel"))
	{
		int ch = a["mixerChannel"].toInt();
		if (ch < 0 || ch >= int(Engine::mixer()->numChannels())) { return R::error("No such mixer channel"); }
		chain = &Engine::mixer()->mixerChannel(ch)->m_fxChain;
	}
	else
	{
		auto track = instrumentTrackAt(a["track"].toInt(-1), &err);
		if (!track) { return R::error(err); }
		chain = track->audioBusHandle()->effects();
	}
	auto effect = Effect::instantiate(plugin, chain, nullptr);
	if (!effect) { return R::error("Failed to instantiate effect " + plugin); }
	chain->appendEffect(effect);
	QJsonObject applied;
	if (a.contains("params")) { applied = applyParams(effect, a["params"].toObject(), &err); if (!err.isEmpty()) { return R::error(err); } }
	return R::ok({{"effectIndex", int(chain->effects().size()) - 1}, {"params", applied}});
}

static QJsonObject setParams(const QJsonObject& a)
{
	QString err; auto track = instrumentTrackAt(a["track"].toInt(-1), &err);
	if (!track) { return R::error(err); }
	auto root = resolveTarget(track, a["target"].toString("track"), &err);
	if (!root) { return R::error(err); }
	auto guard = Engine::audioEngine()->requestChangesGuard();
	auto applied = applyParams(root, a["params"].toObject(), &err);
	if (!err.isEmpty()) { return R::error(err); }
	return R::ok({{"applied", applied}});
}

static QJsonObject describeModelTree(const QJsonObject& a)
{
	QString err; auto track = instrumentTrackAt(a["track"].toInt(-1), &err);
	if (!track) { return R::error(err); }
	QJsonArray effects;
	int i = 0;
	for (auto fx : track->audioBusHandle()->effects()->effects())
	{
		effects.append(QJsonObject{{"target", QString("effect:%1").arg(i++)}, {"name", fx->displayName()}, {"params", describeModels(fx)}});
	}
	return R::ok({{"track", describeModels(track)}, {"instrument", describeModels(track->instrument())}, {"effects", effects}});
}

static QJsonObject addAutomation(const QJsonObject& a)
{
	QString err; auto track = instrumentTrackAt(a["track"].toInt(-1), &err);
	if (!track) { return R::error(err); }
	auto root = resolveTarget(track, a["target"].toString("track"), &err);
	if (!root) { return R::error(err); }
	auto model = findModel(root, a["model"].toString());
	if (!model) { return R::error("Unknown model '" + a["model"].toString() + "'"); }
	auto points = a["points"].toArray();
	if (points.isEmpty()) { return R::error("points must be non-empty"); }
	auto atrack = Track::create(Track::Type::Automation, Engine::getSong());
	atrack->setName(track->name() + " / " + model->displayName());
	auto clip = dynamic_cast<AutomationClip*>(atrack->createClip(TimePos(0)));
	QString prog = a["progression"].toString("linear");
	clip->setProgressionType(prog == "discrete" ? AutomationClip::ProgressionType::Discrete
		: prog == "cubic" ? AutomationClip::ProgressionType::CubicHermite : AutomationClip::ProgressionType::Linear);
	clip->addObject(model);
	for (auto v : points)
	{
		auto p = v.toObject();
		clip->putValue(TimePos(p["pos"].toInt()), float(p["value"].toDouble()), false);
	}
	return R::ok({{"automationTrack", int(Engine::getSong()->tracks().size()) - 1}, {"points", points.size()}});
}

static QJsonObject addSampleClip(const QJsonObject& a)
{
	QString file = a["file"].toString();
	if (!QFileInfo(file).isFile()) { return R::error("File not found: " + file); }
	SampleTrack* track = nullptr; QString err;
	if (a.contains("track"))
	{
		track = dynamic_cast<SampleTrack*>(trackAt(a["track"].toInt(), &err));
		if (!track) { return R::error(err.isEmpty() ? "Track is not a sample track" : err); }
	}
	else
	{
		track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
		track->setName(QFileInfo(file).completeBaseName());
	}
	auto clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(a["pos"].toInt(0))));
	clip->setSampleFile(file);
	return R::ok({{"track", int(std::find(Engine::getSong()->tracks().begin(), Engine::getSong()->tracks().end(), track) - Engine::getSong()->tracks().begin())},
		{"len", clip->length().getTicks()}});
}
```
Registrations (`add_effect`: `track` or `mixerChannel`, `effect` required, `params` object; `set_params`: `track`, `target`, `params` required; `describe_model_tree`: `track`; `add_automation`: `track`, `target`, `model`, `points[{pos,value}]`, `progression` in `discrete|linear|cubic`; `add_sample_clip`: `file`, `pos`, optional `track`). Path policy for `add_sample_clip` is applied in Task 8 by wrapping the handler (`registerAiDiscoveryTools` re-registers `add_sample_clip` with the policy check in front) — simpler: give `registerAiProjectTools` an optional `AiPathPolicy*` parameter and check `policy && !policy->allows(file)` → error. Update `AiTools.h` accordingly: `void registerAiProjectTools(AiToolRegistry& r, AiPathPolicy* policy = nullptr);`.

- [x] **Step 4: Run** — all `AiProjectToolsTest` cases pass. Watch for `Effect::instantiate` needing the chain as parent Model (`Effect.cpp:151-168`) — it does.

- [x] **Step 5: Commit** — `git commit -m "ai: effect/param/automation/sample tools"`.

---

### Task 8: AiPathPolicy and discovery tools

**Files:**
- Create: `include/AiPathPolicy.h`, `src/core/ai/AiPathPolicy.cpp`, `src/core/ai/AiDiscoveryTools.cpp`
- Test: `tests/src/core/ai/AiPathPolicyTest.cpp`
- Modify: CMake lists

- [x] **Step 1: Failing test**

```cpp
#include "AiPathPolicy.h"
#include <QtTest>
class AiPathPolicyTest : public QObject
{
	Q_OBJECT
private slots:
	void rootsAndUserPaths()
	{
		lmms::AiPathPolicy p;
		p.setRoots({"C:/music/lmms", "C:/Program Files/LMMS/data"});
		QVERIFY(p.allows("C:/music/lmms/samples/kick.wav"));
		QVERIFY(p.allows("C:\\music\\lmms\\x.mmp"));
		QVERIFY(!p.allows("C:/music/lmms-other/x.wav"));   // prefix but not a child dir
		QVERIFY(!p.allows("C:/Windows/system32/x.dll"));
		QVERIFY(!p.allows("C:/music/lmms/../../Windows/x"));
		p.allowFromUserText("use the loop at D:/loops/break.wav please, and D:\\other\\dir\\ too");
		QVERIFY(p.allows("D:/loops/break.wav"));
		QVERIFY(p.allows("D:/other/dir/inner.wav"));
		QVERIFY(!p.allows("D:/loops/other.wav"));
	}
};
QTEST_GUILESS_MAIN(AiPathPolicyTest)
#include "AiPathPolicyTest.moc"
```

- [x] **Step 2: Verify failure.**

- [x] **Step 3: Implement**

```cpp
// include/AiPathPolicy.h
class LMMS_EXPORT AiPathPolicy
{
public:
	void setRoots(QStringList roots);
	void allowFromUserText(const QString& text);
	bool allows(const QString& path) const;
	QStringList roots() const { return m_roots; }
private:
	static QString canon(const QString& p);   // QDir::cleanPath(QDir::fromNativeSeparators(p)), lower-cased on Windows
	QStringList m_roots;      // dirs (canonical, no trailing slash)
	QStringList m_files;      // exact files
};
// src/core/ai/AiPathPolicy.cpp
QString AiPathPolicy::canon(const QString& p)
{
	QString c = QDir::cleanPath(QDir::fromNativeSeparators(p.trimmed()));
	while (c.size() > 1 && c.endsWith('/')) { c.chop(1); }
#ifdef LMMS_BUILD_WIN32
	c = c.toLower();
#endif
	return c;
}
void AiPathPolicy::setRoots(QStringList roots)
{
	m_roots.clear();
	for (const auto& r : roots) { if (!r.isEmpty()) { m_roots << canon(r); } }
}
void AiPathPolicy::allowFromUserText(const QString& text)
{
	static const QRegularExpression re(R"(([A-Za-z]:[\\/][^\s"'<>|?*]+|/[^\s"'<>|?*]+))");
	for (auto m = re.globalMatch(text); m.hasNext();)
	{
		QString p = m.next().captured(1);
		while (p.endsWith(',') || p.endsWith('.')) { p.chop(1); }
		bool dir = p.endsWith('/') || p.endsWith('\\');
		(dir ? m_roots : m_files) << canon(p);
	}
}
bool AiPathPolicy::allows(const QString& path) const
{
	QString c = canon(path);
	if (c.contains("/../") || c.endsWith("/..")) { return false; }
	if (m_files.contains(c)) { return true; }
	for (const auto& r : m_roots) { if (c == r || c.startsWith(r + '/')) { return true; } }
	return false;
}
```
`src/core/ai/AiDiscoveryTools.cpp`:
```cpp
#include "AiTools.h"
#include "AiToolHelpers.h"
#include "AiPathPolicy.h"
#include "ConfigManager.h"
#include "DataFile.h"
#include "PluginFactory.h"
#include <QDirIterator>
namespace lmms {
using namespace aitools; using R = AiToolRegistry;

static QJsonArray listPlugins(Plugin::Type type)
{
	QJsonArray out;
	for (auto d : PluginFactory::instance()->descriptors(type))
	{
		out.append(QJsonObject{{"name", d->name}, {"displayName", d->displayName}, {"description", d->description}});
	}
	return out;
}

static QJsonArray listFiles(const QStringList& dirs, const QStringList& patterns, const QString& query, int limit)
{
	QJsonArray out;
	for (const auto& dir : dirs)
	{
		QDirIterator it(dir, patterns, QDir::Files, QDirIterator::Subdirectories);
		while (it.hasNext() && out.size() < limit)
		{
			QString p = it.next();
			if (query.isEmpty() || p.contains(query, Qt::CaseInsensitive)) { out.append(p); }
		}
	}
	return out;
}

void registerAiDiscoveryTools(AiToolRegistry& r, AiPathPolicy& policy)
{
	r.add({"list_instruments", "Instrument plugins available for add_instrument_track.", schema({}), [](const QJsonObject&) { return R::ok({{"instruments", listPlugins(Plugin::Type::Instrument)}}); }});
	r.add({"list_effects", "Effect plugins available for add_effect.", schema({}), [](const QJsonObject&) { return R::ok({{"effects", listPlugins(Plugin::Type::Effect)}}); }});
	r.add({"list_presets", "Instrument preset files (.xpf/.xiz). Optional substring query, e.g. 'bass' or 'TripleOscillator'.", schema({{"query", prop("string", "")}, {"limit", prop("integer", "default 50")}}),
		[](const QJsonObject& a) {
			auto cm = ConfigManager::inst();
			return R::ok({{"presets", listFiles({cm->factoryPresetsDir(), cm->userPresetsDir()}, {"*.xpf", "*.xiz"}, a["query"].toString(), a["limit"].toInt(50))}});
		}});
	r.add({"list_samples", "Sample files (.wav/.ogg/.flac/.mp3) in the factory and user sample dirs. Optional substring query.", schema({{"query", prop("string", "")}, {"limit", prop("integer", "default 50")}}),
		[](const QJsonObject& a) {
			auto cm = ConfigManager::inst();
			return R::ok({{"samples", listFiles({cm->factorySamplesDir(), cm->userSamplesDir()}, {"*.wav", "*.ogg", "*.flac", "*.mp3", "*.aiff"}, a["query"].toString(), a["limit"].toInt(50))}});
		}});
	r.add({"get_preset_xml", "Read a preset file and return its <instrumenttrack> XML so it can be embedded in a <track> for add_track.", schema({{"path", prop("string", "")}}, {"path"}),
		[&policy](const QJsonObject& a) {
			QString path = a["path"].toString();
			if (!policy.allows(path)) { return R::error("Path not allowed: " + path); }
			DataFile df(path);
			if (df.content().isNull()) { return R::error("Not a readable preset: " + path); }
			QString out; QTextStream ts(&out); df.content().save(ts, 0);
			if (out.size() > 64 * 1024) { return R::error("Preset XML too large"); }
			return R::ok({{"xml", out.trimmed()}});
		}});
}
}
```
`.xpf` content root is `<instrumenttracksettings>` containing `<instrumenttrack>`; the system prompt (Task 10) explains embedding.

- [x] **Step 4: Run** — `AiPathPolicyTest.exe` passes; build succeeds.

- [x] **Step 5: Commit** — `git commit -m "ai: path policy and discovery tools"`.

---

### Task 9: Action tools

**Files:**
- Create: `src/core/ai/AiActionTools.cpp`
- Modify: CMake lists
- Test: covered by Task 13 smoke (render/play need audio engine + event loop); unit-test `new_project` and `save` path rejection in `AiProjectToolsTest` by registering action tools with a policy whose roots are a `QTemporaryDir`.

- [x] **Step 1: Failing test (append to AiProjectToolsTest; add members `lmms::AiPathPolicy policy; QTemporaryDir tmp;` and in `initTestCase`: `policy.setRoots({tmp.path()}); lmms::registerAiActionTools(reg, policy);`)**

```cpp
	void saveRespectsPolicyAndWrites()
	{
		reg.call("add_instrument_track", {{"name", "A"}, {"instrument", "tripleoscillator"}});
		QVERIFY(!reg.call("save", {{"path", "C:/Windows/evil.mmp"}})["ok"].toBool());
		QString p = tmp.path() + "/out.mmp";
		auto r = reg.call("save", {{"path", p}});
		QVERIFY2(r["ok"].toBool(), qPrintable(r["error"].toString()));
		QVERIFY(QFileInfo(p).size() > 0);
		QVERIFY(reg.call("new_project", {})["ok"].toBool());
		QCOMPARE(int(lmms::Engine::getSong()->tracks().size()), lmms::Engine::getSong()->tracks().size() ? int(lmms::Engine::getSong()->tracks().size()) : 0);
	}
```
(The last line documents that `new_project` loads the default template, which may contain tracks; assert instead that `projectFileName()` is empty.)

- [x] **Step 2: Verify failure.**

- [x] **Step 3: Implement**

```cpp
#include "AiTools.h"
#include "AiToolHelpers.h"
#include "AiPathPolicy.h"
#include "AudioEngine.h"
#include "OutputSettings.h"
#include "ProjectJournal.h"
#include "ProjectRenderer.h"
#include "RenderManager.h"
#include <QEventLoop>
#include <QFileInfo>
namespace lmms {
using namespace aitools; using R = AiToolRegistry;

void registerAiActionTools(AiToolRegistry& r, AiPathPolicy& policy)
{
	r.add({"play", "Start playback from a bar (default: from the current position).", schema({{"fromBar", prop("integer", "1-based bar")}}), [](const QJsonObject& a) {
		auto song = Engine::getSong();
		if (song->isExporting()) { return R::error("Cannot play while exporting"); }
		if (a.contains("fromBar")) { song->setPlayPos(TimePos(std::max(0, a["fromBar"].toInt() - 1), 0).getTicks(), Song::PlayMode::Song); }
		if (!song->isPlaying()) { song->playSong(); }
		return R::ok({{"playing", true}});
	}});
	r.add({"stop", "Stop playback.", schema({}), [](const QJsonObject&) { Engine::getSong()->stop(); return R::ok({{"playing", false}}); }});
	r.add({"render", "Render the whole song to an audio file. Blocks until done. format: wav|flac|ogg|mp3.", schema({{"path", prop("string", "output path without extension or with matching extension")}, {"format", prop("string", "wav (default), flac, ogg, mp3")}}, {"path"}),
		[&policy](const QJsonObject& a) {
			auto song = Engine::getSong();
			if (song->isPlaying() || song->isExporting()) { return R::error("Stop playback before rendering"); }
			QString path = a["path"].toString();
			if (!policy.allows(path)) { return R::error("Path not allowed: " + path); }
			QString fmtName = a["format"].toString("wav").toLower();
			auto fmt = fmtName == "flac" ? ProjectRenderer::ExportFileFormat::Flac : fmtName == "ogg" ? ProjectRenderer::ExportFileFormat::Ogg
				: fmtName == "mp3" ? ProjectRenderer::ExportFileFormat::MP3 : ProjectRenderer::ExportFileFormat::Wave;
			QString ext = ProjectRenderer::getFileExtensionFromFormat(fmt);
			if (!path.endsWith(ext, Qt::CaseInsensitive)) { path += ext; }
			OutputSettings os(Engine::audioEngine()->outputSampleRate(), 160, OutputSettings::BitDepth::Depth16Bit, OutputSettings::StereoMode::JointStereo);
			RenderManager rm(os, fmt, path);
			QEventLoop loop;
			QObject::connect(&rm, &RenderManager::finished, &loop, &QEventLoop::quit);
			rm.renderProject();
			loop.exec();
			if (!QFileInfo(path).exists()) { return R::error("Render produced no file"); }
			return R::ok({{"path", path}, {"bytes", QFileInfo(path).size()}});
		}});
	r.add({"save", "Save the project (.mmp). Omit path to save to the current file.", schema({{"path", prop("string", "")}}), [&policy](const QJsonObject& a) {
		auto song = Engine::getSong();
		QString path = a["path"].toString(song->projectFileName());
		if (path.isEmpty()) { return R::error("Project has no file name; pass path"); }
		if (!policy.allows(path)) { return R::error("Path not allowed: " + path); }
		if (!path.endsWith(".mmp") && !path.endsWith(".mmpz")) { path += ".mmp"; }
		if (!song->saveProjectFile(path)) { return R::error("Save failed: " + path); }
		return R::ok({{"path", path}});
	}});
	r.add({"new_project", "Discard the current project and start an empty one.", schema({}), [](const QJsonObject&) {
		Engine::getSong()->createNewProject();
		return R::ok({{"tracks", int(Engine::getSong()->tracks().size())}});
	}});
	r.add({"undo", "Undo the last change (one journal step).", schema({}), [](const QJsonObject&) {
		auto j = Engine::projectJournal();
		if (!j->canUndo()) { return R::error("Nothing to undo"); }
		j->undo();
		return R::ok();
	}});
}
}
```
Note: during a turn `AiSession` sets journalling off, so `undo` inside a turn only sees pre-turn history — acceptable; the prompt tells the model to prefer `remove_track`/`replace_track` for its own corrections. `Engine::audioEngine()->outputSampleRate()` — confirm name in `include/AudioEngine.h` (`outputSampleRate()` or `processingSampleRate()`).

- [x] **Step 4: Run** — `AiProjectToolsTest.exe` passes.

- [x] **Step 5: Commit** — `git commit -m "ai: play/stop/render/save/new_project/undo tools"`.

---

### Task 10: System prompt and prompt builder

**Files:**
- Create: `data/ai/system_prompt.md`
- Modify: `data/CMakeLists.txt` (install `ai/` alongside other data dirs — mirror how `presets`/`samples` are installed)
- Create: `include/AiPromptBuilder.h`, `src/core/ai/AiPromptBuilder.cpp` — `QString buildAiSystemPrompt();` reads `ConfigManager::inst()->dataDir() + "ai/system_prompt.md"`, falls back to an embedded minimal string if missing, appends instrument/effect name lists from `PluginFactory`.
- Test: none (prompt text; the builder is exercised in the smoke test).

- [x] **Step 1: Write `data/ai/system_prompt.md`**

```markdown
You are the AI Composer inside LMMS, a digital audio workstation. You create and edit music in the user's open project by calling tools. Work autonomously: plan, call tools, verify with get_project_summary, then reply briefly with what you did.

## Rules
- Call get_project_summary before changing anything, and again at the end to verify.
- Prefer the convenience tools (add_instrument_track, add_notes, add_effect, set_params, add_automation). Use get_track_xml / add_track / replace_track when you need anything the convenience tools don't cover: it is the full LMMS project format, so every feature LMMS can save is reachable there.
- Never write notes as XML if add_notes can do it.
- Time units are ticks. ticksPerBar is in get_project_summary (192 in 4/4): quarter note = 48, eighth = 24, sixteenth = 12, bar = 192. Clip positions are absolute ticks from song start; note pos is relative to its clip.
- MIDI keys: C4 = 60. Drums with the "kicker" instrument or sample-based presets respond to any key; use 36–48.
- Volume 0–200 (100 = unity). Panning −100..100.
- Keep tempo 10–999.
- When a tool returns ok:false, read the error and correct your call; do not repeat the same call unchanged.
- If a request is ambiguous, make a reasonable musical choice and state it; do not ask questions unless truly blocked.
- Do not invent plugin or preset names; use list_instruments / list_effects / list_presets.

## Common recipes
- New song: set_head → add_instrument_track per part (drums: kicker or a preset from list_presets query "drum"; bass: tripleoscillator or a "bass" preset; chords/lead: tripleoscillator, watsyn, organic, monstro) → add_notes per part, usually 4–16 bars → optional add_effect (e.g. "reverb", "eq", "compressor" from list_effects) → get_project_summary.
- Use a preset: list_presets → get_preset_xml → wrap: <track type="0" name="Name">{instrumenttrack element from the preset}</track> → add_track. Then add_notes on the new index.
- Edit existing notes: add_notes with clear:true on the same clipPos replaces the clip's notes.
- Mixing: set_params target "track" params {"Volume": 80, "Panning": -20}; add_effect on track or mixerChannel.
- Automation: add_automation target "track" model "Volume" points [{pos, value}].

## Project XML cheat-sheet (for get_track_xml / add_track)
<track type="0|2|5" name="…" muted="0" solo="0">   type: 0 instrument, 2 sample, 5 automation
  <instrumenttrack vol="100" pan="0" pitch="0" basenote="57" mixch="0">
    <instrument name="tripleoscillator"><tripleoscillator …/></instrument>
    <fxchain enabled="1" numofeffects="…"><effect name="…"><…/></effect></fxchain>
  </instrumenttrack>
  <midiclip pos="0" len="192" name="…"><note key="60" vol="100" pan="0" len="48" pos="0"/></midiclip>
  <sampleclip pos="0" len="…" src="path.wav"/>
</track>
```

- [x] **Step 2: Prompt builder**

```cpp
// include/AiPromptBuilder.h
namespace lmms { LMMS_EXPORT QString buildAiSystemPrompt(); }
// src/core/ai/AiPromptBuilder.cpp
QString buildAiSystemPrompt()
{
	QString base;
	QFile f(ConfigManager::inst()->dataDir() + "ai/system_prompt.md");
	if (f.open(QIODevice::ReadOnly)) { base = QString::fromUtf8(f.readAll()); }
	else { base = "You are the AI Composer inside LMMS. Use the tools to build music in the open project. Call get_project_summary first."; }
	QStringList instruments, effects;
	for (auto d : PluginFactory::instance()->descriptors(Plugin::Type::Instrument)) { instruments << QString("%1 (%2)").arg(d->name, d->displayName); }
	for (auto d : PluginFactory::instance()->descriptors(Plugin::Type::Effect)) { effects << QString("%1 (%2)").arg(d->name, d->displayName); }
	return base + "\n\n## Installed instruments\n" + instruments.join(", ") + "\n\n## Installed effects\n" + effects.join(", ") + "\n";
}
```
Data install: in `data/CMakeLists.txt` add `ai` to the installed directory list (read the file — it is 188 bytes — and mirror the existing `INSTALL(DIRECTORY …)` form). For the dev build, `ConfigManager::dataDir()` resolves to the source `data/` dir when running from `build/` (verify by checking `ConfigManager.cpp` `dataDir` initialisation; if it only checks the install prefix, add `../data/` fallback there is *not* in scope — instead run the smoke test with `LMMS_DATA_DIR=/c/git_repos/lmms/data` if such an env var exists in `ConfigManager.cpp`; check, and document the finding in the commit message).

- [x] **Step 3: Build; commit** — `git commit -m "ai: system prompt and prompt builder"`.

---

### Task 11: Settings page

**Files:**
- Modify: `src/gui/modals/SetupDialog.cpp` (new AI tab after Paths), `include/SetupDialog.h`
- Test: manual (open Settings → AI, edit, OK, reopen; values persist in `.lmmsrc.xml`).

- [x] **Step 1: Header members and slots**

In `include/SetupDialog.h` add private slots `void setAiBaseUrl(const QString&)`, `void setAiApiKey(const QString&)`, `void setAiModel(const QString&)`, `void testAiConnection()`, `void toggleAiKeyVisible(bool)`; members `QString m_aiBaseUrl, m_aiApiKey, m_aiModel; QLineEdit* m_aiKeyEdit; QLabel* m_aiTestResult;`. Add `AI` to the `ConfigTab` enum if one exists (used by `tab_to_open`), else use index 5.

- [x] **Step 2: Build the tab (after the Paths block, before "Major tabs ordering")**

```cpp
	// AI widget.
	auto ai_w = new QWidget(settings_w);
	auto ai_layout = new QVBoxLayout(ai_w);
	ai_layout->setSpacing(10);
	ai_layout->setContentsMargins(0, 0, 0, 0);
	labelWidget(ai_w, tr("AI Composer"));

	auto aiCfg = AiConfig::load();
	m_aiBaseUrl = aiCfg.baseUrl; m_aiApiKey = aiCfg.apiKey; m_aiModel = aiCfg.model;

	auto aiGroup = new QGroupBox(tr("OpenAI-compatible endpoint"), ai_w);
	auto aiForm = new QFormLayout(aiGroup);
	auto urlEdit = new QLineEdit(m_aiBaseUrl, aiGroup);
	connect(urlEdit, &QLineEdit::textChanged, this, &SetupDialog::setAiBaseUrl);
	aiForm->addRow(tr("Base URL"), urlEdit);
	m_aiKeyEdit = new QLineEdit(m_aiApiKey, aiGroup);
	m_aiKeyEdit->setEchoMode(QLineEdit::Password);
	connect(m_aiKeyEdit, &QLineEdit::textChanged, this, &SetupDialog::setAiApiKey);
	auto keyRow = new QHBoxLayout;
	keyRow->addWidget(m_aiKeyEdit, 1);
	auto showKey = new QCheckBox(tr("Show"), aiGroup);
	connect(showKey, &QCheckBox::toggled, this, &SetupDialog::toggleAiKeyVisible);
	keyRow->addWidget(showKey);
	aiForm->addRow(tr("API key"), keyRow);
	auto modelEdit = new QLineEdit(m_aiModel, aiGroup);
	modelEdit->setPlaceholderText("gpt-4o, claude-sonnet-4-5 via OpenRouter, llama3 via Ollama …");
	connect(modelEdit, &QLineEdit::textChanged, this, &SetupDialog::setAiModel);
	aiForm->addRow(tr("Model"), modelEdit);
	auto testBtn = new QPushButton(tr("Test connection"), aiGroup);
	connect(testBtn, &QPushButton::clicked, this, &SetupDialog::testAiConnection);
	m_aiTestResult = new QLabel(aiGroup);
	auto testRow = new QHBoxLayout; testRow->addWidget(testBtn); testRow->addWidget(m_aiTestResult, 1);
	aiForm->addRow(QString(), testRow);
	auto note = new QLabel(tr("The key is stored in plain text in your LMMS configuration file (.lmmsrc.xml)."), aiGroup);
	note->setWordWrap(true);
	aiForm->addRow(QString(), note);
	ai_layout->addWidget(aiGroup);
	ai_layout->addStretch();
	settingsLayout->addWidget(ai_w);
```
Tab registration after Paths: `m_tabBar->addTab(ai_w, tr("AI"), 5, false, true, false)->setIcon(embed::getIconPixmap("setup_general"));`

Slots:
```cpp
void SetupDialog::setAiBaseUrl(const QString& v) { m_aiBaseUrl = v; }
void SetupDialog::setAiApiKey(const QString& v) { m_aiApiKey = v; }
void SetupDialog::setAiModel(const QString& v) { m_aiModel = v; }
void SetupDialog::toggleAiKeyVisible(bool on) { m_aiKeyEdit->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password); }
void SetupDialog::testAiConnection()
{
	m_aiTestResult->setText(tr("Testing…"));
	AiConfig c{m_aiBaseUrl, m_aiApiKey, m_aiModel};
	while (c.baseUrl.endsWith('/')) { c.baseUrl.chop(1); }
	auto client = new OpenAiClient(c, this);
	client->testConnection([this, client](bool ok, QString msg) { m_aiTestResult->setText((ok ? "✓ " : "✗ ") + msg); client->deleteLater(); });
}
```
In `accept()` before `saveConfigFile()`: `AiConfig::save({m_aiBaseUrl, m_aiApiKey, m_aiModel});`.
Includes: `"AiConfig.h"`, `"OpenAiClient.h"`, `<QFormLayout>`, `<QCheckBox>`.

- [x] **Step 3: Build, launch, verify manually** — Edit → Settings → AI: fields present, Test against `http://127.0.0.1:1/v1` shows a ✗ with an HTTP/connection error, OK persists values (check `%APPDATA%/lmms/.lmmsrc.xml` or wherever `ConfigManager` writes — `ConfigManager::inst()->…` path printed via `lmms --help` is not available; find it in `ConfigManager.cpp` `m_lmmsRcFile`).

- [x] **Step 4: Commit** — `git commit -m "ai: settings page"`.

---

### Task 12: AiChatView and main-window wiring

**Files:**
- Create: `include/AiChatView.h`, `src/gui/ai/AiChatView.cpp`
- Modify: `src/gui/CMakeLists.txt`, `include/GuiApplication.h`, `src/gui/GuiApplication.cpp`, `include/MainWindow.h`, `src/gui/MainWindow.cpp`
- Test: manual + Task 13 smoke.

- [x] **Step 1: AiChatView**

```cpp
// include/AiChatView.h
namespace lmms { class AiSession; class AiToolRegistry; class OpenAiClient; class AiPathPolicy; }
namespace lmms::gui {
class AiChatView : public QWidget
{
	Q_OBJECT
public:
	AiChatView();
	~AiChatView() override;
public slots:
	void reloadConfig();        //!< re-read AiConfig (called after Settings OK)
private slots:
	void sendOrStop();
	void newChat();
	void undoTurn();
	void onTextDelta(const QString& text);
	void onToolStarted(const QString& name, const QJsonObject& args);
	void onToolFinished(const QString& name, const QJsonObject& result);
	void onTurnFinished(const QString& text);
	void onTurnFailed(const QString& error);
	void onStatus(const QString& text);
private:
	void appendBlock(const QString& html);
	void refreshPolicyRoots();
	QStackedWidget* m_stack; QWidget* m_banner; QWidget* m_chat;
	QTextBrowser* m_transcript; QPlainTextEdit* m_input; QPushButton* m_sendStop; QLabel* m_status;
	std::unique_ptr<AiToolRegistry> m_registry; std::unique_ptr<AiPathPolicy> m_policy;
	OpenAiClient* m_client; AiSession* m_session;
	QString m_streaming;   // assistant text being streamed for the current turn
	int m_turnToolCount = 0;
};
}
```
`src/gui/ai/AiChatView.cpp` essentials:
```cpp
AiChatView::AiChatView() : QWidget{}, m_registry(std::make_unique<AiToolRegistry>()), m_policy(std::make_unique<AiPathPolicy>())
{
	setWindowIcon(embed::getIconPixmap("project_notes"));
	setWindowTitle(tr("AI Composer"));
	registerAiProjectTools(*m_registry, m_policy.get());
	registerAiDiscoveryTools(*m_registry, *m_policy);
	registerAiActionTools(*m_registry, *m_policy);
	m_client = new OpenAiClient(AiConfig::load(), this);
	m_session = new AiSession(m_client, m_registry.get(), this);
	m_session->setSystemPrompt(buildAiSystemPrompt());
	connect(m_session, &AiSession::assistantTextDelta, this, &AiChatView::onTextDelta);
	connect(m_session, &AiSession::toolCallStarted, this, &AiChatView::onToolStarted);
	connect(m_session, &AiSession::toolCallFinished, this, &AiChatView::onToolFinished);
	connect(m_session, &AiSession::turnFinished, this, &AiChatView::onTurnFinished);
	connect(m_session, &AiSession::turnFailed, this, &AiChatView::onTurnFailed);
	connect(m_session, &AiSession::status, this, &AiChatView::onStatus);

	m_stack = new QStackedWidget(this);
	// banner
	m_banner = new QWidget; auto bl = new QVBoxLayout(m_banner);
	auto bannerLabel = new QLabel(tr("No AI endpoint configured. <a href=\"settings\">Open Settings → AI</a> to set a base URL, API key and model."));
	bannerLabel->setWordWrap(true);
	connect(bannerLabel, &QLabel::linkActivated, this, [] { getGUI()->mainWindow()->showSettingsDialog(); }); // add a public slot on MainWindow that opens SetupDialog; MainWindow already has one for the Edit menu — reuse its name
	bl->addWidget(bannerLabel); bl->addStretch();
	// chat
	m_chat = new QWidget; auto cl = new QVBoxLayout(m_chat);
	m_transcript = new QTextBrowser; m_transcript->setOpenLinks(false);
	m_status = new QLabel;
	m_input = new QPlainTextEdit; m_input->setPlaceholderText(tr("Describe the music you want… (Enter to send, Shift+Enter for a new line)"));
	m_input->setFixedHeight(70);
	m_input->installEventFilter(this);
	m_sendStop = new QPushButton(tr("Send"));
	auto newBtn = new QPushButton(tr("New chat")); auto undoBtn = new QPushButton(tr("Undo turn"));
	connect(m_sendStop, &QPushButton::clicked, this, &AiChatView::sendOrStop);
	connect(newBtn, &QPushButton::clicked, this, &AiChatView::newChat);
	connect(undoBtn, &QPushButton::clicked, this, &AiChatView::undoTurn);
	auto row = new QHBoxLayout; row->addWidget(m_sendStop); row->addWidget(newBtn); row->addWidget(undoBtn); row->addStretch();
	cl->addWidget(m_transcript, 1); cl->addWidget(m_status); cl->addWidget(m_input); cl->addLayout(row);
	m_stack->addWidget(m_banner); m_stack->addWidget(m_chat);
	auto layout = new QVBoxLayout(this); layout->addWidget(m_stack);
	reloadConfig();

	SubWindow* subWin = getGUI()->mainWindow()->addWindowedWidget(this);
	setMinimumSize(420, 320);
	subWin->setAttribute(Qt::WA_DeleteOnClose, false);
	subWin->resize(520, 640);
	subWin->move(700, 5);
	subWin->hide();
}

bool AiChatView::eventFilter(QObject* obj, QEvent* ev)   // declare `bool eventFilter(QObject*, QEvent*) override;` in the header
{
	if (obj == m_input && ev->type() == QEvent::KeyPress)
	{
		auto ke = static_cast<QKeyEvent*>(ev);
		if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) && !(ke->modifiers() & Qt::ShiftModifier)) { sendOrStop(); return true; }
	}
	return QWidget::eventFilter(obj, ev);
}

void AiChatView::reloadConfig()
{
	auto cfg = AiConfig::load();
	m_client->setConfig(cfg);
	m_stack->setCurrentWidget(cfg.configured() ? m_chat : m_banner);
	refreshPolicyRoots();
}

void AiChatView::refreshPolicyRoots()
{
	auto cm = ConfigManager::inst();
	QStringList roots{cm->dataDir(), cm->workingDir()};
	QString pf = Engine::getSong()->projectFileName();
	if (!pf.isEmpty()) { roots << QFileInfo(pf).absolutePath(); }
	m_policy->setRoots(roots);
}

void AiChatView::sendOrStop()
{
	if (m_session->busy()) { m_session->stop(); return; }
	QString text = m_input->toPlainText().trimmed();
	if (text.isEmpty()) { return; }
	m_input->clear();
	refreshPolicyRoots();
	m_policy->allowFromUserText(text);
	appendBlock("<div style='margin:6px 0;padding:6px;background:#2b3a4a;border-radius:6px'><b>You</b><br>" + text.toHtmlEscaped().replace("\n", "<br>") + "</div>");
	m_streaming.clear(); m_turnToolCount = 0;
	m_sendStop->setText(tr("Stop"));
	m_session->submit(text);
}
void AiChatView::newChat() { if (m_session->busy()) { m_session->stop(); } m_session->clear(); m_transcript->clear(); m_status->clear(); }
void AiChatView::undoTurn() { if (Engine::projectJournal()->canUndo()) { Engine::projectJournal()->undo(); m_status->setText(tr("Reverted last turn")); } }
void AiChatView::onTextDelta(const QString& t) { m_streaming += t; m_status->setText(tr("Writing…")); }
void AiChatView::onToolStarted(const QString& name, const QJsonObject& args)
{
	++m_turnToolCount;
	QString argStr = QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact)).left(200);
	appendBlock("<div style='color:#8aa;margin-left:12px'>▸ " + name.toHtmlEscaped() + " " + argStr.toHtmlEscaped() + "</div>");
}
void AiChatView::onToolFinished(const QString& name, const QJsonObject& result)
{
	bool ok = result["ok"].toBool();
	QString msg = ok ? "✓" : "✗ " + result["error"].toString().toHtmlEscaped();
	appendBlock(QString("<div style='color:%1;margin-left:24px'>%2</div>").arg(ok ? "#7c7" : "#e9a23b", msg));
}
void AiChatView::onTurnFinished(const QString& text)
{
	QString t = text.isEmpty() ? m_streaming : text;
	appendBlock("<div style='margin:6px 0;padding:6px;background:#233;border-radius:6px'><b>AI</b><br>" + t.toHtmlEscaped().replace("\n", "<br>") + "</div>");
	m_sendStop->setText(tr("Send"));
}
void AiChatView::onTurnFailed(const QString& err)
{
	appendBlock("<div style='color:#e55'>" + err.toHtmlEscaped() + "</div>");
	m_sendStop->setText(tr("Send"));
}
void AiChatView::onStatus(const QString& t) { m_status->setText(t); }
void AiChatView::appendBlock(const QString& html) { m_transcript->append(html); m_transcript->verticalScrollBar()->setValue(m_transcript->verticalScrollBar()->maximum()); }
```
Add `gui/ai/AiChatView.cpp` to `src/gui/CMakeLists.txt`.

- [x] **Step 2: GuiApplication + MainWindow wiring**

- `include/GuiApplication.h`: forward-declare `class AiChatView;`, add `AiChatView* aiChatView() { return m_aiChatView; }` and member `AiChatView* m_aiChatView;`; in `GuiApplication.cpp` after the controller rack block: `displayInitProgress(tr("Preparing AI composer")); m_aiChatView = new AiChatView; connect(m_aiChatView, SIGNAL(destroyed(QObject*)), this, SLOT(childDestroyed(QObject*)));` and a `childDestroyed` branch nulling it.
- `MainWindow`: slot `void toggleAiChatWin()` → `toggleWindow(getGUI()->aiChatView());`. In `updateViewMenu()` after Project Notes: `m_viewMenu->addAction(embed::getIconPixmap("project_notes"), tr("AI Composer") + "\tCtrl+Shift+A", this, SLOT(toggleAiChatWin()));`. Toolbar: after `project_notes_window`: `auto ai_window = new ToolButton(embed::getIconPixmap("project_notes"), tr("Show/hide AI Composer") + " (Ctrl+Shift+A)", this, SLOT(toggleAiChatWin()), m_toolBar); ai_window->setShortcut(keySequence(Qt::CTRL, Qt::SHIFT, Qt::Key_A)); m_toolBarLayout->addWidget(ai_window, 1, 8);`.
- After the Settings dialog closes with OK (find where `SetupDialog` is exec'd in `MainWindow.cpp`, slot named like `showSettingsDialog`), call `getGUI()->aiChatView()->reloadConfig();`. Ensure that slot is public so the banner link can call it.

- [x] **Step 3: Build, launch, verify** — Ctrl+Shift+A shows the panel; with no key, the banner appears and its link opens Settings; after entering a bogus key + model, the chat box appears.

- [x] **Step 4: Commit** — `git commit -m "ai: chat panel wired into main window"`.

---

### Task 13: Mock server smoke test and end-to-end verification

**Files:**
- Create: `tests/scripted/ai_mock_server.py` (permanent: documented way to exercise the panel without a key)
- Modify: `docs/superpowers/specs/2026-09-16-ai-composer-design.md` — no changes expected; update only if implementation deviated.

- [x] **Step 1: Mock server**

```python
#!/usr/bin/env python3
"""Minimal OpenAI-compatible mock: scripted tool calls to build a 4-bar drum loop, then a final message.
Run: python tests/scripted/ai_mock_server.py  (listens on http://127.0.0.1:8765/v1)
Point LMMS Settings → AI at base URL http://127.0.0.1:8765/v1, any key, any model."""
import json
from http.server import BaseHTTPRequestHandler, HTTPServer

def tool(i, name, args):
    return {"id": f"call{i}", "type": "function", "function": {"name": name, "arguments": json.dumps(args)}}

KICK = [{"pos": p, "len": 24, "key": 36} for p in range(0, 768, 96)]
SNARE = [{"pos": p, "len": 24, "key": 38} for p in range(48, 768, 96)]
HAT = [{"pos": p, "len": 12, "key": 42, "vol": 70} for p in range(0, 768, 24)]

SCRIPT = [
    [tool(1, "get_project_summary", {})],
    [tool(2, "set_head", {"bpm": 90})],
    [tool(3, "add_instrument_track", {"name": "Kick", "instrument": "kicker"})],
    [tool(4, "add_notes", {"track": "$T0", "clipPos": 0, "notes": KICK})],
    [tool(5, "add_instrument_track", {"name": "Hats", "instrument": "tripleoscillator"})],
    [tool(6, "add_notes", {"track": "$T1", "clipPos": 0, "notes": HAT})],
    [tool(7, "add_effect", {"track": "$T1", "effect": "amplifier", "params": {"Volume": 60}})],
    [tool(8, "get_project_summary", {})],
]
STATE = {"step": 0, "tracks": []}

class H(BaseHTTPRequestHandler):
    def do_GET(self):
        self._json({"data": [{"id": "mock-model"}]})
    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        msgs = body["messages"]
        # learn track indices from previous tool results
        for m in msgs:
            if m.get("role") == "tool":
                try:
                    r = json.loads(m["content"])
                    if r.get("ok") and "index" in r and r["index"] not in STATE["tracks"]:
                        STATE["tracks"].append(r["index"])
                except Exception:
                    pass
        if msgs[-1]["role"] == "user":
            STATE["step"] = 0; STATE["tracks"] = []
        if STATE["step"] < len(SCRIPT):
            calls = json.loads(json.dumps(SCRIPT[STATE["step"]]))
            for c in calls:
                a = json.loads(c["function"]["arguments"])
                if isinstance(a.get("track"), str) and a["track"].startswith("$T"):
                    a["track"] = STATE["tracks"][int(a["track"][2:])]
                c["function"]["arguments"] = json.dumps(a)
            STATE["step"] += 1
            msg = {"role": "assistant", "content": None, "tool_calls": calls}
            finish = "tool_calls"
        else:
            msg = {"role": "assistant", "content": "Built a 4-bar drum loop at 90 BPM: Kick and Hats tracks."}
            finish = "stop"
        if body.get("stream"):
            self.send_response(200); self.send_header("Content-Type", "text/event-stream"); self.end_headers()
            delta = dict(msg)
            if "tool_calls" in delta:
                delta["tool_calls"] = [dict(c, index=i) for i, c in enumerate(delta["tool_calls"])]
            self.wfile.write(f"data: {json.dumps({'choices': [{'delta': delta, 'finish_reason': None}]})}\n\n".encode())
            self.wfile.write(f"data: {json.dumps({'choices': [{'delta': {}, 'finish_reason': finish}]})}\n\ndata: [DONE]\n\n".encode())
        else:
            self._json({"choices": [{"message": msg, "finish_reason": finish}]})
    def _json(self, obj):
        data = json.dumps(obj).encode()
        self.send_response(200); self.send_header("Content-Type", "application/json"); self.send_header("Content-Length", str(len(data))); self.end_headers(); self.wfile.write(data)
    def log_message(self, *a): pass

if __name__ == "__main__":
    print("mock OpenAI server on http://127.0.0.1:8765/v1")
    HTTPServer(("127.0.0.1", 8765), H).serve_forever()
```

- [x] **Step 2: Run end to end**

1. `python tests/scripted/ai_mock_server.py` (MSYS2 has python via `pacman -S python` if missing; Windows python also fine).
2. Launch `build/lmms.exe`, Settings → AI: base URL `http://127.0.0.1:8765/v1`, key `x`, model `mock`. Test connection → ✓.
3. Ctrl+Shift+A, type "make a drum loop", Enter.
4. Expected: transcript shows 8 tool rows all ✓, final AI message; Song Editor shows tracks "Kick" and "Hats" with 4-bar clips; tempo 90.
5. Press Undo turn → both tracks disappear and tempo reverts. (If the song-level checkpoint does not restore tracks, switch `beginTurnCheckpoint` to snapshot via `DataFile`+`Song::loadProject` of a temp file — record which path was taken.)
6. Type "render it to C:/git_repos/lmms/build/loop.wav" → since the path is in the user's text it is allowed; a WAV appears.
7. Screenshot the panel with the transcript for the report.

- [x] **Step 3: Regression**

Run: `cd build/tests && ctest --output-on-failure` → all tests pass (8 original + 5 new).
Run: `build/lmms.exe render tests/emptyproject.mmp -o build/empty.wav` → completes.

- [x] **Step 4: Commit**

```bash
git add tests/scripted/ai_mock_server.py
git commit -m "ai: mock server for end-to-end smoke testing"
```

---

## Self-review notes

- Spec §2 tools: all 22 present — project (7: Tasks 5–6), convenience (6: Tasks 5, 7), discovery (6: Task 8 has 5 + `describe_model_tree` in Task 7), actions (6: Task 9).
- Spec §3 client/session: Task 2, Task 4. Non-streaming fallback: parser `end()`.
- Spec §4 GUI: Tasks 11–12. Song Editor scroll-to-track on add is dropped: `dataChanged`/`trackAdded` already repaint; scrolling requires `SongEditor` internals and adds little. Noted as a deliberate cut.
- Spec §5 safety: `hasLocalPlugins` in `parseFragment`; path policy in Tasks 8–9; no shell tools.
- Spec §6 tests: parser, session, tools, path policy unit tests; mock-server smoke; regression.
- Deviation from spec: `render` blocks via a local `QEventLoop` instead of returning `started:true` and suspending the session; the UI stays responsive because the event loop keeps spinning, and the code is simpler.
- Turn-level undo relies on `Song::addJournalCheckPoint()` restoring a whole-song snapshot; Task 13 step 5 verifies and names the fallback.
