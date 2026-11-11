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
 * \file      CANopenWnd.cpp
 * \brief     CANopen window: nodes and decoded events
 */

#include "CANopen_stdafx.h"
#include "CANopenWnd.h"

using canopen::Decoder;
using canopen::Event;
using canopen::EventType;
using canopen::Frame;
using canopen::NodeInfo;

namespace
{
const UINT_PTR TIMER_ID = 1;
const UINT TIMER_MS = 100;
const UINT NODES_EVERY = 5;             // the node list is refreshed every 5 timer ticks
const size_t MAX_ROWS = 100000;         // rows of the event list; the oldest go first
const size_t DROP_ROWS = 10000;

/* Columns, widths in dialog units */
enum { EV_TIME, EV_CHANNEL, EV_DIR, EV_COBID, EV_TYPE, EV_NODE, EV_TEXT, EV_DATA, EV_COUNT };
const struct { const char* title; int width; int format; } sg_asEventColumns[EV_COUNT] =
{
    { "Time",        58, LVCFMT_LEFT  },
    { "Ch",          16, LVCFMT_RIGHT },
    { "Dir",         16, LVCFMT_LEFT  },
    { "COB-ID",      30, LVCFMT_LEFT  },
    { "Type",        60, LVCFMT_LEFT  },
    { "Node",        24, LVCFMT_RIGHT },
    { "Description", 260, LVCFMT_LEFT },
    { "Data",        100, LVCFMT_LEFT },
};

enum { ND_CHANNEL, ND_NODE, ND_STATE, ND_HEARTBEAT, ND_LASTSEEN, ND_EMCY, ND_LASTEMCY, ND_COUNT };
const struct { const char* title; int width; int format; } sg_asNodeColumns[ND_COUNT] =
{
    { "Ch",          16, LVCFMT_RIGHT },
    { "Node",        24, LVCFMT_RIGHT },
    { "State",       90, LVCFMT_LEFT  },
    { "Heartbeat",   40, LVCFMT_RIGHT },
    { "Last seen",   50, LVCFMT_LEFT  },
    { "EMCY",        24, LVCFMT_RIGHT },
    { "Last EMCY",  120, LVCFMT_LEFT  },
};

constexpr uint32_t TypeBit(EventType type)
{
    return 1u << (int)type;
}

/* Types of event in the filter menu */
const struct { const char* name; uint32_t mask; } sg_asTypeGroups[] =
{
    { "NMT",                    TypeBit(EventType::Nmt) },
    { "SYNC",                   TypeBit(EventType::Sync) },
    { "TIME",                   TypeBit(EventType::Time) },
    { "EMCY",                   TypeBit(EventType::Emcy) },
    { "TPDO",                   TypeBit(EventType::Tpdo) },
    { "RPDO",                   TypeBit(EventType::Rpdo) },
    { "SDO",                    TypeBit(EventType::SdoRequest) | TypeBit(EventType::SdoResponse) |
                                TypeBit(EventType::SdoAbort) | TypeBit(EventType::SdoResult) },
    { "Heartbeat and boot-up",  TypeBit(EventType::BootUp) | TypeBit(EventType::Heartbeat) },
    { "Node guarding",          TypeBit(EventType::GuardRequest) | TypeBit(EventType::GuardResponse) },
    { "LSS",                    TypeBit(EventType::Lss) },
    { "Frames that are not CANopen", TypeBit(EventType::Other) },
};
const int TYPE_GROUPS = sizeof(sg_asTypeGroups) / sizeof(sg_asTypeGroups[0]);
const uint32_t ALL_TYPES = (TypeBit(EventType::Other) << 1) - 1;

/* Commands of the context menus */
enum : UINT
{
    CMD_EVENT_COLUMN = 100,     // + column
    CMD_NODE_COLUMN  = 200,     // + column
    CMD_TYPE         = 300,     // + group of sg_asTypeGroups
    CMD_ALL_TYPES    = 400,
    CMD_COPY,
    CMD_SELECT_ALL,
    CMD_ONLY_NODE,
    CMD_ALL_NODES,
};
}

BEGIN_MESSAGE_MAP(CCANopenWnd, CDialog)
    ON_WM_SIZE()
    ON_WM_TIMER()
    ON_WM_DESTROY()
    ON_NOTIFY(LVN_GETDISPINFO, IDC_CO_EVENTS, OnEventsGetDispInfo)
    ON_NOTIFY(NM_CUSTOMDRAW, IDC_CO_EVENTS, OnEventsCustomDraw)
    ON_NOTIFY(LVN_KEYDOWN, IDC_CO_EVENTS, OnEventsKeyDown)
    ON_NOTIFY(HDN_BEGINTRACKA, 0, OnHeaderBeginTrack)
    ON_NOTIFY(HDN_BEGINTRACKW, 0, OnHeaderBeginTrack)
    ON_NOTIFY(HDN_DIVIDERDBLCLICKA, 0, OnHeaderBeginTrack)
    ON_NOTIFY(HDN_DIVIDERDBLCLICKW, 0, OnHeaderBeginTrack)
    ON_WM_CONTEXTMENU()
END_MESSAGE_MAP()

CCANopenWnd::CCANopenWnd(CCANopenMonitor& monitor)
    : m_monitor(monitor)
    , m_ullFirstRow(0)
    , m_unTypes(ALL_TYPES & ~TypeBit(EventType::Other))
    , m_nNodeFilter(-1)
    , m_unLost(0)
    , m_nFirstTick(0)
    , m_bFirstTick(true)
    , m_bLayoutReady(false)
    , m_unTick(0)
{
}

CANopenDisplayOptions& CCANopenWnd::Options()
{
    static CANopenDisplayOptions s_options;
    return s_options;
}

BOOL CCANopenWnd::CreateWnd(CWnd* pParent)
{
    return Create(IDD_CANOPEN, pParent);
}

int CCANopenWnd::DialogUnitsX(int du)
{
    CRect r(0, 0, du, 0);
    MapDialogRect(&r);
    return r.Width();
}

int CCANopenWnd::DialogUnitsY(int du)
{
    CRect r(0, 0, 0, du);
    MapDialogRect(&r);
    return r.Height();
}

BOOL CCANopenWnd::OnInitDialog()
{
    CDialog::OnInitDialog();
    static_assert(_countof(m_asEventColumns) == EV_COUNT && _countof(m_asNodeColumns) == ND_COUNT,
                  "one Column per column of the lists");

    m_omNodes.SubclassDlgItem(IDC_CO_NODES, this);
    m_omEvents.SubclassDlgItem(IDC_CO_EVENTS, this);
    m_omNodes.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    m_omEvents.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    for (int i = 0; i < ND_COUNT; i++)
    {
        m_omNodes.InsertColumn(i, sg_asNodeColumns[i].title, sg_asNodeColumns[i].format,
                               DialogUnitsX(sg_asNodeColumns[i].width));
    }
    for (int i = 0; i < EV_COUNT; i++)
    {
        m_omEvents.InsertColumn(i, sg_asEventColumns[i].title, sg_asEventColumns[i].format,
                                DialogUnitsX(sg_asEventColumns[i].width));
    }
    m_bLayoutReady = true;
    CRect omClient;
    GetClientRect(&omClient);
    Layout(omClient.Width(), omClient.Height());

    SetTimer(TIMER_ID, TIMER_MS, nullptr);
    UpdateStatus();
    return TRUE;
}

void CCANopenWnd::OnOK()
{
    /* Enter does not close the window */
}

void CCANopenWnd::OnCancel()
{
    /* Escape does not close the window either: the frame around it does */
}

void CCANopenWnd::OnDestroy()
{
    KillTimer(TIMER_ID);
    CDialog::OnDestroy();
}

void CCANopenWnd::SetStatus(const CString& omStatus)
{
    m_omStatus = omStatus;
    if (GetSafeHwnd() != nullptr)
    {
        UpdateStatus();
    }
}

void CCANopenWnd::Layout(int cx, int cy)
{
    if (!m_bLayoutReady)
    {
        return;
    }
    const int margin = DialogUnitsX(7);
    const int gap = DialogUnitsX(7);
    const int labelTop = DialogUnitsY(7);
    const int listTop = DialogUnitsY(18);
    const int listHeight = max(cy - listTop - DialogUnitsY(7), 10);
    const int width = max(cx - 2 * margin - gap, 20);
    const int nodesWidth = width * 36 / 100;

    const int eventsLeft = margin + nodesWidth + gap;
    const int labelWidth = DialogUnitsX(70);
    const int labelHeight = DialogUnitsY(8);

    GetDlgItem(IDC_CO_NODES_LABEL)->MoveWindow(margin, labelTop, labelWidth, labelHeight);
    m_omNodes.MoveWindow(margin, listTop, nodesWidth, listHeight);
    GetDlgItem(IDC_CO_EVENTS_LABEL)->MoveWindow(eventsLeft, labelTop, labelWidth, labelHeight);
    m_omEvents.MoveWindow(eventsLeft, listTop, width - nodesWidth, listHeight);
    /* the status line takes the rest of the first row, right aligned */
    const int statusLeft = eventsLeft + labelWidth;
    GetDlgItem(IDC_CO_STATUS)->MoveWindow(statusLeft, labelTop, max(cx - margin - statusLeft, 10), labelHeight);
}

void CCANopenWnd::OnSize(UINT nType, int cx, int cy)
{
    CDialog::OnSize(nType, cx, cy);
    if (nType != SIZE_MINIMIZED)
    {
        Layout(cx, cy);
    }
}

void CCANopenWnd::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == TIMER_ID)
    {
        TakeEvents();
        if (++m_unTick % NODES_EVERY == 0)
        {
            UpdateNodes();
            UpdateStatus();
        }
    }
    CDialog::OnTimer(nIDEvent);
}

CString CCANopenWnd::FormatTime(int64_t time)
{
    /* same format as the message window (CFormatMsgCommon::vFormatTimeStamp): hours:minutes:seconds:100 us */
    if (time < 0)
    {
        time = -time;
    }
    CString omTime;
    omTime.Format("%02d:%02d:%02d:%04d", (int)(time / 36000000), (int)(time / 600000 % 60), (int)(time / 10000 % 60),
                  (int)(time % 10000));
    return omTime;
}

CString CCANopenWnd::RowTime(size_t item) const
{
    const Row& row = ViewRow(item);
    switch (Options().timeMode)
    {
        case CANopenDisplayOptions::AbsoluteTime:
            return FormatTime(row.absoluteTime);
        case CANopenDisplayOptions::RelativeTime:
            if (Options().overwrite)
            {
                /* since the previous event of the same kind */
                return FormatTime(row.previousTick < 0 ? 0 : CCANopenMonitor::TicksBetween(row.event.frame.tick, row.previousTick));
            }
            /* since the event above */
            return FormatTime(item == 0 ? 0 : CCANopenMonitor::TicksBetween(row.event.frame.tick, ViewRow(item - 1).event.frame.tick));
        default:
            return FormatTime(row.systemTime >= 0 ? row.systemTime : row.absoluteTime);
    }
}

int64_t CCANopenWnd::NowTick(const CCANopenMonitor::TimeBase& base) const
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    int64_t now = st.wHour * 36000000LL + st.wMinute * 600000LL + st.wSecond * 10000LL + st.wMilliseconds * 10LL;
    if (now < base.connectTime)
    {
        now += 24 * 36000000LL;     // after midnight
    }
    return base.connectTick + (now - base.connectTime);
}

uint64_t CCANopenWnd::KindKey(const Event& e)
{
    /* as the message window in overwrite mode: one row per channel, identifier, direction and frame type,
       and here also per decoded type, so that an SDO abort does not hide the last SDO response */
    const Frame& f = e.frame;
    return ((uint64_t)f.channel << 40) | ((uint64_t)e.type << 32) | ((uint64_t)f.tx << 31) | ((uint64_t)f.rtr << 30) |
           ((uint64_t)f.ext << 29) | (f.id & 0x1FFFFFFF);
}

void CCANopenWnd::TakeEvents()
{
    std::vector<Event> events;
    size_t unLost = 0;
    m_monitor.TakeEvents(events, unLost);
    m_unLost += unLost;
    if (events.empty() || Options().pause)
    {
        return;
    }

    const CCANopenMonitor::TimeBase base = m_monitor.GetTimeBase();
    const bool bOverwrite = Options().overwrite;
    bool bUpdated = false;      // overwrite mode: a row of the view changed
    for (Event& e : events)
    {
        const uint64_t ullNumber = m_ullFirstRow + m_rows.size();
        const uint64_t ullKey = KindKey(e);
        m_rows.emplace_back();
        Row& row = m_rows.back();
        const int64_t tick = e.frame.tick;
        if (base.valid)
        {
            row.absoluteTime = CCANopenMonitor::TicksSince(tick, base.connectTick);
            row.systemTime = row.absoluteTime + base.connectTime;
        }
        else
        {
            if (m_bFirstTick)
            {
                m_nFirstTick = tick;
                m_bFirstTick = false;
            }
            row.absoluteTime = CCANopenMonitor::TicksSince(tick, m_nFirstTick);
        }
        std::pair<int64_t, uint32_t>& kind = m_kinds.emplace(ullKey, std::make_pair((int64_t)-1, 0u)).first->second;
        row.previousTick = kind.first;
        row.count = ++kind.second;
        kind.first = tick;
        row.event = std::move(e);

        if (!IsVisible(row.event))
        {
            continue;
        }
        if (bOverwrite)
        {
            auto slot = m_slots.find(ullKey);
            if (slot != m_slots.end())
            {
                m_view[slot->second] = ullNumber;
                bUpdated = true;
                continue;
            }
            m_slots[ullKey] = m_view.size();
        }
        m_view.push_back(ullNumber);
    }

    /* the oldest events go first, displayed or not */
    bool bFull = false;
    if (m_rows.size() > MAX_ROWS)
    {
        const size_t unDrop = m_rows.size() - MAX_ROWS + DROP_ROWS;
        m_rows.erase(m_rows.begin(), m_rows.begin() + unDrop);
        m_ullFirstRow += unDrop;
        if (bOverwrite)
        {
            /* the kinds whose last event went are no longer displayed */
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
        m_omEvents.Invalidate(FALSE);
    }
    if (bFull)
    {
        /* rows moved: the selection no longer matches */
        m_omEvents.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        m_omEvents.SetItemCount((int)m_view.size());
        m_omEvents.Invalidate(FALSE);
    }
    else
    {
        m_omEvents.SetItemCountEx((int)m_view.size(), LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
    }
    if (Options().showLast && !m_view.empty())
    {
        m_omEvents.EnsureVisible((int)m_view.size() - 1, FALSE);
    }
}

bool CCANopenWnd::IsVisible(const Event& e) const
{
    if ((m_unTypes & TypeBit(e.type)) == 0)
    {
        return false;
    }
    return m_nNodeFilter < 0 || (e.node != 0 && ((e.frame.channel << 8) | e.node) == m_nNodeFilter);
}

void CCANopenWnd::RebuildView()
{
    m_view.clear();
    m_slots.clear();
    const bool bOverwrite = Options().overwrite;
    for (size_t i = 0; i < m_rows.size(); i++)
    {
        const Event& e = m_rows[i].event;
        if (!IsVisible(e))
        {
            continue;
        }
        if (bOverwrite)
        {
            /* one row per kind, at the place of its first event, with its last event */
            auto slot = m_slots.emplace(KindKey(e), m_view.size());
            if (!slot.second)
            {
                m_view[slot.first->second] = m_ullFirstRow + i;
                continue;
            }
        }
        m_view.push_back(m_ullFirstRow + i);
    }
    m_omEvents.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    m_omEvents.SetItemCount((int)m_view.size());
    m_omEvents.Invalidate(FALSE);
    if (Options().showLast && !m_view.empty())
    {
        m_omEvents.EnsureVisible((int)m_view.size() - 1, FALSE);
    }
    UpdateStatus();
}

void CCANopenWnd::OptionsChanged()
{
    if (GetSafeHwnd() == nullptr)
    {
        return;
    }
    RebuildView();
    UpdateNodes();
}

void CCANopenWnd::UpdateNodes()
{
    std::vector<NodeInfo> nodes;
    m_monitor.GetNodes(nodes);
    const CCANopenMonitor::TimeBase base = m_monitor.GetTimeBase();
    const int64_t now = base.valid ? NowTick(base) : 0;

    m_nodeKeys.clear();
    for (const NodeInfo& n : nodes)
    {
        m_nodeKeys.push_back((uint16_t)((n.channel << 8) | n.node));
    }

    m_omNodes.SetRedraw(FALSE);
    while (m_omNodes.GetItemCount() > (int)nodes.size())
    {
        m_omNodes.DeleteItem(m_omNodes.GetItemCount() - 1);
    }
    for (int i = 0; i < (int)nodes.size(); i++)
    {
        const NodeInfo& n = nodes[i];
        CString omText;
        omText.Format("%u", n.channel);
        if (i >= m_omNodes.GetItemCount())
        {
            m_omNodes.InsertItem(i, omText);
        }
        else
        {
            m_omNodes.SetItemText(i, ND_CHANNEL, omText);
        }
        omText.Format("%u", n.node);
        m_omNodes.SetItemText(i, ND_NODE, omText);

        omText = Decoder::stateName(n.state);
        if (n.guarded)
        {
            omText += " (guarded)";
        }
        if (base.valid && n.heartbeatMs > 0 && n.lastHeartbeat != 0)
        {
            const double silentMs = (double)CCANopenMonitor::TicksBetween(now, n.lastHeartbeat) * 1000.0 / CCANopenMonitor::TICKS_PER_SECOND;
            if (silentMs > max(3 * n.heartbeatMs, n.heartbeatMs + 500))
            {
                CString omSilent;
                omSilent.Format(", no heartbeat for %.1f s", silentMs / 1000.0);
                omText += omSilent;
            }
        }
        m_omNodes.SetItemText(i, ND_STATE, omText);

        if (n.heartbeatMs > 0)
        {
            omText.Format("%.0f ms", n.heartbeatMs);
        }
        else
        {
            omText = "-";
        }
        m_omNodes.SetItemText(i, ND_HEARTBEAT, omText);
        CString omSeen("-");
        if (n.lastSeen != 0)
        {
            const int64_t absolute = base.valid ? CCANopenMonitor::TicksSince(n.lastSeen, base.connectTick) : CCANopenMonitor::TicksSince(n.lastSeen, m_nFirstTick);
            const bool bSystem = base.valid && Options().timeMode != CANopenDisplayOptions::AbsoluteTime;
            omSeen = FormatTime(bSystem ? absolute + base.connectTime : absolute);
        }
        m_omNodes.SetItemText(i, ND_LASTSEEN, omSeen);
        omText.Format("%u", n.emcyCount);
        m_omNodes.SetItemText(i, ND_EMCY, omText);
        if (n.emcyCount > 0)
        {
            omText.Format("0x%04X %s", n.lastEmcyCode, Decoder::emcyText(n.lastEmcyCode));
        }
        else
        {
            omText = "-";
        }
        m_omNodes.SetItemText(i, ND_LASTEMCY, omText);
    }
    m_omNodes.SetRedraw(TRUE);
    m_omNodes.Invalidate(FALSE);
}

void CCANopenWnd::UpdateStatus()
{
    CString omText = m_omStatus;
    if (omText.IsEmpty())
    {
        omText.Format("%d node%s, %u event%s", m_omNodes.GetItemCount(), m_omNodes.GetItemCount() == 1 ? "" : "s",
                      (unsigned)m_rows.size(), m_rows.size() == 1 ? "" : "s");
        if (Options().overwrite)
        {
            CString omShown;
            omShown.Format(" (%u kinds", (unsigned)m_view.size());
            omText += omShown;
            if (m_nNodeFilter >= 0)
            {
                omShown.Format(", node %d only", m_nNodeFilter & 0xFF);
                omText += omShown;
            }
            omText += ")";
        }
        else if (m_unTypes != ALL_TYPES || m_nNodeFilter >= 0)
        {
            CString omShown;
            omShown.Format(" (%u shown", (unsigned)m_view.size());
            omText += omShown;
            if (m_nNodeFilter >= 0)
            {
                omShown.Format(", node %d only", m_nNodeFilter & 0xFF);
                omText += omShown;
            }
            omText += ")";
        }
        if (Options().pause)
        {
            omText += ", paused";
        }
        if (m_unLost > 0)
        {
            CString omLost;
            omLost.Format(", %u events lost", (unsigned)m_unLost);
            omText += omLost;
        }
    }
    CString omOld;
    GetDlgItemText(IDC_CO_STATUS, omOld);
    if (omOld != omText)
    {
        SetDlgItemText(IDC_CO_STATUS, omText);
    }
}

void CCANopenWnd::Clear()
{
    m_monitor.Clear();
    m_rows.clear();
    m_view.clear();
    m_slots.clear();
    m_kinds.clear();
    m_ullFirstRow = 0;
    m_unLost = 0;
    m_bFirstTick = true;
    m_omEvents.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    m_omEvents.SetItemCount(0);
    m_omNodes.DeleteAllItems();
    UpdateStatus();
}

CString CCANopenWnd::CellText(size_t item, int nColumn) const
{
    const Event& e = ViewRow(item).event;
    const bool bResult = e.type == EventType::SdoResult;   // no frame of its own
    CString omText;
    switch (nColumn)
    {
        case EV_TIME:
            return RowTime(item);
        case EV_CHANNEL:
            omText.Format("%u", e.frame.channel);
            break;
        case EV_DIR:
            if (!bResult)
            {
                omText = e.frame.tx ? "Tx" : "Rx";
            }
            break;
        case EV_COBID:
            if (!bResult)
            {
                omText.Format(e.frame.ext ? "0x%08X" : "0x%03X", e.frame.id);
            }
            break;
        case EV_TYPE:
            omText = Decoder::typeName(e.type);
            break;
        case EV_NODE:
            if (e.node != 0)
            {
                omText.Format("%u", e.node);
            }
            break;
        case EV_TEXT:
            omText = e.text.c_str();
            break;
        case EV_DATA:
            if (!bResult)
            {
                omText = e.frame.rtr ? CString("RTR") : CString(Decoder::hexBytes(e.frame.data, e.frame.len).c_str());
            }
            break;
    }
    return omText;
}

void CCANopenWnd::OnEventsGetDispInfo(NMHDR* pNMHDR, LRESULT* pResult)
{
    NMLVDISPINFO* pInfo = reinterpret_cast<NMLVDISPINFO*>(pNMHDR);
    LVITEM& item = pInfo->item;
    if ((item.mask & LVIF_TEXT) && item.iItem >= 0 && item.iItem < (int)m_view.size())
    {
        CString omText = CellText((size_t)item.iItem, item.iSubItem);
        lstrcpyn(item.pszText, omText, item.cchTextMax);
    }
    *pResult = 0;
}

void CCANopenWnd::OnEventsCustomDraw(NMHDR* pNMHDR, LRESULT* pResult)
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
        if (index < m_view.size())
        {
            const Event& e = ViewRow(index).event;
            const bool bEmcyError = e.type == EventType::Emcy && e.frame.len >= 2 &&
                                    (e.frame.data[0] != 0 || e.frame.data[1] != 0);
            if (bEmcyError || e.type == EventType::SdoAbort)
            {
                pDraw->clrText = RGB(192, 0, 0);
            }
            else if (e.type == EventType::SdoResult)
            {
                pDraw->clrText = RGB(0, 70, 170);
            }
            else if (e.type == EventType::Other)
            {
                pDraw->clrText = RGB(128, 128, 128);
            }
        }
    }
}

void CCANopenWnd::OnEventsKeyDown(NMHDR* pNMHDR, LRESULT* pResult)
{
    NMLVKEYDOWN* pKey = reinterpret_cast<NMLVKEYDOWN*>(pNMHDR);
    if (GetKeyState(VK_CONTROL) < 0)
    {
        if (pKey->wVKey == 'C')
        {
            CopySelection();
        }
        else if (pKey->wVKey == 'A')
        {
            m_omEvents.SetItemState(-1, LVIS_SELECTED, LVIS_SELECTED);
        }
    }
    *pResult = 0;
}

void CCANopenWnd::CopySelection()
{
    /* the columns displayed */
    std::vector<int> columns;
    for (int i = 0; i < EV_COUNT; i++)
    {
        if (!m_asEventColumns[i].hidden)
        {
            columns.push_back(i);
        }
    }
    CString omText;
    for (size_t i = 0; i < columns.size(); i++)
    {
        omText += sg_asEventColumns[columns[i]].title;
        omText += i + 1 < columns.size() ? "\t" : "\r\n";
    }
    int nItem = -1;
    int nCount = 0;
    while ((nItem = m_omEvents.GetNextItem(nItem, LVNI_SELECTED)) != -1 && nItem < (int)m_view.size())
    {
        for (size_t i = 0; i < columns.size(); i++)
        {
            omText += CellText((size_t)nItem, columns[i]);
            omText += i + 1 < columns.size() ? "\t" : "\r\n";
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

void CCANopenWnd::OnHeaderBeginTrack(NMHDR* pNMHDR, LRESULT* pResult)
{
    /* a hidden column stays hidden: the context menu shows it again */
    const NMHEADER* pHeader = reinterpret_cast<const NMHEADER*>(pNMHDR);
    *pResult = FALSE;
    if (pHeader->iItem < 0)
    {
        return;
    }
    if (pNMHDR->hwndFrom == m_omEvents.GetHeaderCtrl()->GetSafeHwnd() && pHeader->iItem < EV_COUNT)
    {
        *pResult = m_asEventColumns[pHeader->iItem].hidden ? TRUE : FALSE;
    }
    else if (pNMHDR->hwndFrom == m_omNodes.GetHeaderCtrl()->GetSafeHwnd() && pHeader->iItem < ND_COUNT)
    {
        *pResult = m_asNodeColumns[pHeader->iItem].hidden ? TRUE : FALSE;
    }
}

void CCANopenWnd::AddColumnItems(CMenu& menu, bool bEvents)
{
    const Column* columns = bEvents ? m_asEventColumns : m_asNodeColumns;
    const int count = bEvents ? EV_COUNT : ND_COUNT;
    int nVisible = 0;
    for (int i = 0; i < count; i++)
    {
        nVisible += columns[i].hidden ? 0 : 1;
    }
    for (int i = 0; i < count; i++)
    {
        UINT uFlags = MF_STRING | (columns[i].hidden ? MF_UNCHECKED : MF_CHECKED);
        /* the last column displayed cannot be hidden */
        if (!columns[i].hidden && nVisible <= 1)
        {
            uFlags |= MF_GRAYED;
        }
        menu.AppendMenu(uFlags, (bEvents ? CMD_EVENT_COLUMN : CMD_NODE_COLUMN) + i,
                        bEvents ? sg_asEventColumns[i].title : sg_asNodeColumns[i].title);
    }
}

void CCANopenWnd::ToggleColumn(bool bEvents, int column)
{
    CListCtrl& list = bEvents ? m_omEvents : m_omNodes;
    Column& c = bEvents ? m_asEventColumns[column] : m_asNodeColumns[column];
    if (c.hidden)
    {
        const int defaultWidth = bEvents ? sg_asEventColumns[column].width : sg_asNodeColumns[column].width;
        list.SetColumnWidth(column, c.width > 0 ? c.width : DialogUnitsX(defaultWidth));
        c.hidden = false;
    }
    else
    {
        c.width = list.GetColumnWidth(column);
        list.SetColumnWidth(column, 0);
        c.hidden = true;
    }
}

void CCANopenWnd::AddTypeItems(CMenu& menu)
{
    for (int i = 0; i < TYPE_GROUPS; i++)
    {
        const bool bShown = (m_unTypes & sg_asTypeGroups[i].mask) != 0;
        menu.AppendMenu(MF_STRING | (bShown ? MF_CHECKED : MF_UNCHECKED), CMD_TYPE + i, sg_asTypeGroups[i].name);
    }
    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING | (m_unTypes == ALL_TYPES ? MF_GRAYED : 0), CMD_ALL_TYPES, "Show all types");
}

void CCANopenWnd::OnContextMenu(CWnd* pWnd, CPoint point)
{
    const HWND hWnd = pWnd->GetSafeHwnd();
    const HWND hEventsHeader = m_omEvents.GetHeaderCtrl()->GetSafeHwnd();
    const HWND hNodesHeader = m_omNodes.GetHeaderCtrl()->GetSafeHwnd();
    const bool bEvents = hWnd == m_omEvents.GetSafeHwnd() || hWnd == hEventsHeader;
    const bool bNodes = hWnd == m_omNodes.GetSafeHwnd() || hWnd == hNodesHeader;
    if (!bEvents && !bNodes)
    {
        CDialog::OnContextMenu(pWnd, point);
        return;
    }

    /* from the keyboard (Shift+F10, menu key): near the top left corner of the list */
    const bool bKeyboard = point.x == -1 && point.y == -1;
    if (bKeyboard)
    {
        CRect omRect;
        (bEvents ? m_omEvents : m_omNodes).GetWindowRect(&omRect);
        point = omRect.TopLeft() + CPoint(DialogUnitsX(10), DialogUnitsY(20));
    }
    CRect omHeader;
    ::GetWindowRect(bEvents ? hEventsHeader : hNodesHeader, &omHeader);
    const bool bOnHeader = !bKeyboard && omHeader.PtInRect(point);

    CMenu menu;
    CMenu columns;
    CMenu types;
    if (!menu.CreatePopupMenu() || !columns.CreatePopupMenu() || !types.CreatePopupMenu())
    {
        return;
    }
    AddColumnItems(columns, bEvents);
    AddTypeItems(types);
    int nNodeKey = -1;
    if (bEvents && bOnHeader)
    {
        /* like the message window: the columns, then the filter */
        AddColumnItems(menu, true);
        menu.AppendMenu(MF_SEPARATOR);
        menu.AppendMenu(MF_POPUP, (UINT_PTR)types.Detach(), "Event types");
    }
    else if (bEvents)
    {
        const bool bSelection = m_omEvents.GetSelectedCount() > 0;
        menu.AppendMenu(MF_STRING | (bSelection ? 0 : MF_GRAYED), CMD_COPY, "Copy\tCtrl+C");
        menu.AppendMenu(MF_STRING | (m_view.empty() ? MF_GRAYED : 0), CMD_SELECT_ALL, "Select all\tCtrl+A");
        menu.AppendMenu(MF_SEPARATOR);
        menu.AppendMenu(MF_POPUP, (UINT_PTR)types.Detach(), "Event types");
        menu.AppendMenu(MF_POPUP, (UINT_PTR)columns.Detach(), "Columns");
        if (m_nNodeFilter >= 0)
        {
            menu.AppendMenu(MF_STRING, CMD_ALL_NODES, "Show the events of all nodes");
        }
    }
    else if (bOnHeader)
    {
        AddColumnItems(menu, false);
    }
    else
    {
        /* the node under the mouse, or the selected one from the keyboard */
        int nItem = -1;
        if (bKeyboard)
        {
            nItem = m_omNodes.GetNextItem(-1, LVNI_SELECTED);
        }
        else
        {
            CPoint omClient = point;
            m_omNodes.ScreenToClient(&omClient);
            nItem = m_omNodes.HitTest(omClient);
        }
        if (nItem >= 0 && nItem < (int)m_nodeKeys.size())
        {
            nNodeKey = m_nodeKeys[nItem];
            m_omNodes.SetItemState(nItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            CString omText;
            omText.Format("Show only the events of node %d", nNodeKey & 0xFF);
            menu.AppendMenu(MF_STRING | (nNodeKey == m_nNodeFilter ? MF_CHECKED : 0), CMD_ONLY_NODE, omText);
        }
        menu.AppendMenu(MF_STRING | (m_nNodeFilter < 0 ? MF_GRAYED : 0), CMD_ALL_NODES,
                        "Show the events of all nodes");
        menu.AppendMenu(MF_SEPARATOR);
        menu.AppendMenu(MF_POPUP, (UINT_PTR)columns.Detach(), "Columns");
    }

    const UINT uCmd = (UINT)menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
                                                point.x, point.y, this);
    if (uCmd >= CMD_EVENT_COLUMN && uCmd < CMD_EVENT_COLUMN + EV_COUNT)
    {
        ToggleColumn(true, (int)(uCmd - CMD_EVENT_COLUMN));
    }
    else if (uCmd >= CMD_NODE_COLUMN && uCmd < CMD_NODE_COLUMN + ND_COUNT)
    {
        ToggleColumn(false, (int)(uCmd - CMD_NODE_COLUMN));
    }
    else if (uCmd >= CMD_TYPE && uCmd < CMD_TYPE + TYPE_GROUPS)
    {
        m_unTypes ^= sg_asTypeGroups[uCmd - CMD_TYPE].mask;
        RebuildView();
    }
    else if (uCmd == CMD_ALL_TYPES)
    {
        m_unTypes = ALL_TYPES;
        RebuildView();
    }
    else if (uCmd == CMD_COPY)
    {
        CopySelection();
    }
    else if (uCmd == CMD_SELECT_ALL)
    {
        m_omEvents.SetItemState(-1, LVIS_SELECTED, LVIS_SELECTED);
    }
    else if (uCmd == CMD_ONLY_NODE)
    {
        m_nNodeFilter = nNodeKey == m_nNodeFilter ? -1 : nNodeKey;
        RebuildView();
    }
    else if (uCmd == CMD_ALL_NODES)
    {
        m_nNodeFilter = -1;
        RebuildView();
    }
}
