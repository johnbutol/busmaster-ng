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
 * \file      CANopenFrame.h
 * \brief     MDI child frame of the CANopen window, inside the main window of BUSMASTER
 */

#pragma once

#include "CANopenWnd.h"

class CCANopenFrame : public CMDIChildWnd
{
public:
    explicit CCANopenFrame(CCANopenMonitor& monitor);
    virtual ~CCANopenFrame();

    /** Creates the frame, hidden, in the MDI client area of the main window */
    BOOL CreateFrame(CMDIFrameWnd* pMainFrame);

    /** Shows the frame and makes it the active MDI child */
    void ShowFrame();

    /** Hides the frame and activates the next MDI child: the analysis goes on */
    void HideFrame();

    CCANopenWnd& Content() { return m_omContent; }

    /** The frame that exists, nullptr if none. Its window deletes the object when it is destroyed. */
    static CCANopenFrame* Instance() { return s_pInstance; }

protected:
    virtual BOOL OnCreateClient(LPCREATESTRUCT lpcs, CCreateContext* pContext);

    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnClose();
    afx_msg void OnSetFocus(CWnd* pOldWnd);
    afx_msg void OnGetMinMaxInfo(MINMAXINFO* pMinMaxInfo);
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    DECLARE_MESSAGE_MAP()

private:
    CCANopenWnd m_omContent;
    static CCANopenFrame* s_pInstance;
};
