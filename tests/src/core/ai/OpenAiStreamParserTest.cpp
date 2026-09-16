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
