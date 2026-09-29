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

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryFile>
#include <QTextStream>

#include "AiToolRegistry.h"
#include "DataFile.h"
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
	if (m_busy) { stop(); }
	m_history = QJsonArray{QJsonObject{{"role", "system"}, {"content", m_systemPrompt}}};
}

bool AiSession::busy() const { return m_busy; }
const QJsonArray& AiSession::history() const { return m_history; }
bool AiSession::canRevertLastTurn() const { return !m_busy && !m_snapshot.isEmpty(); }

void AiSession::submit(const QString& userText)
{
	if (m_busy) { return; }
	m_busy = true;
	m_turnStartIndex = m_history.size();
	m_toolCalls = 0;
	m_consecutiveErrors = 0;
	m_pendingCalls.clear();
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

void AiSession::onCompleted(const QJsonObject& assistantMessage)
{
	if (!m_busy) { return; }
	QJsonObject msg = assistantMessage;
	const auto calls = msg["tool_calls"].toArray();
	if (calls.isEmpty())
	{
		msg.remove("tool_calls"); // an empty array is a plain-text reply; don't send it back
		if (msg["content"].toString().trimmed().isEmpty())
		{
			// nothing to keep: roll back like a failure so a retry starts clean
			while (m_history.size() > m_turnStartIndex) { m_history.removeLast(); }
			failTurn(tr("The model ended its reply without any content or tool calls — "
						"its output was most likely cut off by the provider's token limit. "
						"Increase 'Max tokens' in Settings > AI and try again."));
			return;
		}
		m_history.append(msg);
		finishTurn(msg["content"].toString());
		return;
	}
	m_history.append(msg);
	for (const auto& v : calls) { m_pendingCalls << v.toObject()["id"].toString(); }
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
		if (!m_busy) { return; } // stop() from a slot
		const QJsonObject result = perr.error == QJsonParseError::NoError
			? m_registry->call(name, args)
			: AiToolRegistry::error("Arguments are not valid JSON: " + perr.errorString());
		if (!m_busy) { return; } // stop() from inside the handler; failTurn already answered the call
		m_consecutiveErrors = result["ok"].toBool() ? 0 : m_consecutiveErrors + 1;
		m_history.append(QJsonObject{{"role", "tool"}, {"tool_call_id", call["id"].toString()},
			{"content", QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact))}});
		m_pendingCalls.removeFirst();
		emit toolCallFinished(name, result);
		if (!m_busy) { return; }
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
	answerPendingToolCalls();
	endTurnCheckpoint();
	emit status(err);
	emit turnFailed(err);
}

// Keeps history OpenAI-valid when a turn ends mid-dispatch: every tool_call id
// of the last assistant message must have a tool reply before the next request.
void AiSession::answerPendingToolCalls()
{
	for (const auto& id : m_pendingCalls)
	{
		m_history.append(QJsonObject{{"role", "tool"}, {"tool_call_id", id},
			{"content", "{\"ok\":false,\"error\":\"turn stopped\"}"}});
	}
	m_pendingCalls.clear();
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

// The project journal cannot restore a whole song (Song::restoreState tears tracks out from
// under their views and never covers <head>), so a turn is reverted by reloading a full
// project snapshot taken before it, through the same path as File > Open.
void AiSession::beginTurnCheckpoint()
{
	m_snapshot.clear();
	Song* song = Engine::getSong();
	if (!song) { return; }
	m_snapshotFileName = song->projectFileName();
	m_snapshotModified = song->isModified();
	DataFile df(DataFile::Type::SongProject);
	song->saveProjectData(df);
	QTextStream ts(&m_snapshot, QIODevice::WriteOnly);
	df.write(ts);
	ts.flush();
	Engine::projectJournal()->setJournalling(false);
}

void AiSession::endTurnCheckpoint()
{
	if (m_toolCalls == 0) { m_snapshot.clear(); } // nothing could have changed; don't offer a pointless reload
	if (!Engine::getSong()) { return; }
	Engine::projectJournal()->setJournalling(true);
}

bool AiSession::revertLastTurn()
{
	Song* song = Engine::getSong();
	if (!canRevertLastTurn() || !song) { return false; }
	// Next to the project file so "local:" resource paths resolve; no .mmp suffix so the
	// temporary file stays out of the recent-projects list.
	const QString dir = m_snapshotFileName.isEmpty() ? QDir::tempPath() : QFileInfo(m_snapshotFileName).absolutePath();
	QTemporaryFile tmp(dir + "/lmms-ai-turn-XXXXXX");
	if (!tmp.open())
	{
		tmp.setFileTemplate(QDir::tempPath() + "/lmms-ai-turn-XXXXXX");
		if (!tmp.open()) { return false; }
	}
	tmp.write(m_snapshot);
	tmp.close(); // still auto-removed on destruction
	song->loadProject(tmp.fileName()); // leaves the song unmodified
	song->setProjectFileName(m_snapshotFileName);
	if (m_snapshotModified) { song->setModified(); }
	m_snapshot.clear();
	emit status(tr("Reverted last AI turn"));
	return true;
}

} // namespace lmms
