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
 * \file      EthernetTxWnd.cpp
 * \brief     Transmit window of the Ethernet module: frames sent on request or cyclically
 */

#include "Ethernet_stdafx.h"
#include "EthernetTxWnd.h"

namespace
{
enum { COL_NAME, COL_TYPE, COL_DESTINATION, COL_SOURCE, COL_VLAN, COL_CYCLE, COL_DATA, COL_COUNT };
const struct { const char* title; int width; int format; } sg_asColumns[COL_COUNT] =
{
    { "Name",         70, LVCFMT_LEFT  },
    { "Type",         62, LVCFMT_LEFT  },
    { "Destination",  96, LVCFMT_LEFT  },
    { "Source",       96, LVCFMT_LEFT  },
    { "VLAN",         26, LVCFMT_RIGHT },
    { "Cycle",        30, LVCFMT_RIGHT },
    { "Data",        150, LVCFMT_LEFT  },
};

/* kinds of the Type drop-down, in the order of eth::FrameSpec::Kind */
const char* const sg_apszKinds[] = { "Ethernet frame", "UDP over IPv4" };

/* controls whose width follows the window */
const UINT sg_auStretch[] = { IDC_ETH_TX_DATA, IDC_ETH_TX_STATUS, IDC_ETH_TX_HINT };

CString IpToString(const uint8_t* ip)
{
    CString omText;
    omText.Format("%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    return omText;
}

CString Trimmed(CWnd* pWnd, UINT id)
{
    CString omText;
    pWnd->GetDlgItemText(id, omText);
    omText.Trim();
    return omText;
}
}

BEGIN_MESSAGE_MAP(CEthTxWnd, CEthPane)
    ON_WM_SIZE()
    ON_NOTIFY(LVN_ITEMCHANGED, IDC_ETH_TX_LIST, OnItemChanged)
    ON_NOTIFY(NM_DBLCLK, IDC_ETH_TX_LIST, OnDblClk)
    ON_CBN_SELCHANGE(IDC_ETH_TX_KIND, OnKindChanged)
    ON_BN_CLICKED(IDC_ETH_TX_ADD, OnAdd)
    ON_BN_CLICKED(IDC_ETH_TX_UPDATE, OnUpdate)
    ON_BN_CLICKED(IDC_ETH_TX_DELETE, OnDelete)
    ON_BN_CLICKED(IDC_ETH_TX_SEND, OnSend)
END_MESSAGE_MAP()

CEthTxWnd::CEthTxWnd()
    : CEthPane(IDD_ETH_TX), m_bFilling(false)
{
}

BOOL CEthTxWnd::OnInitDialog()
{
    CEthPane::OnInitDialog();
    m_omList.SubclassDlgItem(IDC_ETH_TX_LIST, this);
    m_omKind.SubclassDlgItem(IDC_ETH_TX_KIND, this);
    m_omList.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES | LVS_EX_DOUBLEBUFFER);
    for (int i = 0; i < COL_COUNT; i++)
    {
        m_omList.InsertColumn(i, sg_asColumns[i].title, sg_asColumns[i].format, DialogUnitsX(sg_asColumns[i].width));
    }
    for (const char* pszKind : sg_apszKinds)
    {
        m_omKind.AddString(pszKind);
    }

    /* positions of the template, for OnSize */
    CRect omClient;
    GetClientRect(&omClient);
    m_initialSize = omClient.Size();
    for (CWnd* pChild = GetWindow(GW_CHILD); pChild != nullptr; pChild = pChild->GetNextWindow())
    {
        Anchor anchor;
        anchor.id = (UINT)pChild->GetDlgCtrlID();
        pChild->GetWindowRect(&anchor.rect);
        ScreenToClient(&anchor.rect);
        if (anchor.id != (UINT)IDC_STATIC)
        {
            m_anchors.push_back(anchor);
        }
        else
        {
            /* labels have no ID of their own: give them one so that they can be moved too */
            static UINT s_uLabel = 2000;
            anchor.id = s_uLabel++;
            pChild->SetDlgCtrlID((int)anchor.id);
            m_anchors.push_back(anchor);
        }
    }

    FillList();
    if (ethmodule::TxEntries().empty())
    {
        EthTxEntry entry;
        entry.name = "Frame 1";
        entry.spec.data = { 0x00, 0x01, 0x02, 0x03 };
        Load(entry);
    }
    else
    {
        m_omList.SetItemState(0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    SetStatus("");
    return TRUE;
}

void CEthTxWnd::OnSize(UINT nType, int cx, int cy)
{
    CEthPane::OnSize(nType, cx, cy);
    if (nType == SIZE_MINIMIZED || m_anchors.empty())
    {
        return;
    }
    /* the list takes the extra space, the editor below it keeps its distance to the bottom */
    const int dx = max(cx, m_initialSize.cx) - m_initialSize.cx;
    const int dy = max(cy, DialogUnitsY(MinSize().cy)) - m_initialSize.cy;
    HDWP hDwp = ::BeginDeferWindowPos((int)m_anchors.size());
    for (const Anchor& anchor : m_anchors)
    {
        CRect r = anchor.rect;
        if (anchor.id == IDC_ETH_TX_LIST)
        {
            r.right += dx;
            r.bottom += dy;
        }
        else
        {
            r.OffsetRect(0, dy);
            for (UINT id : sg_auStretch)
            {
                if (anchor.id == id)
                {
                    r.right += dx;
                }
            }
        }
        CWnd* pWnd = GetDlgItem((int)anchor.id);
        if (pWnd != nullptr && hDwp != nullptr)
        {
            hDwp = ::DeferWindowPos(hDwp, pWnd->GetSafeHwnd(), nullptr, r.left, r.top, r.Width(), r.Height(),
                                    SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    if (hDwp != nullptr)
    {
        ::EndDeferWindowPos(hDwp);
    }
    Invalidate();
}

void CEthTxWnd::FillList()
{
    m_bFilling = true;
    m_omList.DeleteAllItems();
    const std::vector<EthTxEntry>& entries = ethmodule::TxEntries();
    for (int i = 0; i < (int)entries.size(); i++)
    {
        m_omList.InsertItem(i, "");
        FillRow(i);
    }
    m_bFilling = false;
}

void CEthTxWnd::FillRow(int item)
{
    const EthTxEntry& e = ethmodule::TxEntries()[(size_t)item];
    const eth::FrameSpec& s = e.spec;
    const bool bUdp = s.kind == eth::FrameSpec::UdpIPv4;
    const bool bWasFilling = m_bFilling;
    m_bFilling = true;
    m_omList.SetItemText(item, COL_NAME, e.name.c_str());
    CString omText;
    if (bUdp)
    {
        omText = "UDP/IPv4";
    }
    else
    {
        omText.Format("0x%04X", s.etherType);
        const char* pszName = eth::EtherTypeName(s.etherType);
        if (pszName != nullptr)
        {
            omText = CString(pszName) + " (" + omText + ")";
        }
    }
    m_omList.SetItemText(item, COL_TYPE, omText);
    if (bUdp)
    {
        omText.Format("%s:%u (%s)", (LPCSTR)IpToString(s.dstIp), s.dstPort, eth::MacToString(s.dst).c_str());
    }
    else
    {
        omText = eth::MacToString(s.dst).c_str();
    }
    m_omList.SetItemText(item, COL_DESTINATION, omText);
    if (bUdp)
    {
        omText.Format("%s:%u", e.srcIpFromAdapter ? "adapter" : (LPCSTR)IpToString(s.srcIp), s.srcPort);
    }
    else
    {
        omText = e.srcMacFromAdapter ? CString("adapter") : CString(eth::MacToString(s.src).c_str());
    }
    m_omList.SetItemText(item, COL_SOURCE, omText);
    omText.Empty();
    if (s.vlanId >= 0)
    {
        omText.Format("%d", s.vlanId);
    }
    m_omList.SetItemText(item, COL_VLAN, omText);
    omText.Empty();
    if (e.cycleMs > 0)
    {
        omText.Format("%u ms", e.cycleMs);
    }
    m_omList.SetItemText(item, COL_CYCLE, omText);
    m_omList.SetItemText(item, COL_DATA, eth::HexBytes(s.data.data(), s.data.size(), 48).c_str());
    m_omList.SetCheck(item, e.cyclic ? TRUE : FALSE);
    m_bFilling = bWasFilling;
}

void CEthTxWnd::Load(const EthTxEntry& e)
{
    const eth::FrameSpec& s = e.spec;
    CString omText;
    SetDlgItemText(IDC_ETH_TX_NAME, e.name.c_str());
    m_omKind.SetCurSel(s.kind == eth::FrameSpec::UdpIPv4 ? 1 : 0);
    omText.Format("%u", e.cycleMs);
    SetDlgItemText(IDC_ETH_TX_CYCLE, omText);
    SetDlgItemText(IDC_ETH_TX_DST, eth::MacToString(s.dst).c_str());
    SetDlgItemText(IDC_ETH_TX_SRC, e.srcMacFromAdapter ? "" : eth::MacToString(s.src).c_str());
    omText.Empty();
    if (s.vlanId >= 0)
    {
        omText.Format("%d", s.vlanId);
    }
    SetDlgItemText(IDC_ETH_TX_VLAN, omText);
    omText.Format("%d", s.vlanPriority);
    SetDlgItemText(IDC_ETH_TX_PRIO, omText);
    omText.Format("0x%04X", s.etherType);
    SetDlgItemText(IDC_ETH_TX_ETHERTYPE, omText);
    SetDlgItemText(IDC_ETH_TX_SRCIP, e.srcIpFromAdapter ? CString() : IpToString(s.srcIp));
    omText.Format("%u", s.srcPort);
    SetDlgItemText(IDC_ETH_TX_SRCPORT, omText);
    SetDlgItemText(IDC_ETH_TX_DSTIP, IpToString(s.dstIp));
    omText.Format("%u", s.dstPort);
    SetDlgItemText(IDC_ETH_TX_DSTPORT, omText);
    SetDlgItemText(IDC_ETH_TX_DATA, eth::HexBytes(s.data.data(), s.data.size(), s.data.size()).c_str());
    EnableFields();
}

bool CEthTxWnd::Fail(UINT idControl, const CString& omText)
{
    SetStatus(omText);
    CWnd* pWnd = GetDlgItem((int)idControl);
    if (pWnd != nullptr)
    {
        pWnd->SetFocus();
        if (idControl != IDC_ETH_TX_KIND)
        {
            pWnd->SendMessage(EM_SETSEL, 0, -1);
        }
    }
    MessageBeep(MB_ICONWARNING);
    return false;
}

bool CEthTxWnd::Read(EthTxEntry& e)
{
    eth::FrameSpec& s = e.spec;
    e.name = (LPCSTR)Trimmed(this, IDC_ETH_TX_NAME);
    s.kind = m_omKind.GetCurSel() == 1 ? eth::FrameSpec::UdpIPv4 : eth::FrameSpec::Raw;
    const bool bUdp = s.kind == eth::FrameSpec::UdpIPv4;

    CString omText = Trimmed(this, IDC_ETH_TX_CYCLE);
    const unsigned long ulCycle = omText.IsEmpty() ? 0 : strtoul(omText, nullptr, 10);
    if (ulCycle > 3600000)
    {
        return Fail(IDC_ETH_TX_CYCLE, "Cycle: at most 3600000 ms");
    }
    e.cycleMs = (unsigned)ulCycle;

    if (!eth::ParseMac((LPCSTR)Trimmed(this, IDC_ETH_TX_DST), s.dst))
    {
        return Fail(IDC_ETH_TX_DST, "Destination MAC: 6 bytes in hex, for example ff:ff:ff:ff:ff:ff");
    }
    omText = Trimmed(this, IDC_ETH_TX_SRC);
    e.srcMacFromAdapter = omText.IsEmpty();
    if (!e.srcMacFromAdapter && !eth::ParseMac((LPCSTR)omText, s.src))
    {
        return Fail(IDC_ETH_TX_SRC, "Source MAC: 6 bytes in hex, or empty for the address of the adapter");
    }

    omText = Trimmed(this, IDC_ETH_TX_VLAN);
    s.vlanId = -1;
    if (!omText.IsEmpty())
    {
        const unsigned long ulVlan = strtoul(omText, nullptr, 10);
        if (ulVlan > 4094)
        {
            return Fail(IDC_ETH_TX_VLAN, "VLAN ID: 0 to 4094, or empty for no VLAN tag");
        }
        s.vlanId = (int)ulVlan;
    }
    omText = Trimmed(this, IDC_ETH_TX_PRIO);
    const unsigned long ulPriority = omText.IsEmpty() ? 0 : strtoul(omText, nullptr, 10);
    if (ulPriority > 7)
    {
        return Fail(IDC_ETH_TX_PRIO, "Priority: 0 to 7");
    }
    s.vlanPriority = (int)ulPriority;

    if (!bUdp)
    {
        omText = Trimmed(this, IDC_ETH_TX_ETHERTYPE);
        char* pEnd = nullptr;
        const unsigned long ulType = strtoul(omText, &pEnd, 16);
        if (omText.IsEmpty() || *pEnd != 0 || ulType < 0x0600 || ulType > 0xFFFF)
        {
            return Fail(IDC_ETH_TX_ETHERTYPE, "EtherType: 0x0600 to 0xFFFF in hex, for example 0x88B5");
        }
        s.etherType = (uint16_t)ulType;
    }
    else
    {
        omText = Trimmed(this, IDC_ETH_TX_SRCIP);
        e.srcIpFromAdapter = omText.IsEmpty();
        if (!e.srcIpFromAdapter && !eth::ParseIPv4((LPCSTR)omText, s.srcIp))
        {
            return Fail(IDC_ETH_TX_SRCIP, "Source IP: a.b.c.d, or empty for the address of the adapter");
        }
        if (!eth::ParseIPv4((LPCSTR)Trimmed(this, IDC_ETH_TX_DSTIP), s.dstIp))
        {
            return Fail(IDC_ETH_TX_DSTIP, "Destination IP: a.b.c.d");
        }
        const UINT ports[2] = { IDC_ETH_TX_SRCPORT, IDC_ETH_TX_DSTPORT };
        uint16_t* values[2] = { &s.srcPort, &s.dstPort };
        for (int i = 0; i < 2; i++)
        {
            omText = Trimmed(this, ports[i]);
            const unsigned long ulPort = strtoul(omText, nullptr, 10);
            if (omText.IsEmpty() || ulPort > 65535)
            {
                return Fail(ports[i], "Port: 0 to 65535");
            }
            *values[i] = (uint16_t)ulPort;
        }
    }

    CString omData;
    GetDlgItemText(IDC_ETH_TX_DATA, omData);
    if (!eth::ParseHexBytes((LPCSTR)omData, s.data))
    {
        return Fail(IDC_ETH_TX_DATA, "Data: bytes in hex, for example 01 02 0A FF");
    }
    std::vector<uint8_t> frame;
    std::string error;
    if (!eth::Build(s, frame, error))
    {
        return Fail(IDC_ETH_TX_DATA, error.c_str());
    }
    if (e.name.empty())
    {
        e.name = "Frame";
    }
    return true;
}

void CEthTxWnd::EnableFields()
{
    const bool bUdp = m_omKind.GetCurSel() == 1;
    GetDlgItem(IDC_ETH_TX_ETHERTYPE)->EnableWindow(!bUdp);
    for (UINT id : { IDC_ETH_TX_SRCIP, IDC_ETH_TX_SRCPORT, IDC_ETH_TX_DSTIP, IDC_ETH_TX_DSTPORT })
    {
        GetDlgItem((int)id)->EnableWindow(bUdp);
    }
}

int CEthTxWnd::Selected() const
{
    return m_omList.GetNextItem(-1, LVNI_SELECTED);
}

void CEthTxWnd::SetStatus(const CString& omText)
{
    SetDlgItemText(IDC_ETH_TX_STATUS, omText);
}

void CEthTxWnd::OnKindChanged()
{
    EnableFields();
}

void CEthTxWnd::OnItemChanged(NMHDR* pNMHDR, LRESULT* pResult)
{
    const NMLISTVIEW* pItem = reinterpret_cast<const NMLISTVIEW*>(pNMHDR);
    *pResult = 0;
    if (m_bFilling || pItem->iItem < 0 || pItem->iItem >= (int)ethmodule::TxEntries().size()
        || (pItem->uChanged & LVIF_STATE) == 0)
    {
        return;
    }
    /* check box: the state image changed */
    if ((pItem->uNewState & LVIS_STATEIMAGEMASK) != (pItem->uOldState & LVIS_STATEIMAGEMASK)
        && (pItem->uOldState & LVIS_STATEIMAGEMASK) != 0)
    {
        EthTxEntry& e = ethmodule::TxEntries()[(size_t)pItem->iItem];
        e.cyclic = m_omList.GetCheck(pItem->iItem) != FALSE;
        if (e.cyclic && e.cycleMs == 0)
        {
            SetStatus("This frame has no cycle time: set Cycle (ms), then Update");
        }
        ethmodule::TxChanged();
    }
    /* selection: the editor shows the frame */
    if ((pItem->uNewState & LVIS_SELECTED) != 0 && (pItem->uOldState & LVIS_SELECTED) == 0)
    {
        Load(ethmodule::TxEntries()[(size_t)pItem->iItem]);
        SetStatus("");
    }
}

void CEthTxWnd::OnDblClk(NMHDR* /*pNMHDR*/, LRESULT* pResult)
{
    /* double click: send the frame, as the Send button */
    *pResult = 0;
    if (Selected() >= 0)
    {
        OnSend();
    }
}

void CEthTxWnd::OnAdd()
{
    EthTxEntry entry;
    if (!Read(entry))
    {
        return;
    }
    std::vector<EthTxEntry>& entries = ethmodule::TxEntries();
    entries.push_back(entry);
    const int item = (int)entries.size() - 1;
    m_bFilling = true;
    m_omList.InsertItem(item, "");
    FillRow(item);
    m_omList.SetItemState(-1, 0, LVIS_SELECTED);
    m_omList.SetItemState(item, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    m_omList.EnsureVisible(item, FALSE);
    m_bFilling = false;
    ethmodule::TxChanged();
    SetStatus("Added");
}

void CEthTxWnd::OnUpdate()
{
    const int item = Selected();
    if (item < 0)
    {
        SetStatus("Select a frame of the list first, or Add a new one");
        return;
    }
    EthTxEntry entry;
    if (!Read(entry))
    {
        return;
    }
    EthTxEntry& e = ethmodule::TxEntries()[(size_t)item];
    entry.cyclic = e.cyclic;
    e = entry;
    FillRow(item);
    ethmodule::TxChanged();
    SetStatus("Updated");
}

void CEthTxWnd::OnDelete()
{
    const int item = Selected();
    if (item < 0)
    {
        return;
    }
    std::vector<EthTxEntry>& entries = ethmodule::TxEntries();
    entries.erase(entries.begin() + item);
    m_bFilling = true;
    m_omList.DeleteItem(item);
    m_bFilling = false;
    ethmodule::TxChanged();
    if (!entries.empty())
    {
        const int next = min(item, (int)entries.size() - 1);
        m_omList.SetItemState(next, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    SetStatus("Deleted");
}

void CEthTxWnd::OnSend()
{
    /* the frame of the editor, so that a change can be tried before Update */
    EthTxEntry entry;
    if (!Read(entry))
    {
        return;
    }
    if (!ethmodule::IsConnected())
    {
        SetStatus("Not connected: Ethernet > Connect first");
        MessageBeep(MB_ICONWARNING);
        return;
    }
    std::vector<uint8_t> frame;
    std::string error;
    if (!ethmodule::BuildTxFrame(entry, frame, error) || !ethmodule::Capture().Send(frame, error))
    {
        SetStatus(error.c_str());
        MessageBeep(MB_ICONWARNING);
        return;
    }
    CString omText;
    omText.Format("Sent: %u bytes", (unsigned)frame.size());
    SetStatus(omText);
}
