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

#include <functional>
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
#include "PatternStore.h"
#include "PluginFactory.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
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

//! Automatable models under `root`. A track lists only its own (volume, panning, pitch, ...):
//! its instrument is a child too but is a separate target. Plugins are searched recursively
//! because their parameters usually sit in a controls sub-model.
static QList<AutomatableModel*> modelsOf(Model* root)
{
	return root->findChildren<AutomatableModel*>(dynamic_cast<Track*>(root) ? Qt::FindDirectChildrenOnly : Qt::FindChildrenRecursively);
}

//! Model called `name` under `root`: displayName match first (case-insensitive), then a
//! "Parent>Name" suffix of fullDisplayName so nested parameters can be disambiguated.
static AutomatableModel* findModel(Model* root, const QString& name)
{
	if (name.isEmpty()) { return nullptr; }
	const auto models = modelsOf(root);
	for (auto m : models)
	{
		if (m->displayName().compare(name, Qt::CaseInsensitive) == 0) { return m; }
	}
	for (auto m : models)
	{
		if (m->fullDisplayName().endsWith(">" + name, Qt::CaseInsensitive)) { return m; }
	}
	return nullptr;
}

static QJsonArray describeModels(Model* root)
{
	QJsonArray out;
	if (!root) { return out; }
	for (auto m : modelsOf(root))
	{
		if (m->displayName().isEmpty()) { continue; }
		out.append(QJsonObject{{"name", m->displayName()}, {"value", double(m->value<float>())},
			{"min", double(m->minValue<float>())}, {"max", double(m->maxValue<float>())}});
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
		// Unquantised, and keep neighbouring nodes: every given point must survive verbatim.
		clip->putValue(TimePos(p["pos"].toInt()), float(p["value"].toDouble()), false, true);
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
	// Decode before touching the project, so an unreadable file changes nothing.
	auto buffer = SampleBuffer::fromFile(file);
	if (buffer->empty()) { return R::error("Could not decode audio file: " + file); }
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
	r.add({"get_project_summary", "Compact overview of the open project: tempo, time signature, tracks, clips. Call this first.", schema({}), projectSummary});
	r.add({"get_head", "Tempo, time signature, master volume/pitch.", schema({}), getHead});
	r.add({"set_head", "Set tempo (bpm), time signature, master volume/pitch. All fields optional.",
		schema({{"bpm", prop("integer", "10..999")}, {"timesigNum", prop("integer", "")}, {"timesigDen", prop("integer", "")},
			{"masterVol", prop("integer", "0..200")}, {"masterPitch", prop("integer", "-12..12 semitones")}}), setHead});
	r.add({"add_instrument_track", "Create an instrument track with the given plugin (see list_instruments); omit instrument for an empty track. Returns its index.",
		schema({{"name", prop("string", "track name")}, {"instrument", prop("string", "plugin name, e.g. tripleoscillator, kicker, sf2player; omit for an empty track")},
			{"mixerChannel", prop("integer", "mixer channel, 0 = master")}}), addInstrumentTrack});
	r.add({"add_notes", "Add notes to a MIDI clip on an instrument track (creates the clip at clipPos if missing). Ticks: 192 per bar in 4/4, quarter=48, 16th=12. key is MIDI number (60 = C4).",
		schema({{"track", prop("integer", "track index")}, {"clipPos", prop("integer", "clip start in ticks")},
			{"notes", QJsonObject{{"type", "array"}, {"items", schema({{"pos", prop("integer", "ticks from clip start")}, {"len", prop("integer", "ticks")},
				{"key", prop("integer", "0..127")}, {"vol", prop("integer", "0..200, default 100")}, {"pan", prop("integer", "-100..100")}}, {"pos", "len", "key"})}}},
			{"clear", prop("boolean", "remove existing notes first")}}, {"track", "clipPos", "notes"}), addNotes});
	r.add({"get_track_xml", "Full <track> XML for one track (instrument settings, effects, clips, notes). Same format as .mmp files.",
		schema({{"index", prop("integer", "track index")}}, {"index"}), getTrackXml});
	r.add({"add_track", "Append a track from <track> XML (as returned by get_track_xml or built from a preset); current .mmp format only (no legacy upgrade). Returns its index.",
		schema({{"xml", prop("string", "<track type=\"0\" name=\"...\">...</track>")}}, {"xml"}), addTrackXml});
	r.add({"replace_track", "Replace the track at index with new <track> XML, keeping its position; current .mmp format only (no legacy upgrade). Automation on other tracks targeting the old track's models is dropped.",
		schema({{"index", prop("integer", "track index")}, {"xml", prop("string", "<track ...>...</track>")}}, {"index", "xml"}), replaceTrackXml});
	r.add({"remove_track", "Delete a track. Returns the remaining track count.", schema({{"index", prop("integer", "track index")}}, {"index"}), removeTrack});
	r.add({"get_mixer_xml", "Mixer channels, names, volumes, sends and channel effect chains as <mixer> XML.", schema({}), getMixerXml});
	r.add({"set_mixer_xml", "Replace the whole mixer from <mixer> XML (as returned by get_mixer_xml). Track routing is preserved.",
		schema({{"xml", prop("string", "<mixer>...</mixer>")}}, {"xml"}), setMixerXml});
	r.add({"add_effect", "Add an effect plugin (see list_effects) to an instrument/sample track's chain or a mixer channel's chain. Optional params are applied as with set_params.",
		schema({{"track", prop("integer", "track index (or give mixerChannel)")}, {"mixerChannel", prop("integer", "mixer channel index")},
			{"effect", prop("string", "plugin name, e.g. amplifier, reverbsc, eq")}, {"params", prop("object", "{name: value} as listed by describe_model_tree")}}, {"effect"}), addEffect});
	r.add({"set_params", "Set parameters by name on a track (volume, panning, pitch...), its instrument, or one of its effects. Names are case-insensitive; describe_model_tree lists them with ranges.",
		schema({{"track", prop("integer", "track index (or give mixerChannel)")}, {"mixerChannel", prop("integer", "mixer channel index (target must be effect:N)")},
			{"target", prop("string", "track | instrument | effect:N")}, {"params", prop("object", "{name: value}")}}, {"target", "params"}), setParams});
	r.add({"describe_model_tree", "List automatable parameters (name, value, min, max) of a track, its instrument and each effect. Call before set_params or add_automation.",
		schema({{"track", prop("integer", "track index (or give mixerChannel)")}, {"mixerChannel", prop("integer", "mixer channel index")}}), describeModelTree});
	r.add({"add_automation", "Create an automation track with one clip driving a parameter through the given points. Ticks: 192 per bar in 4/4.",
		schema({{"track", prop("integer", "track index (or give mixerChannel)")}, {"mixerChannel", prop("integer", "mixer channel index (target must be effect:N)")},
			{"target", prop("string", "track | instrument | effect:N")}, {"model", prop("string", "parameter name from describe_model_tree")},
			{"points", QJsonObject{{"type", "array"}, {"items", schema({{"pos", prop("integer", "ticks")}, {"value", prop("number", "parameter value")}}, {"pos", "value"})}}},
			{"progression", prop("string", "discrete | linear (default) | cubic")}}, {"target", "model", "points"}), addAutomation});
	r.add({"add_sample_clip", "Place an audio file as a clip on a sample track (a new one named after the file unless track is given). Returns the track index and clip length in ticks.",
		schema({{"file", prop("string", "path to a wav/ogg/flac/aiff file")}, {"pos", prop("integer", "clip start in ticks")}, {"track", prop("integer", "existing sample track index")}}, {"file", "pos"}),
		[pathAllowed = std::move(pathAllowed)](const QJsonObject& a) { return addSampleClip(a, pathAllowed); }});
}

} // namespace lmms
