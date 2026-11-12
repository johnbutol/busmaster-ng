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
 * \file      EthernetFrame.cpp
 * \brief     MDI child frame of the Ethernet windows, inside the main window of BUSMASTER
 */

#include "Ethernet_stdafx.h"
#include "EthernetFrame.h"
#include "Ethernet_Resource.h"

int CEthPane::DialogUnitsX(int du)
{
    CRect r(0, 0, du, 0);
    MapDialogRect(&r);
    return r.Width();
}

int CEthPane::DialogUnitsY(int du)
{
    CRect r(0, 0, 0, du);
    MapDialogRect(&r);
    return r.Height();
}

BEGIN_MESSAGE_MAP(CEthChildFrame, CMDIChildWnd)
    ON_WM_SIZE()
    ON_WM_CLOSE()
    ON_WM_SETFOCUS()
    ON_WM_GETMINMAXINFO()
    ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

CEthChildFrame::CEthChildFrame(CEthPane* pPane, LPCTSTR pszTitle, CEthChildFrame** ppInstance)
    : m_pPane(pPane), m_omTitle(pszTitle), m_ppInstance(ppInstance)
{
    *m_ppInstance = this;
}

CEthChildFrame::~CEthChildFrame()
{
    if (*m_ppInstance == this)
    {
        *m_ppInstance = nullptr;
    }
}

BOOL CEthChildFrame::CreateFrame(CMDIFrameWnd* pMainFrame)
{
    const LPCTSTR pszClass = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS,
                                                 ::LoadCursor(nullptr, IDC_ARROW),
                                                 reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1),
                                                 AfxGetApp()->LoadIcon(IDI_ETHERNET));
    /* hidden until ShowFrame, so it does not appear at the default size first */
    if (!Create(pszClass, m_omTitle, WS_CHILD | WS_OVERLAPPEDWINDOW, rectDefault, pMainFrame))
    {
        return FALSE;
    }

    /* default size within the MDI client area */
    const CSize size = m_pPane->DefaultSize();
    CRect omRect(0, 0, m_pPane->DialogUnitsX(size.cx), m_pPane->DialogUnitsY(size.cy));
    CalcWindowRect(&omRect);
    CRect omClient;
    ::GetClientRect(pMainFrame->m_hWndMDIClient, &omClient);
    CRect omWindow;
    GetWindowRect(&omWindow);
    ::ScreenToClient(pMainFrame->m_hWndMDIClient, &omWindow.TopLeft());
    const int x = max(min(omWindow.left, omClient.right - omRect.Width()), 0);
    const int y = max(min(omWindow.top, omClient.bottom - omRect.Height()), 0);
    SetWindowPos(nullptr, x, y, min(omRect.Width(), omClient.Width()), min(omRect.Height(), omClient.Height()),
                 SWP_NOZORDER | SWP_NOACTIVATE);
    return TRUE;
}

void CEthChildFrame::ShowFrame()
{
    if (IsIconic())
    {
        ShowWindow(SW_RESTORE);
    }
    else if (!IsWindowVisible())
    {
        ShowWindow(SW_SHOW);
    }
    MDIActivate();
}

void CEthChildFrame::HideFrame()
{
    GetMDIFrame()->MDINext();
    ShowWindow(SW_HIDE);
}

BOOL CEthChildFrame::OnCreateClient(LPCREATESTRUCT /*lpcs*/, CCreateContext* /*pContext*/)
{
    if (!m_pPane->CreatePane(this))
    {
        return FALSE;
    }
    m_pPane->SetDlgCtrlID(AFX_IDW_PANE_FIRST);
    m_pPane->ShowWindow(SW_SHOW);
    return TRUE;
}

void CEthChildFrame::OnSize(UINT nType, int cx, int cy)
{
    CMDIChildWnd::OnSize(nType, cx, cy);
    if (m_pPane->GetSafeHwnd() != nullptr && nType != SIZE_MINIMIZED)
    {
        CRect omClient;
        GetClientRect(&omClient);
        m_pPane->MoveWindow(&omClient);
    }
}

void CEthChildFrame::OnClose()
{
    /* closing only hides the window: the capture and the transmission go on */
    HideFrame();
}

void CEthChildFrame::OnSetFocus(CWnd* pOldWnd)
{
    CMDIChildWnd::OnSetFocus(pOldWnd);
    if (m_pPane->GetSafeHwnd() != nullptr)
    {
        CWnd* pFocus = m_pPane->FocusControl();
        (pFocus != nullptr ? pFocus : m_pPane.get())->SetFocus();
    }
}

void CEthChildFrame::OnGetMinMaxInfo(MINMAXINFO* pMinMaxInfo)
{
    CMDIChildWnd::OnGetMinMaxInfo(pMinMaxInfo);
    if (m_pPane->GetSafeHwnd() != nullptr)
    {
        const CSize size = m_pPane->MinSize();
        CRect omRect(0, 0, m_pPane->DialogUnitsX(size.cx), m_pPane->DialogUnitsY(size.cy));
        CalcWindowRect(&omRect);
        pMinMaxInfo->ptMinTrackSize.x = omRect.Width();
        pMinMaxInfo->ptMinTrackSize.y = omRect.Height();
    }
}

BOOL CEthChildFrame::OnEraseBkgnd(CDC* /*pDC*/)
{
    /* the pane covers the whole client area */
    return TRUE;
}
