/*
 * OpenAiStreamParser.h - accumulates an OpenAI chat-completions response
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

#ifndef LMMS_OPENAI_STREAM_PARSER_H
#define LMMS_OPENAI_STREAM_PARSER_H

#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QStringList>

#include "lmms_export.h"

namespace lmms
{

//! Accumulates an OpenAI chat-completions response (SSE stream or plain JSON) into one assistant message.
class LMMS_EXPORT OpenAiStreamParser
{
public:
	void feed(const QByteArray& chunk);   //!< call for each network chunk
	void end();                           //!< call when the body is complete (handles non-SSE bodies)
	bool finished() const { return m_finished; }
	QString error() const { return m_error; }
	QJsonObject message() const;          //!< {role, content, tool_calls?}
	QStringList takeTextDeltas();         //!< deltas since last call
	QStringList textDeltas() const { return m_allDeltas; }

private:
	struct ToolCall { QString id; QString name; QString arguments; };
	bool handleLine(const QByteArray& line); //!< false: line is not SSE and no SSE seen yet
	void handleEvent(const QByteArray& data);
	void handleChoice(const QJsonObject& choice, bool streaming);

	QByteArray m_buffer;
	QString m_content;
	QMap<int, ToolCall> m_toolCalls;
	QStringList m_pendingDeltas, m_allDeltas;
	QString m_error;
	bool m_finished = false;
	bool m_sawSse = false;
};

} // namespace lmms

#endif
