/*
 * AiActionToolsTest.cpp - engine-backed tests for the AI Composer action tools
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
#include <QFileInfo>
#include <QTemporaryDir>

#include "AiToolRegistry.h"
#include "AiTools.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "Song.h"

class AiActionToolsTest : public QObject
{
	Q_OBJECT
	lmms::AiToolRegistry reg;
	QTemporaryDir allowed;   // the only directory the path policy admits
	QTemporaryDir forbidden; // writable, but outside the policy: proves rejection leaves no file

	static lmms::Song* song() { return lmms::Engine::getSong(); }

private slots:
	void initTestCase()
	{
		QVERIFY(allowed.isValid() && forbidden.isValid());
		lmms::Engine::init(true);
		lmms::registerAiProjectTools(reg);
		const QString root = QDir(allowed.path()).canonicalPath();
		lmms::registerAiActionTools(reg, [root](const QString& p) {
			return QFileInfo(p).absoluteFilePath().startsWith(root + "/", Qt::CaseInsensitive);
		});
	}
	void cleanupTestCase() { lmms::Engine::destroy(); }
	void init() { song()->stop(); song()->clearProject(); }

	void saveRejectsDisallowedPath()
	{
		const QString evil = forbidden.path() + "/evil.mmp";
		auto r = reg.call("save", {{"path", evil}});
		QVERIFY(!r["ok"].toBool());
		QVERIFY(r["error"].toString().contains("not allowed"));
		QVERIFY(!QFileInfo::exists(evil));
	}
	void saveWritesMmpAndAppendsExtension()
	{
		reg.call("set_head", {{"bpm", 123}});
		auto r = reg.call("save", {{"path", allowed.path() + "/out"}});
		QVERIFY2(r["ok"].toBool(), qPrintable(r["error"].toString()));
		const QString written = allowed.path() + "/out.mmp";
		QCOMPARE(r["path"].toString(), written);
		QVERIFY(QFileInfo(written).size() > 0);
		QVERIFY(!QFileInfo::exists(allowed.path() + "/out"));
		QVERIFY(!QFileInfo::exists(allowed.path() + "/out.mmpz"));
		// The saved project becomes the current file, so a bare save overwrites it.
		QCOMPARE(song()->projectFileName(), written);
		reg.call("set_head", {{"bpm", 77}});
		auto again = reg.call("save", {});
		QVERIFY2(again["ok"].toBool(), qPrintable(again["error"].toString()));
		QCOMPARE(again["path"].toString(), written);
		QFile f(written);
		QVERIFY(f.open(QIODevice::ReadOnly));
		QVERIFY(f.readAll().contains("bpm=\"77\""));
		// An explicit .mmp extension is kept as is.
		auto explicitExt = reg.call("save", {{"path", allowed.path() + "/two.mmp"}});
		QVERIFY(explicitExt["ok"].toBool());
		QCOMPARE(explicitExt["path"].toString(), allowed.path() + "/two.mmp");
		QVERIFY(QFileInfo(allowed.path() + "/two.mmp").size() > 0);
	}
	void newProjectClearsFileName()
	{
		QVERIFY(reg.call("save", {{"path", allowed.path() + "/before.mmp"}})["ok"].toBool());
		QVERIFY(!song()->projectFileName().isEmpty());
		auto r = reg.call("new_project", {});
		QVERIFY2(r["ok"].toBool(), qPrintable(r["error"].toString()));
		QVERIFY(song()->projectFileName().isEmpty());
		QCOMPARE(r["tracks"].toInt(), int(song()->tracks().size()));
		// Without a current file, save needs an explicit path.
		auto s = reg.call("save", {});
		QVERIFY(!s["ok"].toBool());
		QVERIFY(s["error"].toString().contains("path"));
	}
	void undoWithEmptyJournalFails()
	{
		QVERIFY(!lmms::Engine::projectJournal()->canUndo());
		auto r = reg.call("undo", {});
		QVERIFY(!r["ok"].toBool());
		QVERIFY(r["error"].toString().contains("Nothing to undo"));
	}
	void undoRevertsLastChange()
	{
		reg.call("set_head", {{"bpm", 100}});
		reg.call("set_head", {{"bpm", 150}});
		QCOMPARE(song()->getTempo(), lmms::bpm_t(150));
		auto r = reg.call("undo", {});
		QVERIFY2(r["ok"].toBool(), qPrintable(r["error"].toString()));
		QCOMPARE(song()->getTempo(), lmms::bpm_t(100));
	}
	void playStopToggle()
	{
		QVERIFY(!song()->isPlaying());
		auto p = reg.call("play", {});
		QVERIFY2(p["ok"].toBool(), qPrintable(p["error"].toString()));
		QVERIFY(p["playing"].toBool());
		QVERIFY(song()->isPlaying());
		QCOMPARE(song()->playMode(), lmms::Song::PlayMode::Song);
		auto s = reg.call("stop", {});
		QVERIFY(s["ok"].toBool());
		QVERIFY(!s["playing"].toBool());
		QVERIFY(!song()->isPlaying());
		QCOMPARE(song()->playMode(), lmms::Song::PlayMode::None);
		// fromBar is 1-based; playback starts at (or has just advanced past) bar 3.
		auto fromBar = reg.call("play", {{"fromBar", 3}});
		QVERIFY2(fromBar["ok"].toBool(), qPrintable(fromBar["error"].toString()));
		QVERIFY(song()->isPlaying());
		QVERIFY(song()->getPlayPos(lmms::Song::PlayMode::Song).getBar() >= 2);
		reg.call("stop", {});
		QVERIFY(!song()->isPlaying());
		QVERIFY(!reg.call("play", {{"fromBar", 0}})["ok"].toBool());
		QVERIFY(!song()->isPlaying());
	}
	void renderRejectsDisallowedPathAndBadFormat()
	{
		const QString evil = forbidden.path() + "/evil";
		QVERIFY(!reg.call("render", {{"path", evil}})["ok"].toBool());
		QVERIFY(!QFileInfo::exists(evil + ".wav"));
		QVERIFY(!reg.call("render", {{"path", allowed.path() + "/x"}, {"format", "aiff"}})["ok"].toBool());
		QVERIFY(!QFileInfo::exists(allowed.path() + "/x.wav"));
		QVERIFY(!reg.call("render", {})["ok"].toBool());
	}
	void renderWritesWav()
	{
		// An empty project still renders one bar of silence past its (zero) length.
		auto r = reg.call("render", {{"path", allowed.path() + "/mix"}});
		QVERIFY2(r["ok"].toBool(), qPrintable(r["error"].toString()));
		const QString wav = allowed.path() + "/mix.wav";
		QCOMPARE(r["path"].toString(), wav);
		QVERIFY(QFileInfo(wav).size() > 44);
		QCOMPARE(r["bytes"].toDouble(), double(QFileInfo(wav).size()));
		QVERIFY(!song()->isExporting());
		QVERIFY(!song()->isPlaying());
		// A known extension on the path selects the format and is not duplicated.
		auto byExt = reg.call("render", {{"path", allowed.path() + "/mix2.wav"}});
		QVERIFY2(byExt["ok"].toBool(), qPrintable(byExt["error"].toString()));
		QCOMPARE(byExt["path"].toString(), allowed.path() + "/mix2.wav");
		QVERIFY(QFileInfo(allowed.path() + "/mix2.wav").size() > 44);
		// Rendering while playing is refused.
		QVERIFY(reg.call("play", {})["ok"].toBool());
		QVERIFY(!reg.call("render", {{"path", allowed.path() + "/nope"}})["ok"].toBool());
		reg.call("stop", {});
		QVERIFY(!QFileInfo::exists(allowed.path() + "/nope.wav"));
	}
};

QTEST_GUILESS_MAIN(AiActionToolsTest)
#include "AiActionToolsTest.moc"
