/* Copyright The KiCad Developers. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KICAD_API_SCH_CONNECTIVITY_H
#define KICAD_API_SCH_CONNECTIVITY_H
#include <import_export.h>
#include <api/schematic/schematic_types.pb.h>
class SCHEMATIC;

// Caller must first recalculate the live connection graph without cleanup.
// IDs are temporary canonical keys until StampSchematicConnectivity is called.
kiapi::schematic::types::ConnectivitySnapshot BuildSchematicConnectivity( SCHEMATIC& aSchematic );
void StampSchematicConnectivity( kiapi::schematic::types::ConnectivitySnapshot& aSnapshot,
                               const std::string& aSession, uint64_t aRevision );
#endif
