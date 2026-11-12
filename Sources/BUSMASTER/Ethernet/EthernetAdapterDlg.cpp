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
 * \file      EthernetAdapterDlg.cpp
 * \brief     Selection of the network adapter, promiscuous mode and capture filter
 */

#include "Ethernet_stdafx.h"
#include "EthernetAdapterDlg.h"

namespace
{
enum { COL_NAME, COL_DESCRIPTION, COL_MAC, COL_IP, COL_STATE, COL_COUNT };
const struct { const char* title; int width; } sg_asColumns[COL_COUNT] =
{
    { "Name",        90 },
    { "Adapter",    130 },
    { "MAC address", 66 },
    { "IPv4",        70 },
    { "State",       40 },
};
}

BEGIN_MESSAGE_MAP(CEthAdapterDlg, CDialog)
    ON_NOTIFY(NM_DBLCLK, IDC_ETH_ADAPTERS, OnDblClk)
END_MESSAGE_MAP()

CEthAdapterDlg::CEthAdapterDlg(const std::vector<eth::Adapter>& adapters, EthSettings& settings, CWnd* pParent)
    : CDialog(IDD_ETH_ADAPTER, pParent), m_settings(settings)
{
    /* adapters without MAC address (WAN miniports, loopback) give no Ethernet frames */
    for (const eth::Adapter& adapter : adapters)
    {
        if (adapter.hasMac)
        {
            m_adapters.push_back(adapter);
        }
    }
}

int CEthAdapterDlg::DialogUnitsX(int du)
{
    CRect r(0, 0, du, 0);
    MapDialogRect(&r);
    return r.Width();
}

BOOL CEthAdapterDlg::OnInitDialog()
{
    CDialog::OnInitDialog();
    m_omList.SubclassDlgItem(IDC_ETH_ADAPTERS, this);
    m_omList.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    for (int i = 0; i < COL_COUNT; i++)
    {
        m_omList.InsertColumn(i, sg_asColumns[i].title, LVCFMT_LEFT, DialogUnitsX(sg_asColumns[i].width));
    }
    int nSelected = -1;
    for (int i = 0; i < (int)m_adapters.size(); i++)
    {
        const eth::Adapter& a = m_adapters[(size_t)i];
        m_omList.InsertItem(i, a.name.c_str());
        m_omList.SetItemText(i, COL_DESCRIPTION, a.description.c_str());
        m_omList.SetItemText(i, COL_MAC, eth::MacToString(a.mac).c_str());
        m_omList.SetItemText(i, COL_IP, a.ipv4.c_str());
        m_omList.SetItemText(i, COL_STATE, a.up ? "Connected" : "Not connected");
        if (a.device == m_settings.device)
        {
            nSelected = i;
        }
    }
    if (nSelected < 0 && !m_adapters.empty())
    {
        nSelected = 0;
    }
    if (nSelected >= 0)
    {
        m_omList.SetItemState(nSelected, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        m_omList.EnsureVisible(nSelected, FALSE);
    }
    CheckDlgButton(IDC_ETH_PROMISC, m_settings.promiscuous ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemText(IDC_ETH_FILTER, m_settings.filter.c_str());
    CString omVersion = eth::CEthernetCapture::LibraryVersion().c_str();
    if (ethmodule::IsConnected())
    {
        omVersion += ". The new settings apply at the next connection.";
    }
    SetDlgItemText(IDC_ETH_PCAP_VERSION, omVersion);
    m_omList.SetFocus();
    return FALSE;
}

void CEthAdapterDlg::OnDblClk(NMHDR* /*pNMHDR*/, LRESULT* pResult)
{
    *pResult = 0;
    if (m_omList.GetNextItem(-1, LVNI_SELECTED) >= 0)
    {
        OnOK();
    }
}

void CEthAdapterDlg::OnOK()
{
    const int nSelected = m_omList.GetNextItem(-1, LVNI_SELECTED);
    if (nSelected < 0 || nSelected >= (int)m_adapters.size())
    {
        AfxMessageBox("Select a network adapter.", MB_ICONINFORMATION);
        return;
    }
    const eth::Adapter& a = m_adapters[(size_t)nSelected];
    m_settings.device = a.device;
    m_settings.adapterName = a.name;
    m_settings.promiscuous = IsDlgButtonChecked(IDC_ETH_PROMISC) == BST_CHECKED;
    CString omFilter;
    GetDlgItemText(IDC_ETH_FILTER, omFilter);
    omFilter.Trim();
    m_settings.filter = (LPCSTR)omFilter;
    CDialog::OnOK();
}
