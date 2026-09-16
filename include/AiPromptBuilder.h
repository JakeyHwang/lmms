/*
 * AiPromptBuilder.h - builds the AI Composer system prompt
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

#ifndef LMMS_AI_PROMPT_BUILDER_H
#define LMMS_AI_PROMPT_BUILDER_H

#include <QString>

#include "lmms_export.h"

namespace lmms
{

//! Reads data:/ai/system_prompt.md (embedded fallback if missing) and appends
//! the installed instrument and effect plugin names.
LMMS_EXPORT QString buildAiSystemPrompt();

} // namespace lmms

#endif // LMMS_AI_PROMPT_BUILDER_H
