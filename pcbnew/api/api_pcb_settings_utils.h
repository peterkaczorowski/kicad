/* Copyright The KiCad Developers, see AUTHORS.txt. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <import_export.h>
#include <json_common.h>
#include <string>
#include <vector>

namespace PCB_PROJECT_SETTINGS
{
/// Validate the complete candidate before native Load(), whose legacy readers tolerate bad data.
void Validate( const nlohmann::json& aNet, const nlohmann::json& aBoard,
               const nlohmann::json& aBoardSchema );
/// Apply explicit, non-overlapping field paths to a detached candidate, never to live settings.
nlohmann::json Patch( const nlohmann::json& aCurrent, const nlohmann::json& aSupplied,
                       const std::vector<std::string>& aPaths );
}
