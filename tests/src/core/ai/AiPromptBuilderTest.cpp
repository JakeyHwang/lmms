/*
 * AiPromptBuilderTest.cpp - tests for buildAiSystemPrompt
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

#include "AiPromptBuilder.h"

#include <QtTest>

class AiPromptBuilderTest : public QObject
{
	Q_OBJECT

private slots:
	void promptHasBaseTextAndPluginSections()
	{
		const QString prompt = lmms::buildAiSystemPrompt();

		// Base text: either the shipped data/ai/system_prompt.md (found via the
		// data:/ search path) or the embedded fallback.
		const bool fromFile = prompt.contains("## Rules");
		const bool fallback = prompt.contains("Call get_project_summary first.");
		QVERIFY2(fromFile || fallback, qPrintable(prompt.left(200)));
		QVERIFY(prompt.startsWith("You are the AI Composer inside LMMS"));

		// Plugin sections are always appended, in this order, even when no
		// plugins can be loaded (headless test processes cannot load them).
		const int instruments = prompt.indexOf("\n## Installed instruments\n");
		const int effects = prompt.indexOf("\n## Installed effects\n");
		QVERIFY(instruments > 0);
		QVERIFY(effects > instruments);
		QVERIFY(prompt.endsWith('\n'));
	}
};

QTEST_GUILESS_MAIN(AiPromptBuilderTest)
#include "AiPromptBuilderTest.moc"
