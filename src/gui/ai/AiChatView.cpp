/*
 * AiChatView.cpp - chat panel for the AI Composer
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

#include "AiChatView.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

#include "AiConfig.h"
#include "AiPromptBuilder.h"
#include "AiSession.h"
#include "AiTools.h"
#include "ConfigManager.h"
#include "embed.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "OpenAiClient.h"
#include "Song.h"
#include "SubWindow.h"

namespace lmms::gui
{

AiChatView::AiChatView() :
	QWidget{}
{
	setWindowIcon(embed::getIconPixmap("project_notes"));
	setWindowTitle(tr("AI Composer"));

	auto pathAllowed = [this](const QString& p) { return m_policy.allows(p); };
	registerAiProjectTools(m_registry, pathAllowed);
	registerAiDiscoveryTools(m_registry, m_policy);
	registerAiActionTools(m_registry, pathAllowed);

	m_client = new OpenAiClient(AiConfig::load(), this);
	m_session = new AiSession(m_client, &m_registry, this);
	m_session->setSystemPrompt(buildAiSystemPrompt());
	connect(m_session, &AiSession::assistantTextDelta, this, &AiChatView::onTextDelta);
	connect(m_session, &AiSession::toolCallStarted, this, &AiChatView::onToolStarted);
	connect(m_session, &AiSession::toolCallFinished, this, &AiChatView::onToolFinished);
	connect(m_session, &AiSession::turnFinished, this, &AiChatView::onTurnFinished);
	connect(m_session, &AiSession::turnFailed, this, &AiChatView::onTurnFailed);
	connect(m_session, &AiSession::status, this, &AiChatView::onStatus);

	m_stack = new QStackedWidget(this);

	// Banner shown until an endpoint is configured.
	m_banner = new QWidget;
	auto bannerLayout = new QVBoxLayout(m_banner);
	auto bannerLabel = new QLabel(tr("No AI endpoint configured. <a href=\"settings\">Open Settings → AI</a> "
		"to set a base URL, API key and model."));
	bannerLabel->setWordWrap(true);
	bannerLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
	connect(bannerLabel, &QLabel::linkActivated, this, [] { getGUI()->mainWindow()->showSettingsDialog(); });
	bannerLayout->addWidget(bannerLabel);
	bannerLayout->addStretch();

	// Chat UI.
	m_chat = new QWidget;
	auto chatLayout = new QVBoxLayout(m_chat);
	m_transcript = new QTextBrowser;
	m_transcript->setOpenLinks(false);
	m_status = new QLabel;
	m_input = new QPlainTextEdit;
	m_input->setPlaceholderText(tr("Describe the music you want… (Enter to send, Shift+Enter for a new line)"));
	m_input->setFixedHeight(70);
	m_input->installEventFilter(this);
	m_sendStop = new QPushButton(tr("Send"));
	m_newChat = new QPushButton(tr("New chat"));
	m_revertTurn = new QPushButton(tr("Revert turn"));
	m_revertTurn->setToolTip(tr("Restore the project to the state before the last AI turn "
		"(also discards edits made after it)"));
	connect(m_sendStop, &QPushButton::clicked, this, &AiChatView::sendOrStop);
	connect(m_newChat, &QPushButton::clicked, this, &AiChatView::newChat);
	connect(m_revertTurn, &QPushButton::clicked, this, &AiChatView::revertTurn);
	auto buttonRow = new QHBoxLayout;
	buttonRow->addWidget(m_sendStop);
	buttonRow->addWidget(m_newChat);
	buttonRow->addWidget(m_revertTurn);
	buttonRow->addStretch();
	chatLayout->addWidget(m_transcript, 1);
	chatLayout->addWidget(m_status);
	chatLayout->addWidget(m_input);
	chatLayout->addLayout(buttonRow);

	m_stack->addWidget(m_banner);
	m_stack->addWidget(m_chat);
	auto layout = new QVBoxLayout(this);
	layout->addWidget(m_stack);
	reloadConfig();
	setBusyUi(false);

	SubWindow* subWin = getGUI()->mainWindow()->addWindowedWidget(this);
	setMinimumSize(420, 320);
	subWin->setAttribute(Qt::WA_DeleteOnClose, false);
	subWin->resize(520, 640);
	subWin->move(700, 5);
	subWin->hide();
}

bool AiChatView::eventFilter(QObject* obj, QEvent* ev)
{
	if (obj == m_input && ev->type() == QEvent::KeyPress)
	{
		auto ke = static_cast<QKeyEvent*>(ev);
		if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) && !(ke->modifiers() & Qt::ShiftModifier))
		{
			// Enter never submits while a turn is running: Stop is a deliberate click.
			if (!m_session->busy()) { sendOrStop(); }
			return true;
		}
	}
	return QWidget::eventFilter(obj, ev);
}

void AiChatView::reloadConfig()
{
	const AiConfig cfg = AiConfig::load();
	m_client->setConfig(cfg);
	m_stack->setCurrentWidget(cfg.configured() ? m_chat : m_banner);
	refreshPolicyRoots();
}

void AiChatView::refreshPolicyRoots()
{
	const auto cm = ConfigManager::inst();
	QStringList roots{cm->dataDir(), cm->workingDir(), cm->factoryPresetsDir(), cm->userPresetsDir(),
		cm->factorySamplesDir(), cm->userSamplesDir()};
	const QString& projectFile = Engine::getSong()->projectFileName();
	if (!projectFile.isEmpty()) { roots << QFileInfo(projectFile).absolutePath(); }
	m_policy.setRoots(roots);
}

void AiChatView::setBusyUi(bool busy)
{
	m_sendStop->setText(busy ? tr("Stop") : tr("Send"));
	m_newChat->setEnabled(!busy);
	m_revertTurn->setEnabled(!busy && m_session->canRevertLastTurn());
}

void AiChatView::sendOrStop()
{
	if (m_session->busy())
	{
		m_session->stop();
		return;
	}
	const QString text = m_input->toPlainText().trimmed();
	if (text.isEmpty()) { return; }
	m_input->clear();
	refreshPolicyRoots();
	m_policy.allowFromUserText(text);
	appendBlock("<div style='margin:6px 0;padding:6px;background:#2b3a4a;border-radius:6px'><b>You</b><br>"
		+ text.toHtmlEscaped().replace("\n", "<br>") + "</div>");
	m_streaming.clear();
	setBusyUi(true);
	m_session->submit(text);
}

void AiChatView::newChat()
{
	if (m_session->busy()) { return; }
	m_session->clear();
	m_transcript->clear();
	m_status->clear();
	m_streaming.clear();
}

void AiChatView::revertTurn()
{
	if (m_session->revertLastTurn()) { setBusyUi(false); }
}

void AiChatView::onTextDelta(const QString& text)
{
	m_streaming += text;
	m_status->setText(tr("Writing…"));
}

void AiChatView::onToolStarted(const QString& name, const QJsonObject& args)
{
	const QString argStr = QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact)).left(200);
	appendBlock("<div style='color:#8aa;margin-left:12px'>▸ " + name.toHtmlEscaped() + " " + argStr.toHtmlEscaped()
		+ "</div>");
}

void AiChatView::onToolFinished(const QString&, const QJsonObject& result)
{
	const bool ok = result["ok"].toBool();
	const QString msg = ok ? QString("✓") : "✗ " + result["error"].toString().toHtmlEscaped();
	appendBlock(QString("<div style='color:%1;margin-left:24px'>%2</div>").arg(ok ? "#7c7" : "#e9a23b", msg));
}

void AiChatView::onTurnFinished(const QString& text)
{
	appendAssistantBlock(text.isEmpty() ? m_streaming : text);
	m_streaming.clear();
	setBusyUi(false);
}

void AiChatView::onTurnFailed(const QString& error)
{
	// Keep whatever the model already said before the turn broke off.
	appendAssistantBlock(m_streaming);
	m_streaming.clear();
	appendBlock("<div style='color:#e55'>" + error.toHtmlEscaped() + "</div>");
	setBusyUi(false);
}

void AiChatView::onStatus(const QString& text)
{
	m_status->setText(text);
}

void AiChatView::appendAssistantBlock(const QString& text)
{
	if (text.isEmpty()) { return; }
	appendBlock("<div style='margin:6px 0;padding:6px;background:#233;border-radius:6px'><b>AI</b><br>"
		+ text.toHtmlEscaped().replace("\n", "<br>") + "</div>");
}

void AiChatView::appendBlock(const QString& html)
{
	m_transcript->append(html);
	m_transcript->verticalScrollBar()->setValue(m_transcript->verticalScrollBar()->maximum());
}

} // namespace lmms::gui
