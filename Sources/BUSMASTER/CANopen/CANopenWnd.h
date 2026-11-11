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
 * \file      CANopenWnd.h
 * \brief     CANopen window: nodes and decoded events
 */

#pragma once

#include "CANopenMonitor.h"
#include "CANopen_Resource.h"

/** Options of the CANopen drop-down of the ribbon, as those of the message window */
struct CANopenDisplayOptions
{
    enum TimeMode { SystemTime, AbsoluteTime, RelativeTime };
    TimeMode timeMode  = SystemTime;
    bool     overwrite = false;     ///< one row per kind of frame (channel, COB-ID, direction, type), updated in place
    bool     showLast  = true;      ///< scroll to the newest event
    bool     pause     = false;     ///< events are not taken while paused
};

class CCANopenWnd : public CDialog
{
public:
    explicit CCANopenWnd(CCANopenMonitor& monitor);

    /** Options shared by the window and the ribbon: they exist before the window */
    static CANopenDisplayOptions& Options();

    /** To call after the options changed */
    void OptionsChanged();

    /** Forgets the events and the nodes */
    void Clear();

    /** Creates the window as the client area of its MDI frame */
    BOOL CreateWnd(CWnd* pParent);

    /** Text of the status line, for example why the analysis could not start */
    void SetStatus(const CString& omStatus);

    int DialogUnitsX(int du);
    int DialogUnitsY(int du);

protected:
    virtual BOOL OnInitDialog();
    virtual void OnOK();
    virtual void OnCancel();

    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnDestroy();
    afx_msg void OnEventsGetDispInfo(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnEventsCustomDraw(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnEventsKeyDown(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnHeaderBeginTrack(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
    DECLARE_MESSAGE_MAP()

private:
    struct Row
    {
        canopen::Event event;
        int64_t  systemTime = -1;   ///< 100 us since midnight, as the message window; -1 without time base
        int64_t  absoluteTime = 0;  ///< 100 us since the connection (since the first event without time base)
        int64_t  previousTick = -1; ///< time stamp of the previous event of the same kind, for overwrite mode
        uint32_t count = 1;         ///< number of events of this kind so far, for overwrite mode
    };

    /** Column that can be hidden: its width is then 0 and the last width is kept */
    struct Column
    {
        bool hidden = false;
        int width = 0;
    };

    void Layout(int cx, int cy);
    bool IsVisible(const canopen::Event& e) const;
    void RebuildView();
    const Row& ViewRow(size_t item) const { return m_rows[(size_t)(m_view[item] - m_ullFirstRow)]; }
    void AddColumnItems(CMenu& menu, bool bEvents);
    void ToggleColumn(bool bEvents, int column);
    void AddTypeItems(CMenu& menu);
    void TakeEvents();
    void UpdateNodes();
    void UpdateStatus();
    static uint64_t KindKey(const canopen::Event& e);
    static CString FormatTime(int64_t time);
    CString RowTime(size_t item) const;
    int64_t NowTick(const CCANopenMonitor::TimeBase& base) const;
    CString CellText(size_t item, int nColumn) const;
    void CopySelection();

    CCANopenMonitor& m_monitor;
    CListCtrl m_omNodes;
    CListCtrl m_omEvents;
    std::deque<Row> m_rows;             ///< every event taken, displayed or not
    uint64_t m_ullFirstRow;             ///< number of the event in m_rows[0], counted since the last Clear
    std::deque<uint64_t> m_view;        ///< numbers of the events displayed, in order
    std::map<uint64_t, size_t> m_slots; ///< overwrite mode: row of the view of each kind of event
    std::map<uint64_t, std::pair<int64_t, uint32_t>> m_kinds; ///< last time stamp and count of each kind
    uint32_t m_unTypes;                 ///< types of event displayed, one bit per canopen::EventType
    int m_nNodeFilter;                  ///< (channel << 8) | node of the only node displayed, -1 for all
    std::vector<uint16_t> m_nodeKeys;   ///< (channel << 8) | node of each row of the node list
    Column m_asEventColumns[8];
    Column m_asNodeColumns[7];
    CString m_omStatus;
    size_t m_unLost;
    int64_t m_nFirstTick;       ///< for relative times when the driver gives no time base
    bool m_bFirstTick;
    bool m_bLayoutReady;
    UINT m_unTick;
};
