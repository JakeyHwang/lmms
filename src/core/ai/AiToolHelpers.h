/*
 * AiToolHelpers.h - private helpers shared by the agent harness tool handlers
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

#ifndef LMMS_AI_TOOL_HELPERS_H
#define LMMS_AI_TOOL_HELPERS_H

#include <initializer_list>
#include <utility>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "AiToolRegistry.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "Song.h"

namespace lmms::aitools
{

//! Track at `index` in the open song, or nullptr with `*err` set.
inline Track* trackAt(int index, QString* err)
{
	const auto& tracks = Engine::getSong()->tracks();
	if (index < 0 || index >= int(tracks.size()))
	{
		*err = QString("No track at index %1 (project has %2)").arg(index).arg(tracks.size());
		return nullptr;
	}
	return tracks[index];
}

inline InstrumentTrack* instrumentTrackAt(int index, QString* err)
{
	auto t = dynamic_cast<InstrumentTrack*>(trackAt(index, err));
	if (!t && err->isEmpty()) { *err = QString("Track %1 is not an instrument track").arg(index); }
	return t;
}

//! One JSON-schema property: {"type": type, "description": description}.
inline QJsonObject prop(const QString& type, const QString& description)
{
	return {{"type", type}, {"description", description}};
}

//! JSON-schema object with the given properties and required keys.
inline QJsonObject schema(std::initializer_list<std::pair<QString, QJsonObject>> props, QStringList required = {})
{
	QJsonObject p;
	for (auto& [k, v] : props) { p[k] = v; }
	QJsonObject s{{"type", "object"}, {"properties", p}};
	if (!required.isEmpty()) { s["required"] = QJsonArray::fromStringList(required); }
	return s;
}

} // namespace lmms::aitools

#endif // LMMS_AI_TOOL_HELPERS_H
