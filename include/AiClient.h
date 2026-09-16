/*
 * AiClient.h - abstract LLM transport for the AI Composer
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

#ifndef LMMS_AI_CLIENT_H
#define LMMS_AI_CLIENT_H

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

#include "lmms_export.h"

namespace lmms
{

class LMMS_EXPORT AiClient : public QObject
{
	Q_OBJECT
public:
	using QObject::QObject;
	virtual void send(const QJsonArray& messages, const QJsonArray& tools) = 0;
	virtual void abort() = 0;

signals:
	void textDelta(const QString& text);
	void completed(const QJsonObject& assistantMessage); // OpenAI "message" object: role, content, tool_calls?
	void failed(const QString& error);
};

} // namespace lmms

#endif
