/* Copyright The KiCad Developers, see AUTHORS.txt. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KICAD_API_PCB_LIBRARY_UTILS_H
#define KICAD_API_PCB_LIBRARY_UTILS_H

#include <import_export.h>
#include <memory>
#include <string>
#include <api/board/board_types.pb.h>

class FOOTPRINT;

namespace kiapi::board
{
// Detached local model; no source mutation or dependency on the original library.
types::FootprintInstance PackFootprintDefinition( const FOOTPRINT& aSource );

// Strictly validate before using the native decoder, which otherwise skips bad children.
// Library definitions are F.Cu, unrotated, anchored at zero and have no board nets.
std::unique_ptr<FOOTPRINT> UnpackFootprintDefinition( const types::FootprintInstance& aDefinition,
                                                    std::string& aError );
}
#endif
