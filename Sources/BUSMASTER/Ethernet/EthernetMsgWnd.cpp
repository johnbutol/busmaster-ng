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
 * \file      EthernetMsgWnd.cpp
 * \brief     Message window of the Ethernet module: the frames captured on the adapter
 */

#include "Ethernet_stdafx.h"
#include "EthernetMsgWnd.h"

namespace
{
const UINT_PTR TIMER_ID = 1;
const UINT TIMER_MS = 100;
const size_t MAX_ROWS = 50000;          // frames kept; the oldest go first
const size_t DROP_ROWS = 5000;
const size_t DATA_BYTES = 64;           // bytes shown in the Data column

enum { COL_TIME, COL_DIR, COL_SRCMAC, COL_DSTMAC, COL_VLAN, COL_PROTOCOL, COL_SOURCE, COL_DESTINATION, COL_LENGTH,
       COL_INFO, COL_DATA, COL_COUNT };
const struct { const char* title; int width; int format; } sg_asColumns[COL_COUNT] =
{
    { "Time",            62, LVCFMT_LEFT  },
    { "Tx/Rx",           22, LVCFMT_LEFT  },
    { "Source MAC",      70, LVCFMT_LEFT  },
    { "Destination MAC", 70, LVCFMT_LEFT  },
    { "VLAN",            24, LVCFMT_RIGHT },
    { "Protocol",        70, LVCFMT_LEFT  },
    { "Source",          84, LVCFMT_LEFT  },
    { "Destination",     84, LVCFMT_LEFT  },
    { "Length",          28, LVCFMT_RIGHT },
    { "Info",           200, LVCFMT_LEFT  },
    { "Data",           160, LVCFMT_LEFT  },
};

enum : UINT
{
    CMD_COLUMN = 100,           // + column
    CMD_COPY = 200,
    CMD_COPY_HEX,
    CMD_SELECT_ALL,
    CMD_SAVE_PCAP,
};

/** Hours:minutes:seconds:100 us, as the message window of BUSMASTER; time in 100 us */
CString FormatTime(int64_t time)
{
    if (time < 0)
    {
        time = -time;
    }
    CString omTime;
    omTime.Format("%02d:%02d:%02d:%04d", (int)(time / 36000000), (int)(time / 600000 % 60), (int)(time / 10000 % 60),
                  (int)(time % 10000));
    return omTime;
}

/** Local time of day of a UTC time in microseconds since 1970 */
CString FormatSystemTime(int64_t utcUs)
{
    const ULONGLONG ullTicks = (ULONGLONG)utcUs * 10 + 116444736000000000ULL;     // FILETIME, 100 ns since 1601
    FILETIME ftUtc = { (DWORD)ullTicks, (DWORD)(ullTicks >> 32) };
    FILETIME ftLocal;
    SYSTEMTIME st;
    if (!FileTimeToLocalFileTime(&ftUtc, &ftLocal) || !FileTimeToSystemTime(&ftLocal, &st))
    {
        return CString();
    }
    CString omTime;
    omTime.Format("%02d:%02d:%02d:%04d", st.wHour, st.wMinute, st.wSecond, (int)(utcUs / 100 % 10000));
    return omTime;
}
}

BEGIN_MESSAGE_MAP(CEthMsgWnd, CEthPane)
    ON_WM_SIZE()
    ON_WM_TIMER()
    ON_WM_DESTROY()
    ON_NOTIFY(LVN_GETDISPINFO, IDC_ETH_FRAMES, OnGetDispInfo)
    ON_NOTIFY(NM_CUSTOMDRAW, IDC_ETH_FRAMES, OnCustomDraw)
    ON_NOTIFY(LVN_KEYDOWN, IDC_ETH_FRAMES, OnKeyDown)
    ON_NOTIFY(HDN_BEGINTRACKA, 0, OnHeaderBeginTrack)
    ON_NOTIFY(HDN_BEGINTRACKW, 0, OnHeaderBeginTrack)
    ON_NOTIFY(HDN_DIVIDERDBLCLICKA, 0, OnHeaderBeginTrack)
    ON_NOTIFY(HDN_DIVIDERDBLCLICKW, 0, OnHeaderBeginTrack)
    ON_WM_CONTEXTMENU()
END_MESSAGE_MAP()

CEthMsgWnd::CEthMsgWnd()
    : CEthPane(IDD_ETH_MSG), m_ullFirstRow(0), m_unDropped(0), m_unPaused(0), m_bLayoutReady(false)
{
}

BOOL CEthMsgWnd::OnInitDialog()
{
    CEthPane::OnInitDialog();
    static_assert(_countof(m_asColumns) == COL_COUNT, "one Column per column of the list");
    m_omList.SubclassDlgItem(IDC_ETH_FRAMES, this);
    m_omList.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    for (int i = 0; i < COL_COUNT; i++)
    {
        m_omList.InsertColumn(i, sg_asColumns[i].title, sg_asColumns[i].format, DialogUnitsX(sg_asColumns[i].width));
    }
    m_bLayoutReady = true;
    CRect omClient;
    GetClientRect(&omClient);
    Layout(omClient.Width(), omClient.Height());
    SetTimer(TIMER_ID, TIMER_MS, nullptr);
    UpdateStatus();
    return TRUE;
}

void CEthMsgWnd::OnDestroy()
{
    KillTimer(TIMER_ID);
    CEthPane::OnDestroy();
}

void CEthMsgWnd::Layout(int cx, int cy)
{
    if (!m_bLayoutReady)
    {
        return;
    }
    const int margin = DialogUnitsX(7);
    const int top = DialogUnitsY(7);
    const int listTop = DialogUnitsY(18);
    GetDlgItem(IDC_ETH_STATUS)->MoveWindow(margin, top, max(cx - 2 * margin, 10), DialogUnitsY(8));
    m_omList.MoveWindow(margin, listTop, max(cx - 2 * margin, 20), max(cy - listTop - DialogUnitsY(7), 10));
}

void CEthMsgWnd::OnSize(UINT nType, int cx, int cy)
{
    CEthPane::OnSize(nType, cx, cy);
    if (nType != SIZE_MINIMIZED)
    {
        Layout(cx, cy);
    }
}

void CEthMsgWnd::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == TIMER_ID)
    {
        TakeFrames();
        UpdateStatus();
    }
    CEthPane::OnTimer(nIDEvent);
}

void CEthMsgWnd::TakeFrames()
{
    std::vector<eth::CapturedFrame> frames;
    ethmodule::Capture().Take(frames);
    if (frames.empty())
    {
        return;
    }
    if (ethmodule::Options().pause)
    {
        m_unPaused += frames.size();
        return;
    }

    const eth::Adapter& adapter = ethmodule::Capture().CurrentAdapter();
    const bool bOverwrite = ethmodule::Options().overwrite;
    bool bUpdated = false;          // overwrite mode: a row of the view changed
    for (eth::CapturedFrame& frame : frames)
    {
        const uint64_t ullNumber = m_ullFirstRow + m_rows.size();
        eth::Decoded d;
        eth::Decode(frame.data.data(), frame.data.size(), d);
        m_rows.emplace_back();
        Row& row = m_rows.back();
        row.tx = adapter.hasMac && memcmp(d.src, adapter.mac, 6) == 0;
        row.vlanId = d.vlanId;
        row.payloadOffset = d.payloadOffset;
        row.kindKey = d.kindKey ^ (row.tx ? 1 : 0);
        row.protocol = std::move(d.protocol);
        row.source = std::move(d.source);
        row.destination = std::move(d.destination);
        row.info = std::move(d.info);
        auto last = m_lastOfKind.find(row.kindKey);
        if (last != m_lastOfKind.end())
        {
            row.previousUs = last->second;
            last->second = frame.timeUs;
        }
        else
        {
            m_lastOfKind[row.kindKey] = frame.timeUs;
        }
        row.frame = std::move(frame);

        if (bOverwrite)
        {
            auto slot = m_slots.find(row.kindKey);
            if (slot != m_slots.end())
            {
                m_view[slot->second] = ullNumber;
                bUpdated = true;
                continue;
            }
            m_slots[row.kindKey] = m_view.size();
        }
        m_view.push_back(ullNumber);
    }

    /* the oldest frames go first, displayed or not */
    bool bFull = false;
    if (m_rows.size() > MAX_ROWS)
    {
        const size_t unDrop = m_rows.size() - MAX_ROWS + DROP_ROWS;
        m_rows.erase(m_rows.begin(), m_rows.begin() + unDrop);
        m_ullFirstRow += unDrop;
        m_unDropped += unDrop;
        if (bOverwrite)
        {
            RebuildView();
            return;
        }
        while (!m_view.empty() && m_view.front() < m_ullFirstRow)
        {
            m_view.pop_front();
            bFull = true;
        }
    }
    if (bUpdated)
    {
        m_omList.Invalidate(FALSE);
    }
    if (bFull)
    {
        /* rows moved: the selection no longer matches */
        m_omList.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        m_omList.SetItemCount((int)m_view.size());
        m_omList.Invalidate(FALSE);
    }
    else
    {
        m_omList.SetItemCountEx((int)m_view.size(), LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
    }
    if (ethmodule::Options().showLast && !m_view.empty())
    {
        m_omList.EnsureVisible((int)m_view.size() - 1, FALSE);
    }
}

void CEthMsgWnd::RebuildView()
{
    m_view.clear();
    m_slots.clear();
    const bool bOverwrite = ethmodule::Options().overwrite;
    for (size_t i = 0; i < m_rows.size(); i++)
    {
        if (bOverwrite)
        {
            /* one row per kind, at the place of its first frame, with its last frame */
            auto slot = m_slots.emplace(m_rows[i].kindKey, m_view.size());
            if (!slot.second)
            {
                m_view[slot.first->second] = m_ullFirstRow + i;
                continue;
            }
        }
        m_view.push_back(m_ullFirstRow + i);
    }
    m_omList.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    m_omList.SetItemCount((int)m_view.size());
    m_omList.Invalidate(FALSE);
    if (ethmodule::Options().showLast && !m_view.empty())
    {
        m_omList.EnsureVisible((int)m_view.size() - 1, FALSE);
    }
    UpdateStatus();
}

void CEthMsgWnd::OptionsChanged()
{
    if (GetSafeHwnd() != nullptr)
    {
        RebuildView();
    }
}

void CEthMsgWnd::Clear()
{
    m_rows.clear();
    m_view.clear();
    m_slots.clear();
    m_lastOfKind.clear();
    m_ullFirstRow = 0;
    m_unDropped = 0;
    m_unPaused = 0;
    if (GetSafeHwnd() != nullptr)
    {
        m_omList.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        m_omList.SetItemCount(0);
        UpdateStatus();
    }
}

void CEthMsgWnd::UpdateStatus()
{
    const EthSettings& settings = ethmodule::Settings();
    CString omText;
    if (ethmodule::IsConnected())
    {
        omText.Format("%s: connected", ethmodule::Capture().CurrentAdapter().name.c_str());
    }
    else if (!ethmodule::StopReason().empty())
    {
        omText = ethmodule::StopReason().c_str();
    }
    else if (settings.device.empty())
    {
        omText = "No adapter selected: Ethernet > Adapter Selection";
    }
    else
    {
        omText.Format("%s: not connected", settings.adapterName.c_str());
    }
    CString omPart;
    omPart.Format(", %u frame%s", (unsigned)m_rows.size(), m_rows.size() == 1 ? "" : "s");
    omText += omPart;
    if (ethmodule::Options().overwrite)
    {
        omPart.Format(" (%u kinds)", (unsigned)m_view.size());
        omText += omPart;
    }
    const eth::CEthernetCapture::Stats stats = ethmodule::Capture().GetStats();
    if (stats.sent > 0 || stats.sendErrors > 0)
    {
        omPart.Format(", %llu sent", stats.sent);
        omText += omPart;
        if (stats.sendErrors > 0)
        {
            omPart.Format(" (%llu failed)", stats.sendErrors);
            omText += omPart;
        }
    }
    if (stats.dropped > 0 || stats.lost > 0)
    {
        omPart.Format(", %llu lost by the capture", (unsigned long long)stats.dropped + stats.lost);
        omText += omPart;
    }
    if (m_unDropped > 0)
    {
        omPart.Format(", %u oldest removed", (unsigned)m_unDropped);
        omText += omPart;
    }
    if (ethmodule::Options().pause)
    {
        omPart.Format(", paused (%u not taken)", (unsigned)m_unPaused);
        omText += omPart;
    }
    CString omOld;
    GetDlgItemText(IDC_ETH_STATUS, omOld);
    if (omOld != omText)
    {
        SetDlgItemText(IDC_ETH_STATUS, omText);
    }
}

CString CEthMsgWnd::RowTime(size_t item) const
{
    const Row& row = ViewRow(item);
    switch (ethmodule::Options().timeMode)
    {
        case EthDisplayOptions::AbsoluteTime:
            return FormatTime((row.frame.timeUs - ethmodule::ConnectTimeUs()) / 100);
        case EthDisplayOptions::RelativeTime:
            if (ethmodule::Options().overwrite)
            {
                /* since the previous frame of the same kind */
                return FormatTime(row.previousUs < 0 ? 0 : (row.frame.timeUs - row.previousUs) / 100);
            }
            /* since the frame above */
            return FormatTime(item == 0 ? 0 : (row.frame.timeUs - ViewRow(item - 1).frame.timeUs) / 100);
        default:
            return FormatSystemTime(row.frame.timeUs);
    }
}

CString CEthMsgWnd::CellText(size_t item, int nColumn) const
{
    const Row& row = ViewRow(item);
    const std::vector<uint8_t>& data = row.frame.data;
    CString omText;
    switch (nColumn)
    {
        case COL_TIME:
            return RowTime(item);
        case COL_DIR:
            return row.tx ? "Tx" : "Rx";
        case COL_SRCMAC:
            if (data.size() >= 12)
            {
                omText = eth::MacToString(&data[6]).c_str();
            }
            break;
        case COL_DSTMAC:
            if (data.size() >= 6)
            {
                omText = eth::MacToString(&data[0]).c_str();
            }
            break;
        case COL_VLAN:
            if (row.vlanId >= 0)
            {
                omText.Format("%d", row.vlanId);
            }
            break;
        case COL_PROTOCOL:
            return row.protocol.c_str();
        case COL_SOURCE:
            return row.source.c_str();
        case COL_DESTINATION:
            return row.destination.c_str();
        case COL_LENGTH:
            omText.Format("%u", row.frame.length);
            break;
        case COL_INFO:
            return row.info.c_str();
        case COL_DATA:
            if (data.size() > row.payloadOffset)
            {
                omText = eth::HexBytes(&data[row.payloadOffset], data.size() - row.payloadOffset, DATA_BYTES).c_str();
            }
            break;
    }
    return omText;
}

void CEthMsgWnd::OnGetDispInfo(NMHDR* pNMHDR, LRESULT* pResult)
{
    NMLVDISPINFO* pInfo = reinterpret_cast<NMLVDISPINFO*>(pNMHDR);
    LVITEM& item = pInfo->item;
    if ((item.mask & LVIF_TEXT) && item.iItem >= 0 && item.iItem < (int)m_view.size())
    {
        const CString omText = CellText((size_t)item.iItem, item.iSubItem);
        lstrcpyn(item.pszText, omText, item.cchTextMax);
    }
    *pResult = 0;
}

void CEthMsgWnd::OnCustomDraw(NMHDR* pNMHDR, LRESULT* pResult)
{
    NMLVCUSTOMDRAW* pDraw = reinterpret_cast<NMLVCUSTOMDRAW*>(pNMHDR);
    *pResult = CDRF_DODEFAULT;
    if (pDraw->nmcd.dwDrawStage == CDDS_PREPAINT)
    {
        *pResult = CDRF_NOTIFYITEMDRAW;
    }
    else if (pDraw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT)
    {
        const size_t index = (size_t)pDraw->nmcd.dwItemSpec;
        if (index < m_view.size() && ViewRow(index).tx)
        {
            pDraw->clrText = RGB(0, 70, 170);       // frames sent by this PC
        }
    }
}

void CEthMsgWnd::OnKeyDown(NMHDR* pNMHDR, LRESULT* pResult)
{
    NMLVKEYDOWN* pKey = reinterpret_cast<NMLVKEYDOWN*>(pNMHDR);
    if (GetKeyState(VK_CONTROL) < 0)
    {
        if (pKey->wVKey == 'C')
        {
            CopySelection(false);
        }
        else if (pKey->wVKey == 'A')
        {
            m_omList.SetItemState(-1, LVIS_SELECTED, LVIS_SELECTED);
        }
    }
    *pResult = 0;
}

void CEthMsgWnd::CopySelection(bool bHex)
{
    std::vector<int> columns;
    for (int i = 0; i < COL_COUNT; i++)
    {
        if (!m_asColumns[i].hidden)
        {
            columns.push_back(i);
        }
    }
    CString omText;
    if (!bHex)
    {
        for (size_t i = 0; i < columns.size(); i++)
        {
            omText += sg_asColumns[columns[i]].title;
            omText += i + 1 < columns.size() ? "\t" : "\r\n";
        }
    }
    int nItem = -1;
    int nCount = 0;
    while ((nItem = m_omList.GetNextItem(nItem, LVNI_SELECTED)) != -1 && nItem < (int)m_view.size())
    {
        if (bHex)
        {
            /* the whole frame, one line per frame */
            const std::vector<uint8_t>& data = ViewRow((size_t)nItem).frame.data;
            omText += eth::HexBytes(data.data(), data.size(), data.size()).c_str();
            omText += "\r\n";
        }
        else
        {
            for (size_t i = 0; i < columns.size(); i++)
            {
                omText += CellText((size_t)nItem, columns[i]);
                omText += i + 1 < columns.size() ? "\t" : "\r\n";
            }
        }
        nCount++;
    }
    if (nCount == 0 || !OpenClipboard())
    {
        return;
    }
    EmptyClipboard();
    const size_t unSize = (size_t)omText.GetLength() + 1;
    HGLOBAL hData = GlobalAlloc(GMEM_MOVEABLE, unSize);
    if (hData != nullptr)
    {
        memcpy(GlobalLock(hData), (LPCSTR)omText, unSize);
        GlobalUnlock(hData);
        if (SetClipboardData(CF_TEXT, hData) == nullptr)
        {
            GlobalFree(hData);
        }
    }
    CloseClipboard();
}

bool CEthMsgWnd::SavePcap()
{
    if (m_rows.empty())
    {
        AfxMessageBox("There are no frames to save.", MB_ICONINFORMATION);
        return false;
    }
    CFileDialog dialog(FALSE, "pcap", "ethernet.pcap", OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY,
                       "pcap files, for Wireshark (*.pcap)|*.pcap|All files (*.*)|*.*||", this);
    if (dialog.DoModal() != IDOK)
    {
        return false;
    }
    FILE* pFile = nullptr;
    if (fopen_s(&pFile, dialog.GetPathName(), "wb") != 0 || pFile == nullptr)
    {
        AfxMessageBox("The file cannot be written: " + dialog.GetPathName(), MB_ICONERROR);
        return false;
    }
    /* classic pcap format: global header, then a record header and the bytes of each frame */
    const uint32_t header[6] = { 0xa1b2c3d4, 0x00040002, 0, 0, 65535, 1 /* Ethernet */ };
    bool bOk = fwrite(header, sizeof(header), 1, pFile) == 1;
    for (size_t i = 0; bOk && i < m_rows.size(); i++)
    {
        const eth::CapturedFrame& frame = m_rows[i].frame;
        const uint32_t record[4] = { (uint32_t)(frame.timeUs / 1000000), (uint32_t)(frame.timeUs % 1000000),
                                     (uint32_t)frame.data.size(), frame.length };
        bOk = fwrite(record, sizeof(record), 1, pFile) == 1
              && (frame.data.empty() || fwrite(frame.data.data(), frame.data.size(), 1, pFile) == 1);
    }
    bOk = fclose(pFile) == 0 && bOk;
    if (!bOk)
    {
        AfxMessageBox("Writing the file failed: " + dialog.GetPathName(), MB_ICONERROR);
    }
    return bOk;
}

void CEthMsgWnd::OnHeaderBeginTrack(NMHDR* pNMHDR, LRESULT* pResult)
{
    /* a hidden column stays hidden: the context menu shows it again */
    const NMHEADER* pHeader = reinterpret_cast<const NMHEADER*>(pNMHDR);
    *pResult = FALSE;
    if (pHeader->iItem >= 0 && pHeader->iItem < COL_COUNT
        && pNMHDR->hwndFrom == m_omList.GetHeaderCtrl()->GetSafeHwnd())
    {
        *pResult = m_asColumns[pHeader->iItem].hidden ? TRUE : FALSE;
    }
}

void CEthMsgWnd::ToggleColumn(int column)
{
    Column& c = m_asColumns[column];
    if (c.hidden)
    {
        m_omList.SetColumnWidth(column, c.width > 0 ? c.width : DialogUnitsX(sg_asColumns[column].width));
        c.hidden = false;
    }
    else
    {
        c.width = m_omList.GetColumnWidth(column);
        m_omList.SetColumnWidth(column, 0);
        c.hidden = true;
    }
}

void CEthMsgWnd::OnContextMenu(CWnd* pWnd, CPoint point)
{
    const HWND hWnd = pWnd->GetSafeHwnd();
    const HWND hHeader = m_omList.GetHeaderCtrl()->GetSafeHwnd();
    if (hWnd != m_omList.GetSafeHwnd() && hWnd != hHeader)
    {
        CEthPane::OnContextMenu(pWnd, point);
        return;
    }
    const bool bKeyboard = point.x == -1 && point.y == -1;
    if (bKeyboard)
    {
        CRect omRect;
        m_omList.GetWindowRect(&omRect);
        point = omRect.TopLeft() + CPoint(DialogUnitsX(10), DialogUnitsY(20));
    }
    CRect omHeader;
    ::GetWindowRect(hHeader, &omHeader);
    const bool bOnHeader = !bKeyboard && omHeader.PtInRect(point);

    CMenu menu;
    CMenu columns;
    if (!menu.CreatePopupMenu() || !columns.CreatePopupMenu())
    {
        return;
    }
    int nVisible = 0;
    for (int i = 0; i < COL_COUNT; i++)
    {
        nVisible += m_asColumns[i].hidden ? 0 : 1;
    }
    for (int i = 0; i < COL_COUNT; i++)
    {
        UINT uFlags = MF_STRING | (m_asColumns[i].hidden ? MF_UNCHECKED : MF_CHECKED);
        if (!m_asColumns[i].hidden && nVisible <= 1)
        {
            uFlags |= MF_GRAYED;        // the last column displayed cannot be hidden
        }
        (bOnHeader ? menu : columns).AppendMenu(uFlags, CMD_COLUMN + i, sg_asColumns[i].title);
    }
    if (!bOnHeader)
    {
        const UINT uSelection = m_omList.GetSelectedCount() > 0 ? 0 : MF_GRAYED;
        menu.AppendMenu(MF_STRING | uSelection, CMD_COPY, "Copy\tCtrl+C");
        menu.AppendMenu(MF_STRING | uSelection, CMD_COPY_HEX, "Copy the frames in hex");
        menu.AppendMenu(MF_STRING | (m_view.empty() ? MF_GRAYED : 0), CMD_SELECT_ALL, "Select all\tCtrl+A");
        menu.AppendMenu(MF_SEPARATOR);
        menu.AppendMenu(MF_STRING | (m_rows.empty() ? MF_GRAYED : 0), CMD_SAVE_PCAP, "Save as pcap (Wireshark)...");
        menu.AppendMenu(MF_POPUP, (UINT_PTR)columns.Detach(), "Columns");
    }

    const UINT uCmd = (UINT)menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, point.x, point.y, this);
    if (uCmd >= CMD_COLUMN && uCmd < CMD_COLUMN + COL_COUNT)
    {
        ToggleColumn((int)(uCmd - CMD_COLUMN));
    }
    else if (uCmd == CMD_COPY || uCmd == CMD_COPY_HEX)
    {
        CopySelection(uCmd == CMD_COPY_HEX);
    }
    else if (uCmd == CMD_SELECT_ALL)
    {
        m_omList.SetItemState(-1, LVIS_SELECTED, LVIS_SELECTED);
    }
    else if (uCmd == CMD_SAVE_PCAP)
    {
        SavePcap();
    }
}
