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

#ifndef MDW_STATUS_PILL_H
#define MDW_STATUS_PILL_H

#include <kicommon.h>
#include <mdw/mdw_types.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

#include <wx/panel.h>

class MDW_EVENT_BUS;
struct MDW_EVENT;


/**
 * A compact status indicator for the Design Workspace, drawn as a colored pill
 * with a short label. The color follows the lifecycle category and the label
 * carries the lock and offline state. The pill hides itself when the project is
 * not bound so a disabled integration shows no UI.
 *
 * It can observe a session event bus and recompute its state on the main thread
 * whenever a workspace event arrives, so every open frame stays coherent.
 */
class KICOMMON_API MDW_STATUS_PILL : public wxPanel
{
public:
    struct STATE
    {
        bool                   bound = false;
        bool                   online = false;
        MDW_LIFECYCLE_CATEGORY category = MDW_LIFECYCLE_CATEGORY::EDITABLE;
        bool                   locked = false;
        bool                   locked_by_me = false;
    };

    MDW_STATUS_PILL( wxWindow* aParent );

    ~MDW_STATUS_PILL() override;

    /// Replace the displayed state and repaint, hiding the pill when not bound.
    void SetState( const STATE& aState );

    const STATE& GetState() const { return m_state; }

    /// The label shown for a state. Pure and static so it can be unit-tested.
    static wxString LabelFor( const STATE& aState );

    /**
     * Observe a session event bus. When a workspace event arrives the refresh
     * callback recomputes the state on the main thread. Pass a null bus to stop
     * observing. The pill unsubscribes from any previous bus first.
     */
    void Observe( MDW_EVENT_BUS* aBus, std::function<STATE()> aRefresh );

    /**
     * Track a field of the parent status bar, repositioning over it whenever the
     * bar resizes. The pill's parent must be that status bar.
     */
    void PlaceInStatusBar( int aFieldId );

private:
    void onPaint( wxPaintEvent& aEvent );

    void onParentSize( wxSizeEvent& aEvent );

    void reposition();

    void unsubscribe();

    STATE                  m_state;
    MDW_EVENT_BUS*         m_bus;
    std::uint64_t          m_subscription;
    std::function<STATE()> m_refresh;
    int                    m_statusFieldId;               ///< -1 when not bar-hosted

    // A bus event may be dispatched on the transport thread. The handler captures
    // this token by value and only posts to the main loop, so it never touches
    // the pill off-thread; the token gates the refresh once the pill is gone.
    std::shared_ptr<std::atomic<bool>> m_alive;
};

#endif // MDW_STATUS_PILL_H
