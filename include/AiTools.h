/*
 * AiTools.h - registration entry points for the AI Composer tool sets
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

#ifndef LMMS_AI_TOOLS_H
#define LMMS_AI_TOOLS_H

#include "lmms_export.h"

namespace lmms
{

class AiToolRegistry;
class AiPathPolicy;

//! Project tools: summary, head (tempo/time signature/master), tracks, clips, notes, automation.
LMMS_EXPORT void registerAiProjectTools(AiToolRegistry& r);

//! Discovery tools: list instruments/effects/presets/samples, read a preset's XML (paths gated by `policy`).
LMMS_EXPORT void registerAiDiscoveryTools(AiToolRegistry& r, AiPathPolicy& policy);

} // namespace lmms

#endif // LMMS_AI_TOOLS_H
