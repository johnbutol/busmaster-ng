/*
 * BUSMASTER-NG Instruments: the DLL of the plugin.
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Lesser General
 * Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 */
#include "stdafx.h"
#include "Plugin.h"
#include "Render.h"

namespace
{
class InstrumentsApp : public CWinApp
{
public:
    BOOL InitInstance() override
    {
        CWinApp::InitInstance();
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&mGdiplus, &input, nullptr);
        return TRUE;
    }

    int ExitInstance() override
    {
        Instruments::ClearPictureCache();
        if (mGdiplus != 0)
        {
            Gdiplus::GdiplusShutdown(mGdiplus);
            mGdiplus = 0;
        }
        return CWinApp::ExitInstance();
    }

private:
    ULONG_PTR mGdiplus = 0;
};

InstrumentsApp theApp;
}

extern "C" __declspec(dllexport) int GetPlugInInterface(IBusmasterPlugin** plugin)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    if (plugin == nullptr)
    {
        return S_FALSE;
    }
    *plugin = new Instruments::InstrumentsPlugin();
    return S_OK;
}

extern "C" __declspec(dllexport) void FreePlugInInterface(IBusmasterPlugin* plugin)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    delete plugin;
}
