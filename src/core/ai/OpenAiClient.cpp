/*
 * OpenAiClient.cpp - OpenAI-compatible chat-completions client
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

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace lmms
{

OpenAiClient::OpenAiClient(AiConfig config, QObject* parent)
	: AiClient(parent), m_config(std::move(config)), m_nam(new QNetworkAccessManager(this)) {}

void OpenAiClient::send(const QJsonArray& messages, const QJsonArray& tools)
{
	abort();
	m_parser = std::make_unique<OpenAiStreamParser>();
	QJsonObject body{{"model", m_config.model}, {"messages", messages}, {"stream", true}};
	if (m_config.maxTokens > 0) { body["max_tokens"] = m_config.maxTokens; }
	// Qwen/vLLM/SGLang-style gateways read this out of the chat template. Providers that do not
	// recognise it ignore it, so it is only sent when the user explicitly asked for it.
	if (m_config.disableThinking) { body["chat_template_kwargs"] = QJsonObject{{"enable_thinking", false}}; }
	if (!tools.isEmpty()) { body["tools"] = tools; body["tool_choice"] = "auto"; }
	QNetworkRequest req(QUrl(m_config.baseUrl + "/chat/completions"));
	req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	req.setRawHeader("Authorization", ("Bearer " + m_config.apiKey).toUtf8());
	req.setRawHeader("Accept", "text/event-stream, application/json");
	auto reply = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
	m_reply = reply;
	connect(reply, &QIODevice::readyRead, this, [this, reply] {
		m_parser->feed(reply->readAll());
		for (const auto& d : m_parser->takeTextDeltas()) { emit textDelta(d); }
	});
	connect(reply, &QNetworkReply::finished, this, [this, reply] {
		m_reply = nullptr;
		reply->deleteLater();
		if (reply->error() == QNetworkReply::OperationCanceledError) { return; }
		m_parser->feed(reply->readAll());
		m_parser->end();
		for (const auto& d : m_parser->takeTextDeltas()) { emit textDelta(d); }
		int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (!m_parser->error().isEmpty()) { emit failed(QString("HTTP %1: %2").arg(status).arg(m_parser->error())); return; }
		// a fully parsed message wins over a late connection drop
		if (m_parser->finished()) { emit completed(m_parser->message()); return; }
		if (reply->error() != QNetworkReply::NoError) { emit failed(QString("HTTP %1: %2").arg(status).arg(reply->errorString())); return; }
		emit failed("Incomplete response from model");
	});
}

void OpenAiClient::abort()
{
	if (!m_reply) { return; }
	auto r = m_reply;
	m_reply = nullptr;
	// QNetworkReply::abort() emits finished() synchronously; detach our slots first
	// so an aborted request never reaches the completion path above.
	r->disconnect(this);
	r->abort();
	r->deleteLater();
}

void OpenAiClient::testConnection(std::function<void(bool, QString)> done)
{
	QNetworkRequest req(QUrl(m_config.baseUrl + "/models"));
	req.setRawHeader("Authorization", ("Bearer " + m_config.apiKey).toUtf8());
	auto reply = m_nam->get(req);
	connect(reply, &QNetworkReply::finished, this, [reply, done = std::move(done)] {
		reply->deleteLater();
		int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (reply->error() != QNetworkReply::NoError) { done(false, QString("HTTP %1: %2").arg(status).arg(reply->errorString())); return; }
		done(true, QString("OK (HTTP %1)").arg(status));
	});
}

} // namespace lmms
