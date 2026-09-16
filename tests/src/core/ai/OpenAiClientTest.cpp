/*
 * OpenAiClientTest.cpp
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

#include "OpenAiClient.h"

#include <QtTest>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>

//! Minimal one-shot HTTP server: records the full request, replies with a fixed response, closes.
class MockServer : public QTcpServer
{
public:
	explicit MockServer(QByteArray response)
		: m_response(std::move(response))
	{
		listen(QHostAddress::LocalHost);
		connect(this, &QTcpServer::newConnection, this, [this] {
			auto sock = nextPendingConnection();
			connect(sock, &QTcpSocket::readyRead, this, [this, sock] {
				request += sock->readAll();
				if (!requestComplete()) { return; }
				sock->write(m_response);
				sock->disconnectFromHost();
			});
		});
	}
	lmms::AiConfig config() const { return {QString("http://127.0.0.1:%1/v1").arg(serverPort()), "key", "gpt"}; }
	QByteArray request;

private:
	//! headers received and Content-Length bytes of body present
	bool requestComplete() const
	{
		int headerEnd = request.indexOf("\r\n\r\n");
		if (headerEnd < 0) { return false; }
		int bodyLength = 0;
		for (const auto& h : request.left(headerEnd).split('\n'))
		{
			if (h.toLower().startsWith("content-length:")) { bodyLength = h.mid(15).trimmed().toInt(); }
		}
		return request.size() - (headerEnd + 4) >= bodyLength;
	}
	QByteArray m_response;
};

class OpenAiClientTest : public QObject
{
	Q_OBJECT
private slots:
	void sseRoundTripEmitsDeltasAndCompleted()
	{
		MockServer server(
			"HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nConnection: close\r\n\r\n"
			"data: {\"choices\":[{\"delta\":{\"role\":\"assistant\",\"content\":\"Hel\"}}]}\n\n"
			"data: {\"choices\":[{\"delta\":{\"content\":\"lo\"},\"finish_reason\":\"stop\"}]}\n\ndata: [DONE]\n\n");
		lmms::OpenAiClient c(server.config());
		QSignalSpy deltas(&c, &lmms::AiClient::textDelta);
		QSignalSpy completed(&c, &lmms::AiClient::completed);
		QSignalSpy failed(&c, &lmms::AiClient::failed);
		c.send(QJsonArray{QJsonObject{{"role", "user"}, {"content", "hi"}}}, QJsonArray{QJsonObject{{"type", "function"}}});
		QVERIFY(completed.wait(5000));
		QCOMPARE(failed.count(), 0);
		QCOMPARE(completed[0][0].toJsonObject()["content"].toString(), QString("Hello"));
		QStringList got;
		for (const auto& d : deltas) { got << d[0].toString(); }
		QCOMPARE(got, QStringList({"Hel", "lo"}));
		QVERIFY(server.request.startsWith("POST /v1/chat/completions"));
		QVERIFY(server.request.contains("Authorization: Bearer key"));
		QVERIFY(server.request.contains("\"stream\":true"));
		QVERIFY(server.request.contains("\"tool_choice\":\"auto\""));
	}
	void errorBodyFailsWithHttpStatus()
	{
		QByteArray body = "{\"error\":{\"message\":\"bad key\"}}";
		MockServer server("HTTP/1.1 401 Unauthorized\r\nContent-Type: application/json\r\nContent-Length: "
			+ QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
		lmms::OpenAiClient c(server.config());
		QSignalSpy failed(&c, &lmms::AiClient::failed);
		QSignalSpy completed(&c, &lmms::AiClient::completed);
		c.send(QJsonArray{}, {});
		QVERIFY(failed.wait(5000));
		QCOMPARE(failed[0][0].toString(), QString("HTTP 401: bad key"));
		QCOMPARE(completed.count(), 0);
	}
	void completeMessageWinsOverConnectionDrop()
	{
		// Content-Length overstates the body, so Qt reports RemoteHostClosedError after a fully parsed stream.
		MockServer server(
			"HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nContent-Length: 9999\r\nConnection: close\r\n\r\n"
			"data: {\"choices\":[{\"delta\":{\"content\":\"ok\"},\"finish_reason\":\"stop\"}]}\n\n");
		lmms::OpenAiClient c(server.config());
		QSignalSpy completed(&c, &lmms::AiClient::completed);
		QSignalSpy failed(&c, &lmms::AiClient::failed);
		c.send(QJsonArray{}, {});
		QVERIFY(completed.wait(5000));
		QCOMPARE(failed.count(), 0);
		QCOMPARE(completed[0][0].toJsonObject()["content"].toString(), QString("ok"));
	}
	void abortEmitsNothing()
	{
		lmms::OpenAiClient c({"http://127.0.0.1:1/v1", "k", "m"});
		QSignalSpy failed(&c, &lmms::AiClient::failed);
		QSignalSpy completed(&c, &lmms::AiClient::completed);
		c.send(QJsonArray{QJsonObject{{"role", "user"}, {"content", "hi"}}}, {});
		c.abort(); // QNetworkReply::abort() emits finished() synchronously
		QVERIFY(!failed.wait(200));
		QCOMPARE(completed.count(), 0);
	}
	void testConnectionRefused()
	{
		lmms::OpenAiClient c({"http://127.0.0.1:1/v1", "k", "m"});
		bool called = false, ok = true;
		QString msg;
		c.testConnection([&](bool o, QString m) { called = true; ok = o; msg = m; });
		QTRY_VERIFY_WITH_TIMEOUT(called, 5000);
		QVERIFY(!ok);
		QVERIFY(msg.startsWith("HTTP 0: "));
	}
};
QTEST_GUILESS_MAIN(OpenAiClientTest)
#include "OpenAiClientTest.moc"
