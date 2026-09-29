/*
 * AiDiscoveryTools.cpp - agent harness tools that list plugins, presets and samples
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

#include "AiTools.h"

#include <algorithm>

#include <QDirIterator>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QTextStream>

#include "AiPathPolicy.h"
#include "AiToolHelpers.h"
#include "ConfigManager.h"
#include "DataFile.h"
#include "PluginFactory.h"

namespace lmms
{

using namespace aitools;
using R = AiToolRegistry;

static QJsonArray listPlugins(Plugin::Type type)
{
	QJsonArray out;
	for (auto d : PluginFactory::instance()->descriptors(type))
	{
		out.append(QJsonObject{{"name", d->name}, {"displayName", d->displayName}, {"description", d->description}});
	}
	return out;
}

//! Files under `dirs` (recursively) matching `patterns`, optionally filtered by a case-insensitive
//! substring of the full path. Missing directories contribute nothing.
static QJsonObject listFiles(const QString& key, const QStringList& dirs, const QStringList& patterns, const QJsonObject& a)
{
	const QString query = a["query"].toString();
	const int limit = std::clamp(a["limit"].toInt(50), 1, 500);
	QJsonArray out;
	bool truncated = false;
	for (const auto& dir : dirs)
	{
		QDirIterator it(dir, patterns, QDir::Files, QDirIterator::Subdirectories);
		while (it.hasNext())
		{
			const QString p = it.next();
			if (!query.isEmpty() && !p.contains(query, Qt::CaseInsensitive)) { continue; }
			if (out.size() >= limit) { truncated = true; break; }
			out.append(p);
		}
		if (truncated) { break; }
	}
	return R::ok({{key, out}, {"truncated", truncated}});
}

//! The <instrumenttrack> element of a preset file, upgraded to the current format.
static QJsonObject presetXml(const AiPathPolicy& policy, const QString& path)
{
	if (!policy.allows(path)) { return R::error("Path not allowed: " + path); }
	const QFileInfo fi(path);
	if (!fi.isFile()) { return R::error("Not a readable preset: " + path); }
	// .xiz files in the preset dirs are ZynAddSubFX patches loaded by that plugin, not LMMS preset documents.
	if (fi.suffix().compare("xpf", Qt::CaseInsensitive) != 0) { return R::error("Not an LMMS preset file (.xpf): " + path); }
	if (fi.size() > 1024 * 1024) { return R::error("Preset file too large: " + path); }
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) { return R::error("Not a readable preset: " + path); }
	const QByteArray data = f.readAll();

	// DataFile pops a modal dialog on unparsable input when a GUI exists; check the bytes parse first.
	QDomDocument probe;
	if (!probe.setContent(data))
	{
		const QByteArray unpacked = qUncompress(data);
		if (unpacked.isEmpty() || !probe.setContent(unpacked)) { return R::error("Not a readable preset: " + path); }
	}
	DataFile df(data);
	if (df.type() != DataFile::Type::InstrumentTrackSettings || df.content().isNull())
	{
		return R::error("Not an instrument preset: " + path);
	}
	if (df.hasLocalPlugins()) { return R::error("Preset references local plugin paths: " + path); }
	QDomElement track = df.content().firstChildElement("instrumenttrack");
	if (track.isNull()) { return R::error("Preset has no <instrumenttrack>: " + path); }
	QString out;
	QTextStream ts(&out);
	track.save(ts, 0);
	if (out.size() > 64 * 1024) { return R::error("Preset XML too large"); }
	return R::ok({{"xml", out.trimmed()}});
}

void registerAiDiscoveryTools(AiToolRegistry& r, AiPathPolicy& policy)
{
	r.add({"list_instruments",
		"WHAT: every instrument plugin installed, with name (the id add_instrument_track takes), displayName and a one-line description. "
		"WHEN: the system prompt's instrument list is enough for common choices; call this when you need the descriptions or are unsure a name exists. RETURNS {instruments}.",
		schema({}), [](const QJsonObject&) { return R::ok({{"instruments", listPlugins(Plugin::Type::Instrument)}}); }});
	r.add({"list_effects",
		"WHAT: every effect plugin installed, with name (the id add_effect takes), displayName and description. WHEN: choosing reverb/delay/compressor/eq names you are not sure about. "
		"RETURNS {effects}. Gotcha: LADSPA/VST/LV2 host plugins are listed but cannot be added by name.",
		schema({}), [](const QJsonObject&) { return R::ok({{"effects", listPlugins(Plugin::Type::Effect)}}); }});
	r.add({"list_presets",
		"WHAT: instrument preset files (.xpf) in the factory and user preset folders, filtered by a case-insensitive substring of the path (folder = plugin name). "
		"WHEN: you want a ready-made sound (drum kit, bass, pad, lead) instead of raw plugin defaults; feed a path to get_preset_xml. RETURNS {presets, truncated}. Gotcha: query 'drum' or 'kick' for drum sounds.",
		schema({{"query", prop("string", "substring filter, e.g. 'bass', 'drum', 'TripleOscillator'; empty for all")}, {"limit", prop("integer", "max results, default 50, max 500")}}),
		[](const QJsonObject& a) {
			auto cm = ConfigManager::inst();
			return listFiles("presets", {cm->factoryPresetsDir(), cm->userPresetsDir()}, {"*.xpf"}, a);
		}});
	r.add({"list_samples",
		"WHAT: audio files (.wav/.ogg/.flac/.mp3/.aiff) in the factory and user sample folders, filtered by a case-insensitive path substring. "
		"WHEN: you need a one-shot or loop for add_sample_clip, e.g. query 'kick', 'snare', 'hihat'. RETURNS {samples, truncated}. Gotcha: paths are absolute; pass them unchanged.",
		schema({{"query", prop("string", "substring filter, e.g. 'kick'; empty for all")}, {"limit", prop("integer", "max results, default 50, max 500")}}),
		[](const QJsonObject& a) {
			auto cm = ConfigManager::inst();
			return listFiles("samples", {cm->factorySamplesDir(), cm->userSamplesDir()},
				{"*.wav", "*.ogg", "*.flac", "*.mp3", "*.aiff"}, a);
		}});
	r.add({"get_preset_xml",
		"WHAT: read a .xpf preset and return its <instrumenttrack> element, upgraded to the current format. "
		"WHEN: after list_presets; wrap the result as <track type=\"0\" name=\"..\">...</track> and pass it to add_track, then write notes on the returned index. RETURNS {xml}. Gotcha: .xiz files are not presets.",
		schema({{"path", prop("string", "absolute path of a .xpf file from list_presets")}}, {"path"}),
		[&policy](const QJsonObject& a) { return presetXml(policy, a["path"].toString()); }});
}

} // namespace lmms
