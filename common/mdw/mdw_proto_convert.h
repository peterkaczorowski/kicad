/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

/**
 * @file mdw_proto_convert.h
 * @brief The one translation between kiapi.workspace protobufs and the
 *        framework's wxString value structs.
 *
 * The backend proxy, the workspace service and the workspace IPC handler all
 * cross this boundary, several of them in both directions. Keeping the two
 * directions of a type next to each other is what stops one side gaining a
 * field the other never learns about.
 *
 * The conversions are inline rather than compiled once because they are thin
 * field copies and every caller is in the same library.
 */

#ifndef MDW_PROTO_CONVERT_H
#define MDW_PROTO_CONVERT_H

#include "mdw_auth.h"
#include "mdw_capabilities.h"
#include "mdw_item.h"
#include "mdw_lock.h"
#include "mdw_navigation.h"
#include "mdw_revision.h"
#include "mdw_schema.h"
#include "mdw_types.h"

#include <api/workspace/workspace_types.pb.h>

#include <cstdint>
#include <string>

#include <wx/datetime.h>
#include <wx/string.h>


/// Protobuf string fields carry UTF-8 bytes, so name the crossing once rather
/// than spell the encoding out at every field.
inline std::string MdwToUtf8( const wxString& aValue )
{
    return aValue.utf8_string();
}


inline wxString MdwFromUtf8( const std::string& aValue )
{
    return wxString::FromUTF8( aValue );
}


/// Zero is the protocol's absent timestamp. wxDateTime carries its own invalid
/// state and has no epoch-zero value the two could be confused for.
inline wxDateTime MdwFromUnixTime( std::int64_t aSeconds )
{
    return aSeconds != 0 ? wxDateTime( static_cast<time_t>( aSeconds ) ) : wxDateTime();
}


inline std::int64_t MdwToUnixTime( const wxDateTime& aTime )
{
    return aTime.IsValid() ? static_cast<std::int64_t>( aTime.GetTicks() ) : 0;
}


inline MDW_VERSION MdwToStruct( const kiapi::workspace::Version& aProto )
{
    MDW_VERSION version;
    version.major = aProto.major();
    version.minor = aProto.minor();
    version.patch = aProto.patch();
    version.full = MdwFromUtf8( aProto.full() );

    return version;
}


inline void MdwToProto( kiapi::workspace::Version* aOut, const MDW_VERSION& aVersion )
{
    aOut->set_major( aVersion.major );
    aOut->set_minor( aVersion.minor );
    aOut->set_patch( aVersion.patch );
    aOut->set_full( MdwToUtf8( aVersion.full ) );
}


inline MDW_BACKEND_INFO MdwToStruct( const kiapi::workspace::BackendInfo& aProto )
{
    MDW_BACKEND_INFO info;
    info.backend_id = MdwFromUtf8( aProto.backend_id() );
    info.display_name = MdwFromUtf8( aProto.display_name() );
    info.description = MdwFromUtf8( aProto.description() );
    info.vendor = MdwFromUtf8( aProto.vendor() );
    info.vendor_url = MdwFromUtf8( aProto.vendor_url() );
    info.support_url = MdwFromUtf8( aProto.support_url() );
    info.icon_name = MdwFromUtf8( aProto.icon_name() );
    info.version = MdwToStruct( aProto.version() );

    return info;
}


inline void MdwToProto( kiapi::workspace::BackendInfo* aOut, const MDW_BACKEND_INFO& aInfo )
{
    aOut->set_backend_id( MdwToUtf8( aInfo.backend_id ) );
    aOut->set_display_name( MdwToUtf8( aInfo.display_name ) );
    aOut->set_description( MdwToUtf8( aInfo.description ) );
    aOut->set_vendor( MdwToUtf8( aInfo.vendor ) );
    aOut->set_vendor_url( MdwToUtf8( aInfo.vendor_url ) );
    aOut->set_support_url( MdwToUtf8( aInfo.support_url ) );
    aOut->set_icon_name( MdwToUtf8( aInfo.icon_name ) );
    MdwToProto( aOut->mutable_version(), aInfo.version );
}


inline MDW_CUSTOM_STATE MdwToStruct( const kiapi::workspace::CustomState& aProto )
{
    MDW_CUSTOM_STATE state;
    state.state_id = MdwFromUtf8( aProto.state_id() );
    state.display_name = MdwFromUtf8( aProto.display_name() );
    state.description = MdwFromUtf8( aProto.description() );
    state.category = static_cast<MDW_LIFECYCLE_CATEGORY>( aProto.category() );
    state.icon_name = MdwFromUtf8( aProto.icon_name() );
    state.sort_order = aProto.sort_order();

    return state;
}


inline void MdwToProto( kiapi::workspace::CustomState* aOut, const MDW_CUSTOM_STATE& aState )
{
    aOut->set_state_id( MdwToUtf8( aState.state_id ) );
    aOut->set_display_name( MdwToUtf8( aState.display_name ) );
    aOut->set_description( MdwToUtf8( aState.description ) );
    aOut->set_category( static_cast<kiapi::workspace::LifecycleCategory>( aState.category ) );
    aOut->set_icon_name( MdwToUtf8( aState.icon_name ) );
    aOut->set_sort_order( aState.sort_order );
}


inline MDW_CAPABILITIES MdwToStruct( const kiapi::workspace::Capabilities& aProto )
{
    MDW_CAPABILITIES caps;
    caps.supports_offline = aProto.supports_offline();

    for( int group : aProto.groups() )
        caps.groups.insert( static_cast<MDW_CAPABILITY_GROUP>( group ) );

    for( const kiapi::workspace::CustomState& state : aProto.custom_states() )
        caps.custom_states.push_back( MdwToStruct( state ) );

    return caps;
}


inline void MdwToProto( kiapi::workspace::Capabilities* aOut, const MDW_CAPABILITIES& aCaps )
{
    aOut->set_supports_offline( aCaps.supports_offline );

    for( MDW_CAPABILITY_GROUP group : aCaps.groups )
        aOut->add_groups( static_cast<kiapi::workspace::CapabilityGroup>( group ) );

    for( const MDW_CUSTOM_STATE& state : aCaps.custom_states )
        MdwToProto( aOut->add_custom_states(), state );
}


inline MDW_ATTRIBUTE_SCHEMA MdwToStruct( const kiapi::workspace::AttributeSchema& aProto )
{
    MDW_ATTRIBUTE_SCHEMA attr;
    attr.attr_id = MdwFromUtf8( aProto.attr_id() );
    attr.display_name = MdwFromUtf8( aProto.display_name() );
    attr.description = MdwFromUtf8( aProto.description() );
    attr.type = static_cast<MDW_ATTR_TYPE>( aProto.type() );
    attr.required = aProto.required();
    attr.editable_in_kicad = aProto.editable_in_kicad();
    attr.default_value = MdwFromUtf8( aProto.default_value() );
    attr.display_order = aProto.display_order();

    for( const std::string& value : aProto.enum_values() )
        attr.enum_values.push_back( MdwFromUtf8( value ) );

    return attr;
}


inline MDW_SCHEMA MdwToStruct( const kiapi::workspace::Schema& aProto )
{
    MDW_SCHEMA schema;
    schema.schema_version = MdwFromUtf8( aProto.schema_version() );

    for( const kiapi::workspace::AttributeSchema& attr : aProto.attributes() )
        schema.attributes.push_back( MdwToStruct( attr ) );

    return schema;
}


inline MDW_ATTRIBUTE_SET MdwToStruct( const kiapi::workspace::AttributeSet& aProto )
{
    MDW_ATTRIBUTE_SET set;

    for( const kiapi::workspace::AttributeValue& value : aProto.values() )
        set.values.push_back( { MdwFromUtf8( value.attr_id() ), MdwFromUtf8( value.value() ) } );

    return set;
}


inline void MdwToProto( kiapi::workspace::AttributeSet* aOut, const MDW_ATTRIBUTE_SET& aSet )
{
    for( const MDW_ATTRIBUTE_VALUE& value : aSet.values )
    {
        kiapi::workspace::AttributeValue* out = aOut->add_values();
        out->set_attr_id( MdwToUtf8( value.attr_id ) );
        out->set_value( MdwToUtf8( value.value ) );
    }
}


inline MDW_ITEM_INFO MdwToStruct( const kiapi::workspace::ItemInfo& aProto )
{
    MDW_ITEM_INFO info;
    info.item_id = MdwFromUtf8( aProto.item_id() );
    info.display_name = MdwFromUtf8( aProto.display_name() );
    info.folder_id = MdwFromUtf8( aProto.folder_id() );
    info.is_folder = aProto.is_folder();
    info.modified = MdwFromUnixTime( aProto.modified_unix() );
    info.revision_id = MdwFromUtf8( aProto.revision_id() );
    info.category = static_cast<MDW_LIFECYCLE_CATEGORY>( aProto.category() );
    info.custom_state_id = MdwFromUtf8( aProto.custom_state_id() );
    info.is_locked = aProto.is_locked();
    info.lock_owner = MdwFromUtf8( aProto.lock_owner() );

    return info;
}


inline void MdwToProto( kiapi::workspace::ItemInfo* aOut, const MDW_ITEM_INFO& aInfo )
{
    aOut->set_item_id( MdwToUtf8( aInfo.item_id ) );
    aOut->set_display_name( MdwToUtf8( aInfo.display_name ) );
    aOut->set_folder_id( MdwToUtf8( aInfo.folder_id ) );
    aOut->set_is_folder( aInfo.is_folder );
    aOut->set_modified_unix( MdwToUnixTime( aInfo.modified ) );
    aOut->set_revision_id( MdwToUtf8( aInfo.revision_id ) );
    aOut->set_category( static_cast<kiapi::workspace::LifecycleCategory>( aInfo.category ) );
    aOut->set_custom_state_id( MdwToUtf8( aInfo.custom_state_id ) );
    aOut->set_is_locked( aInfo.is_locked );
    aOut->set_lock_owner( MdwToUtf8( aInfo.lock_owner ) );
}


inline MDW_FOLDER MdwToStruct( const kiapi::workspace::Folder& aProto )
{
    MDW_FOLDER folder;
    folder.folder_id = MdwFromUtf8( aProto.folder_id() );
    folder.display_name = MdwFromUtf8( aProto.display_name() );
    folder.parent_folder_id = MdwFromUtf8( aProto.parent_folder_id() );
    folder.has_children = aProto.has_children();

    return folder;
}


inline MDW_LOCK_INFO MdwToStruct( const kiapi::workspace::LockInfo& aProto )
{
    MDW_LOCK_INFO info;
    info.is_locked = aProto.is_locked();
    info.lock_owner = MdwFromUtf8( aProto.lock_owner() );
    info.lock_timestamp = MdwFromUnixTime( aProto.lock_timestamp_unix() );
    info.locked_by_me = aProto.locked_by_me();

    return info;
}


inline void MdwToProto( kiapi::workspace::LockInfo* aOut, const MDW_LOCK_INFO& aLock )
{
    aOut->set_is_locked( aLock.is_locked );
    aOut->set_lock_owner( MdwToUtf8( aLock.lock_owner ) );
    aOut->set_lock_timestamp_unix( MdwToUnixTime( aLock.lock_timestamp ) );
    aOut->set_locked_by_me( aLock.locked_by_me );
}


inline MDW_REVISION MdwToStruct( const kiapi::workspace::Revision& aProto )
{
    MDW_REVISION revision;
    revision.revision_id = MdwFromUtf8( aProto.revision_id() );
    revision.author = MdwFromUtf8( aProto.author() );
    revision.comment = MdwFromUtf8( aProto.comment() );
    revision.timestamp = MdwFromUnixTime( aProto.timestamp_unix() );

    for( const std::string& parent : aProto.parent_revision_ids() )
        revision.parent_revision_ids.push_back( MdwFromUtf8( parent ) );

    return revision;
}


inline void MdwToProto( kiapi::workspace::Revision* aOut, const MDW_REVISION& aRev )
{
    aOut->set_revision_id( MdwToUtf8( aRev.revision_id ) );

    for( const wxString& parent : aRev.parent_revision_ids )
        aOut->add_parent_revision_ids( MdwToUtf8( parent ) );

    aOut->set_author( MdwToUtf8( aRev.author ) );
    aOut->set_timestamp_unix( MdwToUnixTime( aRev.timestamp ) );
    aOut->set_comment( MdwToUtf8( aRev.comment ) );
}

#endif // MDW_PROTO_CONVERT_H
