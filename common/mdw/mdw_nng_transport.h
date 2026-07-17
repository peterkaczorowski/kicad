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
 * @file mdw_nng_transport.h
 * @brief The nng control-channel transport to a spawned backend process.
 */

#ifndef MDW_NNG_TRANSPORT_H
#define MDW_NNG_TRANSPORT_H

#include <kicommon.h>

#include "mdw_transport.h"

#include <memory>
#include <string>

class KINNG_REQUEST_CLIENT;

/**
 * Carries requests to a backend over nng REQ/REP by way of KINNG_REQUEST_CLIENT.
 * Serialization of the envelope and mapping of transport failures onto
 * MDW_TRANSPORT_RESULT happen here.
 */
class KICOMMON_API MDW_NNG_TRANSPORT : public MDW_TRANSPORT
{
public:
    MDW_NNG_TRANSPORT( const std::string& aControlUrl );

    ~MDW_NNG_TRANSPORT() override;

    bool Start() override;

    void Stop() override;

    std::uint64_t SendAsync( const kiapi::mdw::MdwRequest& aRequest, int aTimeoutMs,
                             CALLBACK aOnComplete ) override;

    MDW_TRANSPORT_RESULT SendSync( const kiapi::mdw::MdwRequest& aRequest, int aTimeoutMs ) override;

    void Cancel( std::uint64_t aHandle ) override;

private:
    std::unique_ptr<KINNG_REQUEST_CLIENT> m_client;
};

#endif // MDW_NNG_TRANSPORT_H
