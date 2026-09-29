/*
 * AiTools.h - registration entry points for the agent harness tool sets
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

#include <functional>

#include <QString>

#include "lmms_export.h"

namespace lmms
{

class AiToolRegistry;
class AiPathPolicy;
class AiProjectSnapshot;

//! Project tools: summary, head (tempo/time signature/master), tracks, clips, notes, effects,
//! parameters, automation, sample clips. `pathAllowed`, when set, gates every file path a tool
//! would read (add_sample_clip); an unset callback allows everything.
LMMS_EXPORT void registerAiProjectTools(AiToolRegistry& r, std::function<bool(const QString&)> pathAllowed = {});

//! Discovery tools: list instruments/effects/presets/samples, read a preset's XML (paths gated by `policy`).
LMMS_EXPORT void registerAiDiscoveryTools(AiToolRegistry& r, AiPathPolicy& policy);
//! Action tools: play, stop, render, save, new_project. `pathAllowed`, when set, gates every
//! file path a tool would write (render, save); an unset callback allows everything. `render`
//! blocks the calling thread until the export finishes.
LMMS_EXPORT void registerAiActionTools(AiToolRegistry& r, std::function<bool(const QString&)> pathAllowed = {});

//! Meta tools: ping, list_tools, checkpoint / revert / commit over `snapshot`. `r` and `snapshot` must outlive the registry's use.
LMMS_EXPORT void registerAiMetaTools(AiToolRegistry& r, AiProjectSnapshot& snapshot);

} // namespace lmms

#endif // LMMS_AI_TOOLS_H
