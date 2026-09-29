/*
 * AiSessionTest.cpp - tests for the AiSession agent loop
 *
 * Copyright (c) 2026 LMMS Developers <lmms-devel@lists.sourceforge.net>
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#include "AiSession.h"
#include "AiToolRegistry.h"
#include "Engine.h"
#include "Song.h"
#include "Track.h"

#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>

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

// Every tool_call id of each assistant message must be answered by the tool
// messages that immediately follow it (OpenAI rejects the history otherwise).
static bool allToolCallsAnswered(const QJsonArray& h)
{
	for (int i = 0; i < h.size(); ++i)
	{
		const auto m = h[i].toObject();
		if (m["role"].toString() != "assistant") { continue; }
		QStringList answered;
		for (int j = i + 1; j < h.size() && h[j].toObject()["role"].toString() == "tool"; ++j)
			answered << h[j].toObject()["tool_call_id"].toString();
		for (const auto& c : m["tool_calls"].toArray())
			if (!answered.contains(c.toObject()["id"].toString())) { return false; }
	}
	return true;
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
	lmms::AiSession* current = nullptr;   // target of the "stopper" tool
private slots:
	void initTestCase()
	{
		lmms::Engine::init(true);
		reg.add({"a", "", {}, [this](const QJsonObject&){ calls << "a"; return lmms::AiToolRegistry::ok(); }});
		reg.add({"b", "", {}, [this](const QJsonObject&){ calls << "b"; return lmms::AiToolRegistry::ok({{"big", QString(200000, 'x')}}); }});
		reg.add({"bad", "", {}, [this](const QJsonObject&){ calls << "bad"; return lmms::AiToolRegistry::error("no"); }});
		reg.add({"stopper", "", {}, [this](const QJsonObject&){ calls << "stopper"; current->stop(); return lmms::AiToolRegistry::ok(); }});
		reg.add({"mutate", "", {}, [this](const QJsonObject&){
			calls << "mutate";
			auto song = lmms::Engine::getSong();
			song->setTempo(99);
			lmms::Track::create(lmms::Track::Type::Instrument, song);
			song->setModified();
			return lmms::AiToolRegistry::ok();
		}});
	}
	void cleanupTestCase() { lmms::Engine::destroy(); }
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
		for (int i = 0; i < 10; ++i) c.scripted << toolCallMsg({"bad", "bad", "bad"});
		QSignalSpy failed(&s, &lmms::AiSession::turnFailed);
		s.submit("go");
		QVERIFY(failed.wait(2000));
		QCOMPARE(calls.size(), lmms::AiSession::MaxConsecutiveToolErrors);
		QVERIFY(allToolCallsAnswered(s.history())); // the unrun 3rd call of the last message got a stub reply
		QVERIFY(!s.busy());
	}
	void stopsAtToolCallCap()
	{
		FakeAiClient c; TestSession s(&c, &reg);
		// Three calls per message; one message more than the cap needs, so the cap and not the script ends the turn.
		for (int i = 0; i <= lmms::AiSession::MaxToolCallsPerTurn / 3; ++i) c.scripted << toolCallMsg({"a", "a", "a"});
		QSignalSpy failed(&s, &lmms::AiSession::turnFailed);
		s.submit("go");
		QVERIFY(failed.wait(5000));
		QCOMPARE(calls.size(), lmms::AiSession::MaxToolCallsPerTurn);
		QVERIFY(allToolCallsAnswered(s.history()));
		QVERIFY(!s.busy());
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
	void stopDuringToolDispatchAbortsTurn()
	{
		FakeAiClient c; TestSession s(&c, &reg);
		current = &s;
		c.scripted << toolCallMsg({"stopper", "a"}) << toolCallMsg({"a"});
		QSignalSpy failed(&s, &lmms::AiSession::turnFailed);
		s.submit("go");
		QVERIFY(failed.wait(2000));
		QTest::qWait(50); // any further request would have been answered by now
		QCOMPARE(calls, QStringList({"stopper"}));
		QCOMPARE(c.requests.size(), 1);
		QCOMPARE(failed.count(), 1);
		QCOMPARE(failed[0][0].toString(), QString("Stopped"));
		QVERIFY(allToolCallsAnswered(s.history()));
		QVERIFY(!s.busy());
	}
	void clearWhileBusyStopsTurn()
	{
		FakeAiClient c; TestSession s(&c, &reg);
		c.scripted << toolCallMsg({"a"});
		QSignalSpy failed(&s, &lmms::AiSession::turnFailed);
		s.submit("go");
		s.clear(); // request still in flight
		QCOMPARE(failed.count(), 1);
		QCOMPARE(s.history().size(), 1);
		QVERIFY(!s.busy());
		QTest::qWait(50); // the in-flight completion arrives and must be ignored
		QCOMPARE(calls, QStringList());
		QCOMPARE(s.history().size(), 1);
	}
	void emptyToolCallsArrayIsPlainReply()
	{
		FakeAiClient c; TestSession s(&c, &reg);
		c.scripted << QJsonObject{{"role","assistant"},{"content","hi"},{"tool_calls", QJsonArray{}}};
		QSignalSpy fin(&s, &lmms::AiSession::turnFinished);
		s.submit("go");
		QVERIFY(fin.wait(2000));
		QCOMPARE(fin[0][0].toString(), QString("hi"));
		QCOMPARE(s.history().size(), 3);
		QVERIFY(!s.history()[2].toObject().contains("tool_calls"));
	}
	void emptyCompletionFailsTurn()
	{
		// finish_reason:length with no content and no tool calls: the provider
		// cut the stream off (e.g. reasoning burned the output budget) and the
		// user must be told, not shown a silent "Done".
		FakeAiClient c; TestSession s(&c, &reg);
		c.scripted << QJsonObject{{"role", "assistant"}, {"content", ""}};
		QSignalSpy failed(&s, &lmms::AiSession::turnFailed);
		QSignalSpy fin(&s, &lmms::AiSession::turnFinished);
		s.submit("go");
		QVERIFY(failed.wait(2000));
		QCOMPARE(failed.count(), 1);
		QCOMPARE(fin.count(), 0);
		QVERIFY(failed[0][0].toString().contains("token limit"));
		// history rolled back to the system prompt; the empty reply must not
		// be sent back as an assistant message on retry
		QCOMPARE(s.history().size(), 1);
		QVERIFY(!s.busy());
	}
	void revertLastTurnRestoresProjectSnapshot()
	{
		QTemporaryDir dir;
		auto song = lmms::Engine::getSong();
		song->createNewProject(); // the only public way to an unmodified song
		song->setProjectFileName(dir.path() + "/demo.mmp");
		QVERIFY(!song->isModified());
		const auto tracksBefore = song->tracks().size();
		const auto tempoBefore = song->getTempo();
		QVERIFY(tempoBefore != 99);

		FakeAiClient c; lmms::AiSession s(&c, &reg); // real checkpoint hooks
		QVERIFY(!s.canRevertLastTurn());
		c.scripted << toolCallMsg({"mutate"}) << QJsonObject{{"role","assistant"},{"content","changed"}};
		QSignalSpy fin(&s, &lmms::AiSession::turnFinished);
		s.submit("go");
		QVERIFY(fin.wait(2000));
		QCOMPARE(song->tracks().size(), tracksBefore + 1);
		QCOMPARE(song->getTempo(), lmms::bpm_t(99));
		QVERIFY(song->isModified());
		QVERIFY(s.canRevertLastTurn());

		QSignalSpy status(&s, &lmms::AiSession::status);
		QVERIFY(s.revertLastTurn());
		QCOMPARE(song->tracks().size(), tracksBefore);
		QCOMPARE(song->getTempo(), tempoBefore);
		QCOMPARE(song->projectFileName(), dir.path() + "/demo.mmp");
		QVERIFY(!song->isModified());
		QVERIFY(!s.canRevertLastTurn());
		QVERIFY(!s.revertLastTurn());
		QCOMPARE(status.count(), 1);
		QVERIFY(status[0][0].toString().contains("Reverted"));
		QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(), 0); // temp snapshot file removed

		// A project that was already modified before the turn stays modified after the revert.
		song->setModified();
		c.scripted.clear();
		c.scripted << toolCallMsg({"mutate"}) << QJsonObject{{"role","assistant"},{"content","changed"}};
		c.requests.clear();
		s.submit("again");
		QVERIFY(fin.wait(2000));
		QVERIFY(s.revertLastTurn());
		QCOMPARE(song->tracks().size(), tracksBefore);
		QVERIFY(song->isModified());
	}
	void revertRefusedWhileBusyAndAfterToolLessTurn()
	{
		FakeAiClient c; lmms::AiSession s(&c, &reg);
		c.scripted << toolCallMsg({"mutate"}) << QJsonObject{{"role","assistant"},{"content","done"}};
		QSignalSpy fin(&s, &lmms::AiSession::turnFinished);
		s.submit("go");
		QVERIFY(s.busy());
		QVERIFY(!s.canRevertLastTurn());
		QVERIFY(!s.revertLastTurn());
		QVERIFY(fin.wait(2000));
		QVERIFY(s.canRevertLastTurn());
		// a turn that ran no tool cannot have changed the project: nothing to revert
		c.scripted.clear();
		c.scripted << QJsonObject{{"role","assistant"},{"content","just talk"}};
		c.requests.clear();
		s.submit("again");
		QVERIFY(fin.wait(2000));
		QVERIFY(!s.canRevertLastTurn());
	}
};

QTEST_GUILESS_MAIN(AiSessionTest)
#include "AiSessionTest.moc"
