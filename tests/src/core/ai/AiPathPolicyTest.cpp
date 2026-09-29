/*
 * AiPathPolicyTest.cpp - tests for the agent harness file-path allow list
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
#include <QTemporaryDir>

#include "AiPathPolicy.h"
#include "AiToolRegistry.h"
#include "AiTools.h"
#include "ConfigManager.h"
#include "lmmsconfig.h"

class AiPathPolicyTest : public QObject
{
	Q_OBJECT
private slots:
	void rootsAllowChildrenOnly()
	{
		lmms::AiPathPolicy p;
		p.setRoots({"C:/music/lmms", "C:/Program Files/LMMS/data"});
		QVERIFY(p.allows("C:/music/lmms/samples/kick.wav"));
		QVERIFY(p.allows("C:\\music\\lmms\\x.mmp"));
		QVERIFY(!p.allows("C:/music/lmms-other/x.wav"));   // prefix but not a child dir
		QVERIFY(!p.allows("C:/Windows/system32/x.dll"));
		QVERIFY(!p.allows("C:/music/lmms/../../Windows/x"));
	}
	void emptyPolicyRejectsEverything()
	{
		lmms::AiPathPolicy p;
		QVERIFY(!p.allows("C:/music/lmms/x.wav"));
		QVERIFY(!p.allows("/tmp/x.wav"));
		QVERIFY(!p.allows(""));
	}
	void rootNormalisation()
	{
		lmms::AiPathPolicy p;
		p.setRoots({"C:/music/lmms/", "", "D:\\data\\"});   // trailing slashes and blanks
		QCOMPARE(p.roots().size(), 2);
		QVERIFY(p.allows("C:/music/lmms"));                  // the root itself
		QVERIFY(p.allows("C:/music/lmms/a/b/c.wav"));
		QVERIFY(p.allows("D:/data/x.wav"));
		QVERIFY(!p.allows("C:/music"));                      // parent of a root
		QVERIFY(!p.allows("C:/music/lmms/a/../../secret"));  // dot-dot escaping the root
		QVERIFY(!p.allows("C:/music/lmms/a/../b.wav"));      // dot-dot rejected even when it stays inside
		QVERIFY(!p.allows("C:\\music\\lmms\\..\\lmms\\b.wav"));
#ifdef LMMS_BUILD_WIN32
		QVERIFY(p.allows("c:/MUSIC/Lmms/X.WAV"));            // case-insensitive file system
#else
		QVERIFY(!p.allows("c:/MUSIC/Lmms/X.WAV"));
#endif
	}
	void getPresetXmlHonoursPolicy()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString allowed = dir.filePath("ok.xpf");
		const QString outside = QDir::temp().filePath("ai-path-policy-outside.xpf");
		// version="999" is above every upgrade step, so no upgrade routine (which may need an Engine) runs.
		const QByteArray xml =
			"<?xml version=\"1.0\"?><!DOCTYPE lmms-project><lmms-project version=\"999\" creator=\"LMMS\" "
			"type=\"instrumenttracksettings\"><head/><instrumenttracksettings>"
			"<instrumenttrack name=\"Lead\" vol=\"100\"><instrument name=\"tripleoscillator\"/></instrumenttrack>"
			"</instrumenttracksettings></lmms-project>";
		for (const auto& path : {allowed, outside})
		{
			QFile f(path);
			QVERIFY(f.open(QIODevice::WriteOnly));
			f.write(xml);
		}
		lmms::AiToolRegistry reg;
		lmms::AiPathPolicy policy;
		policy.setRoots({dir.path()});
		lmms::registerAiDiscoveryTools(reg, policy);

		auto r = reg.call("get_preset_xml", {{"path", allowed}});
		QVERIFY2(r["ok"].toBool(), qPrintable(r["error"].toString()));
		QVERIFY(r["xml"].toString().startsWith("<instrumenttrack "));
		QVERIFY(!r["xml"].toString().contains("instrumenttracksettings"));
		QVERIFY(r["xml"].toString().contains("name=\"tripleoscillator\""));

		QVERIFY(!reg.call("get_preset_xml", {{"path", outside}})["ok"].toBool());
		QVERIFY(!reg.call("get_preset_xml", {{"path", dir.filePath("missing.xpf")}})["ok"].toBool());
		QFile::remove(outside);
	}
	void getPresetXmlRejectsNonPresets()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString junk = dir.filePath("junk.xpf");
		{
			QFile f(junk);
			QVERIFY(f.open(QIODevice::WriteOnly));
			f.write("this is not xml <<<");
		}
		const QString song = dir.filePath("song.xpf");
		{
			QFile f(song);
			QVERIFY(f.open(QIODevice::WriteOnly));
			f.write("<?xml version=\"1.0\"?><lmms-project version=\"999\" type=\"song\"><head/><song/></lmms-project>");
		}
		lmms::AiToolRegistry reg;
		lmms::AiPathPolicy policy;
		policy.setRoots({dir.path()});
		lmms::registerAiDiscoveryTools(reg, policy);
		QVERIFY(!reg.call("get_preset_xml", {{"path", junk}})["ok"].toBool());
		QVERIFY(!reg.call("get_preset_xml", {{"path", song}})["ok"].toBool());
		QVERIFY(!reg.call("get_preset_xml", {{"path", dir.filePath("kick.wav")}})["ok"].toBool());
		const QString zyn = dir.filePath("zyn.xiz");   // Zyn patch extension: never an LMMS preset document
		{
			QFile f(zyn);
			QVERIFY(f.open(QIODevice::WriteOnly));
			f.write("<?xml version=\"1.0\"?><lmms-project version=\"999\" type=\"instrumenttracksettings\"><head/>"
				"<instrumenttracksettings><instrumenttrack name=\"x\"/></instrumenttracksettings></lmms-project>");
		}
		QVERIFY(!reg.call("get_preset_xml", {{"path", zyn}})["ok"].toBool());
		const QString local = dir.filePath("local.xpf");   // "local:" plugin path: only valid inside a project dir
		{
			QFile f(local);
			QVERIFY(f.open(QIODevice::WriteOnly));
			f.write("<?xml version=\"1.0\"?><lmms-project version=\"999\" type=\"instrumenttracksettings\"><head/>"
				"<instrumenttracksettings><instrumenttrack name=\"x\"><instrument name=\"vestige\">"
				"<vestige plugin=\"local:evil.dll\"/></instrument></instrumenttrack></instrumenttracksettings></lmms-project>");
		}
		auto r = reg.call("get_preset_xml", {{"path", local}});
		QVERIFY(!r["ok"].toBool());
		QVERIFY(r["error"].toString().contains("local plugin"));
	}
	//! A factory preset with legacy <ladspacontrols port..> attributes: DataFile used to raise a modal
	//! QMessageBox for these unconditionally, which would hang a headless tool call.
	void getPresetXmlLegacyLadspaPresetIsSilent()
	{
		const QString path = lmms::ConfigManager::inst()->factoryPresetsDir() + "TripleOscillator/E-Organ2.xpf";
		if (!QFileInfo::exists(path)) { QSKIP(qPrintable("factory preset not found: " + path)); }
		lmms::AiToolRegistry reg;
		lmms::AiPathPolicy policy;
		policy.setRoots({lmms::ConfigManager::inst()->factoryPresetsDir()});
		lmms::registerAiDiscoveryTools(reg, policy);
		auto r = reg.call("get_preset_xml", {{"path", path}});
		QVERIFY2(r["ok"].toBool(), qPrintable(r["error"].toString()));
		QVERIFY(r["xml"].toString().startsWith("<instrumenttrack "));
		QVERIFY(r["xml"].toString().contains("ladspacontrols"));
	}
};

QTEST_GUILESS_MAIN(AiPathPolicyTest)
#include "AiPathPolicyTest.moc"
