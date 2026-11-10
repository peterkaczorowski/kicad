/* Copyright The KiCad Developers. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KICAD_API_SCH_NET_RENAME_H
#define KICAD_API_SCH_NET_RENAME_H
#include <import_export.h>
#include <api/schematic/schematic_commands.pb.h>
#include <wx/string.h>
#include <vector>
class SCHEMATIC;
class SCH_LABEL_BASE;
class SCH_SCREEN;
struct SCHEMATIC_NET_RENAME_EDIT
{
    SCH_LABEL_BASE* item;
    SCH_SCREEN* screen;
    wxString before;
    wxString after;
};
struct SCHEMATIC_NET_RENAME_ANALYSIS
{
    kiapi::schematic::commands::NetRenamePlan plan;
    std::vector<SCHEMATIC_NET_RENAME_EDIT> edits;
};
// Analyze a current graph, preview text changes and verify the complete native
// graph partition, restoring all text and caches before returning. No commit,
// dirty flags, geometry, disk files or ERC markers are changed.
SCHEMATIC_NET_RENAME_ANALYSIS PlanSchematicNetRename(
        SCHEMATIC& aSchematic, const kiapi::schematic::types::ConnectivitySnapshot& aSnapshot,
        const std::string& aNetId, const std::string& aNewName );
#endif
