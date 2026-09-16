/*
 * AiToolRegistryTest.cpp - tests for AiToolRegistry
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

#include "AiToolRegistry.h"

#include <QtTest>
#include <QJsonArray>

#include <stdexcept>

class AiToolRegistryTest : public QObject
{
	Q_OBJECT
private slots:
	void specsAndDispatch()
	{
		lmms::AiToolRegistry r;
		r.add({"echo", "returns x", QJsonObject{{"type","object"}}, [](const QJsonObject& a){ return lmms::AiToolRegistry::ok({{"x", a["x"]}}); }});
		auto specs = r.specs();
		QCOMPARE(specs.size(), 1);
		QCOMPARE(specs[0].toObject()["function"].toObject()["name"].toString(), QString("echo"));
		auto res = r.call("echo", {{"x", 7}});
		QVERIFY(res["ok"].toBool());
		QCOMPARE(res["x"].toInt(), 7);
	}
	void unknownToolIsError()
	{
		lmms::AiToolRegistry r;
		auto res = r.call("nope", {});
		QVERIFY(!res["ok"].toBool());
		QVERIFY(res["error"].toString().contains("nope"));
	}
	void handlerExceptionIsError()
	{
		lmms::AiToolRegistry r;
		r.add({"boom", "", {}, [](const QJsonObject&) -> QJsonObject { throw std::runtime_error("kaboom"); }});
		auto res = r.call("boom", {});
		QVERIFY(!res["ok"].toBool());
		QVERIFY(res["error"].toString().contains("kaboom"));
	}
};

QTEST_GUILESS_MAIN(AiToolRegistryTest)
#include "AiToolRegistryTest.moc"
