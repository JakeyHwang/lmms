/*
 * AiProjectToolsTest.cpp - engine-backed tests for the AI Composer project tools
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

#include <QtTest>
#include <QDataStream>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>

#include "AiToolRegistry.h"
#include "AiTools.h"
#include "AutomationClip.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Mixer.h"
#include "PluginFactory.h"
#include "ProjectJournal.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"
#include "Track.h"

class AiProjectToolsTest : public QObject
{
	Q_OBJECT
	lmms::AiToolRegistry reg;

	//! Plugins live in DLLs that import from lmms.exe, so they cannot be loaded into a test
	//! process on Windows; elsewhere they need LMMS_PLUGIN_DIR to point at the plugin dir.
	static bool hasPlugin(const char* name)
	{
		return !lmms::PluginFactory::instance()->pluginInfo(name).isNull();
	}
	static bool hasTripleOscillator() { return hasPlugin("tripleoscillator"); }
	//! One second of 16-bit mono PCM at 44100 Hz, written to `dir`; returns the path.
	static QString writeTestWav(const QTemporaryDir& dir)
	{
		const quint32 rate = 44100, frames = 44100;
		QByteArray bytes;
		QDataStream ds(&bytes, QIODevice::WriteOnly);
		ds.setByteOrder(QDataStream::LittleEndian);
		ds.writeRawData("RIFF", 4); ds << quint32(36 + frames * 2); ds.writeRawData("WAVE", 4);
		ds.writeRawData("fmt ", 4); ds << quint32(16) << quint16(1) << quint16(1) << rate << quint32(rate * 2) << quint16(2) << quint16(16);
		ds.writeRawData("data", 4); ds << quint32(frames * 2);
		for (quint32 i = 0; i < frames; ++i) { ds << qint16(int(i % 100) * 300 - 15000); }
		QFile f(dir.path() + "/beep.wav");
		if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size()) { return {}; }
		return f.fileName();
	}
	static QJsonObject modelNamed(const QJsonArray& models, const QString& name)
	{
		for (auto v : models) { if (v.toObject()["name"].toString() == name) { return v.toObject(); } }
		return {};
	}
	//! Instrument track with no plugin loaded (add_instrument_track without `instrument`): enough for note/clip tools.
	int addBareInstrumentTrack(const QString& name = {})
	{
		QJsonObject args;
		if (!name.isEmpty()) { args["name"] = name; }
		auto r = reg.call("add_instrument_track", args);
		if (!r["ok"].toBool()) { qFatal("add_instrument_track failed: %s", qPrintable(r["error"].toString())); }
		return r["index"].toInt();
	}

private slots:
	void initTestCase() { lmms::Engine::init(true); lmms::registerAiProjectTools(reg); }
	void cleanupTestCase() { lmms::Engine::destroy(); }
	void init() { lmms::Engine::getSong()->clearProject(); }

	void headRoundTrip()
	{
		auto r = reg.call("set_head", {{"bpm", 92}, {"timesigNum", 3}, {"timesigDen", 4}});
		QVERIFY(r["ok"].toBool());
		auto h = reg.call("get_head", {});
		QCOMPARE(h["bpm"].toInt(), 92);
		QCOMPARE(h["timesigNum"].toInt(), 3);
		QVERIFY(!reg.call("set_head", {{"bpm", 5}})["ok"].toBool()); // below MinTempo
	}
	void addInstrumentTrackAndNotes()
	{
		if (!hasTripleOscillator()) { QSKIP("tripleoscillator plugin not loadable in this test process"); }
		auto r = reg.call("add_instrument_track", {{"name", "Lead"}, {"instrument", "tripleoscillator"}});
		QVERIFY2(r["ok"].toBool(), qPrintable(r["error"].toString()));
		int idx = r["index"].toInt();
		auto notes = QJsonArray{QJsonObject{{"pos", 0}, {"len", 48}, {"key", 60}}, QJsonObject{{"pos", 48}, {"len", 48}, {"key", 64}, {"vol", 80}}};
		auto n = reg.call("add_notes", {{"track", idx}, {"clipPos", 0}, {"notes", notes}});
		QVERIFY2(n["ok"].toBool(), qPrintable(n["error"].toString()));
		auto track = dynamic_cast<lmms::InstrumentTrack*>(lmms::Engine::getSong()->tracks()[idx]);
		QVERIFY(track);
		QCOMPARE(track->instrumentName(), QString("TripleOscillator")); // Plugin::Descriptor::displayName
		QCOMPARE(track->name(), QString("Lead"));
		auto clip = dynamic_cast<lmms::MidiClip*>(track->getClips()[0]);
		QCOMPARE(int(clip->notes().size()), 2);
		QCOMPARE(clip->notes()[1]->key(), 64);
		QCOMPARE(int(clip->notes()[1]->getVolume()), 80);
		auto s = reg.call("get_project_summary", {});
		QCOMPARE(s["tracks"].toArray()[idx].toObject()["clips"].toArray()[0].toObject()["noteCount"].toInt(), 2);
	}
	void addNotesOnExistingTrack()
	{
		int idx = addBareInstrumentTrack();
		auto notes = QJsonArray{QJsonObject{{"pos", 0}, {"len", 48}, {"key", 60}}, QJsonObject{{"pos", 48}, {"len", 48}, {"key", 64}, {"vol", 80}}};
		auto n = reg.call("add_notes", {{"track", idx}, {"clipPos", 192}, {"notes", notes}});
		QVERIFY2(n["ok"].toBool(), qPrintable(n["error"].toString()));
		QCOMPARE(n["noteCount"].toInt(), 2);
		auto track = dynamic_cast<lmms::InstrumentTrack*>(lmms::Engine::getSong()->tracks()[idx]);
		QVERIFY(track);
		QCOMPARE(int(track->getClips().size()), 1);
		auto clip = dynamic_cast<lmms::MidiClip*>(track->getClips()[0]);
		QVERIFY(clip);
		QCOMPARE(clip->startPosition().getTicks(), 192);
		QCOMPARE(int(clip->notes().size()), 2);
		QCOMPARE(clip->notes()[1]->key(), 64);
		QCOMPARE(int(clip->notes()[1]->getVolume()), 80);

		// Same clipPos reuses the clip; "clear" replaces its notes.
		auto again = reg.call("add_notes", {{"track", idx}, {"clipPos", 192}, {"clear", true},
			{"notes", QJsonArray{QJsonObject{{"pos", 0}, {"len", 12}, {"key", 72}}}}});
		QVERIFY2(again["ok"].toBool(), qPrintable(again["error"].toString()));
		QCOMPARE(int(track->getClips().size()), 1);
		QCOMPARE(int(clip->notes().size()), 1);
		QCOMPARE(clip->notes()[0]->key(), 72);

		auto s = reg.call("get_project_summary", {});
		auto t = s["tracks"].toArray()[idx].toObject();
		QCOMPARE(t["type"].toString(), QString("instrument"));
		QCOMPARE(t["clips"].toArray()[0].toObject()["pos"].toInt(), 192);
		QCOMPARE(t["clips"].toArray()[0].toObject()["noteCount"].toInt(), 1);
		QCOMPARE(s["ticksPerBar"].toInt(), 192);
	}
	void addNotesRejectsBadInput()
	{
		QVERIFY(!reg.call("add_notes", {{"track", 99}, {"clipPos", 0}, {"notes", QJsonArray{}}})["ok"].toBool());
		int idx = addBareInstrumentTrack();
		QVERIFY(!reg.call("add_notes", {{"track", idx}, {"clipPos", 0}, {"notes", QJsonArray{QJsonObject{{"pos", 0}, {"len", 48}, {"key", 200}}}}})["ok"].toBool());
		QVERIFY(!reg.call("add_notes", {{"track", idx}, {"clipPos", 0}, {"notes", QJsonArray{QJsonObject{{"pos", 0}, {"len", 0}, {"key", 60}}}}})["ok"].toBool());
		QVERIFY(!reg.call("add_notes", {{"track", idx}, {"clipPos", 0}, {"notes", QJsonArray{QJsonObject{{"pos", 0}, {"len", 48}, {"key", 60}, {"vol", 300}}}}})["ok"].toBool());
		// Rejected input must not leave a clip behind.
		QCOMPARE(int(lmms::Engine::getSong()->tracks()[idx]->getClips().size()), 0);
		QVERIFY(!reg.call("add_instrument_track", {{"name", "Y"}, {"instrument", "no_such_plugin"}})["ok"].toBool());
		QCOMPARE(int(lmms::Engine::getSong()->tracks().size()), 1);
	}

	// --- Task 6: XML surface ---
	// Instrument plugins cannot load here (see hasTripleOscillator), so round trips use bare
	// instrument tracks: name, notes and mixer routing are asserted for real; the instrument
	// element itself is only exercised in-app.
	void trackXmlRoundTrip()
	{
		int src = addBareInstrumentTrack("Src");
		reg.call("add_notes", {{"track", src}, {"clipPos", 0}, {"notes", QJsonArray{QJsonObject{{"pos", 0}, {"len", 24}, {"key", 62}}}}});
		auto x = reg.call("get_track_xml", {{"index", src}});
		QVERIFY2(x["ok"].toBool(), qPrintable(x["error"].toString()));
		QCOMPARE(x["index"].toInt(), src);
		QString xml = x["xml"].toString();
		QVERIFY(xml.startsWith("<track"));
		QVERIFY(xml.contains("key=\"62\""));
		auto added = reg.call("add_track", {{"xml", xml.replace("name=\"Src\"", "name=\"Copy\"")}});
		QVERIFY2(added["ok"].toBool(), qPrintable(added["error"].toString()));
		QCOMPARE(added["index"].toInt(), 1);
		QCOMPARE(int(lmms::Engine::getSong()->tracks().size()), 2);
		auto copy = dynamic_cast<lmms::InstrumentTrack*>(lmms::Engine::getSong()->tracks()[1]);
		QVERIFY(copy);
		QCOMPARE(copy->name(), QString("Copy"));
		QCOMPARE(int(copy->getClips().size()), 1);
		auto clip = dynamic_cast<lmms::MidiClip*>(copy->getClips()[0]);
		QVERIFY(clip);
		QCOMPARE(int(clip->notes().size()), 1);
		QCOMPARE(clip->notes()[0]->key(), 62);
		QCOMPARE(clip->notes()[0]->length().getTicks(), 24);
		auto s = reg.call("get_project_summary", {});
		QCOMPARE(s["tracks"].toArray()[1].toObject()["clips"].toArray()[0].toObject()["noteCount"].toInt(), 1);
		QVERIFY(!reg.call("get_track_xml", {{"index", 5}})["ok"].toBool());
	}
	void invalidXmlLeavesProjectUntouched()
	{
		addBareInstrumentTrack();
		// malformed
		QVERIFY(!reg.call("add_track", {{"xml", "<track type=\"0\" name=\"x\""}})["ok"].toBool());
		// wrong root element
		QVERIFY(!reg.call("add_track", {{"xml", "<midiclip/>"}})["ok"].toBool());
		// unknown / hidden track types
		QVERIFY(!reg.call("add_track", {{"xml", "<track type=\"6\" name=\"x\"/>"}})["ok"].toBool());
		QVERIFY(!reg.call("add_track", {{"xml", "<track type=\"99\" name=\"x\"/>"}})["ok"].toBool());
		QVERIFY(!reg.call("add_track", {{"xml", "<track name=\"x\"/>"}})["ok"].toBool());
		// local plugin paths are a security hole in .mmp files and are rejected here too
		QVERIFY(!reg.call("add_track", {{"xml", "<track type=\"0\" name=\"x\"><instrumenttrack><instrument name=\"vestige\"><vestige plugin=\"local:evil.dll\"/></instrument></instrumenttrack></track>"}})["ok"].toBool());
		QCOMPARE(int(lmms::Engine::getSong()->tracks().size()), 1);
		QVERIFY(!reg.call("replace_track", {{"index", 7}, {"xml", "<track type=\"0\" name=\"x\"/>"}})["ok"].toBool());
		QVERIFY(!reg.call("replace_track", {{"index", 0}, {"xml", "<nope/>"}})["ok"].toBool());
		QVERIFY(!reg.call("remove_track", {{"index", 3}})["ok"].toBool());
		QCOMPARE(int(lmms::Engine::getSong()->tracks().size()), 1);
	}
	void replaceAndRemove()
	{
		addBareInstrumentTrack("A");
		addBareInstrumentTrack("B");
		addBareInstrumentTrack("C");
		QString xml = reg.call("get_track_xml", {{"index", 0}})["xml"].toString().replace("name=\"A\"", "name=\"A2\"");
		auto r = reg.call("replace_track", {{"index", 0}, {"xml", xml}});
		QVERIFY2(r["ok"].toBool(), qPrintable(r["error"].toString()));
		QCOMPARE(r["index"].toInt(), 0);
		auto& tracks = lmms::Engine::getSong()->tracks();
		QCOMPARE(int(tracks.size()), 3);
		QCOMPARE(tracks[0]->name(), QString("A2"));
		QCOMPARE(tracks[1]->name(), QString("B"));
		QCOMPARE(tracks[2]->name(), QString("C"));
		// middle position is kept too
		xml = reg.call("get_track_xml", {{"index", 1}})["xml"].toString().replace("name=\"B\"", "name=\"B2\"");
		QVERIFY(reg.call("replace_track", {{"index", 1}, {"xml", xml}})["ok"].toBool());
		QCOMPARE(tracks[0]->name(), QString("A2"));
		QCOMPARE(tracks[1]->name(), QString("B2"));
		QCOMPARE(tracks[2]->name(), QString("C"));
		auto rm = reg.call("remove_track", {{"index", 0}});
		QVERIFY2(rm["ok"].toBool(), qPrintable(rm["error"].toString()));
		QCOMPARE(rm["trackCount"].toInt(), 2);
		QCOMPARE(int(tracks.size()), 2);
		QCOMPARE(tracks[0]->name(), QString("B2"));
		QCOMPARE(tracks[1]->name(), QString("C"));
	}
	void mixerXmlRoundTrip()
	{
		auto mixer = lmms::Engine::mixer();
		QCOMPARE(int(mixer->createChannel()), 1);
		mixer->mixerChannel(1)->m_name = "Bus";
		mixer->mixerChannel(1)->m_volumeModel.setValue(0.5f);
		auto track = dynamic_cast<lmms::InstrumentTrack*>(lmms::Engine::getSong()->tracks()[addBareInstrumentTrack()]);
		track->mixerChannelModel()->setValue(1);
		QCOMPARE(track->mixerChannelModel()->value(), 1);

		auto m = reg.call("get_mixer_xml", {});
		QVERIFY2(m["ok"].toBool(), qPrintable(m["error"].toString()));
		QString xml = m["xml"].toString();
		QVERIFY(xml.startsWith("<mixer"));
		QVERIFY(xml.contains("name=\"Bus\""));

		// Diverge, then restore from the snapshot.
		mixer->createChannel();
		mixer->mixerChannel(1)->m_name = "Changed";
		QCOMPARE(int(mixer->numChannels()), 3);
		auto set = reg.call("set_mixer_xml", {{"xml", xml}});
		QVERIFY2(set["ok"].toBool(), qPrintable(set["error"].toString()));
		QCOMPARE(set["channels"].toInt(), 2);
		QCOMPARE(int(mixer->numChannels()), 2);
		QCOMPARE(mixer->mixerChannel(1)->m_name, QString("Bus"));
		QCOMPARE(mixer->mixerChannel(1)->m_volumeModel.value(), 0.5f);
		// Reloading the mixer must not silently re-route tracks to master.
		QCOMPARE(track->mixerChannelModel()->value(), 1);

		QVERIFY(!reg.call("set_mixer_xml", {{"xml", "<nope/>"}})["ok"].toBool());
		QVERIFY(!reg.call("set_mixer_xml", {{"xml", "<mixer><mixerchannel num=\"1\" name=\"x\" foo=\"local:evil.dll\"/></mixer>"}})["ok"].toBool());
		QCOMPARE(int(mixer->numChannels()), 2);
		QCOMPARE(mixer->mixerChannel(1)->m_name, QString("Bus"));
	}
	void addTrackResolvesAutomationIds()
	{
		int idx = addBareInstrumentTrack("Target");
		auto target = dynamic_cast<lmms::InstrumentTrack*>(lmms::Engine::getSong()->tracks()[idx]);
		QVERIFY(target);
		QVERIFY(!target->volumeModel()->isAutomated());
		// Automation-track XML as get_track_xml would return it, targeting the existing track's volume model.
		QString xml = QString(
			"<track type=\"5\" name=\"Auto\" muted=\"0\" solo=\"0\"><automationtrack/>"
			"<automationclip pos=\"0\" len=\"192\" name=\"Vol\" prog=\"1\" tens=\"0\" mute=\"0\">"
			"<time pos=\"0\" value=\"0\" outValue=\"0\" inTan=\"0\" outTan=\"0\" lockedTan=\"0\"/>"
			"<time pos=\"192\" value=\"100\" outValue=\"100\" inTan=\"0\" outTan=\"0\" lockedTan=\"0\"/>"
			"<object id=\"%1\"/></automationclip></track>").arg(lmms::ProjectJournal::idToSave(target->volumeModel()->id()));
		auto r = reg.call("add_track", {{"xml", xml}});
		QVERIFY2(r["ok"].toBool(), qPrintable(r["error"].toString()));
		auto at = lmms::Engine::getSong()->tracks()[r["index"].toInt()];
		QCOMPARE(at->type(), lmms::Track::Type::Automation);
		QCOMPARE(int(at->getClips().size()), 1);
		auto clip = dynamic_cast<lmms::AutomationClip*>(at->getClips()[0]);
		QVERIFY(clip);
		QCOMPARE(int(clip->objects().size()), 1);
		QCOMPARE(clip->firstObject(), target->volumeModel());
		QVERIFY(target->volumeModel()->isAutomated());
		QCOMPARE(clip->valueAt(96), 50.0f);

		// replace_track goes through the same path.
		auto r2 = reg.call("replace_track", {{"index", r["index"].toInt()}, {"xml", xml.replace("name=\"Auto\"", "name=\"Auto2\"")}});
		QVERIFY2(r2["ok"].toBool(), qPrintable(r2["error"].toString()));
		auto at2 = lmms::Engine::getSong()->tracks()[r["index"].toInt()];
		QCOMPARE(at2->name(), QString("Auto2"));
		auto clip2 = dynamic_cast<lmms::AutomationClip*>(at2->getClips()[0]);
		QVERIFY(clip2);
		QCOMPARE(clip2->firstObject(), target->volumeModel());
		QVERIFY(target->volumeModel()->isAutomated());
	}

	// --- Task 7: effects, params, automation, samples ---
	// Effect plugins cannot load here either (see hasPlugin), so the track-level model tree,
	// parameter setting, automation and sample clips are asserted for real; loading an effect
	// is only exercised where the plugin is available.
	void describeAndSetTrackParams()
	{
		int idx = addBareInstrumentTrack();
		auto d = reg.call("describe_model_tree", {{"track", idx}});
		QVERIFY2(d["ok"].toBool(), qPrintable(d["error"].toString()));
		QVERIFY(d["instrument"].toArray().isEmpty());
		QVERIFY(d["effects"].toArray().isEmpty());
		auto vol = modelNamed(d["track"].toArray(), "Volume");
		QCOMPARE(vol["value"].toDouble(), 100.0);
		QCOMPARE(vol["min"].toDouble(), 0.0);
		QCOMPARE(vol["max"].toDouble(), 200.0);
		QVERIFY(!modelNamed(d["track"].toArray(), "Panning").isEmpty());

		auto p = reg.call("set_params", {{"track", idx}, {"target", "track"}, {"params", QJsonObject{{"volume", 42}, {"Panning", -30}}}});
		QVERIFY2(p["ok"].toBool(), qPrintable(p["error"].toString()));
		QCOMPARE(p["applied"].toObject()["volume"].toDouble(), 42.0);
		auto t = dynamic_cast<lmms::InstrumentTrack*>(lmms::Engine::getSong()->tracks()[idx]);
		QVERIFY(t);
		QCOMPARE(int(t->volumeModel()->value()), 42);
		QCOMPARE(int(t->panningModel()->value()), -30);
		// Values are clamped to the model's range.
		QVERIFY(reg.call("set_params", {{"track", idx}, {"target", "track"}, {"params", QJsonObject{{"Volume", 999}}}})["ok"].toBool());
		QCOMPARE(int(t->volumeModel()->value()), 200);

		// An unknown name rejects the whole call: nothing is applied.
		QVERIFY(!reg.call("set_params", {{"track", idx}, {"target", "track"}, {"params", QJsonObject{{"Volume", 7}, {"NoSuchParam", 1}}}})["ok"].toBool());
		QCOMPARE(int(t->volumeModel()->value()), 200);
		QVERIFY(!reg.call("set_params", {{"track", idx}, {"target", "bogus"}, {"params", QJsonObject{{"Volume", 7}}}})["ok"].toBool());
		QVERIFY(!reg.call("set_params", {{"track", idx}, {"target", "effect:0"}, {"params", QJsonObject{{"Volume", 7}}}})["ok"].toBool());
		QVERIFY(!reg.call("set_params", {{"track", idx}, {"target", "instrument"}, {"params", QJsonObject{{"Volume", 7}}}})["ok"].toBool()); // none loaded
		QVERIFY(!reg.call("set_params", {{"track", idx}, {"target", "track"}, {"params", QJsonObject{}}})["ok"].toBool());
		QVERIFY(!reg.call("set_params", {{"track", 99}, {"target", "track"}, {"params", QJsonObject{{"Volume", 7}}}})["ok"].toBool());
		QVERIFY(!reg.call("describe_model_tree", {{"track", 99}})["ok"].toBool());
		QCOMPARE(int(t->volumeModel()->value()), 200);
	}
	void addEffectRejectsBadInput()
	{
		int idx = addBareInstrumentTrack();
		QVERIFY(!reg.call("add_effect", {{"track", idx}, {"effect", "no_such_fx"}})["ok"].toBool());
		QVERIFY(!reg.call("add_effect", {{"track", 99}, {"effect", "amplifier"}})["ok"].toBool());
		QVERIFY(!reg.call("add_effect", {{"mixerChannel", 42}, {"effect", "amplifier"}})["ok"].toBool());
		QVERIFY(!reg.call("add_effect", {{"effect", "amplifier"}})["ok"].toBool()); // neither track nor mixerChannel
		QVERIFY(reg.call("describe_model_tree", {{"track", idx}})["effects"].toArray().isEmpty());
	}
	void addEffectAndParams()
	{
		if (!hasPlugin("amplifier")) { QSKIP("amplifier effect plugin not loadable in this test process"); }
		int idx = addBareInstrumentTrack();
		auto e = reg.call("add_effect", {{"track", idx}, {"effect", "amplifier"}, {"params", QJsonObject{{"Volume", 50}}}});
		QVERIFY2(e["ok"].toBool(), qPrintable(e["error"].toString()));
		QCOMPARE(e["effectIndex"].toInt(), 0);
		QCOMPARE(e["params"].toObject()["Volume"].toDouble(), 50.0);
		auto d = reg.call("describe_model_tree", {{"track", idx}});
		QVERIFY2(d["ok"].toBool(), qPrintable(d["error"].toString()));
		QCOMPARE(d["effects"].toArray().size(), 1);
		auto fx = d["effects"].toArray()[0].toObject();
		QCOMPARE(fx["target"].toString(), QString("effect:0"));
		QCOMPARE(modelNamed(fx["params"].toArray(), "Volume")["value"].toDouble(), 50.0);
		auto p = reg.call("set_params", {{"track", idx}, {"target", "effect:0"}, {"params", QJsonObject{{"Volume", 75}}}});
		QVERIFY2(p["ok"].toBool(), qPrintable(p["error"].toString()));
		d = reg.call("describe_model_tree", {{"track", idx}});
		QCOMPARE(modelNamed(d["effects"].toArray()[0].toObject()["params"].toArray(), "Volume")["value"].toDouble(), 75.0);
		QVERIFY(!reg.call("add_effect", {{"track", idx}, {"effect", "amplifier"}, {"params", QJsonObject{{"Nope", 1}}}})["ok"].toBool());
		QCOMPARE(reg.call("describe_model_tree", {{"track", idx}})["effects"].toArray().size(), 1); // rejected params add nothing

		auto m = reg.call("add_effect", {{"mixerChannel", 0}, {"effect", "amplifier"}});
		QVERIFY2(m["ok"].toBool(), qPrintable(m["error"].toString()));
		QCOMPARE(int(lmms::Engine::mixer()->mixerChannel(0)->m_fxChain.effects().size()), 1);
		auto md = reg.call("describe_model_tree", {{"mixerChannel", 0}});
		QVERIFY2(md["ok"].toBool(), qPrintable(md["error"].toString()));
		QCOMPARE(md["effects"].toArray().size(), 1);
		QVERIFY(reg.call("set_params", {{"mixerChannel", 0}, {"target", "effect:0"}, {"params", QJsonObject{{"Volume", 20}}}})["ok"].toBool());
	}
	void automation()
	{
		int idx = addBareInstrumentTrack();
		auto t = dynamic_cast<lmms::InstrumentTrack*>(lmms::Engine::getSong()->tracks()[idx]);
		auto points = QJsonArray{QJsonObject{{"pos", 0}, {"value", 0}}, QJsonObject{{"pos", 192}, {"value", 100}}};
		auto r = reg.call("add_automation", {{"track", idx}, {"target", "track"}, {"model", "Volume"}, {"points", points}, {"progression", "linear"}});
		QVERIFY2(r["ok"].toBool(), qPrintable(r["error"].toString()));
		int autoIdx = r["automationTrack"].toInt();
		QCOMPARE(autoIdx, idx + 1);
		auto at = lmms::Engine::getSong()->tracks()[autoIdx];
		QCOMPARE(at->type(), lmms::Track::Type::Automation);
		QCOMPARE(int(at->getClips().size()), 1);
		auto clip = dynamic_cast<lmms::AutomationClip*>(at->getClips()[0]);
		QVERIFY(clip);
		QCOMPARE(clip->progressionType(), lmms::AutomationClip::ProgressionType::Linear);
		QCOMPARE(clip->firstObject(), t->volumeModel());
		QCOMPARE(clip->valueAt(0), 0.0f);
		QCOMPARE(clip->valueAt(96), 50.0f);
		QCOMPARE(clip->valueAt(192), 100.0f);

		// Discrete holds the previous point; points may come in any order.
		auto r2 = reg.call("add_automation", {{"track", idx}, {"target", "track"}, {"model", "panning"},
			{"points", QJsonArray{QJsonObject{{"pos", 48}, {"value", 40}}, QJsonObject{{"pos", 0}, {"value", -40}}}}, {"progression", "discrete"}});
		QVERIFY2(r2["ok"].toBool(), qPrintable(r2["error"].toString()));
		auto clip2 = dynamic_cast<lmms::AutomationClip*>(lmms::Engine::getSong()->tracks()[r2["automationTrack"].toInt()]->getClips()[0]);
		QVERIFY(clip2);
		QCOMPARE(clip2->firstObject(), t->panningModel());
		QCOMPARE(clip2->valueAt(24), -40.0f);
		QCOMPARE(clip2->valueAt(48), 40.0f);

		// Invalid input creates no automation track.
		QVERIFY(!reg.call("add_automation", {{"track", idx}, {"target", "track"}, {"model", "NoSuchModel"}, {"points", points}})["ok"].toBool());
		QVERIFY(!reg.call("add_automation", {{"track", idx}, {"target", "track"}, {"model", "Volume"}, {"points", QJsonArray{}}})["ok"].toBool());
		QVERIFY(!reg.call("add_automation", {{"track", idx}, {"target", "track"}, {"model", "Volume"}, {"points", points}, {"progression", "wobbly"}})["ok"].toBool());
		QVERIFY(!reg.call("add_automation", {{"track", idx}, {"target", "track"}, {"model", "Volume"}, {"points", QJsonArray{QJsonObject{{"pos", -5}, {"value", 1}}}}})["ok"].toBool());
		QVERIFY(!reg.call("add_automation", {{"track", autoIdx}, {"target", "track"}, {"model", "Volume"}, {"points", points}})["ok"].toBool());
		QCOMPARE(int(lmms::Engine::getSong()->tracks().size()), 3);
	}
	void sampleClip()
	{
		QVERIFY(!reg.call("add_sample_clip", {{"file", "C:/does/not/exist.wav"}, {"pos", 0}})["ok"].toBool());
		QCOMPARE(int(lmms::Engine::getSong()->tracks().size()), 0);
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString file = writeTestWav(dir);
		QVERIFY(!file.isEmpty());

		// The optional path policy is consulted before anything is touched.
		lmms::AiToolRegistry denied;
		lmms::registerAiProjectTools(denied, [](const QString&) { return false; });
		auto d = denied.call("add_sample_clip", {{"file", file}, {"pos", 0}});
		QVERIFY(!d["ok"].toBool());
		QVERIFY(d["error"].toString().contains("not allowed"));
		QCOMPARE(int(lmms::Engine::getSong()->tracks().size()), 0);

		auto r = reg.call("add_sample_clip", {{"file", file}, {"pos", 192}});
		QVERIFY2(r["ok"].toBool(), qPrintable(r["error"].toString()));
		QCOMPARE(r["track"].toInt(), 0);
		auto st = dynamic_cast<lmms::SampleTrack*>(lmms::Engine::getSong()->tracks()[0]);
		QVERIFY(st);
		QCOMPARE(st->name(), QString("beep"));
		QCOMPARE(int(st->getClips().size()), 1);
		auto clip = dynamic_cast<lmms::SampleClip*>(st->getClips()[0]);
		QVERIFY(clip);
		QCOMPARE(clip->startPosition().getTicks(), 192);
		QVERIFY(clip->sampleFile().endsWith("beep.wav"));
		// One second of audio at the song's tempo.
		QCOMPARE(r["len"].toInt(), int(44100 / lmms::Engine::framesPerTick(44100)));
		QCOMPARE(clip->length().getTicks(), r["len"].toInt());

		// Existing sample track: append a clip; other track types are rejected.
		auto r2 = reg.call("add_sample_clip", {{"file", file}, {"pos", 0}, {"track", 0}});
		QVERIFY2(r2["ok"].toBool(), qPrintable(r2["error"].toString()));
		QCOMPARE(int(st->getClips().size()), 2);
		QCOMPARE(int(lmms::Engine::getSong()->tracks().size()), 1);
		int it = addBareInstrumentTrack();
		QVERIFY(!reg.call("add_sample_clip", {{"file", file}, {"pos", 0}, {"track", it}})["ok"].toBool());
		QVERIFY(!reg.call("add_sample_clip", {{"file", file}, {"pos", 0}, {"track", 9}})["ok"].toBool());
		QCOMPARE(int(st->getClips().size()), 2);
	}
};

QTEST_GUILESS_MAIN(AiProjectToolsTest)
#include "AiProjectToolsTest.moc"
