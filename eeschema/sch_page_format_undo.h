/* Copyright The KiCad Developers. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KICAD_SCH_PAGE_FORMAT_UNDO_H
#define KICAD_SCH_PAGE_FORMAT_UNDO_H

#include <drawing_sheet/ds_proxy_view_item.h>
#include <sch_edit_frame.h>
#include <sch_screen.h>
#include <kiway.h>
#include <local_history.h>
#include <set>

// The GUI page setup proxy includes a title block and the global worksheet
// layout. IPC changes only paper, so retain a paper-only image per screen.
class SCH_PAGE_FORMAT_UNDO_ITEM : public EDA_ITEM
{
public:
    explicit SCH_PAGE_FORMAT_UNDO_ITEM( const PAGE_INFO& aPage ) :
        EDA_ITEM( WS_PROXY_UNDO_ITEM_PLUS_T ), m_page( aPage )
    {
        SetFlags( UR_TRANSIENT ); // The undo list owns this image.
    }

    void Swap( SCH_SCREEN& aScreen )
    {
        PAGE_INFO previous = aScreen.GetPageSettings();
        aScreen.SetPageSettings( m_page );
        m_page = previous;
    }

    wxString GetClass() const override { return wxS( "SCH_PAGE_FORMAT_UNDO_ITEM" ); }
#if defined(DEBUG)
    void Show( int aNestLevel, std::ostream& aStream ) const override {}
#endif

private:
    PAGE_INFO m_page;
};

inline bool IsSchematicPageFormatUndo( const PICKED_ITEMS_LIST& aList )
{
    bool found = false;
    for( unsigned i = 0; i < aList.GetCount(); ++i )
    {
        if( aList.GetPickedItemStatus( i ) == UNDO_REDO::REPEAT_ITEM )
            continue;
        if( !dynamic_cast<SCH_PAGE_FORMAT_UNDO_ITEM*>( aList.GetPickedItem( i ) ) )
            return false;
        found = true;
    }
    return found;
}

inline void NotifySchematicPageFormatChanged( SCH_EDIT_FRAME& aFrame,
                                              const std::set<SCH_SCREEN*>& aScreens )
{
    if( aScreens.empty() )
        return;

    for( SCH_SCREEN* screen : aScreens )
    {
        screen->SetContentModified();
        aFrame.Kiway().LocalHistory().NoteFileChange( screen->GetFileName() );
    }

    // The schematic override also marks the active screen. That screen may not
    // be one of the edited sheets. Keep autosave and the project dirty marker.
    aFrame.EDA_BASE_FRAME::OnModify();
    if( !aFrame.GetTitle().StartsWith( wxS( "*" ) ) )
        aFrame.SetTitle( wxS( "*" ) + aFrame.GetTitle() );

    if( aScreens.count( aFrame.GetScreen() ) )
    {
        KIGFX::SCH_VIEW* view = aFrame.GetCanvas()->GetView();
        // The proxy already points to the screen's PAGE_INFO; update its new
        // bounds without rebuilding the canvas, selection or viewport.
        if( auto* border = view->GetDrawingSheet() )
            view->Update( border, KIGFX::GEOMETRY | KIGFX::REPAINT );
        view->MarkDirty();
        view->UpdateAllItems( KIGFX::REPAINT );
        aFrame.GetCanvas()->Refresh();
    }
}

#endif
