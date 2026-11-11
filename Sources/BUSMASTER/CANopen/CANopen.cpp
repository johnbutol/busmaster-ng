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
 * \file      CANopen.cpp
 * \brief     CANopen module: DLL and functions called by the application
 */

#include "CANopen_stdafx.h"
#include <afxdllx.h>
#include "CANopenMonitor.h"
#include "CANopenFrame.h"

#define CANOPEN_EXPORT
#include "CANopen_Extern.h"

/* MFC extension DLL: its windows are MDI children of the main window and share its message routing */
static AFX_EXTENSION_MODULE sg_sCANopenDLL = { false, nullptr };

static CCANopenMonitor* sg_pouMonitor = nullptr;

extern "C" int APIENTRY DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID /*lpReserved*/)
{
    if (dwReason == DLL_PROCESS_ATTACH)
    {
        if (!AfxInitExtensionModule(sg_sCANopenDLL, hInstance))
        {
            return 0;
        }
        /* resources of this DLL (the dialog and the icon) are found through the resource chain */
        new CDynLinkLibrary(sg_sCANopenDLL);
    }
    else if (dwReason == DLL_PROCESS_DETACH)
    {
        AfxTermExtensionModule(sg_sCANopenDLL);
    }
    return 1;
}

static void vShutdown()
{
    CCANopenFrame* pFrame = CCANopenFrame::Instance();
    if (pFrame != nullptr && pFrame->GetSafeHwnd() != nullptr)
    {
        pFrame->DestroyWindow();        // the frame deletes itself
    }
    if (sg_pouMonitor != nullptr)
    {
        sg_pouMonitor->Detach();
        delete sg_pouMonitor;
        sg_pouMonitor = nullptr;
    }
}

CANOPEN_API HRESULT CANopen_ShowWindow(HWND hMainFrame)
{
    CMDIFrameWnd* pMainFrame = DYNAMIC_DOWNCAST(CMDIFrameWnd, CWnd::FromHandlePermanent(hMainFrame));
    if (pMainFrame == nullptr)
    {
        return E_INVALIDARG;
    }

    if (sg_pouMonitor == nullptr)
    {
        sg_pouMonitor = new CCANopenMonitor();
    }
    std::string error;
    const bool bAttached = sg_pouMonitor->Attach(error);

    CCANopenFrame* pFrame = CCANopenFrame::Instance();
    if (pFrame == nullptr)
    {
        pFrame = new CCANopenFrame(*sg_pouMonitor);
        if (!pFrame->CreateFrame(pMainFrame))
        {
            /* the object is already deleted if its window was created then destroyed */
            if (CCANopenFrame::Instance() == pFrame)
            {
                delete pFrame;
            }
            return E_FAIL;
        }
    }
    pFrame->Content().SetStatus(bAttached ? CString() : CString(error.c_str()));
    pFrame->ShowFrame();
    return bAttached ? S_OK : S_FALSE;
}

CANOPEN_API HRESULT CANopen_DoInitialization(void)
{
    /* nothing to do while the window was never opened */
    if (sg_pouMonitor == nullptr)
    {
        return S_OK;
    }
    std::string error;
    const bool bAttached = sg_pouMonitor->Attach(error);
    CCANopenFrame* pFrame = CCANopenFrame::Instance();
    if (pFrame != nullptr)
    {
        pFrame->Content().SetStatus(bAttached ? CString() : CString(error.c_str()));
    }
    return bAttached ? S_OK : S_FALSE;
}

CANOPEN_API void CANopen_OnConnect(void)
{
    CCANopenMonitor::OnConnect();
}

CANOPEN_API void CANopen_Shutdown(void)
{
    vShutdown();
}

CANOPEN_API HRESULT CANopen_Command(HWND hMainFrame, int nCommand)
{
    CANopenDisplayOptions& options = CCANopenWnd::Options();
    CCANopenFrame* pFrame = CCANopenFrame::Instance();
    switch (nCommand)
    {
        case CANOPEN_CMD_ACTIVATE:
            if (pFrame != nullptr && pFrame->IsWindowVisible())
            {
                pFrame->HideFrame();
                return S_OK;
            }
            return CANopen_ShowWindow(hMainFrame);
        case CANOPEN_CMD_CLEAR:
            if (pFrame != nullptr)
            {
                pFrame->Content().Clear();
            }
            else if (sg_pouMonitor != nullptr)
            {
                sg_pouMonitor->Clear();
            }
            return S_OK;
        case CANOPEN_CMD_SYSTEM_TIME:
            options.timeMode = CANopenDisplayOptions::SystemTime;
            break;
        case CANOPEN_CMD_ABSOLUTE_TIME:
            options.timeMode = CANopenDisplayOptions::AbsoluteTime;
            break;
        case CANOPEN_CMD_RELATIVE_TIME:
            options.timeMode = CANopenDisplayOptions::RelativeTime;
            break;
        case CANOPEN_CMD_OVERWRITE:
            options.overwrite = !options.overwrite;
            break;
        case CANOPEN_CMD_SHOW_LAST:
            options.showLast = !options.showLast;
            break;
        case CANOPEN_CMD_PAUSE:
            options.pause = !options.pause;
            break;
        default:
            return E_INVALIDARG;
    }
    if (pFrame != nullptr)
    {
        pFrame->Content().OptionsChanged();
    }
    return S_OK;
}

CANOPEN_API int CANopen_GetCommandState(int nCommand)
{
    const CANopenDisplayOptions& options = CCANopenWnd::Options();
    bool bChecked = false;
    switch (nCommand)
    {
        case CANOPEN_CMD_ACTIVATE:
        {
            CCANopenFrame* pFrame = CCANopenFrame::Instance();
            bChecked = pFrame != nullptr && pFrame->IsWindowVisible();
            break;
        }
        case CANOPEN_CMD_SYSTEM_TIME:
            bChecked = options.timeMode == CANopenDisplayOptions::SystemTime;
            break;
        case CANOPEN_CMD_ABSOLUTE_TIME:
            bChecked = options.timeMode == CANopenDisplayOptions::AbsoluteTime;
            break;
        case CANOPEN_CMD_RELATIVE_TIME:
            bChecked = options.timeMode == CANopenDisplayOptions::RelativeTime;
            break;
        case CANOPEN_CMD_OVERWRITE:
            bChecked = options.overwrite;
            break;
        case CANOPEN_CMD_SHOW_LAST:
            bChecked = options.showLast;
            break;
        case CANOPEN_CMD_PAUSE:
            bChecked = options.pause;
            break;
        default:
            break;
    }
    return CANOPEN_STATE_ENABLED | (bChecked ? CANOPEN_STATE_CHECKED : 0);
}
