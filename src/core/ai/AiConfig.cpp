/*
 * AiConfig.cpp - implementation of struct AiConfig
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

#include "AiConfig.h"

#include "ConfigManager.h"

namespace lmms
{

AiConfig AiConfig::load()
{
	auto cm = ConfigManager::inst();
	AiConfig c;
	c.baseUrl = cm->value("ai", "baseurl", DefaultBaseUrl);
	c.apiKey = cm->value("ai", "apikey");
	c.model = cm->value("ai", "model");
	c.maxTokens = cm->value("ai", "maxtokens").toInt();
	if (c.maxTokens < 0) { c.maxTokens = 0; }
	c.disableThinking = cm->value("ai", "disablethinking").toInt() != 0;
	while (c.baseUrl.endsWith('/')) { c.baseUrl.chop(1); }
	return c;
}

void AiConfig::save(const AiConfig& c)
{
	auto cm = ConfigManager::inst();
	cm->setValue("ai", "baseurl", c.baseUrl);
	cm->setValue("ai", "apikey", c.apiKey);
	cm->setValue("ai", "model", c.model);
	cm->setValue("ai", "maxtokens", QString::number(c.maxTokens));
	cm->setValue("ai", "disablethinking", QString::number(c.disableThinking ? 1 : 0));
}

} // namespace lmms
