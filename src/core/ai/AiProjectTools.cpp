/*
 * AiProjectTools.cpp - AI Composer tools that read and edit the open project
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

#include <QJsonArray>

#include "AiToolHelpers.h"
#include "AudioEngine.h"
#include "Clip.h"
#include "MidiClip.h"
#include "Mixer.h"
#include "Note.h"
#include "PluginFactory.h"
#include "TimePos.h"
#include "Track.h"
#include "panning.h"
#include "volume.h"

namespace lmms
{

using namespace aitools;
using R = AiToolRegistry;

static QJsonObject headFields(Song* song)
{
	return {{"bpm", song->getTempo()}, {"timesigNum", song->getTimeSigModel().getNumerator()},
		{"timesigDen", song->getTimeSigModel().getDenominator()}};
}

static QJsonObject projectSummary(const QJsonObject&)
{
	auto song = Engine::getSong();
	QJsonArray tracks;
	int i = 0;
	for (auto t : song->tracks())
	{
		QJsonObject to{{"index", i++}, {"name", t->name()}, {"muted", t->isMuted()}};
		switch (t->type())
		{
			case Track::Type::Instrument: to["type"] = "instrument"; break;
			case Track::Type::Sample: to["type"] = "sample"; break;
			case Track::Type::Automation: to["type"] = "automation"; break;
			case Track::Type::Pattern: to["type"] = "pattern"; break;
			default: to["type"] = "other";
		}
		if (auto it = dynamic_cast<InstrumentTrack*>(t))
		{
			to["instrument"] = it->instrumentName();
			to["mixerChannel"] = it->mixerChannelModel()->value();
			to["volume"] = it->volumeModel()->value();
		}
		QJsonArray clips;
		for (auto c : t->getClips())
		{
			QJsonObject co{{"pos", c->startPosition().getTicks()}, {"len", c->length().getTicks()}, {"name", c->name()}};
			if (auto mc = dynamic_cast<MidiClip*>(c)) { co["noteCount"] = int(mc->notes().size()); }
			clips.append(co);
		}
		to["clips"] = clips;
		tracks.append(to);
	}
	auto out = headFields(song);
	out["lengthBars"] = song->length();
	out["ticksPerBar"] = TimePos::ticksPerBar();
	out["tracks"] = tracks;
	return R::ok(out);
}

static QJsonObject getHead(const QJsonObject&)
{
	auto song = Engine::getSong();
	auto out = headFields(song);
	out["masterVol"] = song->masterVolume();
	out["masterPitch"] = song->masterPitch();
	return R::ok(out);
}

static QJsonObject setHead(const QJsonObject& a)
{
	auto song = Engine::getSong();
	if (a.contains("bpm"))
	{
		int bpm = a["bpm"].toInt();
		if (bpm < MinTempo || bpm > MaxTempo) { return R::error(QString("bpm must be %1..%2").arg(MinTempo).arg(MaxTempo)); }
		// Song::setTempo (connected to this model) takes the audio-engine change lock itself.
		song->tempoModel().setValue(bpm);
	}
	if (a.contains("timesigNum")) { song->getTimeSigModel().numeratorModel().setValue(a["timesigNum"].toInt()); }
	if (a.contains("timesigDen")) { song->getTimeSigModel().denominatorModel().setValue(a["timesigDen"].toInt()); }
	if (a.contains("masterVol")) { song->setMasterVolume(a["masterVol"].toInt()); }
	if (a.contains("masterPitch")) { song->setMasterPitch(a["masterPitch"].toInt()); }
	return getHead({});
}

static QJsonObject addInstrumentTrack(const QJsonObject& a)
{
	const QString plugin = a["instrument"].toString();
	if (plugin.isEmpty() || PluginFactory::instance()->pluginInfo(plugin.toUtf8().constData()).isNull())
	{
		return R::error("Unknown instrument plugin: " + plugin + " (use list_instruments)");
	}
	int mixerChannel = a["mixerChannel"].toInt(0);
	if (mixerChannel < 0 || mixerChannel >= Engine::mixer()->numChannels())
	{
		return R::error(QString("mixerChannel must be 0..%1").arg(Engine::mixer()->numChannels() - 1));
	}
	// Track::create takes the audio-engine change lock itself; do not guard it.
	auto track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
	if (!track) { return R::error("Could not create instrument track"); }
	if (!track->loadInstrument(plugin)) { return R::error("Failed to load instrument " + plugin); }
	// loadInstrument renames the track after the plugin, so apply the requested name afterwards.
	if (a.contains("name")) { track->setName(a["name"].toString()); }
	if (a.contains("mixerChannel")) { track->mixerChannelModel()->setValue(mixerChannel); }
	int index = int(Engine::getSong()->tracks().size()) - 1;
	return R::ok({{"index", index}});
}

static QJsonObject addNotes(const QJsonObject& a)
{
	QString err;
	auto track = instrumentTrackAt(a["track"].toInt(-1), &err);
	if (!track) { return R::error(err); }
	if (!a["notes"].isArray()) { return R::error("notes must be an array"); }
	auto notes = a["notes"].toArray();
	for (auto v : notes)
	{
		auto n = v.toObject();
		int key = n["key"].toInt(-1);
		if (key < 0 || key >= NumKeys) { return R::error(QString("note key %1 out of range 0..%2").arg(key).arg(NumKeys - 1)); }
		if (n["len"].toInt(0) <= 0) { return R::error("note len must be > 0 ticks"); }
		if (n["pos"].toInt(0) < 0) { return R::error("note pos must be >= 0 ticks"); }
		int vol = n["vol"].toInt(DefaultVolume);
		if (vol < 0 || vol > MaxVolume) { return R::error(QString("note vol %1 out of range 0..%2").arg(vol).arg(int(MaxVolume))); }
		int pan = n["pan"].toInt(DefaultPanning);
		if (pan < PanningLeft || pan > PanningRight) { return R::error(QString("note pan %1 out of range %2..%3").arg(pan).arg(int(PanningLeft)).arg(int(PanningRight))); }
	}
	TimePos clipPos(a["clipPos"].toInt(0));
	MidiClip* clip = nullptr;
	for (auto c : track->getClips())
	{
		if (c->startPosition() == clipPos) { clip = dynamic_cast<MidiClip*>(c); break; }
	}
	// createClip -> Clip::movePosition takes the audio-engine change lock itself; keep it outside the guard.
	if (!clip) { clip = dynamic_cast<MidiClip*>(track->createClip(clipPos)); }
	if (!clip) { return R::error("Could not create clip"); }
	{
		auto guard = Engine::audioEngine()->requestChangesGuard();
		if (a["clear"].toBool(false)) { clip->clearNotes(); }
		for (auto v : notes)
		{
			auto n = v.toObject();
			Note note(TimePos(n["len"].toInt()), TimePos(n["pos"].toInt(0)), n["key"].toInt(),
				volume_t(n["vol"].toInt(DefaultVolume)), panning_t(n["pan"].toInt(DefaultPanning)));
			clip->addNote(note, false);
		}
	}
	return R::ok({{"track", a["track"].toInt()}, {"clipPos", clipPos.getTicks()}, {"noteCount", int(clip->notes().size())}});
}

void registerAiProjectTools(AiToolRegistry& r)
{
	r.add({"get_project_summary", "Compact overview of the open project: tempo, time signature, tracks, clips. Call this first.", schema({}), projectSummary});
	r.add({"get_head", "Tempo, time signature, master volume/pitch.", schema({}), getHead});
	r.add({"set_head", "Set tempo (bpm), time signature, master volume/pitch. All fields optional.",
		schema({{"bpm", prop("integer", "10..999")}, {"timesigNum", prop("integer", "")}, {"timesigDen", prop("integer", "")},
			{"masterVol", prop("integer", "0..200")}, {"masterPitch", prop("integer", "-12..12 semitones")}}), setHead});
	r.add({"add_instrument_track", "Create an instrument track with the given plugin (see list_instruments). Returns its index.",
		schema({{"name", prop("string", "track name")}, {"instrument", prop("string", "plugin name, e.g. tripleoscillator, kicker, sf2player")},
			{"mixerChannel", prop("integer", "mixer channel, 0 = master")}}, {"instrument"}), addInstrumentTrack});
	r.add({"add_notes", "Add notes to a MIDI clip on an instrument track (creates the clip at clipPos if missing). Ticks: 192 per bar in 4/4, quarter=48, 16th=12. key is MIDI number (60 = C4).",
		schema({{"track", prop("integer", "track index")}, {"clipPos", prop("integer", "clip start in ticks")},
			{"notes", QJsonObject{{"type", "array"}, {"items", schema({{"pos", prop("integer", "ticks from clip start")}, {"len", prop("integer", "ticks")},
				{"key", prop("integer", "0..127")}, {"vol", prop("integer", "0..200, default 100")}, {"pan", prop("integer", "-100..100")}}, {"pos", "len", "key"})}}},
			{"clear", prop("boolean", "remove existing notes first")}}, {"track", "clipPos", "notes"}), addNotes});
}

} // namespace lmms
