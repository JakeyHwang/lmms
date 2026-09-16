/*
 * AiActionTools.cpp - AI Composer tools that drive transport, export and project files
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

#include <functional>
#include <optional>
#include <utility>

#include <QEventLoop>
#include <QFileInfo>

#include "AiToolHelpers.h"
#include "AudioEngine.h"
#include "OutputSettings.h"
#include "ProjectJournal.h"
#include "ProjectRenderer.h"
#include "RenderManager.h"
#include "TimePos.h"

namespace lmms
{

using namespace aitools;
using R = AiToolRegistry;
using PathAllowed = std::function<bool(const QString&)>;

static QJsonObject play(const QJsonObject& a)
{
	auto song = Engine::getSong();
	if (song->isExporting()) { return R::error("Cannot play while exporting"); }
	if (a.contains("fromBar"))
	{
		const int bar = a["fromBar"].toInt();
		if (bar < 1) { return R::error("fromBar must be >= 1"); }
		song->setPlayPos(TimePos(bar - 1, 0).getTicks(), Song::PlayMode::Song);
	}
	// playSong stops any other play mode first and only flips flags; the audio thread picks it up.
	if (!song->isPlaying() || song->playMode() != Song::PlayMode::Song) { song->playSong(); }
	return R::ok({{"playing", true}});
}

static QJsonObject stop(const QJsonObject&)
{
	// Song::stop takes the audio-engine change lock itself; do not guard it.
	Engine::getSong()->stop();
	return R::ok({{"playing", false}});
}

//! Export format for a tool argument, or nullopt for an unknown name.
static std::optional<ProjectRenderer::ExportFileFormat> formatNamed(const QString& name)
{
	using F = ProjectRenderer::ExportFileFormat;
	if (name == "wav" || name == "wave") { return F::Wave; }
	if (name == "flac") { return F::Flac; }
	if (name == "ogg") { return F::Ogg; }
	if (name == "mp3") { return F::MP3; }
	return std::nullopt;
}

static QJsonObject render(const QJsonObject& a, const PathAllowed& pathAllowed)
{
	auto song = Engine::getSong();
	if (song->isPlaying() || song->isExporting()) { return R::error("Stop playback before rendering"); }
	QString path = a["path"].toString();
	if (path.isEmpty()) { return R::error("path is required"); }

	// Explicit format wins; otherwise a known extension on the path selects it; otherwise wav.
	QString fmtName = a["format"].toString().toLower();
	if (fmtName.isEmpty())
	{
		const QString suffix = QFileInfo(path).suffix().toLower();
		fmtName = formatNamed(suffix) ? suffix : QString("wav");
	}
	const auto fmt = formatNamed(fmtName);
	if (!fmt) { return R::error("format must be one of wav, flac, ogg, mp3"); }
	if (!ProjectRenderer::fileEncodeDevices[static_cast<std::size_t>(*fmt)].m_getDevInst)
	{
		return R::error("This build cannot write " + fmtName + " files");
	}
	const QString ext = ProjectRenderer::getFileExtensionFromFormat(*fmt);
	if (!path.endsWith(ext, Qt::CaseInsensitive)) { path += ext; }
	if (pathAllowed && !pathAllowed(path)) { return R::error("Path not allowed: " + path); }

	// Same settings as the command-line renderer; the bit rate only matters for ogg/mp3.
	const OutputSettings os(Engine::audioEngine()->outputSampleRate(), 160, OutputSettings::BitDepth::Depth16Bit,
		OutputSettings::StereoMode::JointStereo);
	// RenderManager swaps the audio device for a file device and restores it in its destructor,
	// so it must outlive the export. finished() is emitted synchronously when the renderer could
	// not open the file, hence the flag: exec() must not wait for a quit that already happened.
	RenderManager rm(os, *fmt, path);
	QEventLoop loop;
	bool done = false;
	QObject::connect(&rm, &RenderManager::finished, &loop, [&] { done = true; loop.quit(); });
	rm.renderProject();
	if (!done) { loop.exec(); }

	const QFileInfo out(path);
	if (!out.exists()) { return R::error("Render produced no file: " + path); }
	return R::ok({{"path", path}, {"bytes", double(out.size())}});
}

static QJsonObject save(const QJsonObject& a, const PathAllowed& pathAllowed)
{
	auto song = Engine::getSong();
	QString path = a["path"].toString(song->projectFileName());
	if (path.isEmpty()) { return R::error("Project has no file name yet; pass path"); }
	// DataFile would otherwise pick .mmpz from the user's config; the tool promises .mmp.
	if (!path.endsWith(".mmp", Qt::CaseInsensitive) && !path.endsWith(".mmpz", Qt::CaseInsensitive)) { path += ".mmp"; }
	if (pathAllowed && !pathAllowed(path)) { return R::error("Path not allowed: " + path); }
	// Despite its name this has no GUI dependency: it writes the file, clears the modified flag
	// and makes `path` the project's file name so a later bare save overwrites it.
	if (!song->guiSaveProjectAs(path)) { return R::error("Save failed: " + path); }
	return R::ok({{"path", path}});
}

static QJsonObject newProject(const QJsonObject&)
{
	auto song = Engine::getSong();
	if (song->isExporting()) { return R::error("Cannot start a new project while exporting"); }
	// createNewProject stops playback and loads the default template itself; never guard it.
	song->createNewProject();
	return R::ok({{"tracks", int(song->tracks().size())}});
}

static QJsonObject undo(const QJsonObject&)
{
	auto journal = Engine::projectJournal();
	if (!journal->canUndo()) { return R::error("Nothing to undo"); }
	journal->undo();
	return R::ok();
}

void registerAiActionTools(AiToolRegistry& r, PathAllowed pathAllowed)
{
	r.add({"play", "Start song playback, optionally from a bar (default: the current position).",
		schema({{"fromBar", prop("integer", "1-based bar to start from")}}), play});
	r.add({"stop", "Stop playback.", schema({}), stop});
	r.add({"render", "Render the whole song to an audio file. Blocks until the export is done; the song must be stopped.",
		schema({{"path", prop("string", "output path, without extension or with the format's extension")},
			{"format", prop("string", "wav (default), flac, ogg or mp3; inferred from the path's extension when omitted")}}, {"path"}),
		[pathAllowed](const QJsonObject& a) { return render(a, pathAllowed); }});
	r.add({"save", "Save the project as .mmp. Omit path to save to the current project file.",
		schema({{"path", prop("string", "output path; .mmp is appended when missing")}}),
		[pathAllowed](const QJsonObject& a) { return save(a, pathAllowed); }});
	r.add({"new_project", "Discard the current project and start a new one from the default template.", schema({}), newProject});
	r.add({"undo", "Undo the last change (one journal step).", schema({}), undo});
}

} // namespace lmms
