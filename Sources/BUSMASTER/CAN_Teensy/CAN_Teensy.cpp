/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file  CAN_Teensy.cpp
 * @brief CAN driver for a Teensy 4.0 with the BUSMASTER-NG CAN firmware (Firmware/Teensy4_CAN).
 *
 * The Teensy is a USB serial device with two classic CAN controllers (CAN1 and CAN3 on the usual
 * breakout boards). Each controller of each board found is an interface the user can give to a
 * BUSMASTER channel. The serial ports are open only while the driver lists the boards and while
 * BUSMASTER is connected, so the Teensy can be programmed in between.
 */

#include "CAN_Teensy_stdafx.h"
#include "CAN_Teensy.h"

#include "BaseDIL_CAN_Controller.h"
#include "DILPluginHelperDefs.h"
#include "DIL_Interface/HardwareListingCAN.h"

#include "TeensyLink.h"
#include "CAN_Teensy_Resource.h"

#define USAGE_EXPORT
#include "CAN_Teensy_Extern.h"

BEGIN_MESSAGE_MAP(CCAN_Teensy, CWinApp)
END_MESSAGE_MAP()

CCAN_Teensy::CCAN_Teensy()
{
}

// The one and only CCAN_Teensy object
CCAN_Teensy theApp;

BOOL CCAN_Teensy::InitInstance()
{
    CWinApp::InitInstance();
    return TRUE;
}

// ---------------------------------------------------------------------------------------------
// State of the driver

#define MAX_BUFF_ALLOWED    16
#define MAX_CLIENT_ALLOWED  16
#define MAX_ACK_ENTRIES     4096

static const char* const MESSAGE_TITLE = "BUSMASTER-NG - Teensy";

enum
{
    STATE_DRIVER_SELECTED = 0,
    STATE_HW_INTERFACE_LISTED,
    STATE_HW_INTERFACE_SELECTED,
    STATE_CONNECTED
};

struct SClient
{
    DWORD m_dwClientID;
    CBaseCANBufFSE* m_pClientBuf[MAX_BUFF_ALLOWED];
    char m_acClientName[MAX_PATH];
    UINT m_unBufCount;

    SClient() : m_dwClientID(0), m_unBufCount(0)
    {
        memset(m_acClientName, 0, sizeof(m_acClientName));
        memset(m_pClientBuf, 0, sizeof(m_pClientBuf));
    }
};

/** A frame given to the Teensy; its echo tells which client sent it. */
struct SAckEntry
{
    UINT m_unMsgID;
    UINT m_unChannel;       // BUSMASTER channel, from 1
    DWORD m_dwClientID;
};

/** A Teensy found by the last scan. */
struct SBoard
{
    teensy::CLink m_link;
    teensy::Info m_info;
    std::string m_port;
    unsigned long long m_baseUs;    // clock of the Teensy at the time reference of the connection
    long long m_offsetUs;           // time between the reference of the connection and m_baseUs
    bool m_hasBase;

    SBoard() : m_baseUs(0), m_offsetUs(0), m_hasBase(false) {}
};

/** One CAN controller of a board: what the hardware selection dialog lists. */
struct SInterface
{
    SBoard* m_board;
    int m_boardChannel;
    bool m_canFd;                   // the controller can do CAN FD
    std::string m_description;      // also saved in the configuration to find the interface again
    std::string m_details;
};

/** A BUSMASTER channel. */
struct SChannel
{
    SBoard* m_board;
    int m_boardChannel;
    std::string m_description;
    unsigned long m_bitRate;
    bool m_canFd;                   // the controller can do CAN FD
    bool m_fd;                      // CAN FD mode asked for this channel
    unsigned long m_dataBitRate;
    bool m_selfTest;                // frames looped inside the controller, nothing on the bus
    volatile unsigned char m_tec;
    volatile unsigned char m_rec;
    volatile unsigned char m_peakTec;
    volatile unsigned char m_peakRec;
    volatile unsigned char m_busState;
    volatile unsigned long m_rxLost;
    volatile unsigned long m_txDropped;

    SChannel() : m_board(nullptr), m_boardChannel(0), m_bitRate(500000), m_canFd(false), m_fd(false),
        m_dataBitRate(2000000), m_selfTest(false), m_tec(0), m_rec(0),
        m_peakTec(0), m_peakRec(0), m_busState(BMT_BUS_ACTIVE), m_rxLost(0), m_txDropped(0) {}
};

/** A critical section that exists as long as the DLL. */
class CLock
{
public:
    CLock()     { InitializeCriticalSection(&m_section); }
    ~CLock()    { DeleteCriticalSection(&m_section); }
    void Enter() { EnterCriticalSection(&m_section); }
    void Leave() { LeaveCriticalSection(&m_section); }
private:
    CRITICAL_SECTION m_section;
};

static CLock sg_lock;                           // clients, their buffers and the echo list
static SClient sg_asClients[MAX_CLIENT_ALLOWED];
static UINT sg_unClientCnt = 0;
static std::list<SAckEntry> sg_ackList;

static std::vector<SBoard*> sg_boards;
static std::vector<SInterface> sg_interfaces;
static SChannel sg_channels[CHANNEL_ALLOWED];
static UINT sg_nNoOfChannels = 0;
static std::string sg_wanted[CHANNEL_ALLOWED];  // interfaces of the configuration, by channel
static UINT sg_unWantedCount = 0;
static bool sg_bListenOnly = false;

static BYTE sg_bCurrState = STATE_DRIVER_SELECTED;
static HWND sg_hOwnerWnd = nullptr;
static std::string sg_lastError;

static SYSTEMTIME sg_CurrSysTime;
static UINT64 sg_TimeStamp = 0;
static LARGE_INTEGER sg_QueryTickCount;

// ---------------------------------------------------------------------------------------------
// Clients and their buffers

static BOOL bGetClientObj(DWORD dwClientID, UINT& unClientIndex)
{
    for (UINT i = 0; i < sg_unClientCnt; i++)
    {
        if (sg_asClients[i].m_dwClientID == dwClientID)
        {
            unClientIndex = i;
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL bClientIdExist(DWORD dwClientID)
{
    UINT unIndex = 0;
    return bGetClientObj(dwClientID, unIndex);
}

static BOOL bClientExist(const char* pcClientName, INT& nIndex)
{
    for (UINT i = 0; i < sg_unClientCnt; i++)
    {
        if (strcmp(pcClientName, sg_asClients[i].m_acClientName) == 0)
        {
            nIndex = (INT)i;
            return TRUE;
        }
    }
    return FALSE;
}

static DWORD dwGetAvailableClientSlot()
{
    DWORD dwClientID = 2;   // 1 is the monitor
    while (bClientIdExist(dwClientID))
    {
        dwClientID++;
    }
    return dwClientID;
}

static BOOL bRemoveClient(DWORD dwClientID)
{
    UINT unIndex = 0;
    if (!bGetClientObj(dwClientID, unIndex))
    {
        return FALSE;
    }
    if (unIndex + 1 < sg_unClientCnt)
    {
        sg_asClients[unIndex] = sg_asClients[sg_unClientCnt - 1];
    }
    sg_asClients[sg_unClientCnt - 1] = SClient();
    sg_unClientCnt--;
    return TRUE;
}

/** Finds the client that sent the frame an echo belongs to. Call with sg_lock. */
static bool bTakeAckEntry(UINT unMsgID, UINT unChannel, DWORD& dwClientID)
{
    for (std::list<SAckEntry>::iterator it = sg_ackList.begin(); it != sg_ackList.end(); ++it)
    {
        if (it->m_unMsgID == unMsgID && it->m_unChannel == unChannel)
        {
            dwClientID = it->m_dwClientID;
            sg_ackList.erase(it);
            return true;
        }
    }
    return false;
}

/** Gives a frame or an error to the clients. Call with sg_lock. */
static void vWriteIntoClientsBuffer(STCANDATA& sCanData)
{
    if (sCanData.m_ucDataType == TX_FLAG)
    {
        // the client that sent the frame sees it as sent, the others as received
        DWORD dwSender = 0;
        UINT unSenderIndex = (UINT) - 1;
        if (bTakeAckEntry(sCanData.m_uDataInfo.m_sCANMsg.m_unMsgID, sCanData.m_uDataInfo.m_sCANMsg.m_ucChannel, dwSender))
        {
            bGetClientObj(dwSender, unSenderIndex);
        }
        STCANDATA sReceived = sCanData;
        sReceived.m_ucDataType = RX_FLAG;
        for (UINT i = 0; i < sg_unClientCnt; i++)
        {
            STCANDATA& sData = (i == unSenderIndex) ? sCanData : sReceived;
            for (UINT j = 0; j < sg_asClients[i].m_unBufCount; j++)
            {
                sg_asClients[i].m_pClientBuf[j]->WriteIntoBuffer(&sData);
            }
        }
    }
    else
    {
        for (UINT i = 0; i < sg_unClientCnt; i++)
        {
            for (UINT j = 0; j < sg_asClients[i].m_unBufCount; j++)
            {
                sg_asClients[i].m_pClientBuf[j]->WriteIntoBuffer(&sCanData);
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// What comes from the boards (read threads)

/** The BUSMASTER channel of a controller of a board, from 0; -1 when it is not used. */
static int nFindChannel(teensy::CLink& link, int nBoardChannel)
{
    for (UINT i = 0; i < sg_nNoOfChannels; i++)
    {
        if (sg_channels[i].m_board != nullptr && &sg_channels[i].m_board->m_link == &link
                && sg_channels[i].m_boardChannel == nBoardChannel)
        {
            return (int)i;
        }
    }
    return -1;
}

/** The time of an event of a board, in units of 100 microseconds since the connection. */
static LONGLONG llTickCount(const SBoard& board, unsigned long long timeUs)
{
    const long long microseconds = (long long)(timeUs - board.m_baseUs) + board.m_offsetUs;
    return microseconds > 0 ? microseconds / 100 : 0;
}

static UCHAR ucErrorCapture(unsigned long esr1)
{
    const bool bSending = (esr1 & (BMT_ESR1_TRANSMITTING | BMT_ESR1_ACK_ERROR | BMT_ESR1_BIT0_ERROR | BMT_ESR1_BIT1_ERROR)) != 0;
    if (esr1 & BMT_ESR1_ACK_ERROR)
    {
        return ACK_ERROR_TX;
    }
    if (esr1 & BMT_ESR1_STUFF_ERROR)
    {
        return bSending ? STUFF_ERROR_TX : STUFF_ERROR_RX;
    }
    if (esr1 & BMT_ESR1_FORM_ERROR)
    {
        return bSending ? FORM_ERROR_TX : FORM_ERROR_RX;
    }
    if (esr1 & BMT_ESR1_CRC_ERROR)
    {
        return CRC_ERROR_RX;
    }
    if (esr1 & (BMT_ESR1_BIT0_ERROR | BMT_ESR1_BIT1_ERROR))
    {
        return BIT_ERROR_TX;
    }
    return bSending ? OTHER_ERROR_TX : OTHER_ERROR_RX;
}

class CBoardListener : public teensy::IListener
{
public:
    void OnFrame(teensy::CLink& link, const teensy::Frame& frame)
    {
        const int nChannel = nFindChannel(link, frame.channel);
        if (nChannel < 0 || !sg_channels[nChannel].m_board->m_hasBase)
        {
            return;
        }
        STCANDATA sCanData;
        memset(&sCanData, 0, sizeof(sCanData));
        sCanData.m_ucDataType = (frame.flags & BMT_FRAME_TX) ? TX_FLAG : RX_FLAG;
        sCanData.m_lTickCount.QuadPart = llTickCount(*sg_channels[nChannel].m_board, frame.timeUs);
        STCAN_MSG& sMsg = sCanData.m_uDataInfo.m_sCANMsg;
        sMsg.m_unMsgID = frame.id;
        sMsg.m_ucEXTENDED = (frame.flags & BMT_FRAME_EXTENDED) ? 1 : 0;
        sMsg.m_ucRTR = (frame.flags & BMT_FRAME_REMOTE) ? 1 : 0;
        sMsg.m_ucDataLen = frame.length;
        sMsg.m_ucChannel = (UCHAR)(nChannel + 1);
        sMsg.m_bCANFD = (frame.flags & BMT_FRAME_FD) != 0;
        memcpy(sMsg.m_ucData, frame.data, sizeof(frame.data));

        sg_lock.Enter();
        vWriteIntoClientsBuffer(sCanData);
        sg_lock.Leave();
    }

    void OnStatus(teensy::CLink& link, const teensy::Status& status)
    {
        const int nChannel = nFindChannel(link, status.channel);
        if (nChannel < 0 || !status.open)
        {
            return;
        }
        SChannel& channel = sg_channels[nChannel];
        channel.m_tec = status.tec;
        channel.m_rec = status.rec;
        if (status.tec > channel.m_peakTec)
        {
            channel.m_peakTec = status.tec;
        }
        if (status.rec > channel.m_peakRec)
        {
            channel.m_peakRec = status.rec;
        }
        channel.m_busState = status.busState;
        channel.m_rxLost = status.rxLost;
        channel.m_txDropped = status.txDropped;
    }

    void OnBusError(teensy::CLink& link, const teensy::BusError& error)
    {
        const int nChannel = nFindChannel(link, error.channel);
        if (nChannel < 0 || !sg_channels[nChannel].m_board->m_hasBase)
        {
            return;
        }
        STCANDATA sCanData;
        memset(&sCanData, 0, sizeof(sCanData));
        sCanData.m_ucDataType = ERR_FLAG;
        sCanData.m_lTickCount.QuadPart = llTickCount(*sg_channels[nChannel].m_board, error.timeUs);
        SERROR_INFO& sError = sCanData.m_uDataInfo.m_sErrInfo;
        sError.m_ucErrType = ERROR_BUS;
        sError.m_ucReg_ErrCap = ucErrorCapture(error.esr1);
        sError.m_ucTxErrCount = error.tec;
        sError.m_ucRxErrCount = error.rec;
        sError.m_ucChannel = (UCHAR)(nChannel + 1);
        sError.m_nSubError = 0;

        sg_lock.Enter();
        vWriteIntoClientsBuffer(sCanData);
        sg_lock.Leave();
    }

    void OnDisconnected(teensy::CLink& link)
    {
        sg_lastError = "The Teensy on " + link.PortName() + " was disconnected";
    }
};

static CBoardListener sg_listener;

// ---------------------------------------------------------------------------------------------
// Boards

static void vStopBoards()
{
    for (size_t i = 0; i < sg_boards.size(); i++)
    {
        SBoard& board = *sg_boards[i];
        if (board.m_link.IsOpen())
        {
            if (board.m_link.IsAlive())
            {
                for (int nChannel = 0; nChannel < board.m_info.channelCount; nChannel++)
                {
                    board.m_link.CloseChannel((unsigned char)nChannel);
                }
            }
            board.m_link.SetListener(nullptr);
            board.m_link.Close();
        }
        board.m_hasBase = false;
    }
}

/** Looks for the Teensy boards that run the firmware; the ports are closed again. */
static void vScanBoards(std::string& problems)
{
    vStopBoards();
    for (UINT i = 0; i < CHANNEL_ALLOWED; i++)
    {
        sg_channels[i].m_board = nullptr;
    }
    for (size_t i = 0; i < sg_boards.size(); i++)
    {
        delete sg_boards[i];
    }
    sg_boards.clear();
    sg_interfaces.clear();

    const std::vector<teensy::Port> ports = teensy::EnumeratePorts();
    if (ports.empty())
    {
        problems = "No Teensy was found.\n\n"
                   "Plug the Teensy 4.0 in a USB port. It must run the BUSMASTER-NG CAN firmware "
                   "(folder Firmware\\Teensy4_CAN of the sources), built with USB type \"Serial\".";
        return;
    }
    for (size_t i = 0; i < ports.size(); i++)
    {
        SBoard* board = new SBoard;
        std::string error;
        if (board->m_link.Open(ports[i].name, error))
        {
            board->m_info = board->m_link.GetInfo();
            board->m_port = ports[i].name;
            board->m_link.Close();
            sg_boards.push_back(board);
        }
        else
        {
            problems += error + ".\n";
            delete board;
        }
    }
    for (size_t i = 0; i < sg_boards.size(); i++)
    {
        SBoard& board = *sg_boards[i];
        for (int nChannel = 0; nChannel < board.m_info.channelCount; nChannel++)
        {
            char acText[160];
            SInterface sInterface;
            sInterface.m_board = &board;
            sInterface.m_boardChannel = nChannel;
            sInterface.m_canFd = (board.m_info.capabilities[nChannel] & BMT_CAP_CANFD) != 0;
            sprintf_s(acText, "%s %lu - %s", board.m_info.board.c_str(), board.m_info.serial,
                      board.m_info.channelName[nChannel].c_str());
            sInterface.m_description = acText;
            sprintf_s(acText, "Firmware %d.%d, %s%s", board.m_info.firmwareMajor, board.m_info.firmwareMinor,
                      board.m_port.c_str(), sInterface.m_canFd ? ", CAN FD" : "");
            sInterface.m_details = acText;
            sg_interfaces.push_back(sInterface);
        }
    }
    if (sg_interfaces.empty() && problems.empty())
    {
        problems = "No Teensy with the BUSMASTER-NG CAN firmware was found.";
    }
    // the channels selected before keep their interface when it is still there
    for (UINT i = 0; i < sg_nNoOfChannels; i++)
    {
        for (size_t j = 0; j < sg_interfaces.size(); j++)
        {
            if (sg_interfaces[j].m_description == sg_channels[i].m_description)
            {
                sg_channels[i].m_board = sg_interfaces[j].m_board;
                sg_channels[i].m_boardChannel = sg_interfaces[j].m_boardChannel;
                sg_channels[i].m_canFd = sg_interfaces[j].m_canFd;
                break;
            }
        }
    }
}

/** Opens a channel of a board that is connected, in the mode its settings ask. */
static bool bOpenChannel(SChannel& channel, std::string& error)
{
    const bool bFd = channel.m_fd && channel.m_canFd;
    const unsigned char ucMode = (unsigned char)((sg_bListenOnly ? BMT_MODE_LISTEN_ONLY : 0) |
                                 (bFd ? BMT_MODE_FD : 0) | (channel.m_selfTest ? BMT_MODE_LOOPBACK : 0));
    return channel.m_board->m_link.OpenChannel((unsigned char)channel.m_boardChannel, channel.m_bitRate, ucMode,
            channel.m_dataBitRate, error);
}

/** Opens the ports and the channels. The reason of a failure is in sg_lastError. */
static bool bStartBoards()
{
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    bool bReferenceTaken = false;

    for (UINT i = 0; i < sg_nNoOfChannels; i++)
    {
        SChannel& channel = sg_channels[i];
        if (channel.m_board == nullptr)
        {
            sg_lastError = "No Teensy is selected: use Channel Configuration";
            return false;
        }
        SBoard& board = *channel.m_board;
        if (!board.m_link.IsOpen())
        {
            if (!board.m_link.Open(board.m_port, sg_lastError))
            {
                return false;
            }
            board.m_link.SetListener(&sg_listener);
            // time reference: the clock of the Teensy now, while its channels are still closed
            teensy::Status status;
            if (!board.m_link.QueryStatus(0, status))
            {
                sg_lastError = "The Teensy on " + board.m_port + " does not answer";
                return false;
            }
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            if (!bReferenceTaken)
            {
                bReferenceTaken = true;
                GetLocalTime(&sg_CurrSysTime);
                sg_QueryTickCount = now;
                sg_TimeStamp = 0;
            }
            board.m_baseUs = status.timeUs;
            board.m_offsetUs = (now.QuadPart - sg_QueryTickCount.QuadPart) * 1000000 / frequency.QuadPart;
            board.m_hasBase = true;
        }
        channel.m_tec = channel.m_rec = channel.m_peakTec = channel.m_peakRec = 0;
        channel.m_busState = BMT_BUS_ACTIVE;
        channel.m_rxLost = channel.m_txDropped = 0;
        std::string error;
        if (!bOpenChannel(channel, error))
        {
            char acText[32];
            sprintf_s(acText, "Channel %u: ", i + 1);
            sg_lastError = acText + error;
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// Advanced settings of a channel (button Advanced of the hardware selection dialog)

static const unsigned long sg_aulDataBitRates[] = { 250000, 500000, 1000000, 2000000, 3000000, 4000000 };

class CTeensySettingsDlg : public CDialog
{
public:
    CString m_omInterface;
    bool m_bCanFdPossible;
    BOOL m_bCanFd;
    unsigned long m_ulDataBitRate;
    BOOL m_bSelfTest;

    explicit CTeensySettingsDlg(CWnd* pParent)
        : CDialog(IDD_TEENSY_SETTINGS, pParent), m_bCanFdPossible(false), m_bCanFd(FALSE),
          m_ulDataBitRate(2000000), m_bSelfTest(FALSE)
    {
    }

protected:
    virtual BOOL OnInitDialog()
    {
        CDialog::OnInitDialog();
        SetDlgItemText(IDC_TEENSY_INTERFACE, m_omInterface);
        CComboBox* pCombo = (CComboBox*)GetDlgItem(IDC_TEENSY_DATA_RATE);
        int nSelected = -1;
        for (int i = 0; i < _countof(sg_aulDataBitRates); i++)
        {
            CString omRate;
            omRate.Format("%lu", sg_aulDataBitRates[i]);
            pCombo->AddString(omRate);
            if (sg_aulDataBitRates[i] == m_ulDataBitRate)
            {
                nSelected = i;
            }
        }
        if (nSelected >= 0)
        {
            pCombo->SetCurSel(nSelected);
        }
        else
        {
            CString omRate;
            omRate.Format("%lu", m_ulDataBitRate);
            pCombo->SetWindowText(omRate);
        }
        CheckDlgButton(IDC_TEENSY_CANFD, (m_bCanFd && m_bCanFdPossible) ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(IDC_TEENSY_SELF_TEST, m_bSelfTest ? BST_CHECKED : BST_UNCHECKED);
        GetDlgItem(IDC_TEENSY_CANFD)->EnableWindow(m_bCanFdPossible);
        if (!m_bCanFdPossible)
        {
            SetDlgItemText(IDC_TEENSY_CANFD, "CAN FD (this controller of the Teensy is a classic CAN controller)");
        }
        vEnableDataRate();
        return TRUE;
    }

    virtual void OnOK()
    {
        CString omRate;
        GetDlgItemText(IDC_TEENSY_DATA_RATE, omRate);
        const unsigned long ulRate = strtoul(omRate, nullptr, 10);
        m_bCanFd = m_bCanFdPossible && IsDlgButtonChecked(IDC_TEENSY_CANFD) == BST_CHECKED;
        if (m_bCanFd && (ulRate < 5000 || ulRate > 8000000))
        {
            MessageBox("The data bit rate is a number of bit/s, for example 2000000.", MESSAGE_TITLE,
                       MB_OK | MB_ICONWARNING);
            return;
        }
        if (ulRate >= 5000 && ulRate <= 8000000)
        {
            m_ulDataBitRate = ulRate;
        }
        m_bSelfTest = IsDlgButtonChecked(IDC_TEENSY_SELF_TEST) == BST_CHECKED;
        CDialog::OnOK();
    }

    afx_msg void OnCanFdClicked()
    {
        vEnableDataRate();
    }

    void vEnableDataRate()
    {
        const BOOL bEnable = m_bCanFdPossible && IsDlgButtonChecked(IDC_TEENSY_CANFD) == BST_CHECKED;
        GetDlgItem(IDC_TEENSY_DATA_RATE)->EnableWindow(bEnable);
        GetDlgItem(IDC_TEENSY_DATA_RATE_LABEL)->EnableWindow(bEnable);
    }

    DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CTeensySettingsDlg, CDialog)
    ON_BN_CLICKED(IDC_TEENSY_CANFD, &CTeensySettingsDlg::OnCanFdClicked)
END_MESSAGE_MAP()

/** The hardware selection dialog asks this for the bit rates and the advanced settings. */
class CTeensySettings : public IChangeRegisters
{
public:
    int InvokeAdavancedSettings(PSCONTROLLER_DETAILS pControllerDetails, UINT nCount, UINT nSelected)
    {
        if (pControllerDetails == nullptr || nSelected >= nCount)
        {
            return WARN_INITDAT_NCONFIRM;
        }
        AFX_MANAGE_STATE(AfxGetStaticModuleState());
        SCONTROLLER_DETAILS& sDetails = pControllerDetails[nSelected];

        // the dialog put the description of the interface of this channel here
        bool bCanFd = false;
        for (size_t i = 0; i < sg_interfaces.size(); i++)
        {
            if (sg_interfaces[i].m_description == sDetails.m_omHardwareDesc)
            {
                bCanFd = sg_interfaces[i].m_canFd;
                break;
            }
        }
        CTeensySettingsDlg dialog(CWnd::GetActiveWindow());
        dialog.m_omInterface.Format("Channel %u: %s", nSelected + 1, sDetails.m_omHardwareDesc.c_str());
        dialog.m_bCanFdPossible = bCanFd;
        dialog.m_bCanFd = sDetails.m_bcanFDEnabled && bCanFd;
        dialog.m_ulDataBitRate = sDetails.m_unDataBitRate;
        dialog.m_bSelfTest = sDetails.m_bDebug != 0;
        if (dialog.DoModal() != IDOK)
        {
            return WARN_INITDAT_NCONFIRM;
        }
        sDetails.m_bSupportCANFD = bCanFd;
        sDetails.m_bcanFDEnabled = dialog.m_bCanFd != FALSE;
        sDetails.m_unDataBitRate = dialog.m_ulDataBitRate;
        sDetails.m_bDebug = dialog.m_bSelfTest;
        return INFO_INITDAT_CONFIRM_CONFIG;
    }

    double vValidateBaudRate(double dBaudRate, int, UINT)
    {
        if (dBaudRate < 5000.0)
        {
            return 5000.0;
        }
        if (dBaudRate > 1000000.0)
        {
            return 1000000.0;
        }
        return dBaudRate;
    }
};

static CTeensySettings sg_settings;

// ---------------------------------------------------------------------------------------------
// The driver

class CDIL_CAN_Teensy : public CBaseDIL_CAN_Controller
{
public:
    HRESULT CAN_PerformInitOperations(void);
    HRESULT CAN_PerformClosureOperations(void);
    HRESULT CAN_GetTimeModeMapping(SYSTEMTIME& CurrSysTime, UINT64& TimeStamp, LARGE_INTEGER& QueryTickCount);
    HRESULT CAN_ListHwInterfaces(INTERFACE_HW_LIST& sSelHwInterface, INT& nCount, PSCONTROLLER_DETAILS InitData);
    HRESULT CAN_SelectHwInterface(const INTERFACE_HW_LIST& sSelHwInterface, INT nCount);
    HRESULT CAN_DeselectHwInterface(void);
    HRESULT CAN_SetConfigData(PSCONTROLLER_DETAILS InitData, int Length);
    HRESULT CAN_StartHardware(void);
    HRESULT CAN_StopHardware(void);
    HRESULT CAN_GetCurrStatus(STATUSMSG& StatusData);
    HRESULT CAN_SendMsg(DWORD dwClientID, const STCAN_MSG& sCanTxMsg);
    HRESULT CAN_GetLastErrorString(std::string& acErrorStr);
    HRESULT CAN_GetControllerParams(LONG& lParam, UINT nChannel, ECONTR_PARAM eContrParam);
    HRESULT CAN_SetControllerParams(int nValue, ECONTR_PARAM eContrparam);
    HRESULT CAN_GetErrorCount(SERROR_CNT& sErrorCnt, UINT nChannel, ECONTR_PARAM eContrParam);
    HRESULT CAN_SetAppParams(HWND hWndOwner);
    HRESULT CAN_ManageMsgBuf(BYTE bAction, DWORD ClientID, CBaseCANBufFSE* pBufObj);
    HRESULT CAN_RegisterClient(BOOL bRegister, DWORD& ClientID, char* pacClientName);
    HRESULT CAN_GetCntrlStatus(const HANDLE& hEvent, UINT& unCntrlStatus);
    HRESULT CAN_LoadDriverLibrary(void);
    HRESULT CAN_UnloadDriverLibrary(void);
    HRESULT CAN_SetHardwareChannel(PSCONTROLLER_DETAILS, DWORD dwDriverId, bool bIsHardwareListed, unsigned int unChannelCount);
};

static CDIL_CAN_Teensy* sg_pouDIL_CAN_Teensy = nullptr;

USAGEMODE HRESULT GetIDIL_CAN_Controller(void** ppvInterface)
{
    if (sg_pouDIL_CAN_Teensy == nullptr)
    {
        sg_pouDIL_CAN_Teensy = new CDIL_CAN_Teensy;
    }
    *ppvInterface = (void*)sg_pouDIL_CAN_Teensy;
    return (sg_pouDIL_CAN_Teensy != nullptr) ? S_OK : S_FALSE;
}

HRESULT CDIL_CAN_Teensy::CAN_SetAppParams(HWND hWndOwner)
{
    sg_hOwnerWnd = hWndOwner;
    CAN_ManageMsgBuf(MSGBUF_CLEAR, 0, nullptr);
    return S_OK;
}

HRESULT CDIL_CAN_Teensy::CAN_LoadDriverLibrary(void)
{
    return S_OK;
}

HRESULT CDIL_CAN_Teensy::CAN_UnloadDriverLibrary(void)
{
    return S_OK;
}

HRESULT CDIL_CAN_Teensy::CAN_PerformInitOperations(void)
{
    DWORD dwClientID = 0;
    CAN_RegisterClient(TRUE, dwClientID, CAN_MONITOR_NODE);
    return S_OK;
}

HRESULT CDIL_CAN_Teensy::CAN_PerformClosureOperations(void)
{
    if (sg_bCurrState == STATE_CONNECTED)
    {
        CAN_StopHardware();
    }
    vStopBoards();
    sg_lock.Enter();
    while (sg_unClientCnt > 0)
    {
        bRemoveClient(sg_asClients[0].m_dwClientID);
    }
    sg_ackList.clear();
    sg_lock.Leave();
    sg_bCurrState = STATE_DRIVER_SELECTED;
    return S_OK;
}

HRESULT CDIL_CAN_Teensy::CAN_ManageMsgBuf(BYTE bAction, DWORD ClientID, CBaseCANBufFSE* pBufObj)
{
    HRESULT hResult = S_FALSE;
    sg_lock.Enter();
    if (ClientID != 0)
    {
        UINT unClientIndex = 0;
        if (bGetClientObj(ClientID, unClientIndex))
        {
            SClient& sClient = sg_asClients[unClientIndex];
            if (bAction == MSGBUF_ADD)
            {
                if (pBufObj != nullptr && sClient.m_unBufCount < MAX_BUFF_ALLOWED)
                {
                    bool bExists = false;
                    for (UINT i = 0; i < sClient.m_unBufCount; i++)
                    {
                        bExists = bExists || (sClient.m_pClientBuf[i] == pBufObj);
                    }
                    if (bExists)
                    {
                        hResult = ERR_BUFFER_EXISTS;
                    }
                    else
                    {
                        sClient.m_pClientBuf[sClient.m_unBufCount++] = pBufObj;
                        hResult = S_OK;
                    }
                }
            }
            else if (bAction == MSGBUF_CLEAR)
            {
                if (pBufObj != nullptr)
                {
                    for (UINT i = 0; i < sClient.m_unBufCount; i++)
                    {
                        if (sClient.m_pClientBuf[i] == pBufObj)
                        {
                            sClient.m_pClientBuf[i] = sClient.m_pClientBuf[sClient.m_unBufCount - 1];
                            sClient.m_unBufCount--;
                            i--;
                        }
                    }
                }
                else
                {
                    memset(sClient.m_pClientBuf, 0, sizeof(sClient.m_pClientBuf));
                    sClient.m_unBufCount = 0;
                }
                hResult = S_OK;
            }
        }
        else
        {
            hResult = ERR_NO_CLIENT_EXIST;
        }
    }
    else
    {
        if (bAction == MSGBUF_CLEAR)
        {
            for (UINT i = 0; i < sg_unClientCnt; i++)
            {
                memset(sg_asClients[i].m_pClientBuf, 0, sizeof(sg_asClients[i].m_pClientBuf));
                sg_asClients[i].m_unBufCount = 0;
            }
        }
        hResult = S_OK;
    }
    sg_lock.Leave();
    return hResult;
}

HRESULT CDIL_CAN_Teensy::CAN_RegisterClient(BOOL bRegister, DWORD& ClientID, char* pacClientName)
{
    HRESULT hResult = S_FALSE;
    sg_lock.Enter();
    if (bRegister)
    {
        INT nIndex = 0;
        if (pacClientName == nullptr)
        {
            hResult = S_FALSE;
        }
        else if (bClientExist(pacClientName, nIndex))
        {
            ClientID = sg_asClients[nIndex].m_dwClientID;
            hResult = ERR_CLIENT_EXISTS;
        }
        else if (sg_unClientCnt >= MAX_CLIENT_ALLOWED)
        {
            hResult = ERR_NO_MORE_CLIENT_ALLOWED;
        }
        else
        {
            // the monitor always has the client id 1
            ClientID = (strcmp(pacClientName, CAN_MONITOR_NODE) == 0) ? 1 : dwGetAvailableClientSlot();
            SClient& sClient = sg_asClients[sg_unClientCnt];
            sClient = SClient();
            sClient.m_dwClientID = ClientID;
            strncpy_s(sClient.m_acClientName, pacClientName, _TRUNCATE);
            sg_unClientCnt++;
            hResult = S_OK;
        }
    }
    else
    {
        hResult = bRemoveClient(ClientID) ? S_OK : ERR_NO_CLIENT_EXIST;
    }
    sg_lock.Leave();
    return hResult;
}

HRESULT CDIL_CAN_Teensy::CAN_GetCntrlStatus(const HANDLE& /*hEvent*/, UINT& /*unCntrlStatus*/)
{
    return S_OK;
}

HRESULT CDIL_CAN_Teensy::CAN_GetTimeModeMapping(SYSTEMTIME& CurrSysTime, UINT64& TimeStamp, LARGE_INTEGER& QueryTickCount)
{
    CurrSysTime = sg_CurrSysTime;
    TimeStamp = sg_TimeStamp;
    QueryTickCount = sg_QueryTickCount;
    return S_OK;
}

/** The interfaces the configuration names, so they can be selected again without asking. */
HRESULT CDIL_CAN_Teensy::CAN_SetHardwareChannel(PSCONTROLLER_DETAILS pControllerDetails, DWORD /*dwDriverId*/,
        bool /*bIsHardwareListed*/, unsigned int unChannelCount)
{
    sg_unWantedCount = 0;
    if (pControllerDetails == nullptr)
    {
        return S_OK;
    }
    for (UINT i = 0; i < unChannelCount && i < (UINT)CHANNEL_ALLOWED; i++)
    {
        const std::string& description = pControllerDetails[i].m_omHardwareDesc;
        if (description.empty() || description == "Simulation")
        {
            break;
        }
        sg_wanted[sg_unWantedCount++] = description;
    }
    return S_OK;
}

static int nFindInterface(const std::string& description, const int* pnUsed, int nUsedCount)
{
    for (size_t i = 0; i < sg_interfaces.size(); i++)
    {
        if (sg_interfaces[i].m_description != description)
        {
            continue;
        }
        bool bUsed = false;
        for (int j = 0; j < nUsedCount; j++)
        {
            bUsed = bUsed || (pnUsed[j] == (int)i);
        }
        if (!bUsed)
        {
            return (int)i;
        }
    }
    return -1;
}

HRESULT CDIL_CAN_Teensy::CAN_ListHwInterfaces(INTERFACE_HW_LIST& asSelHwInterface, INT& nCount, PSCONTROLLER_DETAILS InitData)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    if (sg_bCurrState == STATE_CONNECTED)
    {
        CAN_StopHardware();
    }
    const int nDefaultCount = nCount;   // channels of the configuration being loaded, or 0 / the maximum

    std::string problems;
    {
        CWaitCursor wait;
        vScanBoards(problems);
    }
    if (sg_interfaces.empty())
    {
        nCount = 0;
        sg_nNoOfChannels = 0;
        sg_lastError = problems;
        ::MessageBox(sg_hOwnerWnd, problems.c_str(), MESSAGE_TITLE, MB_OK | MB_ICONWARNING);
        return NO_HW_INTERFACE;
    }

    const int nAvailable = (int)min(sg_interfaces.size(), (size_t)CHANNEL_ALLOWED);
    INTERFACE_HW asAvailable[CHANNEL_ALLOWED];
    for (int i = 0; i < nAvailable; i++)
    {
        asAvailable[i].m_dwIdInterface = (unsigned long)i;
        asAvailable[i].m_dwVendor = sg_interfaces[i].m_board->m_info.serial;
        asAvailable[i].m_bytNetworkID = 0;
        asAvailable[i].m_acNameInterface = sg_interfaces[i].m_description;
        asAvailable[i].m_acDescription = sg_interfaces[i].m_description;
        asAvailable[i].m_acDeviceName = sg_interfaces[i].m_details;
    }

    // the interfaces of the configuration that are present, in the order of the channels
    int anSelected[CHANNEL_ALLOWED];
    int nSelected = 0;
    for (int i = 0; i < CHANNEL_ALLOWED; i++)
    {
        anSelected[i] = -1;
    }
    for (UINT i = 0; i < sg_unWantedCount && nSelected < nAvailable; i++)
    {
        const int nIndex = nFindInterface(sg_wanted[i], anSelected, nSelected);
        if (nIndex >= 0 && nIndex < nAvailable)
        {
            anSelected[nSelected++] = nIndex;
        }
    }

    // A configuration is being loaded and all its interfaces are there: no question.
    const bool bSilent = nDefaultCount > 0 && nDefaultCount < CHANNEL_ALLOWED
                         && (UINT)nDefaultCount == sg_unWantedCount && nSelected == nDefaultCount;
    if (!bSilent)
    {
        if (nSelected == 0)
        {
            // first time: every controller found, in the order of the boards
            for (int i = 0; i < nAvailable; i++)
            {
                anSelected[nSelected++] = i;
            }
        }
        CWnd objMainWnd;
        objMainWnd.Attach(sg_hOwnerWnd);
        CHardwareListingCAN HwList(asAvailable, nAvailable, anSelected, CAN, CHANNEL_ALLOWED, &objMainWnd,
                                   InitData, &sg_settings);
        const INT_PTR nResult = HwList.DoModal();
        objMainWnd.Detach();
        if (nResult != IDOK)
        {
            return HW_INTERFACE_NO_SEL;
        }
        nSelected = HwList.nGetSelectedList(anSelected);
        if (nSelected <= 0)
        {
            return HW_INTERFACE_NO_SEL;
        }
    }

    for (int i = 0; i < nSelected; i++)
    {
        const SInterface& sInterface = sg_interfaces[anSelected[i]];
        sg_channels[i].m_board = sInterface.m_board;
        sg_channels[i].m_boardChannel = sInterface.m_boardChannel;
        sg_channels[i].m_canFd = sInterface.m_canFd;
        sg_channels[i].m_description = sInterface.m_description;
        asSelHwInterface[i] = asAvailable[anSelected[i]];
    }
    sg_nNoOfChannels = (UINT)nSelected;
    nCount = nSelected;
    sg_bCurrState = STATE_HW_INTERFACE_LISTED;
    return S_OK;
}

HRESULT CDIL_CAN_Teensy::CAN_SelectHwInterface(const INTERFACE_HW_LIST& /*asSelHwInterface*/, INT /*nCount*/)
{
    if (sg_nNoOfChannels == 0)
    {
        return ERR_IMPROPER_STATE;
    }
    if (sg_bCurrState != STATE_CONNECTED)
    {
        sg_bCurrState = STATE_HW_INTERFACE_SELECTED;
    }
    return S_OK;
}

HRESULT CDIL_CAN_Teensy::CAN_DeselectHwInterface(void)
{
    if (sg_bCurrState == STATE_CONNECTED)
    {
        CAN_StopHardware();
    }
    if (sg_bCurrState == STATE_HW_INTERFACE_SELECTED)
    {
        sg_bCurrState = STATE_HW_INTERFACE_LISTED;
    }
    return S_OK;
}

HRESULT CDIL_CAN_Teensy::CAN_SetConfigData(PSCONTROLLER_DETAILS InitData, int Length)
{
    if (InitData == nullptr)
    {
        return S_FALSE;
    }
    for (int i = 0; i < Length && i < (int)sg_nNoOfChannels; i++)
    {
        double dBitRate = atof(InitData[i].m_omStrBaudrate.c_str());
        if (dBitRate > 0.0 && dBitRate <= 1000.0)
        {
            dBitRate *= 1000.0;     // old configurations have kbit/s
        }
        if (dBitRate >= 1000.0 && dBitRate <= 1000000.0)
        {
            sg_channels[i].m_bitRate = (unsigned long)(dBitRate + 0.5);
        }
        InitData[i].m_omHardwareDesc = sg_channels[i].m_description;
        InitData[i].m_bSupportCANFD = sg_channels[i].m_canFd;
        sg_channels[i].m_fd = InitData[i].m_bcanFDEnabled && sg_channels[i].m_canFd;
        if (InitData[i].m_unDataBitRate >= 5000 && InitData[i].m_unDataBitRate <= 8000000)
        {
            sg_channels[i].m_dataBitRate = InitData[i].m_unDataBitRate;
        }
        sg_channels[i].m_selfTest = InitData[i].m_bDebug != 0;
    }
    if (Length > 0)
    {
        // as the other drivers: the mode of the first channel is the mode of the interface
        sg_bListenOnly = InitData[0].m_ucControllerMode == defMODE_PASSIVE;
    }
    return S_OK;
}

HRESULT CDIL_CAN_Teensy::CAN_StartHardware(void)
{
    if (sg_bCurrState == STATE_CONNECTED)
    {
        return S_OK;
    }
    if (sg_nNoOfChannels == 0)
    {
        return ERR_IMPROPER_STATE;
    }
    sg_lock.Enter();
    sg_ackList.clear();
    sg_lock.Leave();
    sg_lastError.clear();

    if (!bStartBoards())
    {
        vStopBoards();
        const std::string text = "The Teensy interface cannot be started.\n\n" + sg_lastError + ".";
        ::MessageBox(sg_hOwnerWnd, text.c_str(), MESSAGE_TITLE, MB_OK | MB_ICONWARNING);
        return S_FALSE;
    }
    sg_bCurrState = STATE_CONNECTED;
    return S_OK;
}

HRESULT CDIL_CAN_Teensy::CAN_StopHardware(void)
{
    VALIDATE_VALUE_RETURN_VAL(sg_bCurrState, STATE_CONNECTED, ERR_IMPROPER_STATE);
    vStopBoards();
    sg_lock.Enter();
    sg_ackList.clear();
    sg_lock.Leave();
    sg_bCurrState = STATE_HW_INTERFACE_SELECTED;
    return S_OK;
}

HRESULT CDIL_CAN_Teensy::CAN_GetCurrStatus(STATUSMSG& StatusData)
{
    StatusData.wControllerStatus = NORMAL_ACTIVE;
    return S_OK;
}

HRESULT CDIL_CAN_Teensy::CAN_SendMsg(DWORD dwClientID, const STCAN_MSG& sCanTxMsg)
{
    VALIDATE_VALUE_RETURN_VAL(sg_bCurrState, STATE_CONNECTED, ERR_IMPROPER_STATE);

    if (sCanTxMsg.m_ucChannel == 0 || sCanTxMsg.m_ucChannel > sg_nNoOfChannels)
    {
        return ERR_INVALID_CHANNEL;
    }
    SChannel& channel = sg_channels[sCanTxMsg.m_ucChannel - 1];
    if (channel.m_board == nullptr || sg_bListenOnly)
    {
        return S_FALSE;
    }
    const bool bFd = sCanTxMsg.m_bCANFD;
    if ((bFd && !(channel.m_fd && channel.m_canFd)) || sCanTxMsg.m_ucDataLen > (bFd ? BMT_MAX_DATA : 8))
    {
        return S_FALSE;         // a CAN FD frame on a channel that is not in CAN FD mode, or too long
    }
    // the data of a CAN FD frame is sent at the data bit rate when it is the faster one
    const unsigned char ucFlags = (unsigned char)((sCanTxMsg.m_ucEXTENDED ? BMT_FRAME_EXTENDED : 0) |
                                  ((!bFd && sCanTxMsg.m_ucRTR) ? BMT_FRAME_REMOTE : 0) |
                                  (bFd ? BMT_FRAME_FD : 0) |
                                  ((bFd && channel.m_dataBitRate > channel.m_bitRate) ? BMT_FRAME_BRS : 0));

    sg_lock.Enter();
    if (!bClientIdExist(dwClientID))
    {
        sg_lock.Leave();
        return ERR_NO_CLIENT_EXIST;
    }
    SAckEntry sEntry;
    sEntry.m_unMsgID = sCanTxMsg.m_unMsgID;
    sEntry.m_unChannel = sCanTxMsg.m_ucChannel;
    sEntry.m_dwClientID = dwClientID;
    sg_ackList.push_back(sEntry);
    if (sg_ackList.size() > MAX_ACK_ENTRIES)
    {
        sg_ackList.pop_front();     // frames that never left (nobody acknowledges)
    }
    sg_lock.Leave();

    const bool bSent = channel.m_board->m_link.Transmit((unsigned char)channel.m_boardChannel, sCanTxMsg.m_unMsgID,
                       ucFlags, sCanTxMsg.m_ucDataLen, sCanTxMsg.m_ucData);
    return bSent ? S_OK : S_FALSE;
}

HRESULT CDIL_CAN_Teensy::CAN_GetLastErrorString(std::string& acErrorStr)
{
    acErrorStr = sg_lastError;
    return S_OK;
}

HRESULT CDIL_CAN_Teensy::CAN_GetControllerParams(LONG& lParam, UINT nChannel, ECONTR_PARAM eContrParam)
{
    HRESULT hResult = S_OK;
    switch (eContrParam)
    {
        case NUMBER_HW:
        case NUMBER_CONNECTED_HW:
            lParam = (LONG)sg_nNoOfChannels;
            break;
        case DRIVER_STATUS:
            lParam = TRUE;
            break;
        case HW_MODE:
            if (nChannel < sg_nNoOfChannels)
            {
                lParam = sg_bListenOnly ? defMODE_PASSIVE : defMODE_ACTIVE;
            }
            else
            {
                lParam = defCONTROLLER_BUSOFF + 1;  // unknown
            }
            break;
        case CON_TEST:
            lParam = TRUE;
            break;
        default:
            hResult = S_FALSE;
            break;
    }
    return hResult;
}

HRESULT CDIL_CAN_Teensy::CAN_SetControllerParams(int nValue, ECONTR_PARAM eContrparam)
{
    if (eContrparam != HW_MODE || (nValue != defMODE_ACTIVE && nValue != defMODE_PASSIVE))
    {
        return S_OK;
    }
    const bool bListenOnly = nValue == defMODE_PASSIVE;
    if (bListenOnly == sg_bListenOnly)
    {
        return S_OK;
    }
    sg_bListenOnly = bListenOnly;
    if (sg_bCurrState == STATE_CONNECTED)
    {
        // opening a channel again changes its mode
        for (UINT i = 0; i < sg_nNoOfChannels; i++)
        {
            SChannel& channel = sg_channels[i];
            std::string error;
            if (channel.m_board != nullptr)
            {
                bOpenChannel(channel, error);
            }
        }
    }
    return S_OK;
}

HRESULT CDIL_CAN_Teensy::CAN_GetErrorCount(SERROR_CNT& sErrorCnt, UINT nChannel, ECONTR_PARAM eContrParam)
{
    if (nChannel >= sg_nNoOfChannels)
    {
        return ERR_INVALID_CHANNEL;
    }
    const SChannel& channel = sg_channels[nChannel];
    if (eContrParam == ERR_CNT)
    {
        sErrorCnt.m_ucTxErrCount = channel.m_tec;
        sErrorCnt.m_ucRxErrCount = channel.m_rec;
    }
    else if (eContrParam == PEAK_ERR_CNT)
    {
        sErrorCnt.m_ucTxErrCount = channel.m_peakTec;
        sErrorCnt.m_ucRxErrCount = channel.m_peakRec;
    }
    return S_OK;
}
