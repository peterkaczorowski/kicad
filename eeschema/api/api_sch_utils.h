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
#include <google/protobuf/any.pb.h>
#include <optional>
#include <set>
#include <math/vector2d.h>
#include <core/typeinfo.h>
#include <import_export.h>
#include <api/schematic/schematic_types.pb.h>

class EDA_ITEM;
class EDA_TEXT;
class SCH_ITEM;
class SCH_SHEET_PATH;
class SCH_SHEET;
class SCH_SHEET_PIN;

kiapi::schematic::types::Sheet PackSchematicSheet( const SCH_SHEET& aSheet );
kiapi::schematic::types::SheetPin PackSchematicSheetPin( const SCH_SHEET_PIN& aPin );
std::unique_ptr<SCH_SHEET> UnpackSchematicSheet( const kiapi::schematic::types::Sheet& aSheet,
                                              EDA_ITEM* aParent, std::string& aError );

std::unique_ptr<EDA_ITEM> CreateItemForType( KICAD_T aType, EDA_ITEM* aContainer );

bool ConvertSchematicUnits( google::protobuf::Message&, bool toApi, std::string& error );
void PackSchematicText( kiapi::common::types::Text&, const EDA_TEXT& );
bool UnpackSchematicText( const kiapi::common::types::Text&, EDA_TEXT&, std::string& error );

// Schematic model units are 100 nm; all public API vectors are nanometers.
void PackSchematicVector( kiapi::common::types::Vector2&, const VECTOR2I& );
bool UnpackSchematicVector( const kiapi::common::types::Vector2&, VECTOR2I& );
std::set<int> SupportedSchematicApiTypes();
std::optional<google::protobuf::Any> PackSchematicItem( const SCH_ITEM&,
                                                      const SCH_SHEET_PATH* = nullptr );
std::unique_ptr<SCH_ITEM> UnpackSchematicItem( const google::protobuf::Any&, std::string&,
                                             const SCH_ITEM* = nullptr,
                                             const SCH_SHEET_PATH* = nullptr );
std::string SchematicApiItemId( const google::protobuf::Any& );
bool MergeSchematicApiItem( const google::protobuf::Any& patch,
                           const google::protobuf::Any& original,
                           const google::protobuf::FieldMask&, google::protobuf::Any& out,
                           std::string& error );

#endif //KICAD_API_SCH_UTILS_H
