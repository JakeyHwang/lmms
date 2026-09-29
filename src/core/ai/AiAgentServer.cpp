/*
 * AiAgentServer.cpp - loopback control server dispatching JSON lines into the AI tool registry
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

#include "AiAgentServer.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QTcpServer>
#include <QTcpSocket>

#include "AiToolRegistry.h"
#include "ConfigManager.h"

namespace lmms
{

AiAgentServer::AiAgentServer(QObject* parent) : QObject(parent) {}

AiAgentServer::~AiAgentServer() { stop(); }

QString AiAgentServer::defaultTokenFilePath()
{
	return ConfigManager::inst()->workingDir() + ".lmms-agent.json";
}

bool AiAgentServer::start(AiToolRegistry* registry, const QString& tokenFilePath, quint16 port)
{
	stop();
	m_registry = registry;
	m_server = new QTcpServer(this);
	if (!m_server->listen(QHostAddress::LocalHost, port))
	{
		qWarning("AiAgentServer: could not listen on 127.0.0.1:%u: %s", port, qPrintable(m_server->errorString()));
		stop();
		return false;
	}
	QByteArray raw(32, Qt::Uninitialized);
	QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(raw.data()), raw.size() / int(sizeof(quint32)));
	m_token = QString::fromLatin1(raw.toHex());
	m_tokenFile = tokenFilePath;
	QSaveFile f(m_tokenFile);
	const QJsonObject info{{"port", int(m_server->serverPort())}, {"token", m_token}};
	if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(info).toJson(QJsonDocument::Compact) + '\n') < 0 || !f.commit())
	{
		qWarning("AiAgentServer: could not write %s: %s", qPrintable(m_tokenFile), qPrintable(f.errorString()));
		stop();
		return false;
	}
	connect(m_server, &QTcpServer::newConnection, this, &AiAgentServer::onNewConnection);
	qInfo("AiAgentServer: listening on 127.0.0.1:%u, token file %s", m_server->serverPort(), qPrintable(m_tokenFile));
	return true;
}

void AiAgentServer::stop()
{
	for (auto it = m_buffers.begin(); it != m_buffers.end(); ++it)
	{
		it.key()->disconnect(this);
		it.key()->abort();
		it.key()->deleteLater();
	}
	m_buffers.clear();
	if (m_server)
	{
		m_server->close();
		m_server->deleteLater();
		m_server = nullptr;
	}
	if (!m_tokenFile.isEmpty()) { QFile::remove(m_tokenFile); m_tokenFile.clear(); }
	m_token.clear();
	m_registry = nullptr;
}

bool AiAgentServer::running() const { return m_server && m_server->isListening(); }

quint16 AiAgentServer::port() const { return running() ? m_server->serverPort() : 0; }

void AiAgentServer::onNewConnection()
{
	while (auto socket = m_server->nextPendingConnection())
	{
		m_buffers.insert(socket, {});
		connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
		connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
			m_buffers.remove(socket);
			socket->deleteLater();
		});
	}
}

void AiAgentServer::onReadyRead(QTcpSocket* socket)
{
	auto it = m_buffers.find(socket);
	if (it == m_buffers.end()) { return; }
	it->append(socket->readAll());
	if (m_dispatching) { return; } // a handler is running (e.g. render's nested loop); drained afterwards
	drainAll();
}

void AiAgentServer::drainAll()
{
	m_dispatching = true;
	bool progressed = true;
	while (progressed)
	{
		progressed = false;
		for (auto socket : m_buffers.keys())
		{
			auto it = m_buffers.find(socket);
			if (it == m_buffers.end()) { continue; }
			const int nl = it->indexOf('\n');
			if (nl < 0)
			{
				if (it->size() > MaxLineBytes)
				{
					socket->write(QJsonDocument(QJsonObject{{"id", QJsonValue::Null}, {"error", "line too long"}}).toJson(QJsonDocument::Compact) + '\n');
					socket->disconnectFromHost();
					m_buffers.remove(socket);
					progressed = true;
				}
				continue;
			}
			const QByteArray line = it->left(nl);
			it->remove(0, nl + 1);
			bool closeAfter = false;
			const QByteArray reply = handleLine(line, &closeAfter);  // may run a nested event loop
			if (!m_buffers.contains(socket)) { continue; }           // client went away meanwhile
			socket->write(reply);
			if (closeAfter) { socket->disconnectFromHost(); m_buffers.remove(socket); }
			progressed = true;
		}
	}
	m_dispatching = false;
}

QByteArray AiAgentServer::handleLine(const QByteArray& line, bool* closeAfter)
{
	auto respond = [](const QJsonValue& id, const char* key, const QJsonValue& v) {
		return QJsonDocument(QJsonObject{{"id", id}, {key, v}}).toJson(QJsonDocument::Compact) + '\n';
	};
	if (line.size() > MaxLineBytes) { *closeAfter = true; return respond(QJsonValue::Null, "error", "line too long"); }
	QJsonParseError perr;
	const auto doc = QJsonDocument::fromJson(line, &perr);
	if (!doc.isObject()) { return respond(QJsonValue::Null, "error", "request is not a JSON object: " + perr.errorString()); }
	const QJsonObject req = doc.object();
	const QJsonValue id = req.contains("id") ? req["id"] : QJsonValue::Null;
	if (req["token"].toString() != m_token) { *closeAfter = true; return respond(id, "error", "bad token"); }
	if (!req["tool"].isString() || req["tool"].toString().isEmpty()) { return respond(id, "error", "missing string field 'tool'"); }
	const QString tool = req["tool"].toString();
	if (!m_registry || !m_registry->has(tool)) { return respond(id, "error", "unknown tool '" + tool + "'"); }
	if (req.contains("args") && !req["args"].isObject()) { return respond(id, "error", "'args' must be an object"); }
	return respond(id, "result", m_registry->call(tool, req["args"].toObject()));
}

} // namespace lmms
