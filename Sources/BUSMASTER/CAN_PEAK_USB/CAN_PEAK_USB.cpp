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
 * \file      CAN_PEAK_USB.cpp
 * \brief     BUSMASTER-NG: PEAK CAN driver built on PCAN-Basic
 *
 * Replaces the closed CAN_PEAK_USB.dll of BUSMASTER 3.2.2, which needed CanApi2.dll. This one uses
 * PCANBasic.dll, the free API that comes with the PEAK device driver, loaded at run time (see PCANBasicDyn.h).
 *
 * - Every PEAK channel PCAN-Basic knows (USB, PCI, LAN, PC Card) is listed in the hardware selection dialog;
 *   up to CHANNEL_ALLOWED channels can be used at the same time.
 * - Classic CAN (11 / 29 bit identifiers, RTR, 0..8 data bytes). The bit rate comes from the hardware selection
 *   dialog: the usual rates use the BTR0/BTR1 values PEAK recommends, any other rate the values computed for an
 *   SJA1000 at 16 MHz (the format of CAN_Initialize).
 * - Listen-only when the controller mode is passive.
 * - Reception in one thread woken by the PCAN-Basic receive events. Hardware time stamps (microseconds) are
 *   brought into the QueryPerformanceCounter time base of BUSMASTER.
 * - A transmitted frame is shown as Tx when CAN_Write has accepted it.
 * - PCAN-Basic has no error counters: the bus state read with CAN_GetStatus is given as counter values of the
 *   same state (0 error active, 96 warning, 128 error passive, 255 bus off), so that state changes and the
 *   error handlers of the node simulation work.
 */

#include "CAN_PEAK_USB_stdafx.h"
#include "CAN_PEAK_USB.h"
#include "BaseDIL_CAN_Controller.h"
#include "DILPluginHelperDefs.h"
#include "Utility/UtilFunctions.h"
#include "PCANBasicDyn.h"

#define USAGE_EXPORT
#include "CAN_PEAK_USB_Extern.h"

BEGIN_MESSAGE_MAP(CCAN_PEAK_USB, CWinApp)
END_MESSAGE_MAP()

CCAN_PEAK_USB::CCAN_PEAK_USB()
{
}

// The one and only CCAN_PEAK_USB object
CCAN_PEAK_USB theApp;

BOOL CCAN_PEAK_USB::InitInstance()
{
    CWinApp::InitInstance();
    return TRUE;
}

/* ---------------------------------------------------------------------------------------------------------------
   State
   --------------------------------------------------------------------------------------------------------------- */

#define MAX_BUFF_ALLOWED    16
#define MAX_CLIENT_ALLOWED  16

enum
{
    STATE_DRIVER_SELECTED = 0x0,
    STATE_HW_INTERFACE_LISTED,
    STATE_HW_INTERFACE_SELECTED,
    STATE_CONNECTED
};

static BYTE sg_bCurrState = STATE_DRIVER_SELECTED;
static HWND sg_hOwnerWnd = nullptr;
static std::string sg_omErrStr;

/* Lock of the client buffers, used by the read thread and the threads of the application. Created with the DLL,
   so it is there whatever order the application calls the driver in. */
static struct SDriverLock
{
    CRITICAL_SECTION m_sCS;
    SDriverLock()
    {
        InitializeCriticalSection(&m_sCS);
    }
    ~SDriverLock()
    {
        DeleteCriticalSection(&m_sCS);
    }
} sg_sLock;

class CAutoLock
{
public:
    CAutoLock()
    {
        EnterCriticalSection(&sg_sLock.m_sCS);
    }
    ~CAutoLock()
    {
        LeaveCriticalSection(&sg_sLock.m_sCS);
    }
};

/* Clients and their buffers */
struct SCLIENTBUFMAP
{
    DWORD m_dwClientID;
    CBaseCANBufFSE* m_pClientBuf[MAX_BUFF_ALLOWED];
    char m_acClientName[MAX_PATH];
    UINT m_unBufCount;
    SCLIENTBUFMAP()
    {
        vClear();
    }
    void vClear()
    {
        m_dwClientID = 0;
        m_unBufCount = 0;
        memset(m_acClientName, 0, sizeof(m_acClientName));
        for (UINT i = 0; i < MAX_BUFF_ALLOWED; i++)
        {
            m_pClientBuf[i] = nullptr;
        }
    }
};

static SCLIENTBUFMAP sg_asClientToBufMap[MAX_CLIENT_ALLOWED];
static UINT sg_unClientCnt = 0;

/* PEAK channels found by PCAN-Basic */
struct SPeakHwChannel
{
    TPCANHandle m_hHandle;
    std::string m_omBaseName;       // "PCAN-USB 1"
    std::string m_omDescription;    // shown in the dialog and saved in the configuration
    std::string m_omDetails;        // shown as "Firmware" in the dialog
    unsigned long m_dwDeviceId;     // shown as "Driver ID" in the dialog
};

static SPeakHwChannel sg_asAvailable[defNO_OF_CHANNELS];
static int sg_nAvailable = 0;

/* Channels in use, in the order of the BUSMASTER channels 1..n */
struct SPeakChannel
{
    TPCANHandle m_hHandle;
    std::string m_omDescription;
    bool m_bInitialized;
    bool m_bListenOnly;
    HANDLE m_hRxEvent;
    bool m_bRxEventSet;
    unsigned char m_ucErrorCount;   // stands for the bus state, see the top of the file
    bool m_bOverrunReported;

    void vReset()
    {
        m_hHandle = PCAN_NONEBUS;
        m_omDescription.clear();
        m_bInitialized = false;
        m_bListenOnly = false;
        m_hRxEvent = nullptr;
        m_bRxEventSet = false;
        m_ucErrorCount = 0;
        m_bOverrunReported = false;
    }
};

static SPeakChannel sg_asChannel[CHANNEL_ALLOWED];
static UINT sg_nNoOfChannels = 0;

/* Channel descriptions of the last selection or of the loaded configuration, used to select the same channels
   again */
static std::string sg_aomSavedChannels[CHANNEL_ALLOWED];
static UINT sg_unSavedChannels = 0;

static SCONTROLLER_DETAILS sg_asControllerDetails[defNO_OF_CHANNELS];
static int sg_nControllerMode = defMODE_ACTIVE;   // set by CAN_SetControllerParams(HW_MODE)

/* Read thread */
static HANDLE sg_hReadThread = nullptr;
static HANDLE sg_hStopEvent = nullptr;

/* Time: BUSMASTER time stamps are in 100 us steps of QueryPerformanceCounter */
static SYSTEMTIME sg_CurrSysTime;
static UINT64 sg_TimeStamp = 0;
static LARGE_INTEGER sg_QueryTickCount;
static LONGLONG sg_llQpcFrequency = 0;
static LONGLONG sg_llHwOffsetUs = 0;      // QueryPerformanceCounter time minus hardware time, in us
static bool sg_bHwOffsetKnown = false;

/* ---------------------------------------------------------------------------------------------------------------
   PCAN-Basic, loaded at run time
   --------------------------------------------------------------------------------------------------------------- */

static HMODULE sg_hPcanBasic = nullptr;
static PFN_CAN_Initialize   pfCAN_Initialize = nullptr;
static PFN_CAN_Uninitialize pfCAN_Uninitialize = nullptr;
static PFN_CAN_GetStatus    pfCAN_GetStatus = nullptr;
static PFN_CAN_Read         pfCAN_Read = nullptr;
static PFN_CAN_Write        pfCAN_Write = nullptr;
static PFN_CAN_GetValue     pfCAN_GetValue = nullptr;
static PFN_CAN_SetValue     pfCAN_SetValue = nullptr;
static PFN_CAN_GetErrorText pfCAN_GetErrorText = nullptr;

static void vUnloadPcanBasic()
{
    if (nullptr != sg_hPcanBasic)
    {
        FreeLibrary(sg_hPcanBasic);
        sg_hPcanBasic = nullptr;
    }
    pfCAN_Initialize = nullptr;
    pfCAN_Uninitialize = nullptr;
    pfCAN_GetStatus = nullptr;
    pfCAN_Read = nullptr;
    pfCAN_Write = nullptr;
    pfCAN_GetValue = nullptr;
    pfCAN_SetValue = nullptr;
    pfCAN_GetErrorText = nullptr;
}

static bool bLoadPcanBasic()
{
    if (nullptr != sg_hPcanBasic)
    {
        return true;
    }
    sg_hPcanBasic = LoadLibrary(_T("PCANBasic.dll"));
    if (nullptr == sg_hPcanBasic)
    {
        sg_omErrStr = "PCANBasic.dll was not found. Install the PEAK device driver (it contains PCAN-Basic) "
                      "from www.peak-system.com, or put the 32-bit PCANBasic.dll next to BUSMASTER.exe.";
        return false;
    }
    pfCAN_Initialize   = (PFN_CAN_Initialize)   GetProcAddress(sg_hPcanBasic, "CAN_Initialize");
    pfCAN_Uninitialize = (PFN_CAN_Uninitialize) GetProcAddress(sg_hPcanBasic, "CAN_Uninitialize");
    pfCAN_GetStatus    = (PFN_CAN_GetStatus)    GetProcAddress(sg_hPcanBasic, "CAN_GetStatus");
    pfCAN_Read         = (PFN_CAN_Read)         GetProcAddress(sg_hPcanBasic, "CAN_Read");
    pfCAN_Write        = (PFN_CAN_Write)        GetProcAddress(sg_hPcanBasic, "CAN_Write");
    pfCAN_GetValue     = (PFN_CAN_GetValue)     GetProcAddress(sg_hPcanBasic, "CAN_GetValue");
    pfCAN_SetValue     = (PFN_CAN_SetValue)     GetProcAddress(sg_hPcanBasic, "CAN_SetValue");
    pfCAN_GetErrorText = (PFN_CAN_GetErrorText) GetProcAddress(sg_hPcanBasic, "CAN_GetErrorText");

    if ((nullptr == pfCAN_Initialize) || (nullptr == pfCAN_Uninitialize) || (nullptr == pfCAN_GetStatus) ||
            (nullptr == pfCAN_Read) || (nullptr == pfCAN_Write) || (nullptr == pfCAN_GetValue) ||
            (nullptr == pfCAN_SetValue) || (nullptr == pfCAN_GetErrorText))
    {
        vUnloadPcanBasic();
        sg_omErrStr = "The PCANBasic.dll found is not a usable PCAN-Basic library (a function is missing). "
                      "Install the current PEAK device driver from www.peak-system.com.";
        return false;
    }
    return true;
}

static std::string omGetPcanErrorText(TPCANStatus dwStatus)
{
    char acText[256] = { 0 };
    if ((nullptr != pfCAN_GetErrorText) && (pfCAN_GetErrorText(dwStatus, 0x09 /* English */, acText) == PCAN_ERROR_OK))
    {
        return acText;
    }
    sprintf_s(acText, sizeof(acText), "PCAN-Basic error 0x%X", (unsigned int)dwStatus);
    return acText;
}

/* ---------------------------------------------------------------------------------------------------------------
   Time
   --------------------------------------------------------------------------------------------------------------- */

static LONGLONG llQpcMicroseconds()
{
    LARGE_INTEGER sNow;
    QueryPerformanceCounter(&sNow);
    if (sg_llQpcFrequency <= 0)
    {
        LARGE_INTEGER sFreq;
        QueryPerformanceFrequency(&sFreq);
        sg_llQpcFrequency = sFreq.QuadPart;
    }
    return (sNow.QuadPart / sg_llQpcFrequency) * 1000000 + ((sNow.QuadPart % sg_llQpcFrequency) * 1000000) / sg_llQpcFrequency;
}

/* Time stamp (100 us) of a received frame from its hardware time stamp. Called by the read thread only.
   The PEAK driver gives its time stamps in the QueryPerformanceCounter time base (checked with PCAN-Basic 5.1 and a
   PCAN-USB: the frames are read 0.5 to 2 ms after their time stamp), so they are used as they are. Should the first
   frame show another time base, the offset between the clocks is the smallest difference seen since the connection
   instead: a frame is never read before it is received, so the smallest difference has the least delay. */
static bool sg_bHwClockIsQpc = false;

static LONGLONG llGetRxTimeStamp(const TPCANTimestamp& sTime)
{
    LONGLONG llHwUs = (LONGLONG)sTime.micros + 1000 * ((LONGLONG)sTime.millis + 0x100000000LL * (LONGLONG)sTime.millis_overflow);
    if (sg_bHwOffsetKnown && sg_bHwClockIsQpc)
    {
        return llHwUs / 100;
    }
    LONGLONG llOffset = llQpcMicroseconds() - llHwUs;
    if (!sg_bHwOffsetKnown)
    {
        // read at most 10 s after it was received: the same clock
        sg_bHwClockIsQpc = (llOffset > -10000) && (llOffset < 10000000);
        sg_bHwOffsetKnown = true;
        sg_llHwOffsetUs = llOffset;
        if (sg_bHwClockIsQpc)
        {
            return llHwUs / 100;
        }
    }
    else if (llOffset < sg_llHwOffsetUs)
    {
        sg_llHwOffsetUs = llOffset;
    }
    return (llHwUs + sg_llHwOffsetUs) / 100;
}

/* ---------------------------------------------------------------------------------------------------------------
   Clients
   --------------------------------------------------------------------------------------------------------------- */

static BOOL bGetClientObj(DWORD dwClientID, UINT& unClientIndex)
{
    for (UINT i = 0; i < sg_unClientCnt; i++)
    {
        if (sg_asClientToBufMap[i].m_dwClientID == dwClientID)
        {
            unClientIndex = i;
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL bClientIdExist(DWORD dwClientId)
{
    UINT unIndex;
    return bGetClientObj(dwClientId, unIndex);
}

static BOOL bClientExist(const char* pcClientName, INT& Index)
{
    for (UINT i = 0; i < sg_unClientCnt; i++)
    {
        if (strcmp(pcClientName, sg_asClientToBufMap[i].m_acClientName) == 0)
        {
            Index = (INT)i;
            return TRUE;
        }
    }
    return FALSE;
}

static DWORD dwGetAvailableClientSlot()
{
    DWORD nClientId = 2;
    for (INT i = 0; i < MAX_CLIENT_ALLOWED; i++)
    {
        if (!bClientIdExist(nClientId))
        {
            break;
        }
        nClientId++;
    }
    return nClientId;
}

static BOOL bRemoveClient(DWORD dwClientId)
{
    UINT unClientIndex = 0;
    if ((sg_unClientCnt == 0) || !bGetClientObj(dwClientId, unClientIndex))
    {
        return FALSE;
    }
    if ((unClientIndex + 1) < sg_unClientCnt)
    {
        sg_asClientToBufMap[unClientIndex] = sg_asClientToBufMap[sg_unClientCnt - 1];
    }
    sg_asClientToBufMap[sg_unClientCnt - 1].vClear();
    sg_unClientCnt--;
    return TRUE;
}

static BOOL bIsBufferExists(const SCLIENTBUFMAP& sClientObj, const CBaseCANBufFSE* pBuf)
{
    for (UINT i = 0; i < sClientObj.m_unBufCount; i++)
    {
        if (pBuf == sClientObj.m_pClientBuf[i])
        {
            return TRUE;
        }
    }
    return FALSE;
}

static void vRemoveClientBuffer(SCLIENTBUFMAP& sClientObj, CBaseCANBufFSE* pBuf)
{
    for (UINT i = 0; i < sClientObj.m_unBufCount; i++)
    {
        if (sClientObj.m_pClientBuf[i] == pBuf)
        {
            sClientObj.m_pClientBuf[i] = sClientObj.m_pClientBuf[sClientObj.m_unBufCount - 1];
            sClientObj.m_pClientBuf[sClientObj.m_unBufCount - 1] = nullptr;
            sClientObj.m_unBufCount--;
            break;
        }
    }
}

/* Gives a frame to the clients. A transmitted frame is Tx for the client that sent it and Rx for the others.
   The caller holds the lock. */
static void vWriteIntoClientsBuffer(STCANDATA& sCanData, DWORD dwSenderClientID = 0)
{
    for (UINT i = 0; i < sg_unClientCnt; i++)
    {
        SCLIENTBUFMAP& sClient = sg_asClientToBufMap[i];
        if ((sCanData.m_ucDataType == TX_FLAG) && (sClient.m_dwClientID != dwSenderClientID))
        {
            STCANDATA sRxCopy = sCanData;
            sRxCopy.m_ucDataType = RX_FLAG;
            for (UINT j = 0; j < sClient.m_unBufCount; j++)
            {
                sClient.m_pClientBuf[j]->WriteIntoBuffer(&sRxCopy);
            }
        }
        else
        {
            for (UINT j = 0; j < sClient.m_unBufCount; j++)
            {
                sClient.m_pClientBuf[j]->WriteIntoBuffer(&sCanData);
            }
        }
    }
}

static void vWriteErrorIntoClientsBuffer(UINT unChannelIndex, unsigned char ucErrType, unsigned char ucErrCap, LONGLONG llTimeStamp)
{
    STCANDATA sCanData;
    memset(&sCanData, 0, sizeof(sCanData));
    sCanData.m_ucDataType = ERR_FLAG;
    sCanData.m_lTickCount.QuadPart = llTimeStamp;
    sCanData.m_uDataInfo.m_sErrInfo.m_ucErrType = ucErrType;
    sCanData.m_uDataInfo.m_sErrInfo.m_ucReg_ErrCap = ucErrCap;
    sCanData.m_uDataInfo.m_sErrInfo.m_ucTxErrCount = sg_asChannel[unChannelIndex].m_ucErrorCount;
    sCanData.m_uDataInfo.m_sErrInfo.m_ucRxErrCount = 0;
    sCanData.m_uDataInfo.m_sErrInfo.m_ucChannel = (unsigned char)(unChannelIndex + 1);
    sCanData.m_uDataInfo.m_sErrInfo.m_nSubError = 0;

    CAutoLock oLock;
    vWriteIntoClientsBuffer(sCanData);
}

/* ---------------------------------------------------------------------------------------------------------------
   Channels
   --------------------------------------------------------------------------------------------------------------- */

static void vAddIfPresent(TPCANHandle hHandle, const char* pcType, int nNumber)
{
    if (sg_nAvailable >= defNO_OF_CHANNELS)
    {
        return;
    }
    DWORD dwCondition = PCAN_CHANNEL_UNAVAILABLE;
    if ((pfCAN_GetValue(hHandle, PCAN_CHANNEL_CONDITION, &dwCondition, sizeof(dwCondition)) != PCAN_ERROR_OK) ||
            (dwCondition == PCAN_CHANNEL_UNAVAILABLE))
    {
        return;
    }

    SPeakHwChannel& sChannel = sg_asAvailable[sg_nAvailable++];
    char acText[MAX_LENGTH_VERSION_STRING] = { 0 };

    sChannel.m_hHandle = hHandle;
    sprintf_s(acText, sizeof(acText), "%s %d", pcType, nNumber);
    sChannel.m_omBaseName = acText;
    sChannel.m_omDescription = acText;

    // The hardware name (PCAN-USB, PCAN-USB Pro FD, ...) is not given by every version for a channel not in use
    char acHwName[MAX_LENGTH_HARDWARE_NAME + 1] = { 0 };
    if ((pfCAN_GetValue(hHandle, PCAN_HARDWARE_NAME, acHwName, MAX_LENGTH_HARDWARE_NAME) == PCAN_ERROR_OK) && (acHwName[0] != '\0'))
    {
        sChannel.m_omDescription += " - ";
        sChannel.m_omDescription += acHwName;
    }
    // The dialog uses the description as a format string
    std::replace(sChannel.m_omDescription.begin(), sChannel.m_omDescription.end(), '%', ' ');

    // "Driver ID" of the dialog: the device ID set with PCAN-View, which tells two interfaces apart
    DWORD dwDeviceId = 0;
    sChannel.m_dwDeviceId = (pfCAN_GetValue(hHandle, PCAN_DEVICE_ID, &dwDeviceId, sizeof(dwDeviceId)) == PCAN_ERROR_OK) ? dwDeviceId : 0;

    // "Firmware" of the dialog. PCAN-Basic gives it for an initialised channel only: a channel that no other
    // application uses is opened for a moment in listen-only mode, which never drives the bus.
    sChannel.m_omDetails.clear();
    if (dwCondition == PCAN_CHANNEL_AVAILABLE)
    {
        DWORD dwOn = PCAN_PARAMETER_ON;
        DWORD dwOff = PCAN_PARAMETER_OFF;
        pfCAN_SetValue(hHandle, PCAN_LISTEN_ONLY, &dwOn, sizeof(dwOn));
        if (pfCAN_Initialize(hHandle, PCAN_BAUD_500K, 0, 0, 0) == PCAN_ERROR_OK)
        {
            char acFirmware[MAX_LENGTH_VERSION_STRING] = { 0 };
            if (pfCAN_GetValue(hHandle, PCAN_FIRMWARE_VERSION, acFirmware, sizeof(acFirmware)) == PCAN_ERROR_OK)
            {
                sChannel.m_omDetails = acFirmware;
            }
            pfCAN_Uninitialize(hHandle);
        }
        pfCAN_SetValue(hHandle, PCAN_LISTEN_ONLY, &dwOff, sizeof(dwOff));
    }
    else if ((dwCondition & PCAN_CHANNEL_AVAILABLE) == 0)
    {
        sChannel.m_omDetails = "In use by another application";
    }
}

/* Lists the PEAK channels PCAN-Basic knows */
static void vEnumerateChannels()
{
    sg_nAvailable = 0;
    for (int i = 1; i <= 16; i++)
    {
        vAddIfPresent((TPCANHandle)((i <= 8) ? (PCAN_USBBUS1 + i - 1) : (PCAN_USBBUS9 + i - 9)), "PCAN-USB", i);
    }
    for (int i = 1; i <= 16; i++)
    {
        vAddIfPresent((TPCANHandle)((i <= 8) ? (PCAN_PCIBUS1 + i - 1) : (PCAN_PCIBUS9 + i - 9)), "PCAN-PCI", i);
    }
    for (int i = 1; i <= 16; i++)
    {
        vAddIfPresent((TPCANHandle)(PCAN_LANBUS1 + i - 1), "PCAN-LAN", i);
    }
    for (int i = 1; i <= 2; i++)
    {
        vAddIfPresent((TPCANHandle)(PCAN_PCCBUS1 + i - 1), "PCAN-PC Card", i);
    }
}

/* Index in sg_asAvailable of a channel saved with its description, -1 if it is not there. The hardware name may be
   missing from the description, so the channel name alone ("PCAN-USB 1") is compared when there is no exact match. */
static int nFindAvailableChannel(const std::string& omDescription)
{
    for (int i = 0; i < sg_nAvailable; i++)
    {
        if (sg_asAvailable[i].m_omDescription == omDescription)
        {
            return i;
        }
    }
    std::string omBaseName = omDescription.substr(0, omDescription.find(" - "));
    for (int i = 0; i < sg_nAvailable; i++)
    {
        if (sg_asAvailable[i].m_omBaseName == omBaseName)
        {
            return i;
        }
    }
    return -1;
}

static void vSetChannels(const int* pnAvailableIndex, UINT unCount)
{
    sg_nNoOfChannels = 0;
    for (UINT i = 0; (i < unCount) && (i < CHANNEL_ALLOWED); i++)
    {
        const SPeakHwChannel& sHw = sg_asAvailable[pnAvailableIndex[i]];
        sg_asChannel[i].vReset();
        sg_asChannel[i].m_hHandle = sHw.m_hHandle;
        sg_asChannel[i].m_omDescription = sHw.m_omDescription;
        sg_aomSavedChannels[i] = sHw.m_omDescription;
        sg_nNoOfChannels++;
    }
    sg_unSavedChannels = sg_nNoOfChannels;
}

/* BTR0/BTR1 of the bit rate of a channel */
static TPCANBaudrate wGetBtr0Btr1(const SCONTROLLER_DETAILS& sDetails)
{
    double dBaudRate = atof(sDetails.m_omStrBaudrate.c_str());
    long lBaudRate = (long)(dBaudRate + 0.5);
    switch (lBaudRate)
    {
        case 1000000: return PCAN_BAUD_1M;
        case 800000:  return PCAN_BAUD_800K;
        case 500000:  return PCAN_BAUD_500K;
        case 250000:  return PCAN_BAUD_250K;
        case 125000:  return PCAN_BAUD_125K;
        case 100000:  return PCAN_BAUD_100K;
        case 50000:   return PCAN_BAUD_50K;
        case 20000:   return PCAN_BAUD_20K;
        case 10000:   return PCAN_BAUD_10K;
        case 5000:    return PCAN_BAUD_5K;
        default:      break;
    }
    if (dBaudRate > 0)
    {
        sBTRTemp asBTRValues[defREG_VALUE_LIST_COUNT_MAX];
        if (TRUE == CUtilFunctions::nCalculateCANChannelParameters(asBTRValues, dBaudRate, 16, 1))
        {
            return (TPCANBaudrate)((asBTRValues[0].uBTRReg0.ucBTR0 << 8) | asBTRValues[0].uBTRReg1.ucBTR1);
        }
    }
    if (sDetails.m_nBTR0BTR1 != 0)
    {
        return (TPCANBaudrate)sDetails.m_nBTR0BTR1;
    }
    return PCAN_BAUD_500K;
}

static bool bIsListenOnly(UINT unChannelIndex)
{
    const SCONTROLLER_DETAILS& sDetails = sg_asControllerDetails[unChannelIndex];
    return (sg_nControllerMode == defMODE_PASSIVE) || (sDetails.m_ucControllerMode == defMODE_PASSIVE) || (sDetails.m_bPassiveMode != 0);
}

static void vCloseChannels()
{
    for (UINT i = 0; i < sg_nNoOfChannels; i++)
    {
        SPeakChannel& sChannel = sg_asChannel[i];
        if (sChannel.m_bInitialized)
        {
            if (sChannel.m_bRxEventSet)
            {
                HANDLE hNone = nullptr;
                pfCAN_SetValue(sChannel.m_hHandle, PCAN_RECEIVE_EVENT, &hNone, sizeof(hNone));
            }
            pfCAN_Uninitialize(sChannel.m_hHandle);
        }
        if (nullptr != sChannel.m_hRxEvent)
        {
            CloseHandle(sChannel.m_hRxEvent);
        }
        sChannel.m_bInitialized = false;
        sChannel.m_hRxEvent = nullptr;
        sChannel.m_bRxEventSet = false;
    }
}

/* ---------------------------------------------------------------------------------------------------------------
   Reception
   --------------------------------------------------------------------------------------------------------------- */

/* Reads the frames of a channel. Returns true when it stopped with frames left, so that the caller goes on. */
static bool bReadChannel(UINT unChannelIndex)
{
    SPeakChannel& sChannel = sg_asChannel[unChannelIndex];
    int nErrorsInRow = 0;
    for (int nRead = 0; nRead < 256; nRead++)
    {
        TPCANMsg sMsg;
        TPCANTimestamp sTime;
        TPCANStatus dwStatus = pfCAN_Read(sChannel.m_hHandle, &sMsg, &sTime);
        if (dwStatus & PCAN_ERROR_QRCVEMPTY)
        {
            return false;
        }
        if (dwStatus & (PCAN_ERROR_OVERRUN | PCAN_ERROR_QOVERRUN))
        {
            if (!sChannel.m_bOverrunReported)
            {
                sChannel.m_bOverrunReported = true;
                vWriteErrorIntoClientsBuffer(unChannelIndex,
                                             (dwStatus & PCAN_ERROR_QOVERRUN) ? ERROR_DRIVER_BUFF_OVERFLOW : ERROR_DEVICE_BUFF_OVERFLOW,
                                             0, llQpcMicroseconds() / 100);
            }
            if (++nErrorsInRow >= 8)
            {
                return false;
            }
            continue;
        }
        if (dwStatus != PCAN_ERROR_OK)
        {
            // A change of the bus state (vPollBusState reads it with CAN_GetStatus): the frame is not used. Stop
            // if the same error comes again and again, the next turn tries again.
            if (++nErrorsInRow >= 8)
            {
                return false;
            }
            continue;
        }
        nErrorsInRow = 0;
        sChannel.m_bOverrunReported = false;

        if (sMsg.MSGTYPE & (PCAN_MESSAGE_STATUS | PCAN_MESSAGE_ECHO | PCAN_MESSAGE_FD))
        {
            continue;
        }
        if (sMsg.MSGTYPE & PCAN_MESSAGE_ERRFRAME)
        {
            vWriteErrorIntoClientsBuffer(unChannelIndex, ERROR_BUS, OTHER_ERROR_RX, llGetRxTimeStamp(sTime));
            continue;
        }

        STCANDATA sCanData;
        memset(&sCanData, 0, sizeof(sCanData));
        sCanData.m_ucDataType = RX_FLAG;
        sCanData.m_lTickCount.QuadPart = llGetRxTimeStamp(sTime);
        STCAN_MSG& sCanMsg = sCanData.m_uDataInfo.m_sCANMsg;
        sCanMsg.m_unMsgID = sMsg.ID;
        sCanMsg.m_ucEXTENDED = (sMsg.MSGTYPE & PCAN_MESSAGE_EXTENDED) ? 1 : 0;
        sCanMsg.m_ucRTR = (sMsg.MSGTYPE & PCAN_MESSAGE_RTR) ? 1 : 0;
        sCanMsg.m_ucDataLen = (sMsg.LEN > 8) ? 8 : sMsg.LEN;
        sCanMsg.m_ucChannel = (unsigned char)(unChannelIndex + 1);
        sCanMsg.m_bCANFD = false;
        memcpy(sCanMsg.m_ucData, sMsg.DATA, sCanMsg.m_ucDataLen);

        CAutoLock oLock;
        vWriteIntoClientsBuffer(sCanData);
    }
    return true;
}

static void vPollBusState(UINT unChannelIndex)
{
    SPeakChannel& sChannel = sg_asChannel[unChannelIndex];
    TPCANStatus dwStatus = pfCAN_GetStatus(sChannel.m_hHandle);
    unsigned char ucErrorCount = 0;
    if (dwStatus & PCAN_ERROR_BUSOFF)
    {
        ucErrorCount = 255;
    }
    else if (dwStatus & PCAN_ERROR_BUSPASSIVE)
    {
        ucErrorCount = 128;
    }
    else if (dwStatus & (PCAN_ERROR_BUSHEAVY | PCAN_ERROR_BUSLIGHT))
    {
        ucErrorCount = 96;
    }
    sChannel.m_ucErrorCount = ucErrorCount;
}

static DWORD WINAPI dwReadThreadProc(LPVOID /*pParam*/)
{
    HANDLE ahWait[CHANNEL_ALLOWED + 1];
    DWORD dwWaitCount = 0;
    bool bAllEvents = true;
    ahWait[dwWaitCount++] = sg_hStopEvent;
    for (UINT i = 0; i < sg_nNoOfChannels; i++)
    {
        if (sg_asChannel[i].m_bRxEventSet)
        {
            ahWait[dwWaitCount++] = sg_asChannel[i].m_hRxEvent;
        }
        else
        {
            bAllEvents = false;
        }
    }
    // Without a receive event a channel is read every 5 ms; the bus state is read every 100 ms
    const DWORD dwTimeout = bAllEvents ? 100 : 5;
    DWORD dwLastPoll = GetTickCount() - 100;
    // The first turn reads at once: frames received before the receive events were set do not signal them
    DWORD dwWait = 0;

    for (;;)
    {
        DWORD dwResult = WaitForMultipleObjects(dwWaitCount, ahWait, FALSE, dwWait);
        dwWait = dwTimeout;
        if (dwResult == WAIT_OBJECT_0)
        {
            break;
        }
        if (dwResult == WAIT_FAILED)
        {
            Sleep(10);
        }

        bool bMore;
        do
        {
            bMore = false;
            for (UINT i = 0; i < sg_nNoOfChannels; i++)
            {
                bMore |= bReadChannel(i);
            }
        }
        while (bMore && (WaitForSingleObject(sg_hStopEvent, 0) != WAIT_OBJECT_0));

        DWORD dwNow = GetTickCount();
        if ((DWORD)(dwNow - dwLastPoll) >= 100)
        {
            dwLastPoll = dwNow;
            for (UINT i = 0; i < sg_nNoOfChannels; i++)
            {
                vPollBusState(i);
            }
        }
    }
    return 0;
}

/* ---------------------------------------------------------------------------------------------------------------
   Driver interface
   --------------------------------------------------------------------------------------------------------------- */

class CDIL_CAN_PEAK_USB : public CBaseDIL_CAN_Controller
{
public:
    HRESULT CAN_PerformInitOperations(void);
    HRESULT CAN_PerformClosureOperations(void);
    HRESULT CAN_GetTimeModeMapping(SYSTEMTIME& CurrSysTime, UINT64& TimeStamp, LARGE_INTEGER& QueryTickCount);
    HRESULT CAN_ListHwInterfaces(INTERFACE_HW_LIST& sSelHwInterface, INT& nCount, PSCONTROLLER_DETAILS InitData);
    HRESULT CAN_SelectHwInterface(const INTERFACE_HW_LIST& sSelHwInterface, INT nCount);
    HRESULT CAN_DeselectHwInterface(void);
    HRESULT CAN_SetConfigData(PSCONTROLLER_DETAILS ConfigFile, int Length);
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

static CDIL_CAN_PEAK_USB* sg_pouDIL_CAN_PEAK_USB = nullptr;

USAGEMODE HRESULT GetIDIL_CAN_Controller(void** ppvInterface)
{
    if (nullptr == sg_pouDIL_CAN_PEAK_USB)
    {
        sg_pouDIL_CAN_PEAK_USB = new CDIL_CAN_PEAK_USB;
    }
    *ppvInterface = (void*)sg_pouDIL_CAN_PEAK_USB;
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_LoadDriverLibrary(void)
{
    // PCANBasic.dll is loaded when it is first needed, so that the reason of a failure can be shown then
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_UnloadDriverLibrary(void)
{
    if (sg_bCurrState == STATE_CONNECTED)
    {
        CAN_StopHardware();
    }
    vUnloadPcanBasic();
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_SetAppParams(HWND hWndOwner)
{
    sg_hOwnerWnd = hWndOwner;
    GetLocalTime(&sg_CurrSysTime);
    sg_TimeStamp = 0;
    sg_QueryTickCount.QuadPart = 0;
    sg_omErrStr.clear();
    CAN_ManageMsgBuf(MSGBUF_CLEAR, 0, nullptr);
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_PerformInitOperations(void)
{
    for (UINT i = 0; i < CHANNEL_ALLOWED; i++)
    {
        sg_asChannel[i].vReset();
    }
    sg_nControllerMode = defMODE_ACTIVE;
    DWORD dwClientID = 0;
    CAN_RegisterClient(TRUE, dwClientID, CAN_MONITOR_NODE);
    sg_bCurrState = STATE_DRIVER_SELECTED;
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_PerformClosureOperations(void)
{
    if (sg_bCurrState == STATE_CONNECTED)
    {
        CAN_StopHardware();
    }
    {
        CAutoLock oLock;
        while (sg_unClientCnt > 0)
        {
            bRemoveClient(sg_asClientToBufMap[0].m_dwClientID);
        }
    }
    sg_bCurrState = STATE_DRIVER_SELECTED;
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_GetTimeModeMapping(SYSTEMTIME& CurrSysTime, UINT64& TimeStamp, LARGE_INTEGER& QueryTickCount)
{
    CurrSysTime = sg_CurrSysTime;
    TimeStamp = sg_TimeStamp;
    QueryTickCount = sg_QueryTickCount;
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_ListHwInterfaces(INTERFACE_HW_LIST& asSelHwInterface, INT& nCount, PSCONTROLLER_DETAILS InitData)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    if (sg_bCurrState == STATE_CONNECTED)
    {
        return ERR_IMPROPER_STATE;
    }
    if (!bLoadPcanBasic())
    {
        ::MessageBox(sg_hOwnerWnd, sg_omErrStr.c_str(), "BUSMASTER-NG - PEAK CAN", MB_OK | MB_ICONWARNING);
        nCount = 0;
        return ERR_LOAD_DRIVER;
    }
    vEnumerateChannels();
    if (sg_nAvailable == 0)
    {
        sg_omErrStr = "No PEAK CAN channel was found. Check that the PEAK interface is connected and that its "
                      "device driver is installed.";
        ::MessageBox(sg_hOwnerWnd, sg_omErrStr.c_str(), "BUSMASTER-NG - PEAK CAN", MB_OK | MB_ICONWARNING);
        nCount = 0;
        return NO_HW_INTERFACE;
    }

    // The channels of the last selection or of the configuration, in their order
    int anSelected[defNO_OF_CHANNELS];
    for (int i = 0; i < defNO_OF_CHANNELS; i++)
    {
        anSelected[i] = -1;
    }
    UINT unSelected = 0;
    bool bAllFound = true;      // the first nCount saved channels are all there
    for (UINT i = 0; (i < sg_unSavedChannels) && (unSelected < CHANNEL_ALLOWED); i++)
    {
        int nIndex = nFindAvailableChannel(sg_aomSavedChannels[i]);
        if ((nIndex >= 0) && (std::find(anSelected, anSelected + unSelected, nIndex) == anSelected + unSelected))
        {
            anSelected[unSelected++] = nIndex;
        }
        else if ((INT)i < nCount)
        {
            bAllFound = false;
        }
    }

    // nCount > 0: the channel count of a loaded configuration. When its channels are all there they are used
    // without asking.
    bool bUseSaved = (nCount > 0) && ((UINT)nCount <= sg_unSavedChannels) && bAllFound;
    if (bUseSaved)
    {
        unSelected = (UINT)nCount;
    }
    else
    {
        INTERFACE_HW asHwInterface[defNO_OF_CHANNELS];
        for (int i = 0; i < sg_nAvailable; i++)
        {
            asHwInterface[i].m_dwIdInterface = sg_asAvailable[i].m_dwDeviceId;   // only shown by the dialog
            asHwInterface[i].m_dwVendor = 0;
            asHwInterface[i].m_bytNetworkID = 0;
            asHwInterface[i].m_acNameInterface = sg_asAvailable[i].m_omDescription;
            asHwInterface[i].m_acDescription = sg_asAvailable[i].m_omDescription;
            asHwInterface[i].m_acDeviceName = sg_asAvailable[i].m_omDetails;
        }

        CWnd objMainWnd;
        objMainWnd.Attach(sg_hOwnerWnd);
        CPeakCanBaudRateCheck ouBaudRateCheck;
        CHardwareListingCAN ouHwList(asHwInterface, sg_nAvailable, anSelected, CAN, CHANNEL_ALLOWED, &objMainWnd, InitData, &ouBaudRateCheck);
        INT_PTR nRet = ouHwList.DoModal();
        objMainWnd.Detach();
        if (nRet != IDOK)
        {
            return HW_INTERFACE_NO_SEL;
        }
        unSelected = (UINT)ouHwList.nGetSelectedList(anSelected);
    }
    if (unSelected > CHANNEL_ALLOWED)
    {
        unSelected = CHANNEL_ALLOWED;
    }
    vSetChannels(anSelected, unSelected);

    nCount = (INT)sg_nNoOfChannels;
    for (UINT i = 0; i < sg_nNoOfChannels; i++)
    {
        asSelHwInterface[i].m_dwIdInterface = i;
        asSelHwInterface[i].m_dwVendor = 0;
        asSelHwInterface[i].m_acNameInterface = sg_asChannel[i].m_omDescription;
        asSelHwInterface[i].m_acDescription = sg_asChannel[i].m_omDescription;
        asSelHwInterface[i].m_acDeviceName = sg_asAvailable[anSelected[i]].m_omDetails;
    }
    sg_bCurrState = STATE_HW_INTERFACE_LISTED;
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_SelectHwInterface(const INTERFACE_HW_LIST& /*asSelHwInterface*/, INT /*nCount*/)
{
    VALIDATE_VALUE_RETURN_VAL(sg_bCurrState, STATE_HW_INTERFACE_LISTED, ERR_IMPROPER_STATE);
    sg_bCurrState = STATE_HW_INTERFACE_SELECTED;
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_DeselectHwInterface(void)
{
    VALIDATE_VALUE_RETURN_VAL(sg_bCurrState, STATE_HW_INTERFACE_SELECTED, ERR_IMPROPER_STATE);
    sg_bCurrState = STATE_HW_INTERFACE_LISTED;
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_SetHardwareChannel(PSCONTROLLER_DETAILS psControllerDetails, DWORD /*dwDriverId*/, bool bIsHardwareListed, unsigned int unChannelCount)
{
    if (nullptr == psControllerDetails)
    {
        return S_FALSE;
    }
    // Remember the channels of the configuration, in their order, so that the next listing selects them
    UINT unSaved = 0;
    for (UINT i = 0; (i < unChannelCount) && (i < CHANNEL_ALLOWED); i++)
    {
        const std::string& omDesc = psControllerDetails[i].m_omHardwareDesc;
        if (omDesc.empty() || (omDesc == "Simulation"))
        {
            break;
        }
        sg_aomSavedChannels[unSaved++] = omDesc;
    }
    sg_unSavedChannels = unSaved;

    // After a configuration is loaded, the channels already listed follow it at once
    if (bIsHardwareListed && (sg_bCurrState != STATE_CONNECTED) && (sg_bCurrState != STATE_DRIVER_SELECTED) && (sg_nAvailable > 0))
    {
        int anSelected[CHANNEL_ALLOWED];
        UINT unSelected = 0;
        for (UINT i = 0; i < unSaved; i++)
        {
            int nIndex = nFindAvailableChannel(sg_aomSavedChannels[i]);
            if ((nIndex >= 0) && (std::find(anSelected, anSelected + unSelected, nIndex) == anSelected + unSelected))
            {
                anSelected[unSelected++] = nIndex;
            }
        }
        if (unSelected == 0)
        {
            return S_FALSE;
        }
        vSetChannels(anSelected, unSelected);
    }
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_SetConfigData(PSCONTROLLER_DETAILS ConfigFile, int Length)
{
    if (nullptr == ConfigFile)
    {
        return S_FALSE;
    }
    // Some callers give the size in bytes of the whole array: it always has defNO_OF_CHANNELS entries
    int nEntries = ((Length <= 0) || (Length > defNO_OF_CHANNELS)) ? defNO_OF_CHANNELS : Length;
    for (int i = 0; i < nEntries; i++)
    {
        if ((UINT)i < sg_nNoOfChannels)
        {
            ConfigFile[i].m_omHardwareDesc = sg_asChannel[i].m_omDescription;
        }
        sg_asControllerDetails[i] = ConfigFile[i];
    }
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_StartHardware(void)
{
    VALIDATE_VALUE_RETURN_VAL(sg_bCurrState, STATE_HW_INTERFACE_SELECTED, ERR_IMPROPER_STATE);

    sg_omErrStr.clear();
    if (!bLoadPcanBasic())
    {
        return ERR_LOAD_DRIVER;
    }
    if (sg_nNoOfChannels == 0)
    {
        sg_omErrStr = "No PEAK CAN channel is selected.";
        return NO_HW_INTERFACE;
    }

    for (UINT i = 0; i < sg_nNoOfChannels; i++)
    {
        SPeakChannel& sChannel = sg_asChannel[i];
        TPCANBaudrate wBtr0Btr1 = wGetBtr0Btr1(sg_asControllerDetails[i]);
        sChannel.m_bListenOnly = bIsListenOnly(i);
        sChannel.m_ucErrorCount = 0;
        sChannel.m_bOverrunReported = false;

        // Listen-only is set before the initialisation; some versions accept it only afterwards
        DWORD dwListenOnly = sChannel.m_bListenOnly ? PCAN_PARAMETER_ON : PCAN_PARAMETER_OFF;
        TPCANStatus dwListenStatus = pfCAN_SetValue(sChannel.m_hHandle, PCAN_LISTEN_ONLY, &dwListenOnly, sizeof(dwListenOnly));

        TPCANStatus dwStatus = pfCAN_Initialize(sChannel.m_hHandle, wBtr0Btr1, 0, 0, 0);
        if (dwStatus != PCAN_ERROR_OK)
        {
            sg_omErrStr = sChannel.m_omDescription + ": " + omGetPcanErrorText(dwStatus);
            vCloseChannels();
            return ERR_LOAD_HW_INTERFACE;
        }
        sChannel.m_bInitialized = true;

        if (dwListenStatus != PCAN_ERROR_OK)
        {
            dwListenStatus = pfCAN_SetValue(sChannel.m_hHandle, PCAN_LISTEN_ONLY, &dwListenOnly, sizeof(dwListenOnly));
            if ((dwListenStatus != PCAN_ERROR_OK) && sChannel.m_bListenOnly)
            {
                sg_omErrStr = sChannel.m_omDescription + ": listen-only mode cannot be set (" + omGetPcanErrorText(dwListenStatus) + ")";
                vCloseChannels();
                return ERR_LOAD_HW_INTERFACE;
            }
        }

        sChannel.m_hRxEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (nullptr != sChannel.m_hRxEvent)
        {
            sChannel.m_bRxEventSet = (pfCAN_SetValue(sChannel.m_hHandle, PCAN_RECEIVE_EVENT, &sChannel.m_hRxEvent, sizeof(sChannel.m_hRxEvent)) == PCAN_ERROR_OK);
        }
    }

    // Time reference of the connection
    GetLocalTime(&sg_CurrSysTime);
    QueryPerformanceCounter(&sg_QueryTickCount);
    sg_TimeStamp = (UINT64)(llQpcMicroseconds() / 100);
    sg_bHwOffsetKnown = false;

    sg_hStopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (nullptr != sg_hStopEvent)
    {
        sg_hReadThread = CreateThread(nullptr, 0, dwReadThreadProc, nullptr, 0, nullptr);
    }
    if (nullptr == sg_hReadThread)
    {
        sg_omErrStr = "The receive thread of the PEAK driver could not be started.";
        if (nullptr != sg_hStopEvent)
        {
            CloseHandle(sg_hStopEvent);
            sg_hStopEvent = nullptr;
        }
        vCloseChannels();
        return S_FALSE;
    }

    sg_bCurrState = STATE_CONNECTED;
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_StopHardware(void)
{
    VALIDATE_VALUE_RETURN_VAL(sg_bCurrState, STATE_CONNECTED, ERR_IMPROPER_STATE);

    if (nullptr != sg_hReadThread)
    {
        SetEvent(sg_hStopEvent);
        if (WaitForSingleObject(sg_hReadThread, 3000) != WAIT_OBJECT_0)
        {
            TerminateThread(sg_hReadThread, 0);
        }
        CloseHandle(sg_hReadThread);
        sg_hReadThread = nullptr;
    }
    if (nullptr != sg_hStopEvent)
    {
        CloseHandle(sg_hStopEvent);
        sg_hStopEvent = nullptr;
    }
    vCloseChannels();
    for (UINT i = 0; i < sg_nNoOfChannels; i++)
    {
        sg_asChannel[i].m_ucErrorCount = 0;
    }
    sg_bCurrState = STATE_HW_INTERFACE_SELECTED;
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_GetCurrStatus(STATUSMSG& StatusData)
{
    StatusData.wControllerStatus = NORMAL_ACTIVE;
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_SendMsg(DWORD dwClientID, const STCAN_MSG& sCanTxMsg)
{
    VALIDATE_VALUE_RETURN_VAL(sg_bCurrState, STATE_CONNECTED, ERR_IMPROPER_STATE);

    {
        CAutoLock oLock;
        if (!bClientIdExist(dwClientID))
        {
            return ERR_NO_CLIENT_EXIST;
        }
    }
    if ((sCanTxMsg.m_ucChannel < 1) || (sCanTxMsg.m_ucChannel > sg_nNoOfChannels))
    {
        return ERR_INVALID_CHANNEL;
    }
    if (sCanTxMsg.m_bCANFD || (sCanTxMsg.m_ucDataLen > 8))
    {
        sg_omErrStr = "The PEAK driver sends classic CAN frames only (0 to 8 data bytes).";
        return S_FALSE;
    }

    const SPeakChannel& sChannel = sg_asChannel[sCanTxMsg.m_ucChannel - 1];
    if (sChannel.m_bListenOnly)
    {
        sg_omErrStr = sChannel.m_omDescription + " is in listen-only mode.";
        return S_FALSE;
    }

    TPCANMsg sMsg;
    memset(&sMsg, 0, sizeof(sMsg));
    sMsg.ID = sCanTxMsg.m_unMsgID;
    sMsg.MSGTYPE = (TPCANMessageType)((sCanTxMsg.m_ucEXTENDED ? PCAN_MESSAGE_EXTENDED : PCAN_MESSAGE_STANDARD) |
                                      (sCanTxMsg.m_ucRTR ? PCAN_MESSAGE_RTR : 0));
    sMsg.LEN = sCanTxMsg.m_ucDataLen;
    memcpy(sMsg.DATA, sCanTxMsg.m_ucData, sMsg.LEN);

    TPCANStatus dwStatus = pfCAN_Write(sChannel.m_hHandle, &sMsg);
    if (dwStatus != PCAN_ERROR_OK)
    {
        sg_omErrStr = sChannel.m_omDescription + ": " + omGetPcanErrorText(dwStatus);
        return S_FALSE;
    }

    STCANDATA sCanData;
    memset(&sCanData, 0, sizeof(sCanData));
    sCanData.m_ucDataType = TX_FLAG;
    sCanData.m_lTickCount.QuadPart = llQpcMicroseconds() / 100;
    sCanData.m_uDataInfo.m_sCANMsg = sCanTxMsg;
    sCanData.m_uDataInfo.m_sCANMsg.m_bCANFD = false;

    CAutoLock oLock;
    vWriteIntoClientsBuffer(sCanData, dwClientID);
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_GetLastErrorString(std::string& acErrorStr)
{
    acErrorStr = sg_omErrStr;
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_GetControllerParams(LONG& lParam, UINT nChannel, ECONTR_PARAM eContrParam)
{
    HRESULT hResult = S_OK;
    switch (eContrParam)
    {
        case NUMBER_HW:
        case NUMBER_CONNECTED_HW:
            lParam = (LONG)sg_nNoOfChannels;
            break;
        case DRIVER_STATUS:
            lParam = (nullptr != sg_hPcanBasic) ? TRUE : FALSE;
            break;
        case HW_MODE:
            if (nChannel < sg_nNoOfChannels)
            {
                lParam = bIsListenOnly(nChannel) ? defMODE_PASSIVE : defMODE_ACTIVE;
            }
            else
            {
                lParam = defCONTROLLER_BUSOFF + 1;   // unknown
            }
            break;
        case CON_TEST:
        {
            lParam = FALSE;
            DWORD dwCondition = PCAN_CHANNEL_UNAVAILABLE;
            if ((nChannel < sg_nNoOfChannels) && (nullptr != pfCAN_GetValue) &&
                    (pfCAN_GetValue(sg_asChannel[nChannel].m_hHandle, PCAN_CHANNEL_CONDITION, &dwCondition, sizeof(dwCondition)) == PCAN_ERROR_OK))
            {
                lParam = (dwCondition != PCAN_CHANNEL_UNAVAILABLE) ? TRUE : FALSE;
            }
            break;
        }
        default:
            hResult = S_FALSE;
            break;
    }
    return hResult;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_SetControllerParams(int nValue, ECONTR_PARAM eContrparam)
{
    if ((eContrparam == HW_MODE) && ((nValue == defMODE_ACTIVE) || (nValue == defMODE_PASSIVE)))
    {
        sg_nControllerMode = nValue;
        if (sg_bCurrState == STATE_CONNECTED)
        {
            for (UINT i = 0; i < sg_nNoOfChannels; i++)
            {
                bool bListenOnly = bIsListenOnly(i);
                DWORD dwListenOnly = bListenOnly ? PCAN_PARAMETER_ON : PCAN_PARAMETER_OFF;
                if (pfCAN_SetValue(sg_asChannel[i].m_hHandle, PCAN_LISTEN_ONLY, &dwListenOnly, sizeof(dwListenOnly)) == PCAN_ERROR_OK)
                {
                    sg_asChannel[i].m_bListenOnly = bListenOnly;
                }
            }
        }
    }
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_GetErrorCount(SERROR_CNT& sErrorCnt, UINT nChannel, ECONTR_PARAM /*eContrParam*/)
{
    if (nChannel >= sg_nNoOfChannels)
    {
        return ERR_INVALID_CHANNEL;
    }
    sErrorCnt.m_ucTxErrCount = (sg_bCurrState == STATE_CONNECTED) ? sg_asChannel[nChannel].m_ucErrorCount : 0;
    sErrorCnt.m_ucRxErrCount = 0;
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_ManageMsgBuf(BYTE bAction, DWORD ClientID, CBaseCANBufFSE* pBufObj)
{
    CAutoLock oLock;
    if (ClientID == 0)
    {
        if (bAction == MSGBUF_CLEAR)
        {
            for (UINT i = 0; i < sg_unClientCnt; i++)
            {
                SCLIENTBUFMAP& sClient = sg_asClientToBufMap[i];
                for (UINT j = 0; j < MAX_BUFF_ALLOWED; j++)
                {
                    sClient.m_pClientBuf[j] = nullptr;
                }
                sClient.m_unBufCount = 0;
            }
        }
        return S_OK;
    }

    UINT unClientIndex = 0;
    if (!bGetClientObj(ClientID, unClientIndex))
    {
        return ERR_NO_CLIENT_EXIST;
    }
    SCLIENTBUFMAP& sClient = sg_asClientToBufMap[unClientIndex];
    if (bAction == MSGBUF_ADD)
    {
        if ((nullptr == pBufObj) || (sClient.m_unBufCount >= MAX_BUFF_ALLOWED))
        {
            return S_FALSE;
        }
        if (bIsBufferExists(sClient, pBufObj))
        {
            return ERR_BUFFER_EXISTS;
        }
        sClient.m_pClientBuf[sClient.m_unBufCount++] = pBufObj;
        return S_OK;
    }
    if (bAction == MSGBUF_CLEAR)
    {
        if (nullptr != pBufObj)
        {
            vRemoveClientBuffer(sClient, pBufObj);
        }
        else
        {
            for (UINT j = 0; j < MAX_BUFF_ALLOWED; j++)
            {
                sClient.m_pClientBuf[j] = nullptr;
            }
            sClient.m_unBufCount = 0;
        }
        return S_OK;
    }
    return S_FALSE;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_RegisterClient(BOOL bRegister, DWORD& ClientID, char* pacClientName)
{
    CAutoLock oLock;
    if (!bRegister)
    {
        return bRemoveClient(ClientID) ? S_OK : ERR_NO_CLIENT_EXIST;
    }
    if (nullptr == pacClientName)
    {
        return S_FALSE;
    }
    INT Index = 0;
    if (bClientExist(pacClientName, Index))
    {
        ClientID = sg_asClientToBufMap[Index].m_dwClientID;
        return ERR_CLIENT_EXISTS;
    }
    if (sg_unClientCnt >= MAX_CLIENT_ALLOWED)
    {
        return ERR_NO_MORE_CLIENT_ALLOWED;
    }
    // The monitor node has client ID 1
    ClientID = (strcmp(pacClientName, CAN_MONITOR_NODE) == 0) ? 1 : dwGetAvailableClientSlot();
    SCLIENTBUFMAP& sClient = sg_asClientToBufMap[sg_unClientCnt];
    sClient.vClear();
    strncpy_s(sClient.m_acClientName, sizeof(sClient.m_acClientName), pacClientName, _TRUNCATE);
    sClient.m_dwClientID = ClientID;
    sg_unClientCnt++;
    return S_OK;
}

HRESULT CDIL_CAN_PEAK_USB::CAN_GetCntrlStatus(const HANDLE& /*hEvent*/, UINT& /*unCntrlStatus*/)
{
    return WARN_DUMMY_API;
}

/* ---------------------------------------------------------------------------------------------------------------
   Baud rate check of the hardware selection dialog
   --------------------------------------------------------------------------------------------------------------- */

int CPeakCanBaudRateCheck::InvokeAdavancedSettings(PSCONTROLLER_DETAILS /*pControllerDetails*/, UINT /*nCount*/, UINT /*nSelected*/)
{
    // No advanced settings: the bit rate is all PCAN-Basic needs for classic CAN
    return 0;
}

double CPeakCanBaudRateCheck::vValidateBaudRate(double dBaudRate, int /*nItem*/, UINT /*unClock*/)
{
    // 5 kbit/s is the slowest rate of PCAN-Basic, 1 Mbit/s the fastest of classic CAN
    if ((dBaudRate < 5000) || (dBaudRate > 1000000))
    {
        return 500000;
    }
    return dBaudRate;
}
