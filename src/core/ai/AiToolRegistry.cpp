/*
 * AiToolRegistry.cpp - named tool table the AI Composer session dispatches into
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

#include "AiToolRegistry.h"

#include <exception>

namespace lmms
{

void AiToolRegistry::add(AiTool tool)
{
	m_tools.insert(tool.name, std::move(tool));
}

bool AiToolRegistry::has(const QString& name) const
{
	return m_tools.contains(name);
}

QJsonArray AiToolRegistry::specs() const
{
	QJsonArray out;
	for (const auto& t : m_tools)
	{
		out.append(QJsonObject{{"type", "function"}, {"function", QJsonObject{
			{"name", t.name}, {"description", t.description}, {"parameters", t.parameters}}}});
	}
	return out;
}

QJsonObject AiToolRegistry::call(const QString& name, const QJsonObject& args) const
{
	auto it = m_tools.find(name);
	if (it == m_tools.end()) { return error("Unknown tool: " + name); }
	try { return it->handler(args); }
	catch (const std::exception& e) { return error(QString("Tool %1 failed: %2").arg(name, e.what())); }
	catch (...) { return error("Tool " + name + " failed"); }
}

QJsonObject AiToolRegistry::ok(QJsonObject fields)
{
	fields["ok"] = true;
	return fields;
}

QJsonObject AiToolRegistry::error(const QString& message)
{
	return {{"ok", false}, {"error", message}};
}

} // namespace lmms
