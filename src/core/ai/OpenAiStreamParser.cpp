/*
 * OpenAiStreamParser.cpp - accumulates an OpenAI chat-completions response
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

#include <QJsonArray>
#include <QJsonDocument>

namespace lmms
{

void OpenAiStreamParser::feed(const QByteArray& chunk)
{
	m_buffer += chunk;
	int nl;
	while ((nl = m_buffer.indexOf('\n')) >= 0)
	{
		// not SSE: keep whole body until end()
		if (!handleLine(m_buffer.left(nl).trimmed())) { return; }
		m_buffer.remove(0, nl + 1);
	}
}

void OpenAiStreamParser::end()
{
	QByteArray body = m_buffer.trimmed();
	m_buffer.clear();
	if (m_sawSse)
	{
		// drain an unterminated trailing line
		if (!body.isEmpty()) { handleLine(body); }
		return;
	}
	if (body.isEmpty()) { return; }
	QJsonParseError err;
	auto doc = QJsonDocument::fromJson(body, &err);
	if (err.error != QJsonParseError::NoError) { m_error = "Unparseable response: " + QString::fromUtf8(body.left(200)); return; }
	auto obj = doc.object();
	if (obj.contains("error")) { m_error = obj["error"].toObject()["message"].toString("unknown error"); return; }
	auto choices = obj["choices"].toArray();
	if (choices.isEmpty()) { m_error = "Response has no choices"; return; }
	handleChoice(choices[0].toObject(), false);
	m_finished = true;
}

bool OpenAiStreamParser::handleLine(const QByteArray& line)
{
	if (line.startsWith("data:"))
	{
		m_sawSse = true;
		QByteArray data = line.mid(5).trimmed();
		if (data == "[DONE]") { if (m_error.isEmpty()) { m_finished = true; } }
		else { handleEvent(data); }
		return true;
	}
	if (line.startsWith(':') || line.startsWith("event:") || line.startsWith("id:") || line.startsWith("retry:"))
	{
		// SSE comment/keepalive (e.g. ": OPENROUTER PROCESSING") or a field we don't use
		m_sawSse = true;
		return true;
	}
	return line.isEmpty() || m_sawSse;
}

void OpenAiStreamParser::handleEvent(const QByteArray& data)
{
	QJsonParseError err;
	auto doc = QJsonDocument::fromJson(data, &err);
	if (err.error != QJsonParseError::NoError) { m_error = "Malformed stream event: " + QString::fromUtf8(data.left(120)); return; }
	auto obj = doc.object();
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
	if (!choice["finish_reason"].isNull() && choice["finish_reason"].isString())
	{
		m_finished = true;
		m_finishReason = choice["finish_reason"].toString();
	}
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
	QStringList out;
	out.swap(m_pendingDeltas);
	return out;
}

} // namespace lmms
