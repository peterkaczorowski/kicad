/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2026 Krishna Swaroop <krishna.swaroop@pixxel.co.in>
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <dialogs/dialog_hyperlynx_export_job.h>


DIALOG_HYPERLYNX_EXPORT_JOB::DIALOG_HYPERLYNX_EXPORT_JOB( wxWindow* aParent, JOB_EXPORT_PCB_HYPERLYNX* aJob ) :
        DIALOG_HYPERLYNX_EXPORT_JOB_BASE( aParent, wxID_ANY ),
        m_job( aJob )
{
    if( aJob )
        SetTitle( aJob->GetSettingsDialogTitle() );

    SetupStandardButtons();

    finishDialogSettings();
}


bool DIALOG_HYPERLYNX_EXPORT_JOB::TransferDataToWindow()
{
    m_textCtrlOutputPath->SetValue( m_job->GetConfiguredOutputPath() );
    return true;
}


bool DIALOG_HYPERLYNX_EXPORT_JOB::TransferDataFromWindow()
{
    m_job->SetConfiguredOutputPath( m_textCtrlOutputPath->GetValue() );
    return true;
}
