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
#include <QJsonArray>

#include "AiToolRegistry.h"
#include "AiTools.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Mixer.h"
#include "PluginFactory.h"
#include "Song.h"
#include "Track.h"

class AiProjectToolsTest : public QObject
{
	Q_OBJECT
	lmms::AiToolRegistry reg;

	//! Instrument plugins live in DLLs that import from lmms.exe, so they cannot be loaded into a
	//! test process on Windows; elsewhere they need LMMS_PLUGIN_DIR to point at the plugin dir.
	static bool hasTripleOscillator()
	{
		return !lmms::PluginFactory::instance()->pluginInfo("tripleoscillator").isNull();
	}
	//! Instrument track with no plugin loaded: enough for note/clip tools.
	static int addBareInstrumentTrack()
	{
		lmms::Track::create(lmms::Track::Type::Instrument, lmms::Engine::getSong());
		return int(lmms::Engine::getSong()->tracks().size()) - 1;
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
		int src = addBareInstrumentTrack();
		lmms::Engine::getSong()->tracks()[src]->setName("Src");
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
		lmms::Engine::getSong()->tracks()[addBareInstrumentTrack()]->setName("A");
		lmms::Engine::getSong()->tracks()[addBareInstrumentTrack()]->setName("B");
		lmms::Engine::getSong()->tracks()[addBareInstrumentTrack()]->setName("C");
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
};

QTEST_GUILESS_MAIN(AiProjectToolsTest)
#include "AiProjectToolsTest.moc"
