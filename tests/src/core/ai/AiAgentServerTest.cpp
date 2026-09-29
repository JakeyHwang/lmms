/*
 * AiAgentServerTest.cpp - tests for AiAgentServer
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
		f.close(); // Windows refuses to remove a file while a handle is open
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
