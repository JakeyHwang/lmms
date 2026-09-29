/*
 * AiPathPolicy.cpp - allow list of file-system paths the AI Composer tools may read
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

#include "AiPathPolicy.h"

#include <QDir>

#include "lmmsconfig.h"

namespace lmms
{

//! True if any segment of `p` (either separator style) is `..`. Checked before cleanPath() folds them away.
static bool hasDotDot(const QString& p)
{
	return QDir::fromNativeSeparators(p).split('/').contains("..");
}

QString AiPathPolicy::canon(const QString& p)
{
	QString c = QDir::cleanPath(QDir::fromNativeSeparators(p.trimmed()));
	while (c.size() > 1 && c.endsWith('/')) { c.chop(1); }
#ifdef LMMS_BUILD_WIN32
	c = c.toLower();
#endif
	return c;
}

void AiPathPolicy::setRoots(QStringList roots)
{
	m_roots.clear();
	for (const auto& r : roots)
	{
		if (!r.trimmed().isEmpty()) { m_roots << canon(r); }
	}
}

bool AiPathPolicy::allows(const QString& path) const
{
	const QString trimmed = path.trimmed();
	if (trimmed.isEmpty() || hasDotDot(trimmed)) { return false; }
	const QString c = canon(trimmed);
	for (const auto& r : m_roots)
	{
		if (c == r || c.startsWith(r + '/')) { return true; }
	}
	return false;
}

} // namespace lmms
