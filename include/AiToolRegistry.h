/*
 * AiToolRegistry.h - named tool table the AI Composer session dispatches into
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

#ifndef LMMS_AI_TOOL_REGISTRY_H
#define LMMS_AI_TOOL_REGISTRY_H

#include <functional>

#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QString>

#include "lmms_export.h"

namespace lmms
{

struct AiTool
{
	QString name;
	QString description;
	QJsonObject parameters;                               // JSON schema
	std::function<QJsonObject(const QJsonObject&)> handler;
};

class LMMS_EXPORT AiToolRegistry
{
public:
	void add(AiTool tool);
	bool has(const QString& name) const;
	QJsonArray specs() const;                             // [{type:"function", function:{name,description,parameters}}]
	QJsonObject call(const QString& name, const QJsonObject& args) const; // never throws
	static QJsonObject ok(QJsonObject fields = {});
	static QJsonObject error(const QString& message);

private:
	QMap<QString, AiTool> m_tools;
};

} // namespace lmms

#endif // LMMS_AI_TOOL_REGISTRY_H
