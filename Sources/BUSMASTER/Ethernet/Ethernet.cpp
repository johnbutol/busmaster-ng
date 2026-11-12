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
 * \file      Ethernet.cpp
 * \brief     Ethernet module: DLL, settings, connection and the commands of the ribbon
 */

#include "Ethernet_stdafx.h"
#include <afxdllx.h>
#include <sstream>
#include "EthernetAdapterDlg.h"
#include "EthernetMsgWnd.h"
#include "EthernetTxWnd.h"

#define ETHERNET_EXPORT
#include "Ethernet_Extern.h"

/* MFC extension DLL: its windows are MDI children of the main window and share its message routing */
static AFX_EXTENSION_MODULE sg_sEthernetDLL = { false, nullptr };

extern "C" int APIENTRY DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID /*lpReserved*/)
{
    if (dwReason == DLL_PROCESS_ATTACH)
    {
        if (!AfxInitExtensionModule(sg_sEthernetDLL, hInstance))
        {
            return 0;
        }
        /* resources of this DLL (dialogs, icon) are found through the resource chain */
        new CDynLinkLibrary(sg_sEthernetDLL);
    }
    else if (dwReason == DLL_PROCESS_DETACH)
    {
        AfxTermExtensionModule(sg_sEthernetDLL);
    }
    return 1;
}

namespace
{
const char SECTION[] = "Ethernet";

eth::CEthernetCapture* sg_pCapture = nullptr;
EthSettings sg_settings;
EthDisplayOptions sg_options;
std::vector<EthTxEntry> sg_txEntries;
bool sg_bSettingsLoaded = false;
bool sg_bConnected = false;
int64_t sg_llConnectTimeUs = 0;
std::string sg_stopReason;
CEthChildFrame* sg_pMsgFrame = nullptr;
CEthChildFrame* sg_pTxFrame = nullptr;

CEthMsgWnd* MsgWnd()
{
    return sg_pMsgFrame != nullptr ? static_cast<CEthMsgWnd*>(&sg_pMsgFrame->Pane()) : nullptr;
}

int64_t NowUtcUs()
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    const ULONGLONG ullTicks = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (int64_t)((ullTicks - 116444736000000000ULL) / 10);
}

std::vector<std::string> Split(const std::string& text, char separator)
{
    std::vector<std::string> fields;
    std::string field;
    std::istringstream stream(text);
    while (std::getline(stream, field, separator))
    {
        fields.push_back(field);
    }
    if (!text.empty() && text.back() == separator)
    {
        fields.push_back("");
    }
    return fields;
}

std::string IpText(const uint8_t* ip)
{
    char text[20];
    sprintf_s(text, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    return text;
}

/* a frame of the transmit window in the registry: fields separated by tabs */
std::string Serialize(const EthTxEntry& e)
{
    const eth::FrameSpec& s = e.spec;
    std::ostringstream o;
    std::string name = e.name;
    for (char& c : name)
    {
        if (c == '\t')
        {
            c = ' ';
        }
    }
    o << name << '\t' << (s.kind == eth::FrameSpec::UdpIPv4 ? "udp" : "raw") << '\t' << eth::MacToString(s.dst)
      << '\t' << (e.srcMacFromAdapter ? "" : eth::MacToString(s.src)) << '\t' << s.vlanId << '\t' << s.vlanPriority
      << '\t' << s.etherType << '\t' << (e.srcIpFromAdapter ? "" : IpText(s.srcIp)) << '\t' << s.srcPort
      << '\t' << IpText(s.dstIp) << '\t' << s.dstPort << '\t' << e.cycleMs << '\t' << (e.cyclic ? 1 : 0)
      << '\t' << eth::HexBytes(s.data.data(), s.data.size(), s.data.size());
    return o.str();
}

bool Deserialize(const std::string& text, EthTxEntry& e)
{
    const std::vector<std::string> f = Split(text, '\t');
    if (f.size() < 14)
    {
        return false;
    }
    eth::FrameSpec& s = e.spec;
    e.name = f[0];
    s.kind = f[1] == "udp" ? eth::FrameSpec::UdpIPv4 : eth::FrameSpec::Raw;
    if (!eth::ParseMac(f[2], s.dst))
    {
        return false;
    }
    e.srcMacFromAdapter = f[3].empty() || !eth::ParseMac(f[3], s.src);
    s.vlanId = atoi(f[4].c_str());
    s.vlanPriority = atoi(f[5].c_str()) & 7;
    s.etherType = (uint16_t)atoi(f[6].c_str());
    e.srcIpFromAdapter = f[7].empty() || !eth::ParseIPv4(f[7], s.srcIp);
    s.srcPort = (uint16_t)atoi(f[8].c_str());
    eth::ParseIPv4(f[9], s.dstIp);
    s.dstPort = (uint16_t)atoi(f[10].c_str());
    e.cycleMs = (unsigned)strtoul(f[11].c_str(), nullptr, 10);
    e.cyclic = f[12] == "1";
    return eth::ParseHexBytes(f[13], s.data);
}

void LoadSettings()
{
    if (sg_bSettingsLoaded)
    {
        return;
    }
    sg_bSettingsLoaded = true;
    CWinApp* pApp = AfxGetApp();
    sg_settings.device = (LPCSTR)pApp->GetProfileString(SECTION, "Device", "");
    sg_settings.adapterName = (LPCSTR)pApp->GetProfileString(SECTION, "AdapterName", "");
    sg_settings.promiscuous = pApp->GetProfileInt(SECTION, "Promiscuous", 1) != 0;
    sg_settings.filter = (LPCSTR)pApp->GetProfileString(SECTION, "Filter", "");
    const int nCount = pApp->GetProfileInt(SECTION, "TxCount", 0);
    for (int i = 0; i < nCount; i++)
    {
        CString omKey;
        omKey.Format("Tx%d", i);
        EthTxEntry entry;
        if (Deserialize((LPCSTR)pApp->GetProfileString(SECTION, omKey, ""), entry))
        {
            sg_txEntries.push_back(entry);
        }
    }
}

/** Frame of the main window, nullptr if hMainFrame is not one */
CMDIFrameWnd* MainFrame(HWND hMainFrame)
{
    return DYNAMIC_DOWNCAST(CMDIFrameWnd, CWnd::FromHandlePermanent(hMainFrame));
}

/** Creates the window if needed, then shows it, or hides it if bToggle and it is shown */
template <typename Pane>
HRESULT ShowWindowOf(CEthChildFrame*& pFrame, LPCTSTR pszTitle, HWND hMainFrame, bool bToggle)
{
    if (pFrame != nullptr && bToggle && pFrame->IsWindowVisible())
    {
        pFrame->HideFrame();
        return S_OK;
    }
    if (pFrame == nullptr)
    {
        CMDIFrameWnd* pMainFrame = MainFrame(hMainFrame);
        if (pMainFrame == nullptr)
        {
            return E_INVALIDARG;
        }
        CEthChildFrame* pNew = new CEthChildFrame(new Pane(), pszTitle, &pFrame);
        if (!pNew->CreateFrame(pMainFrame))
        {
            /* the object is already deleted if its window was created then destroyed */
            if (pFrame == pNew)
            {
                delete pNew;
            }
            return E_FAIL;
        }
    }
    pFrame->ShowFrame();
    return S_OK;
}

/** The capture stopped by itself (adapter removed): forget the connection, keep the reason */
void CheckConnection()
{
    if (sg_bConnected && sg_pCapture != nullptr && !sg_pCapture->IsRunning())
    {
        sg_stopReason = sg_pCapture->LastError();
        sg_pCapture->Stop();
        sg_bConnected = false;
    }
}

bool SelectAdapter(HWND hMainFrame)
{
    std::vector<eth::Adapter> adapters;
    std::string error;
    if (!eth::CEthernetCapture::ListAdapters(adapters, error))
    {
        AfxMessageBox(error.c_str(), MB_ICONERROR);
        return false;
    }
    CEthAdapterDlg dialog(adapters, sg_settings, CWnd::FromHandle(hMainFrame));
    if (dialog.DoModal() != IDOK)
    {
        return false;
    }
    ethmodule::SaveSettings();
    return true;
}
}

namespace ethmodule
{
eth::CEthernetCapture& Capture()
{
    if (sg_pCapture == nullptr)
    {
        sg_pCapture = new eth::CEthernetCapture();
    }
    return *sg_pCapture;
}

EthSettings& Settings()
{
    LoadSettings();
    return sg_settings;
}

EthDisplayOptions& Options()
{
    return sg_options;
}

std::vector<EthTxEntry>& TxEntries()
{
    LoadSettings();
    return sg_txEntries;
}

void SaveSettings()
{
    CWinApp* pApp = AfxGetApp();
    pApp->WriteProfileString(SECTION, "Device", sg_settings.device.c_str());
    pApp->WriteProfileString(SECTION, "AdapterName", sg_settings.adapterName.c_str());
    pApp->WriteProfileInt(SECTION, "Promiscuous", sg_settings.promiscuous ? 1 : 0);
    pApp->WriteProfileString(SECTION, "Filter", sg_settings.filter.c_str());
    const int nOld = pApp->GetProfileInt(SECTION, "TxCount", 0);
    pApp->WriteProfileInt(SECTION, "TxCount", (int)sg_txEntries.size());
    for (int i = 0; i < max(nOld, (int)sg_txEntries.size()); i++)
    {
        CString omKey;
        omKey.Format("Tx%d", i);
        /* a null value deletes the entries that are no longer used */
        pApp->WriteProfileString(SECTION, omKey, i < (int)sg_txEntries.size()
                                 ? Serialize(sg_txEntries[(size_t)i]).c_str() : nullptr);
    }
}

bool Connect(std::string& error)
{
    LoadSettings();
    if (sg_settings.device.empty())
    {
        error = "Select a network adapter first: Ethernet > Adapter Selection.";
        return false;
    }
    /* current MAC and IP addresses of the adapter */
    std::vector<eth::Adapter> adapters;
    if (!eth::CEthernetCapture::ListAdapters(adapters, error))
    {
        return false;
    }
    const eth::Adapter* pAdapter = nullptr;
    for (const eth::Adapter& adapter : adapters)
    {
        if (adapter.device == sg_settings.device)
        {
            pAdapter = &adapter;
        }
    }
    if (pAdapter == nullptr)
    {
        error = "The network adapter \"" + sg_settings.adapterName
                + "\" is not available: connect it, or select another one (Ethernet > Adapter Selection).";
        return false;
    }
    if (!Capture().Start(*pAdapter, sg_settings.promiscuous, sg_settings.filter, error))
    {
        return false;
    }
    sg_bConnected = true;
    sg_llConnectTimeUs = NowUtcUs();
    sg_stopReason.clear();
    TxChanged();
    return true;
}

void Disconnect()
{
    if (sg_pCapture != nullptr)
    {
        sg_pCapture->Stop();
    }
    sg_bConnected = false;
}

bool IsConnected()
{
    return sg_bConnected && sg_pCapture != nullptr && sg_pCapture->IsRunning();
}

int64_t ConnectTimeUs()
{
    return sg_llConnectTimeUs;
}

std::string StopReason()
{
    return sg_stopReason;
}

bool BuildTxFrame(const EthTxEntry& entry, std::vector<uint8_t>& frame, std::string& error)
{
    eth::FrameSpec spec = entry.spec;
    const eth::Adapter& adapter = Capture().CurrentAdapter();
    if (entry.srcMacFromAdapter)
    {
        if (!adapter.hasMac)
        {
            error = "The adapter has no MAC address: enter a source MAC";
            return false;
        }
        memcpy(spec.src, adapter.mac, 6);
    }
    if (spec.kind == eth::FrameSpec::UdpIPv4 && entry.srcIpFromAdapter)
    {
        const std::string first = Split(adapter.ipv4, ',').empty() ? std::string() : Split(adapter.ipv4, ',')[0];
        if (!eth::ParseIPv4(first, spec.srcIp))
        {
            error = "The adapter has no IPv4 address: enter a source IP";
            return false;
        }
    }
    return eth::Build(spec, frame, error);
}

void TxChanged()
{
    std::vector<eth::CyclicFrame> cyclic;
    if (IsConnected())
    {
        for (const EthTxEntry& entry : sg_txEntries)
        {
            eth::CyclicFrame frame;
            std::string error;
            if (entry.cyclic && entry.cycleMs > 0 && BuildTxFrame(entry, frame.data, error))
            {
                frame.periodMs = entry.cycleMs;
                cyclic.push_back(frame);
            }
        }
    }
    Capture().SetCyclic(cyclic);
    SaveSettings();
}
}

ETHERNET_API HRESULT Ethernet_Command(HWND hMainFrame, int nCommand)
{
    LoadSettings();
    EthDisplayOptions& options = sg_options;
    CEthMsgWnd* pMsgWnd = MsgWnd();
    switch (nCommand)
    {
        case ETH_CMD_CONNECT:
        {
            CheckConnection();
            if (ethmodule::IsConnected())
            {
                ethmodule::Disconnect();
                return S_OK;
            }
            if (sg_settings.device.empty() && !SelectAdapter(hMainFrame))
            {
                return S_FALSE;
            }
            /* the message window takes the frames: it exists while connected */
            ShowWindowOf<CEthMsgWnd>(sg_pMsgFrame, _T("Ethernet Message Window"), hMainFrame, false);
            std::string error;
            if (!ethmodule::Connect(error))
            {
                AfxMessageBox(error.c_str(), MB_ICONERROR);
                return E_FAIL;
            }
            return S_OK;
        }
        case ETH_CMD_ADAPTER:
            return SelectAdapter(hMainFrame) ? S_OK : S_FALSE;
        case ETH_CMD_MSG_WINDOW:
            return ShowWindowOf<CEthMsgWnd>(sg_pMsgFrame, _T("Ethernet Message Window"), hMainFrame, true);
        case ETH_CMD_TX_WINDOW:
            return ShowWindowOf<CEthTxWnd>(sg_pTxFrame, _T("Ethernet Transmit Window"), hMainFrame, true);
        case ETH_CMD_CLEAR:
            if (pMsgWnd != nullptr)
            {
                pMsgWnd->Clear();
            }
            return S_OK;
        case ETH_CMD_SYSTEM_TIME:
            options.timeMode = EthDisplayOptions::SystemTime;
            break;
        case ETH_CMD_ABSOLUTE_TIME:
            options.timeMode = EthDisplayOptions::AbsoluteTime;
            break;
        case ETH_CMD_RELATIVE_TIME:
            options.timeMode = EthDisplayOptions::RelativeTime;
            break;
        case ETH_CMD_OVERWRITE:
            options.overwrite = !options.overwrite;
            break;
        case ETH_CMD_SHOW_LAST:
            options.showLast = !options.showLast;
            break;
        case ETH_CMD_PAUSE:
            options.pause = !options.pause;
            break;
        case ETH_CMD_SAVE_PCAP:
            return pMsgWnd != nullptr && pMsgWnd->SavePcap() ? S_OK : S_FALSE;
        default:
            return E_INVALIDARG;
    }
    if (pMsgWnd != nullptr)
    {
        pMsgWnd->OptionsChanged();
    }
    return S_OK;
}

ETHERNET_API int Ethernet_GetCommandState(int nCommand)
{
    const EthDisplayOptions& options = sg_options;
    bool bChecked = false;
    bool bEnabled = true;
    switch (nCommand)
    {
        case ETH_CMD_CONNECT:
            CheckConnection();
            bChecked = ethmodule::IsConnected();
            break;
        case ETH_CMD_MSG_WINDOW:
            bChecked = sg_pMsgFrame != nullptr && sg_pMsgFrame->IsWindowVisible();
            break;
        case ETH_CMD_TX_WINDOW:
            bChecked = sg_pTxFrame != nullptr && sg_pTxFrame->IsWindowVisible();
            break;
        case ETH_CMD_SYSTEM_TIME:
            bChecked = options.timeMode == EthDisplayOptions::SystemTime;
            break;
        case ETH_CMD_ABSOLUTE_TIME:
            bChecked = options.timeMode == EthDisplayOptions::AbsoluteTime;
            break;
        case ETH_CMD_RELATIVE_TIME:
            bChecked = options.timeMode == EthDisplayOptions::RelativeTime;
            break;
        case ETH_CMD_OVERWRITE:
            bChecked = options.overwrite;
            break;
        case ETH_CMD_SHOW_LAST:
            bChecked = options.showLast;
            break;
        case ETH_CMD_PAUSE:
            bChecked = options.pause;
            break;
        case ETH_CMD_SAVE_PCAP:
            bEnabled = MsgWnd() != nullptr && MsgWnd()->HasFrames();
            break;
        default:
            break;
    }
    return (bEnabled ? ETH_STATE_ENABLED : 0) | (bChecked ? ETH_STATE_CHECKED : 0);
}

ETHERNET_API BOOL Ethernet_IsConnected(void)
{
    CheckConnection();
    return ethmodule::IsConnected() ? TRUE : FALSE;
}

ETHERNET_API void Ethernet_Shutdown(void)
{
    ethmodule::Disconnect();
    for (CEthChildFrame* pFrame : { sg_pMsgFrame, sg_pTxFrame })
    {
        if (pFrame != nullptr && pFrame->GetSafeHwnd() != nullptr)
        {
            pFrame->DestroyWindow();        // the frame deletes itself
        }
    }
    if (sg_bSettingsLoaded)
    {
        ethmodule::SaveSettings();
    }
    delete sg_pCapture;
    sg_pCapture = nullptr;
}
