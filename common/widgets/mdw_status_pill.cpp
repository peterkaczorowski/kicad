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

#include <widgets/mdw_status_pill.h>

#include <mdw/mdw_event_bus.h>
#include <mdw/mdw_framework.h>
#include <mdw/mdw_session.h>
#include <mdw/mdw_state_mapper.h>

#include <wx/app.h>
#include <wx/dcbuffer.h>
#include <wx/statusbr.h>


MDW_STATUS_PILL::MDW_STATUS_PILL( wxWindow* aParent ) :
        wxPanel( aParent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE ),
        m_bus( nullptr ),
        m_subscription( 0 ),
        m_statusFieldId( -1 ),
        m_alive( std::make_shared<std::atomic<bool>>( true ) )
{
    SetBackgroundStyle( wxBG_STYLE_PAINT );
    Bind( wxEVT_PAINT, &MDW_STATUS_PILL::onPaint, this );
}


MDW_STATUS_PILL::~MDW_STATUS_PILL()
{
    // Trip the token before unsubscribing so any handler already copied for an
    // in-flight dispatch posts a refresh that the main loop then drops.
    m_alive->store( false );

    unsubscribe();

    if( m_statusFieldId >= 0 && GetParent() )
        GetParent()->Unbind( wxEVT_SIZE, &MDW_STATUS_PILL::onParentSize, this );
}


wxString MDW_STATUS_PILL::LabelFor( const STATE& aState )
{
    if( !aState.bound )
        return wxEmptyString;

    if( !aState.online )
        return _( "Workspace offline" );

    wxString label = MDW_STATE_MAPPER::CategoryDisplayName( aState.category );

    if( aState.locked )
        label << wxS( " — " ) << ( aState.locked_by_me ? _( "Locked by you" )
                                                            : _( "Locked" ) );

    return label;
}


void MDW_STATUS_PILL::SetState( const STATE& aState )
{
    m_state = aState;

    if( !m_state.bound )
    {
        Hide();
        return;
    }

    wxString label = LabelFor( m_state );
    wxSize   text = GetTextExtent( label );

    SetMinSize( wxSize( text.GetWidth() + 20, text.GetHeight() + 6 ) );

    Show();
    Refresh();
}


void MDW_STATUS_PILL::Observe( MDW_EVENT_BUS* aBus, std::function<STATE()> aRefresh )
{
    unsubscribe();

    m_bus = aBus;
    m_refresh = std::move( aRefresh );

    if( !m_bus )
        return;

    // The handler may fire on the bus transport thread, so it captures a weak
    // token and the pill pointer by value and only queues onto the main loop. It
    // never dereferences the pill off-thread; the main-loop body checks the token
    // (both it and the destructor run on the main thread) before touching it.
    std::weak_ptr<std::atomic<bool>> alive = m_alive;
    MDW_STATUS_PILL*                 self = this;

    m_subscription = m_bus->Subscribe(
            [alive, self]( const MDW_EVENT& )
            {
                wxTheApp->CallAfter(
                        [alive, self]()
                        {
                            std::shared_ptr<std::atomic<bool>> token = alive.lock();

                            if( token && token->load() && self->m_refresh )
                                self->SetState( self->m_refresh() );
                        } );
            } );
}


MDW_SESSION* MDW_STATUS_PILL::TrackSession( MDW_FRAMEWORK* aFramework,
                                            const wxString& aProjectPath, bool aEnabled )
{
    // Captured by value because the bus may call this back long after the frame
    // has moved on, and resolved fresh each time so a reopened project picks up
    // its new session rather than a stale pointer.
    auto computeState =
            [aFramework, aProjectPath, aEnabled]() -> STATE
            {
                STATE state;

                if( !aEnabled || !aFramework )
                    return state;

                MDW_SESSION* session = aFramework->Session( aProjectPath );

                if( !session )
                    return state;

                state.bound = true;
                state.online = session->Online();

                return state;
            };

    MDW_SESSION* session = aFramework ? aFramework->Session( aProjectPath ) : nullptr;

    Observe( session ? &session->EventBus() : nullptr, computeState );
    SetState( computeState() );

    return session;
}


void MDW_STATUS_PILL::PlaceInStatusBar( int aFieldId )
{
    wxStatusBar* bar = dynamic_cast<wxStatusBar*>( GetParent() );

    if( !bar )
        return;

    m_statusFieldId = aFieldId;
    bar->Bind( wxEVT_SIZE, &MDW_STATUS_PILL::onParentSize, this );

    reposition();
}


void MDW_STATUS_PILL::onParentSize( wxSizeEvent& aEvent )
{
    reposition();
    aEvent.Skip();
}


void MDW_STATUS_PILL::reposition()
{
    wxStatusBar* bar = dynamic_cast<wxStatusBar*>( GetParent() );

    if( !bar || m_statusFieldId < 0 )
        return;

    wxRect rect;

    if( bar->GetFieldRect( m_statusFieldId, rect ) )
        SetSize( rect.Deflate( 2, 2 ) );
}


void MDW_STATUS_PILL::unsubscribe()
{
    if( m_bus && m_subscription )
        m_bus->Unsubscribe( m_subscription );

    m_bus = nullptr;
    m_subscription = 0;
}


void MDW_STATUS_PILL::onPaint( wxPaintEvent& aEvent )
{
    wxAutoBufferedPaintDC dc( this );

    dc.SetBackground( wxBrush( GetParent()->GetBackgroundColour() ) );
    dc.Clear();

    if( !m_state.bound )
        return;

    KIGFX::COLOR4D fill = m_state.online ? MDW_STATE_MAPPER::CategoryColor( m_state.category )
                                         : KIGFX::COLOR4D( 0.5, 0.5, 0.5, 1.0 );

    wxColour fillColour = fill.ToColour();

    // Pick a text color that stays legible on the fill.
    wxColour textColour = fill.GetBrightness() > 0.5 ? *wxBLACK : *wxWHITE;

    wxRect area = GetClientRect();
    area.Deflate( 1, 1 );

    dc.SetBrush( wxBrush( fillColour ) );
    dc.SetPen( wxPen( fillColour ) );
    dc.DrawRoundedRectangle( area, area.GetHeight() / 2 );

    dc.SetTextForeground( textColour );
    dc.DrawLabel( LabelFor( m_state ), area, wxALIGN_CENTER_HORIZONTAL | wxALIGN_CENTER_VERTICAL );
}
