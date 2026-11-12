/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file      EthernetAdapterDlg.h
 * \brief     Selection of the network adapter, promiscuous mode and capture filter
 */

#pragma once

#include "EthernetModule.h"
#include "Ethernet_Resource.h"

class CEthAdapterDlg : public CDialog
{
public:
    /** adapters: those pcap can open; settings: changed when OK is clicked */
    CEthAdapterDlg(const std::vector<eth::Adapter>& adapters, EthSettings& settings, CWnd* pParent);

protected:
    virtual BOOL OnInitDialog();
    virtual void OnOK();
    afx_msg void OnDblClk(NMHDR* pNMHDR, LRESULT* pResult);
    DECLARE_MESSAGE_MAP()

private:
    int DialogUnitsX(int du);

    std::vector<eth::Adapter> m_adapters;   ///< those shown: with a MAC address (Ethernet, Wi-Fi)
    EthSettings& m_settings;
    CListCtrl m_omList;
};
