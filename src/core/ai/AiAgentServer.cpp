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
#include <QList>
#include <QPointer>
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
	QSaveFile f(tokenFilePath);
	const QJsonObject info{{"port", int(m_server->serverPort())}, {"token", m_token}};
	if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(info).toJson(QJsonDocument::Compact) + '\n') < 0 || !f.commit())
	{
		qWarning("AiAgentServer: could not write %s: %s", qPrintable(tokenFilePath), qPrintable(f.errorString()));
		stop(); // m_tokenFile is still empty, so nothing at tokenFilePath is touched
		return false;
	}
	m_tokenFile = tokenFilePath;
	// Defence in depth: keep the port/token pair out of other accounts' reach. On Windows this only
	// maps onto the read-only attribute, so the call is best effort and its result is not checked.
	QFile::setPermissions(m_tokenFile, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
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
	if (!m_tokenFile.isEmpty())
	{
		// Windows refuses to unlink a file that anyone still holds open without FILE_SHARE_DELETE.
		// Leaving a readable port/token behind is worse than leaving an empty file, so truncate it.
		if (!QFile::remove(m_tokenFile) && QFile::exists(m_tokenFile))
		{
			QFile stale(m_tokenFile);
			if (stale.open(QIODevice::WriteOnly | QIODevice::Truncate))
			{
				qWarning("AiAgentServer: could not remove %s; truncated it instead", qPrintable(m_tokenFile));
			}
			else
			{
				qWarning("AiAgentServer: could not remove or truncate %s: %s", qPrintable(m_tokenFile),
					qPrintable(stale.errorString()));
			}
		}
		m_tokenFile.clear();
	}
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
	// Cap the unterminated tail here, not in drainAll: while a handler runs (render's nested loop)
	// drainAll is suppressed, and a client that never sends a newline would grow the buffer forever.
	if (it->size() > MaxLineBytes && it->size() - (it->lastIndexOf('\n') + 1) > MaxLineBytes)
	{
		socket->write(QJsonDocument(QJsonObject{{"id", QJsonValue::Null}, {"error", "line too long"}})
						  .toJson(QJsonDocument::Compact)
			+ '\n');
		socket->disconnectFromHost();
		m_buffers.remove(socket);
		return;
	}
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
		// A copy, and guarded: a handler may run a nested event loop in which sockets are accepted,
		// disconnected and deleted. QPointer keeps a freed socket from matching a new one allocated
		// at the same address (the lookups below are by address).
		QList<QPointer<QTcpSocket>> sockets;
		sockets.reserve(m_buffers.size());
		for (auto socket : m_buffers.keys()) { sockets.append(socket); }
		for (const QPointer<QTcpSocket>& sock : sockets)
		{
			if (!sock) { continue; }
			auto it = m_buffers.find(sock.data());
			if (it == m_buffers.end()) { continue; }
			const qsizetype nl = it->indexOf('\n');
			if (nl < 0) { continue; } // over-long unterminated lines are dropped in onReadyRead
			const QByteArray line = it->left(nl);
			it->remove(0, nl + 1);
			bool closeAfter = false;
			const QByteArray reply = handleLine(line, &closeAfter); // may run a nested event loop
			progressed = true;                                      // the line was consumed either way
			// Client went away meanwhile; `it` may also be dangling by now, so it is not reused.
			if (!sock || !m_buffers.contains(sock.data())) { continue; }
			sock->write(reply);
			if (closeAfter)
			{
				sock->disconnectFromHost();
				m_buffers.remove(sock.data());
			}
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
