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
 * \file      CANopenFrame.cpp
 * \brief     MDI child frame of the CANopen window, inside the main window of BUSMASTER
 */

#include "CANopen_stdafx.h"
#include "CANopenFrame.h"

namespace
{
/* size of the window, in dialog units of its content */
const int DEFAULT_WIDTH = 560;
const int DEFAULT_HEIGHT = 320;
const int MIN_WIDTH = 400;
const int MIN_HEIGHT = 150;
}

CCANopenFrame* CCANopenFrame::s_pInstance = nullptr;

BEGIN_MESSAGE_MAP(CCANopenFrame, CMDIChildWnd)
    ON_WM_SIZE()
    ON_WM_CLOSE()
    ON_WM_SETFOCUS()
    ON_WM_GETMINMAXINFO()
    ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

CCANopenFrame::CCANopenFrame(CCANopenMonitor& monitor)
    : m_omContent(monitor)
{
    s_pInstance = this;
}

CCANopenFrame::~CCANopenFrame()
{
    if (s_pInstance == this)
    {
        s_pInstance = nullptr;
    }
}

BOOL CCANopenFrame::CreateFrame(CMDIFrameWnd* pMainFrame)
{
    const LPCTSTR pszClass = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS,
                                                 ::LoadCursor(nullptr, IDC_ARROW),
                                                 reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1),
                                                 AfxGetApp()->LoadIcon(IDI_CANOPEN));
    /* hidden until ShowFrame, so it does not appear at the default size first */
    if (!Create(pszClass, _T("CANopen"), WS_CHILD | WS_OVERLAPPEDWINDOW, rectDefault, pMainFrame))
    {
        return FALSE;
    }

    /* default size: the one of the dialog template, within the MDI client area */
    CRect omRect(0, 0, m_omContent.DialogUnitsX(DEFAULT_WIDTH), m_omContent.DialogUnitsY(DEFAULT_HEIGHT));
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

void CCANopenFrame::ShowFrame()
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

BOOL CCANopenFrame::OnCreateClient(LPCREATESTRUCT /*lpcs*/, CCreateContext* /*pContext*/)
{
    if (!m_omContent.CreateWnd(this))
    {
        return FALSE;
    }
    m_omContent.SetDlgCtrlID(AFX_IDW_PANE_FIRST);
    m_omContent.ShowWindow(SW_SHOW);
    return TRUE;
}

void CCANopenFrame::OnSize(UINT nType, int cx, int cy)
{
    CMDIChildWnd::OnSize(nType, cx, cy);
    if (m_omContent.GetSafeHwnd() != nullptr && nType != SIZE_MINIMIZED)
    {
        CRect omClient;
        GetClientRect(&omClient);
        m_omContent.MoveWindow(&omClient);
    }
}

void CCANopenFrame::HideFrame()
{
    GetMDIFrame()->MDINext();
    ShowWindow(SW_HIDE);
}

void CCANopenFrame::OnClose()
{
    /* closing only hides the window: the analysis goes on and the events are kept */
    HideFrame();
}

void CCANopenFrame::OnSetFocus(CWnd* pOldWnd)
{
    CMDIChildWnd::OnSetFocus(pOldWnd);
    if (m_omContent.GetSafeHwnd() != nullptr)
    {
        CWnd* pEvents = m_omContent.GetDlgItem(IDC_CO_EVENTS);
        if (pEvents != nullptr)
        {
            pEvents->SetFocus();
        }
    }
}

void CCANopenFrame::OnGetMinMaxInfo(MINMAXINFO* pMinMaxInfo)
{
    CMDIChildWnd::OnGetMinMaxInfo(pMinMaxInfo);
    if (m_omContent.GetSafeHwnd() != nullptr)
    {
        CRect omRect(0, 0, m_omContent.DialogUnitsX(MIN_WIDTH), m_omContent.DialogUnitsY(MIN_HEIGHT));
        CalcWindowRect(&omRect);
        pMinMaxInfo->ptMinTrackSize.x = omRect.Width();
        pMinMaxInfo->ptMinTrackSize.y = omRect.Height();
    }
}

BOOL CCANopenFrame::OnEraseBkgnd(CDC* /*pDC*/)
{
    /* the content covers the whole client area */
    return TRUE;
}
