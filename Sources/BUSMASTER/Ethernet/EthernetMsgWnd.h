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
 * \file      EthernetMsgWnd.h
 * \brief     Message window of the Ethernet module: the frames captured on the adapter
 */

#pragma once

#include "EthernetFrame.h"
#include "EthernetModule.h"
#include "Ethernet_Resource.h"

class CEthMsgWnd : public CEthPane
{
public:
    CEthMsgWnd();

    virtual CSize DefaultSize() const { return CSize(600, 300); }
    virtual CSize MinSize() const { return CSize(300, 120); }
    virtual CWnd* FocusControl() { return &m_omList; }

    /** To call after the options of the ribbon changed */
    void OptionsChanged();

    /** Forgets the frames */
    void Clear();

    /** Takes the frames captured so far (also called by the timer) */
    void TakeFrames();

    /** Writes the frames in a pcap file chosen by the user; false if cancelled or failed */
    bool SavePcap();

    bool HasFrames() const { return !m_rows.empty(); }

protected:
    virtual BOOL OnInitDialog();

    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnDestroy();
    afx_msg void OnGetDispInfo(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnCustomDraw(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnKeyDown(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnHeaderBeginTrack(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
    DECLARE_MESSAGE_MAP()

private:
    struct Row
    {
        eth::CapturedFrame frame;
        bool tx = false;
        int vlanId = -1;
        size_t payloadOffset = 0;
        uint64_t kindKey = 0;
        int64_t previousUs = -1;    ///< time of the previous frame of the same kind, for overwrite mode
        std::string protocol;
        std::string source;
        std::string destination;
        std::string info;
    };

    /** Column that can be hidden: its width is then 0 and the last width is kept */
    struct Column
    {
        bool hidden = false;
        int width = 0;
    };

    void Layout(int cx, int cy);
    void RebuildView();
    void UpdateStatus();
    const Row& ViewRow(size_t item) const { return m_rows[(size_t)(m_view[item] - m_ullFirstRow)]; }
    CString RowTime(size_t item) const;
    CString CellText(size_t item, int nColumn) const;
    void CopySelection(bool bHex);
    void ToggleColumn(int column);

    CListCtrl m_omList;
    std::deque<Row> m_rows;             ///< every frame taken, displayed or not
    uint64_t m_ullFirstRow;             ///< number of the frame in m_rows[0], counted since the last Clear
    std::deque<uint64_t> m_view;        ///< numbers of the frames displayed, in order
    std::map<uint64_t, size_t> m_slots; ///< overwrite mode: row of the view of each kind of frame
    std::map<uint64_t, int64_t> m_lastOfKind;   ///< time of the last frame of each kind
    Column m_asColumns[11];
    size_t m_unDropped;                 ///< frames removed to keep the memory bounded
    size_t m_unPaused;                  ///< frames not taken while paused
    bool m_bLayoutReady;
};
