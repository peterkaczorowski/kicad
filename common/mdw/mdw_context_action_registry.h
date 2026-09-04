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
 * @file mdw_context_action_registry.h
 * @brief External client context actions offered on workspace items.
 *
 * An IPC client registers actions that the project manager offers on managed
 * items. Because a registered action lets an external process inject entries
 * into KiCad's menus, the first registration from a given client identity
 * requires explicit user consent; the decision is remembered for the identity
 * so later registrations do not prompt again.
 *
 * The identity is the client_name the caller puts in its request header. That
 * name is authenticated only by the API transport token, not per-client, so the
 * gate distinguishes already-trusted local clients from each other rather than
 * defending against a hostile local process holding the token. A remembered
 * decision, grant or deny, lives for the process only; a denial is kept for the
 * session so a client cannot spam the consent dialog by retrying, and restarting
 * KiCad clears it.
 */

#ifndef MDW_CONTEXT_ACTION_REGISTRY_H
#define MDW_CONTEXT_ACTION_REGISTRY_H

#include <kicommon.h>

#include <functional>
#include <map>
#include <mutex>
#include <vector>

#include <wx/string.h>

struct MDW_CONTEXT_ACTION
{
    wxString action_id;                                 ///< Assigned by the registry
    wxString client_identity;
    wxString item_type_predicate;
    wxString label;
    wxString target_method;
};


class KICOMMON_API MDW_CONTEXT_ACTION_REGISTRY
{
public:
    /// Asked once per client identity to approve its context actions. Runs on
    /// the dispatch (main) thread, so it may show a dialog.
    using CONSENT_FN = std::function<bool( const wxString& aClientIdentity )>;

    /// Install the consent prompt. With none installed every registration is
    /// denied, so a project manager with no UI never injects external actions.
    void SetConsentHandler( CONSENT_FN aHandler );

    /**
     * Record an action for a client, prompting for consent on the identity's
     * first registration and caching the decision. Fills aSpec.action_id and
     * returns true when accepted; returns false when the identity is denied.
     */
    bool Register( MDW_CONTEXT_ACTION& aSpec );

    /// Drop an action owned by the given client. Returns false when the id is
    /// unknown or registered by a different client, so one client cannot remove
    /// another's actions.
    bool Unregister( const wxString& aActionId, const wxString& aClientIdentity );

    /// Every registered action, for the project manager to render.
    std::vector<MDW_CONTEXT_ACTION> Actions() const;

private:
    bool consents( const wxString& aClientIdentity );

    mutable std::mutex                     m_mutex;
    CONSENT_FN                             m_consent;
    std::map<wxString, MDW_CONTEXT_ACTION> m_actions;
    std::map<wxString, bool>               m_decisions;
};

#endif // MDW_CONTEXT_ACTION_REGISTRY_H
