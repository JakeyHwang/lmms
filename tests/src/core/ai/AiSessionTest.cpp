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

#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
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
