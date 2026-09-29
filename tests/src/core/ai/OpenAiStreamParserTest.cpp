/*
 * OpenAiStreamParserTest.cpp
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
	void doneWithoutFinishReasonIsTerminal()
	{
		lmms::OpenAiStreamParser p;
		p.feed("data: {\"choices\":[{\"delta\":{\"content\":\"Hel\"}}]}\n\n");
		p.feed("data: {\"choices\":[{\"delta\":{\"content\":\"lo\"}}]}\n\n");
		QVERIFY(!p.finished());
		p.feed("data: [DONE]\n\n");
		QVERIFY(p.finished());
		QVERIFY(p.error().isEmpty());
		QCOMPARE(p.message()["content"].toString(), QString("Hello"));
	}
	void doneAfterErrorEventIsNotTerminal()
	{
		lmms::OpenAiStreamParser p;
		p.feed("data: {\"error\":{\"message\":\"quota\"}}\n\ndata: [DONE]\n\n");
		QVERIFY(!p.finished());
		QCOMPARE(p.error(), QString("quota"));
	}
	void unterminatedTrailingLineDrainedByEnd()
	{
		lmms::OpenAiStreamParser p;
		p.feed("data: {\"choices\":[{\"delta\":{\"content\":\"Hel\"}}]}\n\n");
		p.feed("data: {\"choices\":[{\"delta\":{\"content\":\"lo\"},\"finish_reason\":\"stop\"}]}");
		QVERIFY(!p.finished());
		p.end();
		QVERIFY(p.finished());
		QCOMPARE(p.message()["content"].toString(), QString("Hello"));
		QCOMPARE(p.textDeltas(), QStringList({"Hel", "lo"}));
	}
	void commentLineBeforeFirstData()
	{
		lmms::OpenAiStreamParser p;
		p.feed(": OPENROUTER PROCESSING\n\n");
		p.feed("event: message\nid: 1\ndata: {\"choices\":[{\"delta\":{\"content\":\"Hi\"},\"finish_reason\":\"stop\"}]}\n\n");
		p.end();
		QVERIFY(p.finished());
		QVERIFY(p.error().isEmpty());
		QCOMPARE(p.message()["content"].toString(), QString("Hi"));
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
	void nonJsonBodyErrorCarriesExcerpt()
	{
		lmms::OpenAiStreamParser p;
		p.feed("<html>\n<body>502 Bad Gateway</body>\n</html>\n");
		p.end();
		QVERIFY(!p.finished());
		QCOMPARE(p.error(), QString("Unparseable response: <html>\n<body>502 Bad Gateway</body>\n</html>"));
	}
	void malformedStreamEventIsAnError()
	{
		lmms::OpenAiStreamParser p;
		p.feed("data: {\"choices\":[{\"delta\":{\"content\":\"Hi\"}}]}\n\ndata: {not json\n\n");
		QVERIFY(!p.finished());
		QCOMPARE(p.error(), QString("Malformed stream event: {not json"));
	}
	void finishReasonIsRecorded()
	{
		lmms::OpenAiStreamParser p;
		p.feed("data: {\"choices\":[{\"delta\":{\"role\":\"assistant\",\"content\":\"\"},\"finish_reason\":\"length\"}]}\n\ndata: [DONE]\n\n");
		QVERIFY(p.finished());
		QCOMPARE(p.finishReason(), QString("length"));
		QCOMPARE(p.message()["content"].toString(), QString(""));
	}
};
QTEST_GUILESS_MAIN(OpenAiStreamParserTest)
#include "OpenAiStreamParserTest.moc"
