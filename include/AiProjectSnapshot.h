/*
 * AiProjectSnapshot.h - whole-project snapshot behind the agent's checkpoint / revert tools
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

#ifndef LMMS_AI_PROJECT_SNAPSHOT_H
#define LMMS_AI_PROJECT_SNAPSHOT_H

#include <QByteArray>
#include <QString>

#include "lmms_export.h"

namespace lmms
{

/*! Whole-project snapshot held in memory, used by the agent's checkpoint / revert / commit tools.
 *
 * The project journal cannot restore a whole song (Song::restoreState tears tracks out from under
 * their views and never covers <head>), so a checkpoint serialises the project the way File > Save
 * does and restore() reloads it the way File > Open does. Journalling is off while a snapshot is
 * held so the agent's edits do not pile into the undo history.
 */
class LMMS_EXPORT AiProjectSnapshot
{
public:
	//! Serialise the open project into memory; replaces any held snapshot. Journalling off.
	void take();
	/*! Reload the project from the snapshot and drop it; journalling back on.
	 *
	 * False when none is held, or when the temporary file could not be written. A failed restore
	 * keeps the snapshot held and journalling off — the state a held checkpoint always implies —
	 * so the caller can retry rather than lose the only copy of the project.
	 */
	bool restore();
	//! Forget the snapshot. Journalling on.
	void drop();
	bool held() const { return !m_data.isEmpty(); }

private:
	QByteArray m_data;
	QString m_fileName;        // Song::projectFileName() at take()
	bool m_modified = false;   // Song::isModified() at take()
};

} // namespace lmms

#endif // LMMS_AI_PROJECT_SNAPSHOT_H
