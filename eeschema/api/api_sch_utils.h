/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2024 Jon Evans <jon@craftyjon.com>
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef KICAD_API_SCH_UTILS_H
#define KICAD_API_SCH_UTILS_H

#include <memory>
#include <core/typeinfo.h>
#include <import_export.h>
#include <api/schematic/schematic_types.pb.h>

class EDA_ITEM;
class SCH_SHEET;
class SCH_SHEET_PIN;

kiapi::schematic::types::Sheet PackSchematicSheet( const SCH_SHEET& aSheet );
kiapi::schematic::types::SheetPin PackSchematicSheetPin( const SCH_SHEET_PIN& aPin );

std::unique_ptr<EDA_ITEM> CreateItemForType( KICAD_T aType, EDA_ITEM* aContainer );

#endif //KICAD_API_SCH_UTILS_H
