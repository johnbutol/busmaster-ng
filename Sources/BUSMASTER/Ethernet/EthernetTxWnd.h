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
 * \file      EthernetTxWnd.h
 * \brief     Transmit window of the Ethernet module: frames sent on request or cyclically
 */

#pragma once

#include "EthernetFrame.h"
#include "EthernetModule.h"
#include "Ethernet_Resource.h"

class CEthTxWnd : public CEthPane
{
public:
    CEthTxWnd();

    virtual CSize DefaultSize() const { return CSize(520, 268); }
    virtual CSize MinSize() const { return CSize(520, 200); }
    virtual CWnd* FocusControl() { return &m_omList; }

protected:
    virtual BOOL OnInitDialog();

    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnItemChanged(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnKindChanged();
    afx_msg void OnAdd();
    afx_msg void OnUpdate();
    afx_msg void OnDelete();
    afx_msg void OnSend();
    afx_msg void OnDblClk(NMHDR* pNMHDR, LRESULT* pResult);
    DECLARE_MESSAGE_MAP()

private:
    /** Position of a control in the template, to move it when the window is resized */
    struct Anchor
    {
        UINT id;
        CRect rect;
    };

    void FillList();
    void FillRow(int item);
    void Load(const EthTxEntry& entry);
    bool Read(EthTxEntry& entry);
    void EnableFields();
    int Selected() const;
    void SetStatus(const CString& omText);
    bool Fail(UINT idControl, const CString& omText);

    CListCtrl m_omList;
    CComboBox m_omKind;
    std::vector<Anchor> m_anchors;
    CSize m_initialSize;
    bool m_bFilling;
};
