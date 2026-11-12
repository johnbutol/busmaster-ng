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
 * \file      EthernetFrame.h
 * \brief     MDI child frame of the Ethernet windows, inside the main window of BUSMASTER
 */

#pragma once

#include <memory>

/** Content of an Ethernet window: a dialog template that fills the frame */
class CEthPane : public CDialog
{
public:
    explicit CEthPane(UINT nIDTemplate) : CDialog(nIDTemplate) {}

    BOOL CreatePane(CWnd* pParent) { return Create(m_nIDHelp, pParent); }

    int DialogUnitsX(int du);
    int DialogUnitsY(int du);

    /** Default and minimum sizes of the frame, in dialog units of the content */
    virtual CSize DefaultSize() const = 0;
    virtual CSize MinSize() const = 0;

    /** Control that gets the focus when the frame is activated */
    virtual CWnd* FocusControl() { return nullptr; }

protected:
    virtual void OnOK() {}          // Enter does not close the window
    virtual void OnCancel() {}      // nor Escape: the frame around it does
};

class CEthChildFrame : public CMDIChildWnd
{
public:
    /** The frame owns the pane; *ppInstance is cleared when the window is destroyed */
    CEthChildFrame(CEthPane* pPane, LPCTSTR pszTitle, CEthChildFrame** ppInstance);
    virtual ~CEthChildFrame();

    /** Creates the frame, hidden, in the MDI client area of the main window */
    BOOL CreateFrame(CMDIFrameWnd* pMainFrame);

    /** Shows the frame and makes it the active MDI child */
    void ShowFrame();

    /** Hides the frame and activates the next MDI child; the window goes on working */
    void HideFrame();

    CEthPane& Pane() { return *m_pPane; }

protected:
    virtual BOOL OnCreateClient(LPCREATESTRUCT lpcs, CCreateContext* pContext);

    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnClose();
    afx_msg void OnSetFocus(CWnd* pOldWnd);
    afx_msg void OnGetMinMaxInfo(MINMAXINFO* pMinMaxInfo);
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    DECLARE_MESSAGE_MAP()

private:
    std::unique_ptr<CEthPane> m_pPane;
    CString m_omTitle;
    CEthChildFrame** m_ppInstance;
};
