/*
 * AiSession.h - AI Composer agent loop: drives an AiClient with tool dispatch
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

#ifndef LMMS_AI_SESSION_H
#define LMMS_AI_SESSION_H

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

#include "AiClient.h"
#include "lmms_export.h"

namespace lmms
{

class AiToolRegistry;

class LMMS_EXPORT AiSession : public QObject
{
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

protected:
	// One song-level journal checkpoint per user turn; virtual so tests can stub them.
	virtual void beginTurnCheckpoint();
	virtual void endTurnCheckpoint();

private:
	void request();
	void onCompleted(const QJsonObject& assistantMessage);
	void onFailed(const QString& err);
	void finishTurn(const QString& text);
	void failTurn(const QString& err);
	void elideIfLarge();
	void answerPendingToolCalls();

	AiClient* m_client;
	AiToolRegistry* m_registry;
	QJsonArray m_history;
	QString m_systemPrompt;
	QStringList m_pendingCalls;      // tool_call ids of the current assistant message not yet answered
	int m_turnStartIndex = 0;
	int m_toolCalls = 0;
	int m_consecutiveErrors = 0;
	bool m_busy = false;
};

} // namespace lmms

#endif // LMMS_AI_SESSION_H
