/*
 * AiAgentServer.h - loopback control server dispatching JSON lines into the AI tool registry
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

#ifndef LMMS_AI_AGENT_SERVER_H
#define LMMS_AI_AGENT_SERVER_H

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>

#include "lmms_export.h"

class QTcpServer;
class QTcpSocket;

namespace lmms
{

class AiToolRegistry;

/*! Loopback control server: newline-delimited JSON requests dispatched into an AiToolRegistry.
 *
 * Request  {"id": any, "token": "...", "tool": "name", "args": {...}}
 * Response {"id": any, "result": {...}} or, for transport problems only, {"id": any, "error": "..."}.
 * Binds 127.0.0.1 only. The port and a per-launch token are written to `tokenFilePath` while
 * running. Handlers run on this object's thread (the GUI thread), one request at a time, in
 * arrival order; bytes that arrive while a handler runs are dispatched after it returns.
 */
class LMMS_EXPORT AiAgentServer : public QObject
{
	Q_OBJECT
public:
	static constexpr int MaxLineBytes = 4 * 1024 * 1024;

	explicit AiAgentServer(QObject* parent = nullptr);
	~AiAgentServer() override;

	//! Listens on 127.0.0.1 (ephemeral port unless `port` > 0) and writes the token file.
	//! False, listening on nothing, when the bind or the file write fails.
	bool start(AiToolRegistry* registry, const QString& tokenFilePath, quint16 port = 0);
	//! Closes every socket and removes the token file.
	void stop();
	bool running() const;
	quint16 port() const;
	QString token() const { return m_token; }
	static QString defaultTokenFilePath();

private:
	void onNewConnection();
	void onReadyRead(QTcpSocket* socket);
	void drainAll();
	//! Handles one complete line; sets `*closeAfter` when the socket must be dropped afterwards.
	QByteArray handleLine(const QByteArray& line, bool* closeAfter);

	QTcpServer* m_server = nullptr;
	AiToolRegistry* m_registry = nullptr;
	QString m_token;
	QString m_tokenFile;
	QHash<QTcpSocket*, QByteArray> m_buffers;
	bool m_dispatching = false;
};

} // namespace lmms

#endif // LMMS_AI_AGENT_SERVER_H
