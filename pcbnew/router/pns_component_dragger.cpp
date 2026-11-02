/*
 * KiRouter - a push-and-(sometimes-)shove PCB router
 *
 * Copyright (C) 2013-2020 CERN
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 * Author: Tomasz Wlostowski <tomasz.wlostowski@cern.ch>
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <memory>

#include "pns_arc.h"
#include "pns_line.h"
#include "pns_solid.h"
#include "pns_router.h"

#include "pns_component_dragger.h"
#include "pns_debug_decorator.h"
#include "pns_walkaround.h"
#include "pns_optimizer.h"

namespace PNS
{

COMPONENT_DRAGGER::COMPONENT_DRAGGER( ROUTER* aRouter ) : DRAG_ALGO( aRouter )
{
    // ensure all variables are initialized
    m_dragStatus = false;
    m_currentNode = nullptr;
}


COMPONENT_DRAGGER::~COMPONENT_DRAGGER()
{
}


bool COMPONENT_DRAGGER::Start( const VECTOR2I& aP, ITEM_SET& aPrimitives )
{
    assert( m_world );

    m_currentNode         = nullptr;
    m_initialDraggedItems = aPrimitives;
    m_p0                  = aP;

    std::unordered_set<LINKED_ITEM*> seenItems;

    auto addLinked =
            [&]( SOLID* aSolid, const JOINT* aJoint, LINKED_ITEM* aItem, VECTOR2I aOffset = {} )
            {
                if( seenItems.count( aItem ) )
                    return;

                seenItems.insert( aItem );

                // Segments that go directly between two linked pads are special-cased
                VECTOR2I otherEnd = ( aJoint->Pos() == aItem->Anchor( 0 ) ) ? aItem->Anchor( 1 )
                                                                            : aItem->Anchor( 0 );
                const JOINT* otherJoint = m_world->FindJoint( otherEnd, aItem->Layer(), aItem->Net() );

                if( otherJoint && otherJoint->LinkCount( ITEM::SOLID_T ) )
                {
                    for( ITEM* otherItem : otherJoint->LinkList() )
                    {
                        if( aPrimitives.Contains( otherItem ) )
                        {
                            m_fixedItems.insert( aItem );
                            return;
                        }
                    }
                }

                int segIndex;
                DRAGGED_CONNECTION cn;

                cn.origLine    = m_world->AssembleLine( aItem, &segIndex );
                cn.attachedPad = aSolid;
                cn.offset      = aOffset;

                // Lines that go directly between two linked pads are also special-cased
                const SHAPE_LINE_CHAIN& line = cn.origLine.CLine();
                const JOINT* jA = m_world->FindJoint( line.CPoint( 0 ), aItem->Layer(), aItem->Net() );
                const JOINT* jB = m_world->FindJoint( line.CLastPoint(), aItem->Layer(), aItem->Net() );

                wxASSERT( jA == aJoint || jB == aJoint );
                const JOINT* jSearch = ( jA == aJoint ) ? jB : jA;

                if( jSearch && jSearch->LinkCount( ITEM::SOLID_T ) )
                {
                    for( ITEM* otherItem : jSearch->LinkList() )
                    {
                        if( aPrimitives.Contains( otherItem ) )
                        {
                            for( ITEM* item : cn.origLine.Links() )
                                m_fixedItems.insert( item );

                            return;
                        }
                    }
                }

                m_conns.push_back( std::move( cn ) );
            };

    for( ITEM* item : aPrimitives.Items() )
    {
        if( item->Kind() != ITEM::SOLID_T )
            continue;

        SOLID* solid = static_cast<SOLID*>( item );

        m_solids.insert( solid );

        if( !item->IsRoutable() )
            continue;

        const JOINT* jt = m_world->FindJoint( solid->Pos(), solid );

        for( ITEM* link : jt->LinkList() )
        {
            if( link->OfKind( ITEM::SEGMENT_T | ITEM::ARC_T ) )
                addLinked( solid, jt, static_cast<LINKED_ITEM*>( link ) );
        }

        std::vector<JOINT*> extraJoints;

        m_world->QueryJoints( solid->Hull().BBox(), extraJoints, solid->Layers(),
                              ITEM::SEGMENT_T | ITEM::ARC_T );

        for( JOINT* extraJoint : extraJoints )
        {
            if( extraJoint->Net() == jt->Net() && extraJoint->LinkCount() == 1 )
            {
                LINKED_ITEM* li = static_cast<LINKED_ITEM*>( extraJoint->LinkList().front() );

                if( li->Collide( solid, m_world, solid->Layer() ) )
                    addLinked( solid, extraJoint, li, extraJoint->Pos() - solid->Pos() );
            }
        }
    }

    return true;
}


bool COMPONENT_DRAGGER::tryWalkaround( NODE* aNode, LINE& aOrig, LINE& aWalk )
{
    WALKAROUND walkaround( aNode, Router() );
    bool       ok = false;
    walkaround.SetSolidsOnly( false );
    walkaround.SetDebugDecorator( Dbg() );
    walkaround.SetLogger( Logger() );
    walkaround.SetIterationLimit( Settings().WalkaroundIterationLimit() );
    walkaround.SetLengthLimit( true, 30.0 );
    walkaround.SetAllowedPolicies( { WALKAROUND::WP_SHORTEST } );

    aWalk = aOrig;

    WALKAROUND::RESULT wr = walkaround.Route( aWalk );

    if( wr.status[WALKAROUND::WP_SHORTEST] == WALKAROUND::ST_DONE )
    {
        aWalk = wr.lines[WALKAROUND::WP_SHORTEST];
        return true;
    }

    return false;
}


bool COMPONENT_DRAGGER::Drag( const VECTOR2I& aP )
{
    assert( m_world );

    m_world->KillChildren();
    m_currentNode = m_world->Branch();

    for( ITEM* item : m_initialDraggedItems )
        m_currentNode->Remove( item );

    m_draggedItems.Clear();

    for( SOLID* s : m_solids )
    {
        VECTOR2I               p_next = aP - m_p0 + s->Pos();
        std::unique_ptr<SOLID> snew( static_cast<SOLID*>( s->Clone() ) );
        snew->SetPos( p_next );

        m_draggedItems.Add( snew.get() );
        m_currentNode->Add( std::move( snew ) );

        if( !s->IsRoutable() )
            continue;

        for( DRAGGED_CONNECTION& l : m_conns )
        {
            if( l.attachedPad == s )
            {
                l.p_orig = s->Pos() + l.offset;
                l.p_next = p_next + l.offset;
            }
        }
    }

    for( ITEM* item : m_fixedItems )
    {
        m_currentNode->Remove( item );

        switch( item->Kind() )
        {
        case ITEM::SEGMENT_T:
        {
            SEGMENT*                 s = static_cast<SEGMENT*>( item );
            std::unique_ptr<SEGMENT> s_new( s->Clone() );

            SEG orig = s->Seg();
            s_new->SetEnds( aP - m_p0 + orig.A, aP - m_p0 + orig.B );

            m_draggedItems.Add( s_new.get() );
            m_currentNode->Add( std::move( s_new ) );

            break;
        }

        case ITEM::ARC_T:
        {
            ARC*                 a = static_cast<ARC*>( item );
            std::unique_ptr<ARC> a_new( a->Clone() );

            SHAPE_ARC& arc = a_new->Arc();
            arc.Move( aP - m_p0 );

            m_draggedItems.Add( a_new.get() );
            m_currentNode->Add( std::move( a_new ) );
            break;
        }

        default: wxFAIL_MSG( wxT( "Unexpected item type in COMPONENT_DRAGGER::m_fixedItems" ) );
        }
    }

    for( COMPONENT_DRAGGER::DRAGGED_CONNECTION& cn : m_conns )
    {
        LINE origLine( cn.origLine );
        m_currentNode->Remove( origLine );
    }

    std::map<long long, std::set<int>> toRouteOrder;
    std::set<int>                      firstToRoute;

    for( int i = 0; i < m_conns.size(); i++ )
        firstToRoute.emplace( i );

    toRouteOrder.emplace( 0, firstToRoute );

    std::map<int, LINE> addedLines;
    std::set<int>       failedConnections;

    auto routeConnection = [&]( int aCn, bool aOptimize )
    {
        COMPONENT_DRAGGER::DRAGGED_CONNECTION cn = m_conns[aCn];
        bool                                  status = true;

        LINE origLine( cn.origLine );
        LINE draggedLine( cn.origLine );

        draggedLine.Unmark();
        draggedLine.ClearLinks();

        draggedLine.DragCorner( cn.p_next, origLine.CLine().Find( cn.p_orig ) );

        if( Settings().Mode() != RM_MarkObstacles )
        {
            LINE walkLine( draggedLine );

            status = tryWalkaround( m_currentNode, draggedLine, walkLine );

            if( status )
            {
                if( aOptimize )
                {
                    OPTIMIZER optimizer( m_currentNode );

                    int effort = OPTIMIZER::MERGE_SEGMENTS | OPTIMIZER::KEEP_TOPOLOGY
                                 | OPTIMIZER::RESTRICT_AREA;

                    if( Settings().SmoothDraggedSegments() )
                        effort |= OPTIMIZER::MERGE_COLINEAR;

                    optimizer.SetEffortLevel( effort );

                    OPT_BOX2I affectedArea = walkLine.ChangedArea( &origLine );
                    VECTOR2I  anchor( aP );

                    if( walkLine.CLine().Find( aP ) < 0 )
                        anchor = walkLine.CLine().NearestPoint( aP );

                    optimizer.SetPreserveVertex( anchor );

                    if( !affectedArea )
                        affectedArea = BOX2I(
                                aP ); // No valid area yet? set to minimum to disable optimization

                    PNS_DBG( Dbg(), AddPoint, anchor, YELLOW, 100000, wxT( "drag-anchor" ) );
                    PNS_DBG( Dbg(), AddShape, *affectedArea, RED, 0, wxT( "drag-affected-area" ) );

                    optimizer.SetRestrictArea( *affectedArea );

                    PNS_DBG( Dbg(), AddItem, walkLine.Clone(), RED, 0, wxT( "drag-preopt" ) );
                    walkLine.Line().Split( anchor );

                    optimizer.Optimize( &walkLine, &draggedLine, &origLine );
                    PNS_DBG( Dbg(), AddItem, walkLine.Clone(), GREEN, 0, wxT( "drag-postopt" ) );
                }
                else
                {
                    draggedLine = walkLine;
                }
            }
        }

        m_currentNode->Add( draggedLine );
        addedLines.emplace( aCn, draggedLine );

        return status;
    };

    for( auto& [_, toRouteSet] : toRouteOrder )
    {
        for( int cnId : toRouteSet )
        {
            if( !routeConnection( cnId, false ) )
                failedConnections.emplace( cnId );
        }
    }

    // If there's a length increase of more than this value, this track will be routed before routing failed connections
    const long long c_failed_vs_length = pcbIUScale.mmToIU( 100 );

    toRouteOrder.clear();
    toRouteOrder[-c_failed_vs_length] = failedConnections;

    for( auto& [cId, newLine] : addedLines )
    {
        if( !failedConnections.contains( cId ) )
        {
            long long increase = newLine.Line().Length() - m_conns[cId].origLine.Line().Length();
            toRouteOrder[-increase].emplace( cId );
        }
    }

    for( auto& [_, line] : addedLines )
        m_currentNode->Remove( line );

    addedLines.clear();
    failedConnections.clear();

    for( auto& [_, toRouteSet] : toRouteOrder )
    {
        for( int cnId : toRouteSet )
        {
            if( !routeConnection( cnId, true ) )
                failedConnections.emplace( cnId );
        }
    }

    for( auto& [_, line] : addedLines )
        m_draggedItems.Add( line );

    return true;
}


bool COMPONENT_DRAGGER::FixRoute( bool aForceCommit )
{
    NODE* node = CurrentNode();

    if( node )
    {
        if( Settings().AllowDRCViolations() || aForceCommit || !node->CheckColliding( m_draggedItems ) )
        {
            Router()->CommitRouting( node );
            return true;
        }
    }

    return false;
}


NODE* COMPONENT_DRAGGER::CurrentNode() const
{
    return m_currentNode ? m_currentNode : m_world;
}


const ITEM_SET COMPONENT_DRAGGER::Traces()
{
    return m_draggedItems;
}

}; // namespace PNS
