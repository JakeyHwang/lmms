/*
 * AiChatView.h - chat panel for the AI Composer
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

#ifndef LMMS_GUI_AI_CHAT_VIEW_H
#define LMMS_GUI_AI_CHAT_VIEW_H

#include <QWidget>

#include "AiPathPolicy.h"
#include "AiToolRegistry.h"

class QJsonObject;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTextBrowser;

namespace lmms
{
class AiSession;
class OpenAiClient;
}

namespace lmms::gui
{

//! MDI sub-window hosting the AI Composer chat: a "not configured" banner or the transcript/input UI.
class LMMS_EXPORT AiChatView : public QWidget
{
	Q_OBJECT
public:
	AiChatView();
	~AiChatView() override = default;

public slots:
	//! Re-read AiConfig (called after the Settings dialog closes with OK).
	void reloadConfig();

protected:
	bool eventFilter(QObject* obj, QEvent* ev) override;

private slots:
	void sendOrStop();
	void newChat();
	void revertTurn();
	void onTextDelta(const QString& text);
	void onToolStarted(const QString& name, const QJsonObject& args);
	void onToolFinished(const QString& name, const QJsonObject& result);
	void onTurnFinished(const QString& text);
	void onTurnFailed(const QString& error);
	void onStatus(const QString& text);

private:
	void appendBlock(const QString& html);
	void appendAssistantBlock(const QString& text);
	void refreshPolicyRoots();
	void setBusyUi(bool busy);

	QStackedWidget* m_stack;
	QWidget* m_banner;
	QWidget* m_chat;
	QTextBrowser* m_transcript;
	QPlainTextEdit* m_input;
	QPushButton* m_sendStop;
	QPushButton* m_newChat;
	QPushButton* m_revertTurn;
	QLabel* m_status;

	// The registry's tool handlers capture m_policy by reference: keep the policy declared first.
	AiPathPolicy m_policy;
	AiToolRegistry m_registry;
	OpenAiClient* m_client;
	AiSession* m_session;
	QString m_streaming;   // assistant text streamed so far in the current turn
};

} // namespace lmms::gui

#endif // LMMS_GUI_AI_CHAT_VIEW_H
