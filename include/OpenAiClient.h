/*
 * OpenAiClient.h - OpenAI-compatible chat-completions client
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

#ifndef LMMS_OPENAI_CLIENT_H
#define LMMS_OPENAI_CLIENT_H

#include <functional>
#include <memory>

#include "AiClient.h"
#include "AiConfig.h"
#include "OpenAiStreamParser.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace lmms
{

class LMMS_EXPORT OpenAiClient : public AiClient
{
	Q_OBJECT
public:
	explicit OpenAiClient(AiConfig config, QObject* parent = nullptr);
	void setConfig(AiConfig config) { m_config = std::move(config); }
	void send(const QJsonArray& messages, const QJsonArray& tools) override;
	void abort() override;
	//! GET {baseUrl}/models; calls back with (ok, message). Used by the settings page.
	void testConnection(std::function<void(bool, QString)> done);

private:
	AiConfig m_config;
	QNetworkAccessManager* m_nam;
	QNetworkReply* m_reply = nullptr;
	std::unique_ptr<OpenAiStreamParser> m_parser;
};

} // namespace lmms

#endif
