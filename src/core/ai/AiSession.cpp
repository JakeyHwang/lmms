/*
 * AiSession.cpp - AI Composer agent loop: drives an AiClient with tool dispatch
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

#include <QJsonDocument>

#include "AiToolRegistry.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "Song.h"

namespace lmms
{

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
	m_toolCalls = 0;
	m_consecutiveErrors = 0;
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
	const auto calls = msg["tool_calls"].toArray();
	if (calls.isEmpty()) { finishTurn(msg["content"].toString()); return; }
	for (const auto& v : calls)
	{
		const auto call = v.toObject();
		const auto fn = call["function"].toObject();
		const QString name = fn["name"].toString();
		QJsonParseError perr;
		const auto argsDoc = QJsonDocument::fromJson(fn["arguments"].toString().toUtf8(), &perr);
		const QJsonObject args = perr.error == QJsonParseError::NoError ? argsDoc.object() : QJsonObject{};
		if (++m_toolCalls > MaxToolCallsPerTurn)
		{
			failTurn(tr("Stopped: more than %1 tool calls in one turn").arg(MaxToolCallsPerTurn));
			return;
		}
		emit status(tr("Running %1…").arg(name));
		emit toolCallStarted(name, args);
		const QJsonObject result = perr.error == QJsonParseError::NoError
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
	m_busy = false;
	endTurnCheckpoint();
	emit status(tr("Done (%1 tool calls)").arg(m_toolCalls));
	emit turnFinished(text);
}

void AiSession::failTurn(const QString& err)
{
	m_busy = false;
	endTurnCheckpoint();
	emit status(err);
	emit turnFailed(err);
}

void AiSession::elideIfLarge()
{
	const auto size = [this] { return QJsonDocument(m_history).toJson(QJsonDocument::Compact).size(); };
	for (int i = 1; i < m_history.size() && size() > MaxRequestChars; ++i)
	{
		auto m = m_history[i].toObject();
		if (m["role"].toString() == "tool" && m["content"].toString() != "[elided]")
		{
			m["content"] = "[elided]";
			m_history[i] = m;
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

} // namespace lmms
