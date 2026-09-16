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
};

QTEST_GUILESS_MAIN(AiProjectToolsTest)
#include "AiProjectToolsTest.moc"
