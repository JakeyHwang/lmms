/*
 * AiPathPolicy.h - allow list of file-system paths the AI Composer tools may read
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

#ifndef LMMS_AI_PATH_POLICY_H
#define LMMS_AI_PATH_POLICY_H

#include <QString>
#include <QStringList>

#include "lmms_export.h"

namespace lmms
{

/*! Which files the model may reference by path.
 *
 * Allowed are files under any root directory (LMMS data/working dirs). Paths are compared in a
 * canonical form: forward slashes, cleaned, no trailing slash, lower-cased on Windows. Any `..`
 * segment is refused.
 */
class LMMS_EXPORT AiPathPolicy
{
public:
	//! Replace the root directories; blank entries are dropped.
	void setRoots(QStringList roots);
	bool allows(const QString& path) const;
	QStringList roots() const { return m_roots; }

private:
	static QString canon(const QString& p);
	QStringList m_roots; // directories, canonical, no trailing slash
};

} // namespace lmms

#endif // LMMS_AI_PATH_POLICY_H
