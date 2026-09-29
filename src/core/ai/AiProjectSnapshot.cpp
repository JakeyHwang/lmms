/*
 * AiProjectSnapshot.cpp - whole-project snapshot behind the agent's checkpoint / revert tools
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

#include "AiProjectSnapshot.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QTextStream>

#include "DataFile.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "Song.h"

namespace lmms
{

void AiProjectSnapshot::take()
{
	m_data.clear();
	Song* song = Engine::getSong();
	if (!song) { return; }
	m_fileName = song->projectFileName();
	m_modified = song->isModified();
	DataFile df(DataFile::Type::SongProject);
	song->saveProjectData(df);
	QTextStream ts(&m_data, QIODevice::WriteOnly);
	df.write(ts);
	ts.flush();
	Engine::projectJournal()->setJournalling(false);
}

bool AiProjectSnapshot::restore()
{
	Song* song = Engine::getSong();
	if (!held() || !song) { return false; }
	// Next to the project file so "local:" resource paths resolve; no .mmp suffix so the
	// temporary file stays out of the recent-projects list.
	const QString dir = m_fileName.isEmpty() ? QDir::tempPath() : QFileInfo(m_fileName).absolutePath();
	QTemporaryFile tmp(dir + "/lmms-agent-checkpoint-XXXXXX");
	if (!tmp.open())
	{
		tmp.setFileTemplate(QDir::tempPath() + "/lmms-agent-checkpoint-XXXXXX");
		if (!tmp.open()) { return false; }
	}
	tmp.write(m_data);
	tmp.close(); // still auto-removed on destruction
	song->loadProject(tmp.fileName()); // leaves the song unmodified
	song->setProjectFileName(m_fileName);
	if (m_modified) { song->setModified(); }
	drop();
	return true;
}

void AiProjectSnapshot::drop()
{
	m_data.clear();
	if (Engine::getSong()) { Engine::projectJournal()->setJournalling(true); }
}

} // namespace lmms
