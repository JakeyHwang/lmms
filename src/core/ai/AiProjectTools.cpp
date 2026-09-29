/*
 * AiProjectTools.cpp - agent harness tools that read and edit the open project
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
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include <QCoreApplication>
#include <QDomDocument>
#include <QFileInfo>
#include <QJsonArray>
#include <QTextStream>

#include "AiToolHelpers.h"
#include "AudioBusHandle.h"
#include "AudioEngine.h"
#include "AutomatableModel.h"
#include "AutomationClip.h"
#include "Clip.h"
#include "DataFile.h"
#include "DeprecationHelper.h"
#include "Effect.h"
#include "EffectChain.h"
#include "GuiApplication.h"
#include "Instrument.h"
#include "MidiClip.h"
#include "Mixer.h"
#include "MixerView.h"
#include "Note.h"
#include "PathUtil.h"
#include "PatternStore.h"
#include "PluginFactory.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleDecoder.h"
#include "SampleTrack.h"
#include "SongEditor.h"
#include "TimePos.h"
#include "Track.h"
#include "TrackView.h"
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
	if (!plugin.isEmpty() && PluginFactory::instance()->pluginInfo(plugin.toUtf8().constData()).isNull())
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
	if (!plugin.isEmpty() && !track->loadInstrument(plugin)) { return R::error("Failed to load instrument " + plugin); }
	// loadInstrument renames the track after the plugin, so apply the requested name afterwards.
	if (a.contains("name")) { track->setName(a["name"].toString()); }
	if (a.contains("mixerChannel")) { track->mixerChannelModel()->setValue(mixerChannel); }
	int index = int(Engine::getSong()->tracks().size()) - 1;
	return R::ok({{"index", index}});
}

// Both defined further down, with the XML surface they belong to.
static QString elementToString(const QDomElement& e);
static QJsonObject addTrackXml(const QJsonObject& a);

//! Instrument track playing a SoundFont preset through sf2player, built as <track> XML so it goes
//! through the same validation as add_track. Checks that need no plugin (file, path policy, ranges)
//! run first so they hold in headless builds.
static QJsonObject addSf2Track(const QJsonObject& a, const std::function<bool(const QString&)>& pathAllowed)
{
	const QString file = a["file"].toString();
	if (file.isEmpty()) { return R::error("file is required (absolute path to a .sf2)"); }
	if (pathAllowed && !pathAllowed(file)) { return R::error("Path not allowed: " + file); }
	if (!QFileInfo(file).isFile()) { return R::error("No such file: " + file); }
	const int bank = a["bank"].toInt(0);
	const int patch = a["patch"].toInt(0);
	if (bank < 0 || bank > 128 || patch < 0 || patch > 127) { return R::error("bank must be 0..128 (128 = drum kits) and patch 0..127"); }
	const int mixerChannel = a["mixerChannel"].toInt(0);
	if (mixerChannel < 0 || mixerChannel >= Engine::mixer()->numChannels())
	{
		return R::error(QString("mixerChannel must be 0..%1").arg(Engine::mixer()->numChannels() - 1));
	}
	if (PluginFactory::instance()->pluginInfo("sf2player").isNull()) { return R::error("sf2player plugin is not available in this build"); }

	QDomDocument doc;
	QDomElement track = doc.createElement("track");
	track.setAttribute("type", int(Track::Type::Instrument));
	track.setAttribute("name", a["name"].toString(QFileInfo(file).completeBaseName()));
	QDomElement it = doc.createElement("instrumenttrack");
	it.setAttribute("mixch", mixerChannel);
	QDomElement inst = doc.createElement("instrument");
	inst.setAttribute("name", "sf2player");
	QDomElement sf2 = doc.createElement("sf2player");
	sf2.setAttribute("src", file);
	sf2.setAttribute("bank", bank);
	sf2.setAttribute("patch", patch);
	inst.appendChild(sf2);
	it.appendChild(inst);
	track.appendChild(it);
	doc.appendChild(track);
	return addTrackXml({{"xml", elementToString(track)}});
}

// --- Notes and clips ------------------------------------------------------------------------

//! End (ticks from clip start) of the last-ending note in `notes`, 0 when empty.
static int notesEnd(const NoteVector& notes)
{
	int end = 0;
	for (auto n : notes) { end = std::max(end, int(n->endPos())); }
	return end;
}

//! Checks one clip spec {clipPos, len?, name?, notes, clear?} against `track` without touching
//! it: every note, and `len` against the notes it would have to cover (the new ones plus the
//! existing ones when not clearing). `*err` is set on failure.
static bool validateClipSpec(InstrumentTrack* track, const QJsonObject& spec, QString* err)
{
	if (!spec["notes"].isArray()) { *err = "notes must be an array"; return false; }
	const int clipPos = spec["clipPos"].toInt(-1);
	if (clipPos < 0) { *err = "clipPos must be >= 0 ticks"; return false; }
	int end = 0;
	for (auto v : spec["notes"].toArray())
	{
		auto n = v.toObject();
		const int key = n["key"].toInt(-1);
		if (key < 0 || key >= NumKeys) { *err = QString("note key %1 out of range 0..%2").arg(key).arg(NumKeys - 1); return false; }
		if (n["len"].toInt(0) <= 0) { *err = "note len must be > 0 ticks"; return false; }
		if (n["pos"].toInt(0) < 0) { *err = "note pos must be >= 0 ticks"; return false; }
		const int vol = n["vol"].toInt(DefaultVolume);
		if (vol < 0 || vol > MaxVolume) { *err = QString("note vol %1 out of range 0..%2").arg(vol).arg(int(MaxVolume)); return false; }
		const int pan = n["pan"].toInt(DefaultPanning);
		if (pan < PanningLeft || pan > PanningRight) { *err = QString("note pan %1 out of range %2..%3").arg(pan).arg(int(PanningLeft)).arg(int(PanningRight)); return false; }
		end = std::max(end, n["pos"].toInt(0) + n["len"].toInt());
	}
	if (spec.contains("len"))
	{
		const int len = spec["len"].toInt(0);
		if (len <= 0) { *err = "clip len must be > 0 ticks"; return false; }
		if (!spec["clear"].toBool(false))
		{
			for (auto c : track->getClips())
			{
				if (auto mc = dynamic_cast<MidiClip*>(c); mc && c->startPosition() == TimePos(clipPos)) { end = std::max(end, notesEnd(mc->notes())); }
			}
		}
		if (len < end) { *err = QString("clip len %1 is shorter than its notes (last note ends at %2)").arg(len).arg(end); return false; }
	}
	return true;
}

//! Applies a validated clip spec: the clip at clipPos is reused or created, optionally cleared
//! and named, the notes are added, and an explicit `len` fixes the clip length (which otherwise
//! auto-grows to whole bars covering the notes). Returns the clip, or nullptr when none could be made.
static MidiClip* applyClipSpec(InstrumentTrack* track, const QJsonObject& spec)
{
	const TimePos clipPos(spec["clipPos"].toInt(0));
	MidiClip* clip = nullptr;
	for (auto c : track->getClips())
	{
		if (c->startPosition() == clipPos) { clip = dynamic_cast<MidiClip*>(c); break; }
	}
	// createClip -> Clip::movePosition takes the audio-engine change lock itself; keep it outside the guard.
	if (!clip) { clip = dynamic_cast<MidiClip*>(track->createClip(clipPos)); }
	if (!clip) { return nullptr; }
	auto guard = Engine::audioEngine()->requestChangesGuard();
	if (spec["clear"].toBool(false)) { clip->clearNotes(); }
	if (spec.contains("name")) { clip->setName(spec["name"].toString()); }
	for (auto v : spec["notes"].toArray())
	{
		auto n = v.toObject();
		Note note(TimePos(n["len"].toInt()), TimePos(n["pos"].toInt(0)), n["key"].toInt(),
			volume_t(n["vol"].toInt(DefaultVolume)), panning_t(n["pan"].toInt(DefaultPanning)));
		clip->addNote(note, false);
	}
	if (spec.contains("len"))
	{
		// A manually sized clip stays that size when notes are added later, as in the song editor.
		clip->setAutoResize(false);
		clip->changeLength(TimePos(spec["len"].toInt()));
	}
	return clip;
}

static QJsonObject addNotes(const QJsonObject& a)
{
	QString err;
	auto track = instrumentTrackAt(a["track"].toInt(-1), &err);
	if (!track) { return R::error(err); }
	if (!validateClipSpec(track, a, &err)) { return R::error(err); }
	auto clip = applyClipSpec(track, a);
	if (!clip) { return R::error("Could not create clip"); }
	return R::ok({{"track", a["track"].toInt()}, {"clipPos", clip->startPosition().getTicks()},
		{"len", clip->length().getTicks()}, {"noteCount", int(clip->notes().size())}});
}

static QJsonObject addClips(const QJsonObject& a)
{
	QString err;
	auto track = instrumentTrackAt(a["track"].toInt(-1), &err);
	if (!track) { return R::error(err); }
	if (!a["clips"].isArray() || a["clips"].toArray().isEmpty()) { return R::error("clips must be a non-empty array"); }
	const auto clips = a["clips"].toArray();
	// Every clip is checked before any is written, so one bad note leaves the track untouched.
	for (int i = 0; i < clips.size(); ++i)
	{
		if (!validateClipSpec(track, clips[i].toObject(), &err)) { return R::error(QString("clip %1: %2").arg(i).arg(err)); }
	}
	int noteCount = 0;
	QJsonArray made;
	for (auto v : clips)
	{
		auto clip = applyClipSpec(track, v.toObject());
		if (!clip) { return R::error("Could not create clip"); }
		noteCount += int(clip->notes().size());
		made.append(QJsonObject{{"clipPos", clip->startPosition().getTicks()}, {"len", clip->length().getTicks()}, {"noteCount", int(clip->notes().size())}});
	}
	return R::ok({{"track", a["track"].toInt()}, {"clipCount", made.size()}, {"noteCount", noteCount}, {"clips", made}});
}

static QJsonObject removeClip(const QJsonObject& a)
{
	QString err;
	auto track = trackAt(a["track"].toInt(-1), &err);
	if (!track) { return R::error(err); }
	const TimePos clipPos(a["clipPos"].toInt(-1));
	Clip* clip = nullptr;
	for (auto c : track->getClips())
	{
		if (c->startPosition() == clipPos) { clip = c; break; }
	}
	if (!clip) { return R::error(QString("Track %1 has no clip starting at %2 (see get_project_summary)").arg(a["track"].toInt()).arg(a["clipPos"].toInt(-1))); }
	{
		// ~Clip unlinks itself from the track and closes its view; the audio engine must be paused for it.
		auto guard = Engine::audioEngine()->requestChangesGuard();
		delete clip;
	}
	return R::ok({{"track", a["track"].toInt()}, {"clipCount", int(track->getClips().size())}});
}

// --- XML surface ---------------------------------------------------------------------------

static constexpr int MaxXmlBytes = 64 * 1024;

//! Error for a result fragment over MaxXmlBytes (UTF-8), or an empty string when it fits.
static QString xmlTooLarge(const QString& what, const QString& xml)
{
	const auto bytes = xml.toUtf8().size();
	if (bytes <= MaxXmlBytes) { return {}; }
	return QString("%1 XML is %2 bytes (limit %3)").arg(what).arg(bytes).arg(MaxXmlBytes);
}

static QString elementToString(const QDomElement& e)
{
	QString out;
	QTextStream ts(&out);
	e.save(ts, 0);
	return out.trimmed();
}

//! Parses a <track> or <mixer> fragment submitted by the model and imports it into `df`, a fresh
//! SongProject DataFile, so the same security check as for .mmp files (hasLocalPlugins) applies.
//! Nothing in the project is touched; on failure `*err` is set and `element` is null.
static bool parseFragment(const QString& xml, const QString& expectedTag, DataFile& df, QDomElement& element, QString* err)
{
	QDomDocument probe;
	QString perr;
	int line = 0;
	if (!setContent(probe, xml.toUtf8(), &perr, &line))
	{
		*err = QString("XML parse error at line %1: %2").arg(line).arg(perr);
		return false;
	}
	const QDomElement root = probe.documentElement();
	if (root.tagName() != expectedTag)
	{
		*err = QString("Root element must be <%1>, got <%2>").arg(expectedTag, root.tagName());
		return false;
	}
	element = df.importNode(root, true).toElement();
	df.content().appendChild(element);
	if (df.hasLocalPlugins())
	{
		element = QDomElement();
		*err = "XML references local plugin paths (local:), which is not allowed";
		return false;
	}
	return true;
}

//! Track types a model may create; Event/Video are unimplemented and HiddenAutomation is never listed in the song.
static bool creatableTrackType(const QDomElement& track, QString* err)
{
	bool isInt = false;
	const int type = track.attribute("type").toInt(&isInt);
	switch (isInt ? static_cast<Track::Type>(type) : Track::Type::Count)
	{
		case Track::Type::Instrument:
		case Track::Type::Pattern:
		case Track::Type::Sample:
		case Track::Type::Automation:
			return true;
		default:
			*err = QString("Unsupported track type \"%1\": use 0 (instrument), 1 (pattern), 2 (sample) or 5 (automation)").arg(track.attribute("type"));
			return false;
	}
}

//! Track::create appends; this puts `track` at `index`. With a GUI the song editor owns the
//! ordering (its views are created through a queued trackAdded connection), so move through it.
static void moveTrackTo(Track* track, int index)
{
	if (auto g = gui::getGUI(); g && g->songEditor())
	{
		QCoreApplication::sendPostedEvents();
		auto editor = g->songEditor()->m_editor;
		for (auto view : editor->trackViews())
		{
			if (view->getTrack() == track)
			{
				editor->moveTrackView(view, index);
				return;
			}
		}
	}
	Engine::getSong()->moveTrack(track, index);
}

//! ~Track unlinks itself from the container and closes its view; the audio engine must be paused for it.
static void deleteTrack(Track* track)
{
	auto guard = Engine::audioEngine()->requestChangesGuard();
	delete track;
}

static QJsonObject getTrackXml(const QJsonObject& a)
{
	QString err;
	auto track = trackAt(a["index"].toInt(-1), &err);
	if (!track) { return R::error(err); }
	QDomDocument doc;
	QDomElement root = doc.createElement("root");
	doc.appendChild(root);
	const QString xml = elementToString(track->saveState(doc, root));
	if (auto err = xmlTooLarge("Track", xml); !err.isEmpty())
	{
		return R::error(err + "; use get_project_summary and the convenience tools instead");
	}
	return R::ok({{"index", a["index"].toInt()}, {"xml", xml}});
}

static QJsonObject addTrackXml(const QJsonObject& a)
{
	DataFile df(DataFile::Type::SongProject);
	QDomElement el;
	QString err;
	if (!parseFragment(a["xml"].toString(), "track", df, el, &err) || !creatableTrackType(el, &err)) { return R::error(err); }
	// Track::create takes the audio-engine change lock itself; do not guard it.
	if (!Track::create(el, Engine::getSong())) { return R::error("Track could not be created from XML"); }
	// Automation clips in the fragment only recorded their target model ids; connect them now.
	AutomationClip::resolveAllIDs();
	return R::ok({{"index", int(Engine::getSong()->tracks().size()) - 1}});
}

static QJsonObject replaceTrackXml(const QJsonObject& a)
{
	QString err;
	const int index = a["index"].toInt(-1);
	auto old = trackAt(index, &err);
	if (!old) { return R::error(err); }
	DataFile df(DataFile::Type::SongProject);
	QDomElement el;
	if (!parseFragment(a["xml"].toString(), "track", df, el, &err) || !creatableTrackType(el, &err)) { return R::error(err); }
	auto track = Track::create(el, Engine::getSong());
	if (!track) { return R::error("Track could not be created from XML"); }
	AutomationClip::resolveAllIDs();
	{
		auto guard = Engine::audioEngine()->requestChangesGuard();
		moveTrackTo(track, index);
	}
	deleteTrack(old);
	return R::ok({{"index", index}});
}

static QJsonObject removeTrack(const QJsonObject& a)
{
	QString err;
	auto track = trackAt(a["index"].toInt(-1), &err);
	if (!track) { return R::error(err); }
	deleteTrack(track);
	return R::ok({{"trackCount", int(Engine::getSong()->tracks().size())}});
}

static QJsonObject getMixerXml(const QJsonObject&)
{
	QDomDocument doc;
	QDomElement root = doc.createElement("root");
	doc.appendChild(root);
	const QString xml = elementToString(Engine::mixer()->saveState(doc, root));
	if (auto err = xmlTooLarge("Mixer", xml); !err.isEmpty()) { return R::error(err); }
	return R::ok({{"xml", xml}});
}

//! Mixer channel of every instrument/sample track in the song and pattern store.
static std::vector<std::pair<IntModel*, int>> trackMixerChannels()
{
	std::vector<std::pair<IntModel*, int>> out;
	for (auto container : {static_cast<TrackContainer*>(Engine::getSong()), static_cast<TrackContainer*>(Engine::patternStore())})
	{
		for (auto t : container->tracks())
		{
			IntModel* m = nullptr;
			if (auto it = dynamic_cast<InstrumentTrack*>(t)) { m = it->mixerChannelModel(); }
			else if (auto st = dynamic_cast<SampleTrack*>(t)) { m = st->mixerChannelModel(); }
			if (m) { out.emplace_back(m, m->value()); }
		}
	}
	return out;
}

static QJsonObject setMixerXml(const QJsonObject& a)
{
	DataFile df(DataFile::Type::SongProject);
	QDomElement el;
	QString err;
	if (!parseFragment(a["xml"].toString(), "mixer", df, el, &err)) { return R::error(err); }
	auto mixer = Engine::mixer();
	// Mixer::loadSettings starts by deleting every channel, which re-routes tracks to master;
	// remember the routing and put it back afterwards (same order as Song::loadProject).
	const auto routing = trackMixerChannels();
	auto g = gui::getGUI();
	if (g && g->mixerView()) { g->mixerView()->clear(); }
	{
		auto guard = Engine::audioEngine()->requestChangesGuard();
		mixer->restoreState(el);
		const int last = mixer->numChannels() - 1;
		for (auto [model, channel] : routing)
		{
			model->setRange(0, last);
			model->setValue(channel <= last ? channel : 0);
		}
	}
	if (g && g->mixerView()) { g->mixerView()->refreshDisplay(); }
	return R::ok({{"channels", int(mixer->numChannels())}});
}

// --- Effects, parameters, automation, samples ---------------------------------------------

//! Where a model-tree tool looks: an instrument/sample track, or a mixer channel (effects only).
struct ModelScope
{
	Track* track = nullptr;
	int mixerChannel = -1;
	EffectChain* chain = nullptr;
};

static bool resolveScope(const QJsonObject& a, ModelScope& scope, QString* err)
{
	if (a.contains("mixerChannel"))
	{
		const int ch = a["mixerChannel"].toInt(-1);
		if (ch < 0 || ch >= Engine::mixer()->numChannels())
		{
			*err = QString("mixerChannel must be 0..%1").arg(Engine::mixer()->numChannels() - 1);
			return false;
		}
		scope.mixerChannel = ch;
		scope.chain = &Engine::mixer()->mixerChannel(ch)->m_fxChain;
		return true;
	}
	if (!a.contains("track")) { *err = "Give either track or mixerChannel"; return false; }
	scope.track = trackAt(a["track"].toInt(-1), err);
	if (!scope.track) { return false; }
	if (auto it = dynamic_cast<InstrumentTrack*>(scope.track)) { scope.chain = it->audioBusHandle()->effects(); }
	else if (auto st = dynamic_cast<SampleTrack*>(scope.track)) { scope.chain = st->audioBusHandle()->effects(); }
	if (!scope.chain) { *err = QString("Track %1 is not an instrument or sample track").arg(a["track"].toInt()); return false; }
	return true;
}

//! Resolves `target` ("track" | "instrument" | "effect:N") to the model whose parameters are meant.
static Model* resolveTarget(const ModelScope& scope, const QString& target, QString* err)
{
	if (target.startsWith("effect:"))
	{
		bool isInt = false;
		const int n = target.mid(7).toInt(&isInt);
		const auto& fx = scope.chain->effects();
		if (!isInt || n < 0 || n >= int(fx.size()))
		{
			*err = QString("No effect %1 (chain has %2)").arg(target.mid(7)).arg(fx.size());
			return nullptr;
		}
		return fx[n];
	}
	if (!scope.track) { *err = "On a mixer channel the target must be effect:N"; return nullptr; }
	if (target == "track") { return scope.track; }
	if (target == "instrument")
	{
		auto it = dynamic_cast<InstrumentTrack*>(scope.track);
		if (!it || !it->instrument()) { *err = "Track has no instrument loaded"; return nullptr; }
		return it->instrument();
	}
	*err = "target must be 'track', 'instrument' or 'effect:N'";
	return nullptr;
}

//! An automatable model and its "Parent>Name" path below the root it was listed under.
struct NamedModel
{
	AutomatableModel* model;
	QString path;
};

static QString pathUnder(Model* root, AutomatableModel* m)
{
	const QString prefix = root->fullDisplayName();
	const QString full = m->fullDisplayName();
	return !prefix.isEmpty() && full.startsWith(prefix + ">") ? full.mid(prefix.size() + 1) : full;
}

//! Automatable models under `root`. Plugins are searched whole (parameters usually sit in a
//! controls sub-model). A track lists its own models first, then those of its sub-models
//! (envelopes/LFOs, filter, arpeggio, chords, MIDI port); its instrument is a separate target
//! and each clip carries its own mute switch, so both subtrees are left out.
static std::vector<NamedModel> modelsOf(Model* root)
{
	std::vector<NamedModel> out;
	auto add = [&](AutomatableModel* m) { out.push_back({m, pathUnder(root, m)}); };
	if (!dynamic_cast<Track*>(root))
	{
		for (auto m : root->findChildren<AutomatableModel*>()) { add(m); }
		return out;
	}
	QList<QObject*> nested;
	for (auto child : root->children())
	{
		if (dynamic_cast<Instrument*>(child) || dynamic_cast<Clip*>(child)) { continue; }
		if (auto m = dynamic_cast<AutomatableModel*>(child)) { add(m); }
		else { nested.append(child); }
	}
	for (auto child : nested)
	{
		for (auto m : child->findChildren<AutomatableModel*>()) { add(m); }
	}
	return out;
}

//! Model called `name` under `root`: displayName match first (case-insensitive), then the path
//! or a "Parent>Name" suffix of it, so duplicates such as the envelope attacks can be told apart.
static AutomatableModel* findModel(Model* root, const QString& name)
{
	if (name.isEmpty()) { return nullptr; }
	const auto models = modelsOf(root);
	for (auto& [m, path] : models)
	{
		if (m->displayName().compare(name, Qt::CaseInsensitive) == 0) { return m; }
	}
	for (auto& [m, path] : models)
	{
		if (path.compare(name, Qt::CaseInsensitive) == 0 || path.endsWith(">" + name, Qt::CaseInsensitive)) { return m; }
	}
	return nullptr;
}

//! Quick mixing: name, volume, pan, mute, solo and mixer channel of one track in a single call.
//! Volume/pan/mixer channel exist on instrument and sample tracks only; everything is checked
//! before anything is written.
static QJsonObject setTrack(const QJsonObject& a)
{
	QString err;
	auto track = trackAt(a["index"].toInt(-1), &err);
	if (!track) { return R::error(err); }
	// Both track types name their models "Volume"/"Panning"; SampleTrack has no accessor for them.
	AutomatableModel* volume = findModel(track, "Volume");
	AutomatableModel* pan = findModel(track, "Panning");
	IntModel* mixer = nullptr;
	if (auto it = dynamic_cast<InstrumentTrack*>(track)) { mixer = it->mixerChannelModel(); }
	else if (auto st = dynamic_cast<SampleTrack*>(track)) { mixer = st->mixerChannelModel(); }
	const QString notAudio = QString("Track %1 is not an instrument or sample track").arg(a["index"].toInt());
	if (a.contains("volume"))
	{
		if (!volume) { return R::error(notAudio); }
		const double v = a["volume"].toDouble(-1);
		if (v < MinVolume || v > MaxVolume) { return R::error(QString("volume must be %1..%2").arg(int(MinVolume)).arg(int(MaxVolume))); }
	}
	if (a.contains("pan"))
	{
		if (!pan) { return R::error(notAudio); }
		const double v = a["pan"].toDouble(PanningLeft - 1);
		if (v < PanningLeft || v > PanningRight) { return R::error(QString("pan must be %1..%2").arg(int(PanningLeft)).arg(int(PanningRight))); }
	}
	if (a.contains("mixerChannel"))
	{
		if (!mixer) { return R::error(notAudio); }
		const int ch = a["mixerChannel"].toInt(-1);
		if (ch < 0 || ch >= Engine::mixer()->numChannels()) { return R::error(QString("mixerChannel must be 0..%1").arg(Engine::mixer()->numChannels() - 1)); }
	}
	{
		auto guard = Engine::audioEngine()->requestChangesGuard();
		if (a.contains("name")) { track->setName(a["name"].toString()); }
		if (a.contains("volume")) { volume->setValue(float(a["volume"].toDouble())); }
		if (a.contains("pan")) { pan->setValue(float(a["pan"].toDouble())); }
		if (a.contains("muted")) { track->setMuted(a["muted"].toBool()); }
		// The solo model does the cross-track muting through the song editor's view, as the solo button does.
		if (a.contains("solo")) { track->setSolo(a["solo"].toBool()); }
		if (a.contains("mixerChannel"))
		{
			// The model's range is only widened by the mixer view when channels are added; refresh it as set_mixer_xml does.
			mixer->setRange(0, Engine::mixer()->numChannels() - 1);
			mixer->setValue(a["mixerChannel"].toInt());
		}
	}
	QJsonObject out{{"index", a["index"].toInt()}, {"name", track->name()}, {"muted", track->isMuted()}, {"solo", track->isSolo()}};
	if (volume) { out["volume"] = double(volume->value<float>()); }
	if (pan) { out["pan"] = double(pan->value<float>()); }
	if (mixer) { out["mixerChannel"] = mixer->value(); }
	return R::ok(out);
}

static QJsonArray describeModels(Model* root)
{
	QJsonArray out;
	if (!root) { return out; }
	for (auto& [m, path] : modelsOf(root))
	{
		if (m->displayName().isEmpty()) { continue; }
		out.append(QJsonObject{{"name", m->displayName()}, {"path", path}, {"value", double(m->value<float>())},
			{"min", double(m->minValue<float>())}, {"max", double(m->maxValue<float>())}, {"automated", m->isAutomated()}});
	}
	return out;
}

//! Sets each `params` entry on the model of that name under `root`. Every name is resolved
//! before anything is written, so an unknown one leaves the project untouched. On success
//! `applied` holds the values as the models clamped them.
static bool applyParams(Model* root, const QJsonObject& params, QJsonObject& applied, QString* err)
{
	if (params.isEmpty()) { *err = "params must be a non-empty object of name: value"; return false; }
	struct Edit { QString name; AutomatableModel* model; float value; };
	std::vector<Edit> pending;
	for (auto it = params.begin(); it != params.end(); ++it)
	{
		auto m = findModel(root, it.key());
		if (!m) { *err = "Unknown parameter '" + it.key() + "' (use describe_model_tree)"; return false; }
		if (it.value().isBool()) { pending.push_back({it.key(), m, it.value().toBool() ? 1.f : 0.f}); }
		else if (it.value().isDouble()) { pending.push_back({it.key(), m, float(it.value().toDouble())}); }
		else { *err = "Parameter '" + it.key() + "' must be a number"; return false; }
	}
	{
		// Raw model edits while the engine may be reading them.
		auto guard = Engine::audioEngine()->requestChangesGuard();
		for (auto& e : pending) { e.model->setValue(e.value); }
	}
	for (auto& e : pending) { applied[e.name] = double(e.model->value<float>()); }
	return true;
}

static QJsonObject addEffect(const QJsonObject& a)
{
	ModelScope scope;
	QString err;
	if (!resolveScope(a, scope, &err)) { return R::error(err); }
	const QString plugin = a["effect"].toString();
	const auto info = PluginFactory::instance()->pluginInfo(plugin.toUtf8().constData());
	if (plugin.isEmpty() || info.isNull() || info.descriptor->type != Plugin::Type::Effect)
	{
		return R::error("Unknown effect plugin: " + plugin + " (use list_effects)");
	}
	// LADSPA/VST/LV2 hosts need a sub-plugin key; only self-contained effects can be added by name.
	if (info.descriptor->subPluginFeatures) { return R::error(plugin + " hosts sub-plugins and cannot be added by name"); }
	auto effect = Effect::instantiate(plugin, scope.chain, nullptr);
	if (!effect) { return R::error("Failed to instantiate effect " + plugin); }
	// Parameters are applied before the effect is in the chain, so a bad name leaves the chain as it was.
	QJsonObject applied;
	if (a.contains("params") && !applyParams(effect, a["params"].toObject(), applied, &err))
	{
		delete effect;
		return R::error(err);
	}
	// appendEffect takes the audio-engine change lock itself; do not guard it.
	scope.chain->appendEffect(effect);
	return R::ok({{"effectIndex", int(scope.chain->effects().size()) - 1}, {"params", applied}});
}

static QJsonObject setParams(const QJsonObject& a)
{
	ModelScope scope;
	QString err;
	if (!resolveScope(a, scope, &err)) { return R::error(err); }
	auto root = resolveTarget(scope, a["target"].toString("track"), &err);
	if (!root) { return R::error(err); }
	QJsonObject applied;
	if (!applyParams(root, a["params"].toObject(), applied, &err)) { return R::error(err); }
	return R::ok({{"applied", applied}});
}

static QJsonObject describeModelTree(const QJsonObject& a)
{
	ModelScope scope;
	QString err;
	if (!resolveScope(a, scope, &err)) { return R::error(err); }
	QJsonArray effects;
	int i = 0;
	for (auto fx : scope.chain->effects())
	{
		effects.append(QJsonObject{{"target", QString("effect:%1").arg(i++)}, {"name", fx->displayName()}, {"params", describeModels(fx)}});
	}
	QJsonObject out{{"effects", effects}};
	if (scope.track)
	{
		out["track"] = describeModels(scope.track);
		auto it = dynamic_cast<InstrumentTrack*>(scope.track);
		out["instrument"] = describeModels(it ? it->instrument() : nullptr);
	}
	return R::ok(out);
}

static QJsonObject addAutomation(const QJsonObject& a)
{
	ModelScope scope;
	QString err;
	if (!resolveScope(a, scope, &err)) { return R::error(err); }
	auto root = resolveTarget(scope, a["target"].toString("track"), &err);
	if (!root) { return R::error(err); }
	auto model = findModel(root, a["model"].toString());
	if (!model) { return R::error("Unknown model '" + a["model"].toString() + "' (use describe_model_tree)"); }
	const auto points = a["points"].toArray();
	if (points.isEmpty()) { return R::error("points must be a non-empty array of {pos, value}"); }
	for (auto v : points)
	{
		auto p = v.toObject();
		if (!p["pos"].isDouble() || p["pos"].toInt() < 0 || !p["value"].isDouble())
		{
			return R::error("each point needs pos >= 0 (ticks) and a numeric value");
		}
	}
	const QString prog = a["progression"].toString("linear");
	AutomationClip::ProgressionType type;
	if (prog == "discrete") { type = AutomationClip::ProgressionType::Discrete; }
	else if (prog == "linear") { type = AutomationClip::ProgressionType::Linear; }
	else if (prog == "cubic") { type = AutomationClip::ProgressionType::CubicHermite; }
	else { return R::error("progression must be discrete, linear or cubic"); }
	// Track::create and createClip (Clip::movePosition) take the audio-engine change lock themselves.
	auto atrack = Track::create(Track::Type::Automation, Engine::getSong());
	if (!atrack) { return R::error("Could not create automation track"); }
	atrack->setName((scope.track ? scope.track->name() : QString("Mixer %1").arg(scope.mixerChannel)) + " / " + model->displayName());
	auto clip = dynamic_cast<AutomationClip*>(atrack->createClip(TimePos(0)));
	if (!clip) { return R::error("Could not create automation clip"); }
	// The clip serialises node edits with its own mutex, like the automation editor does.
	clip->setProgressionType(type);
	clip->addObject(model);
	for (auto v : points)
	{
		auto p = v.toObject();
		// Unquantised; the default ignoreSurroundingPoints=true keeps every given point verbatim.
		clip->putValue(TimePos(p["pos"].toInt()), float(p["value"].toDouble()), false);
	}
	return R::ok({{"automationTrack", int(Engine::getSong()->tracks().size()) - 1}, {"model", model->displayName()}, {"points", points.size()}});
}

static QJsonObject addSampleClip(const QJsonObject& a, const std::function<bool(const QString&)>& pathAllowed)
{
	const QString file = a["file"].toString();
	if (pathAllowed && !pathAllowed(file)) { return R::error("Path not allowed: " + file); }
	if (file.isEmpty() || !QFileInfo(file).isFile()) { return R::error("File not found: " + file); }
	const int pos = a["pos"].toInt(0);
	if (pos < 0) { return R::error("pos must be >= 0 ticks"); }
	// Decode before touching the project, so an unreadable file changes nothing. (SampleBuffer::fromFile
	// would pop up a message box in-app instead of reporting the failure.)
	auto decoded = SampleDecoder::decode(PathUtil::toAbsolute(file));
	if (!decoded || decoded->data.empty()) { return R::error("Could not decode audio file: " + file); }
	auto buffer = std::make_shared<SampleBuffer>(std::move(decoded->data), decoded->sampleRate, PathUtil::toShortestRelative(file));
	SampleTrack* track = nullptr;
	QString err;
	if (a.contains("track"))
	{
		track = dynamic_cast<SampleTrack*>(trackAt(a["track"].toInt(-1), &err));
		if (!track) { return R::error(err.isEmpty() ? QString("Track %1 is not a sample track").arg(a["track"].toInt()) : err); }
	}
	else
	{
		// Track::create takes the audio-engine change lock itself; do not guard it.
		track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
		if (!track) { return R::error("Could not create sample track"); }
		track->setName(QFileInfo(file).completeBaseName());
	}
	// createClip -> Clip::movePosition and setSampleBuffer take the change lock themselves.
	auto clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(pos)));
	if (!clip) { return R::error("Could not create sample clip"); }
	clip->setSampleBuffer(std::move(buffer));
	const auto& tracks = Engine::getSong()->tracks();
	const int index = int(std::find(tracks.begin(), tracks.end(), track) - tracks.begin());
	return R::ok({{"track", index}, {"pos", pos}, {"len", clip->length().getTicks()}});
}

void registerAiProjectTools(AiToolRegistry& r, std::function<bool(const QString&)> pathAllowed)
{
	// Shared schema pieces for add_notes / add_clips.
	const QJsonObject noteItems = schema({{"pos", prop("integer", "ticks from clip start")}, {"len", prop("integer", "ticks, > 0")},
		{"key", prop("integer", "MIDI key 0..127, 60 = C4")}, {"vol", prop("integer", "0..200, default 100")}, {"pan", prop("integer", "-100..100, default 0")}}, {"pos", "len", "key"});
	const QJsonObject notesArray{{"type", "array"}, {"items", noteItems}};
	const QJsonObject clipPos = prop("integer", "clip start, absolute ticks from song start (bar N = (N-1)*ticksPerBar)");
	const QJsonObject clipLen = prop("integer", "clip length in ticks; must cover the notes. Omit to auto-size to whole bars");
	const QJsonObject clipName = prop("string", "clip name shown in the song editor, e.g. 'Verse'");
	const QJsonObject clipClear = prop("boolean", "replace the notes of an existing clip at clipPos instead of adding to them");
	const QJsonObject trackIndex = prop("integer", "track index from get_project_summary");
	const QJsonObject trackOrMixer = prop("integer", "track index (or give mixerChannel instead)");
	const QJsonObject mixerIndex = prop("integer", "mixer channel index, 0 = master");

	r.add({"get_project_summary",
		"WHAT: compact state of the open project: bpm, time signature, ticksPerBar, lengthBars, every track (index, type, instrument, volume, muted, clips with pos/len/noteCount). "
		"WHEN: first call of every turn and last call to verify; cheaper than get_track_xml. RETURNS the summary object. Gotcha: indices shift after remove_track.",
		schema({}), projectSummary});
	r.add({"get_head",
		"WHAT: tempo, time signature, master volume and master pitch only. WHEN: you need masterVol/masterPitch, which get_project_summary omits. "
		"RETURNS {bpm, timesigNum, timesigDen, masterVol, masterPitch}.",
		schema({}), getHead});
	r.add({"set_head",
		"WHAT: set tempo, time signature, master volume/pitch; every field optional. WHEN: once at the start of a song, before writing notes (ticksPerBar depends on the time signature). "
		"UNITS: bpm 10..999, masterVol 0..200, masterPitch semitones. RETURNS the new head. Gotcha: changing the time signature later does not move existing clips.",
		schema({{"bpm", prop("integer", "10..999")}, {"timesigNum", prop("integer", "beats per bar, e.g. 4")}, {"timesigDen", prop("integer", "beat unit, e.g. 4")},
			{"masterVol", prop("integer", "0..200, 100 = unity")}, {"masterPitch", prop("integer", "-12..12 semitones")}}), setHead});
	r.add({"add_instrument_track",
		"WHAT: append an instrument track loading a plugin by name (list_instruments). WHEN: one call per part: drums, bass, chords, lead. Use add_track with get_preset_xml when you want a preset sound instead. "
		"RETURNS {index} for add_clips/add_notes/set_track. Gotcha: omitting instrument gives a silent empty track.",
		schema({{"name", prop("string", "track name, e.g. 'Bass'")}, {"instrument", prop("string", "plugin name from list_instruments, e.g. tripleoscillator, kicker, sf2player")},
			{"mixerChannel", mixerIndex}}), addInstrumentTrack});
	r.add({"add_sf2_track",
		"WHAT: append an instrument track playing one SoundFont preset (sf2player). WHEN: realistic instruments: piano, guitars, bass, drum kits, strings, brass — the default palette. "
		"bank 0 = melodic GM patches (0 piano, 33 finger bass, 27 clean guitar, 29 overdriven, 30 distortion, 48 strings), bank 128 = drum kits (patch 0 standard; GM drum map: 36 kick, 38 snare, 42 closed hat, 46 open hat, 49 crash, 51 ride). "
		"RETURNS {index}. Gotcha: file must be an absolute path inside the allowed roots (the LMMS working directory's samples/soundfonts/ is).",
		schema({{"name", prop("string", "track name")}, {"file", prop("string", "absolute path to the .sf2")},
			{"bank", prop("integer", "0..128, default 0; 128 = drum kits")}, {"patch", prop("integer", "0..127 GM program, default 0")},
			{"mixerChannel", mixerIndex}}, {"file"}),
		[pathAllowed](const QJsonObject& a) { return addSf2Track(a, pathAllowed); }});
	r.add({"add_notes",
		"WHAT: write notes into one MIDI clip on an instrument track, creating the clip at clipPos if missing. WHEN: a single clip, or editing one (clear:true replaces its notes); several clips per track → add_clips. "
		"UNITS: ticks (192/bar in 4/4: quarter 48, eighth 24, sixteenth 12); note pos is relative to the clip. RETURNS {track, clipPos, len, noteCount}. Gotcha: without len the clip auto-grows to whole bars covering its notes.",
		schema({{"track", trackIndex}, {"clipPos", clipPos}, {"len", clipLen}, {"name", clipName}, {"notes", notesArray}, {"clear", clipClear}}, {"track", "clipPos", "notes"}), addNotes});
	r.add({"add_clips",
		"WHAT: batch of add_notes: several clips on one instrument track in one call (intro/verse/chorus material). WHEN: laying out a song section by section; one call per track. "
		"Each clip is {clipPos, notes, len?, name?, clear?} exactly as add_notes. All are validated before any is written: one bad note creates nothing. RETURNS {track, clipCount, noteCount, clips}. Gotcha: clipPos must equal the start of a clip you want to reuse.",
		schema({{"track", trackIndex}, {"clips", QJsonObject{{"type", "array"}, {"items",
			schema({{"clipPos", clipPos}, {"len", clipLen}, {"name", clipName}, {"notes", notesArray}, {"clear", clipClear}}, {"clipPos", "notes"})}}}}, {"track", "clips"}), addClips});
	r.add({"remove_clip",
		"WHAT: delete the clip that starts at clipPos on a track (MIDI, sample or automation). WHEN: dropping a section or redoing one clip; to rewrite notes in place use add_notes with clear:true instead. "
		"UNITS: absolute ticks, as listed by get_project_summary. RETURNS {track, clipCount}. Gotcha: clipPos must match the clip's start exactly.",
		schema({{"track", trackIndex}, {"clipPos", prop("integer", "clip start in absolute ticks")}}, {"track", "clipPos"}), removeClip});
	r.add({"set_track",
		"WHAT: quick mix of one track: name, volume, pan, mute, solo, mixer channel; every field optional. WHEN: balancing levels and panning after writing parts; faster than describe_model_tree + set_params. "
		"UNITS: volume 0..200 (100 = unity), pan -100..100. RETURNS the resulting values. Gotcha: volume/pan/mixerChannel need an instrument or sample track; use add_automation for changes over time.",
		schema({{"index", trackIndex}, {"name", prop("string", "new track name")}, {"volume", prop("number", "0..200, 100 = unity")}, {"pan", prop("number", "-100 (left)..100 (right)")},
			{"muted", prop("boolean", "mute the track")}, {"solo", prop("boolean", "solo the track")}, {"mixerChannel", mixerIndex}}, {"index"}), setTrack});
	r.add({"get_track_xml",
		"WHAT: the full <track> element of one track in .mmp format: instrument settings, effect chain, clips and notes. WHEN: you need something the convenience tools do not expose, or as a template for add_track/replace_track. "
		"RETURNS {index, xml}. Gotcha: fails over 64 KB; fall back to get_project_summary.",
		schema({{"index", trackIndex}}, {"index"}), getTrackXml});
	r.add({"add_track",
		"WHAT: append a track from <track> XML (from get_track_xml, or <track type=\"0\" name=\"..\"> wrapping get_preset_xml output). WHEN: using a preset sound, cloning a track, or any feature the convenience tools lack. "
		"RETURNS {index}. Gotcha: current .mmp format only; local: plugin paths are rejected.",
		schema({{"xml", prop("string", "<track type=\"0|1|2|5\" name=\"...\">...</track>")}}, {"xml"}), addTrackXml});
	r.add({"replace_track",
		"WHAT: swap the track at index for new <track> XML, keeping its position. WHEN: changing a track's instrument or effects wholesale after editing get_track_xml output. "
		"RETURNS {index}. Gotcha: automation clips on other tracks that targeted the old track's parameters are disconnected.",
		schema({{"index", trackIndex}, {"xml", prop("string", "<track ...>...</track>")}}, {"index", "xml"}), replaceTrackXml});
	r.add({"remove_track",
		"WHAT: delete a track and all its clips. WHEN: removing a part you replaced; for one clip use remove_clip. RETURNS {trackCount}. Gotcha: every track after it moves down one index; re-read get_project_summary.",
		schema({{"index", trackIndex}}, {"index"}), removeTrack});
	r.add({"get_mixer_xml",
		"WHAT: the whole mixer as <mixer> XML: channels, names, volumes, sends, effect chains. WHEN: building buses or sends (edit and pass to set_mixer_xml). RETURNS {xml}. Gotcha: add_effect with mixerChannel is enough for channel effects.",
		schema({}), getMixerXml});
	r.add({"set_mixer_xml",
		"WHAT: replace the entire mixer from <mixer> XML. WHEN: after editing get_mixer_xml output to add channels or sends. RETURNS {channels}. Gotcha: replaces everything; tracks keep their channel numbers, so keep the channels they point at.",
		schema({{"xml", prop("string", "<mixer>...</mixer>")}}, {"xml"}), setMixerXml});
	r.add({"add_effect",
		"WHAT: append an effect plugin (list_effects) to a track's chain or a mixer channel's chain, optionally with initial params. WHEN: reverb/delay/compressor/eq per part, or on a channel to share it. "
		"RETURNS {effectIndex, params}; effectIndex is the target effect:N for set_params. Gotcha: give track OR mixerChannel; param names come from describe_model_tree.",
		schema({{"track", trackOrMixer}, {"mixerChannel", mixerIndex},
			{"effect", prop("string", "plugin name from list_effects, e.g. reverbsc, delay, compressor, eq, bassbooster, amplifier")}, {"params", prop("object", "{name: value} as listed by describe_model_tree")}}, {"effect"}), addEffect});
	r.add({"set_params",
		"WHAT: set named parameters on a track, its instrument or an effect. WHEN: sound design (instrument knobs, effect settings); for level/pan/mute use set_track. "
		"Names are case-insensitive; 'Parent>Name' paths disambiguate duplicates. RETURNS {applied} with clamped values. Gotcha: one unknown name rejects the whole call; check describe_model_tree.",
		schema({{"track", trackOrMixer}, {"mixerChannel", prop("integer", "mixer channel index (then target must be effect:N)")},
			{"target", prop("string", "track | instrument | effect:N")}, {"params", prop("object", "{name: value}")}}, {"target", "params"}), setParams});
	r.add({"describe_model_tree",
		"WHAT: every automatable parameter (name, path, value, min, max, automated) of a track, its instrument and each effect, or of a mixer channel's effects. "
		"WHEN: before set_params or add_automation on anything but track Volume/Panning. RETURNS {track, instrument, effects}. Gotcha: large for complex instruments; call once and remember.",
		schema({{"track", trackOrMixer}, {"mixerChannel", mixerIndex}}), describeModelTree});
	r.add({"add_automation",
		"WHAT: new automation track with one clip driving a parameter through points over time. WHEN: filter sweeps, volume builds, fades; static values belong in set_params/set_track. "
		"UNITS: pos in absolute ticks, value in the parameter's own range. RETURNS {automationTrack, model, points}. Gotcha: each call adds one automation track; put all points of one parameter in one call.",
		schema({{"track", trackOrMixer}, {"mixerChannel", prop("integer", "mixer channel index (then target must be effect:N)")},
			{"target", prop("string", "track | instrument | effect:N")}, {"model", prop("string", "parameter name from describe_model_tree, e.g. Volume, Cutoff frequency")},
			{"points", QJsonObject{{"type", "array"}, {"items", schema({{"pos", prop("integer", "absolute ticks")}, {"value", prop("number", "parameter value")}}, {"pos", "value"})}}},
			{"progression", prop("string", "discrete | linear (default) | cubic")}}, {"target", "model", "points"}), addAutomation});
	r.add({"add_sample_clip",
		"WHAT: place an audio file (list_samples or a user path) as a clip on a sample track, creating a track named after the file unless track is given. WHEN: one-shots, loops, vocals. "
		"UNITS: pos in absolute ticks. RETURNS {track, pos, len} (len in ticks at the current tempo). Gotcha: the clip plays at natural speed; tempo changes never stretch it.",
		schema({{"file", prop("string", "path to a wav/ogg/flac/mp3/aiff file")}, {"pos", prop("integer", "clip start in absolute ticks")}, {"track", prop("integer", "existing sample track index")}}, {"file", "pos"}),
		[pathAllowed = std::move(pathAllowed)](const QJsonObject& a) { return addSampleClip(a, pathAllowed); }});
}

} // namespace lmms
