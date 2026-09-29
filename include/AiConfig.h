/*
 * AiConfig.h - agent harness configuration
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

#ifndef LMMS_AI_CONFIG_H
#define LMMS_AI_CONFIG_H

#include "lmms_export.h"

namespace lmms
{

struct LMMS_EXPORT AiConfig
{
	//! Start the loopback agent server at launch (Settings > AI). Read once at startup.
	bool agentServer = false;
	static AiConfig load();
	static void save(const AiConfig& c);
};

} // namespace lmms

#endif
