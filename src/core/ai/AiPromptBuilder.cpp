/*
 * AiPromptBuilder.cpp - builds the AI Composer system prompt
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

#include <QFile>
#include <QStringList>

#include "ConfigManager.h"
#include "Plugin.h"
#include "PluginFactory.h"

namespace lmms
{

static QStringList pluginNames(Plugin::Type type)
{
	QStringList names;
	for (const Plugin::Descriptor* d : PluginFactory::instance()->descriptors(type))
	{
		names << QString("%1 (%2)").arg(d->name, d->displayName);
	}
	names.sort(Qt::CaseInsensitive);
	return names;
}

QString buildAiSystemPrompt()
{
	QString base;
	QFile f(ConfigManager::inst()->dataDir() + "ai/system_prompt.md");
	if (f.open(QIODevice::ReadOnly))
	{
		base = QString::fromUtf8(f.readAll()).trimmed();
	}
	else
	{
		base = "You are the AI Composer inside LMMS. Use the tools to build music in the open project. Call get_project_summary first.";
	}
	return base
		+ "\n\n## Installed instruments\n" + pluginNames(Plugin::Type::Instrument).join(", ")
		+ "\n\n## Installed effects\n" + pluginNames(Plugin::Type::Effect).join(", ")
		+ "\n";
}

} // namespace lmms
