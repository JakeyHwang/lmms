/*
 * AiMetaTools.cpp - tools about the harness itself: ping, list_tools, checkpoint / revert / commit
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
#include "AiToolHelpers.h"
#include "AiTools.h"
#include "lmmsversion.h"

namespace lmms
{

using namespace aitools;
using R = AiToolRegistry;

void registerAiMetaTools(AiToolRegistry& r, AiProjectSnapshot& snapshot)
{
	r.add({"ping", "WHAT: liveness check. RETURNS {version}.", schema({}),
		[](const QJsonObject&) { return R::ok({{"version", LMMS_VERSION}}); }});
	r.add({"list_tools", "WHAT: every tool with its JSON schema, in OpenAI function format. RETURNS {tools:[{type,function:{name,description,parameters}}]}.",
		schema({}), [&r](const QJsonObject&) { return R::ok({{"tools", r.specs()}}); }});
	r.add({"checkpoint",
		"WHAT: snapshot the whole project in memory so revert can restore it; replaces an earlier checkpoint. "
		"WHEN: before a batch of edits. Undo history is paused until revert or commit.",
		schema({}), [&snapshot](const QJsonObject&) {
			snapshot.take();
			// take() is a no-op without a song; nothing to revert to is a failure, not an ok.
			return snapshot.held() ? R::ok() : R::error("No project open");
		}});
	r.add({"revert", "WHAT: reload the project from the last checkpoint and drop it (also discards edits made since). RETURNS ok, or error when none is held.",
		schema({}), [&snapshot](const QJsonObject&) {
			if (!snapshot.held()) { return R::error("No checkpoint held"); }
			return snapshot.restore() ? R::ok() : R::error("Could not restore the checkpoint");
		}});
	r.add({"commit", "WHAT: keep the current project state and drop the checkpoint; undo history resumes. RETURNS ok, or error when none is held.",
		schema({}), [&snapshot](const QJsonObject&) {
			if (!snapshot.held()) { return R::error("No checkpoint held"); }
			snapshot.drop();
			return R::ok();
		}});
}

} // namespace lmms
