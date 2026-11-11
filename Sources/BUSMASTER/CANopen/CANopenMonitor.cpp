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
 * \file      CANopenMonitor.cpp
 * \brief     Receives the CAN frames of the bus and decodes them
 */

#include "CANopen_stdafx.h"
#include "CANopenMonitor.h"

#include "IBusMasterKernel.h"
#include "BaseDIL_CAN.h"
#include "MsgBufFSE.h"

namespace
{
/** Events kept while the window does not take them (it takes them every 100 ms) */
const size_t MAX_PENDING_EVENTS = 200000;

char sg_acClientName[] = CAN_MONITOR_NODE;
}

CCANopenMonitor::CCANopenMonitor()
    : m_pBuffer(new CMsgBufFSE<STCANDATA>())
    , m_pDil(nullptr)
    , m_dwClientId(0)
    , m_bAttached(false)
    , m_hThread(nullptr)
    , m_hStop(CreateEvent(nullptr, TRUE, FALSE, nullptr))
    , m_decoder(TICKS_PER_SECOND)
    , m_dropped(0)
{
    InitializeCriticalSection(&m_cs);
}

CCANopenMonitor::~CCANopenMonitor()
{
    Detach();
    CloseHandle(m_hStop);
    DeleteCriticalSection(&m_cs);
    delete m_pBuffer;
}

bool CCANopenMonitor::Attach(std::string& error)
{
    CBaseDIL_CAN* pDil = nullptr;
    if (DIL_GetInterface(CAN, (void**)&pDil) != S_OK || pDil == nullptr)
    {
        error = "The CAN interface of BUSMASTER is not available.";
        return false;
    }

    DWORD dwClientId = 0;
    HRESULT hResult = pDil->DILC_RegisterClient(TRUE, dwClientId, sg_acClientName);
    if (hResult != S_OK && hResult != ERR_CLIENT_EXISTS)
    {
        error = "The CANopen analysis could not register with the CAN driver.";
        return false;
    }
    hResult = pDil->DILC_ManageMsgBuf(MSGBUF_ADD, dwClientId, m_pBuffer);
    if (hResult != S_OK && hResult != ERR_BUFFER_EXISTS)
    {
        error = "The CANopen analysis could not attach its receive buffer to the CAN driver.";
        return false;
    }
    m_pDil = pDil;
    m_dwClientId = dwClientId;
    m_bAttached = true;

    if (m_hThread == nullptr)
    {
        ResetEvent(m_hStop);
        m_hThread = CreateThread(nullptr, 0, ThreadProc, this, 0, nullptr);
        if (m_hThread == nullptr)
        {
            error = "The CANopen receive thread could not start.";
            return false;
        }
    }
    return true;
}

void CCANopenMonitor::Detach()
{
    if (m_bAttached && m_pDil != nullptr)
    {
        m_pDil->DILC_ManageMsgBuf(MSGBUF_CLEAR, m_dwClientId, m_pBuffer);
    }
    m_bAttached = false;
    if (m_hThread != nullptr)
    {
        SetEvent(m_hStop);
        WaitForSingleObject(m_hThread, 5000);
        CloseHandle(m_hThread);
        m_hThread = nullptr;
    }
}

DWORD WINAPI CCANopenMonitor::ThreadProc(LPVOID pParam)
{
    CCANopenMonitor* pThis = static_cast<CCANopenMonitor*>(pParam);
    HANDLE ahWait[2] = { pThis->m_hStop, pThis->m_pBuffer->hGetNotifyingEvent() };
    for (;;)
    {
        /* the timeout also catches frames written without a notification */
        DWORD dwResult = WaitForMultipleObjects(2, ahWait, FALSE, 200);
        if (dwResult == WAIT_OBJECT_0)
        {
            break;
        }
        pThis->ReadBuffer();
    }
    return 0;
}

void CCANopenMonitor::ReadBuffer()
{
    std::vector<canopen::Event> events;
    STCANDATA sData;
    while (m_pBuffer->GetMsgCount() > 0)
    {
        if (m_pBuffer->ReadFromBuffer(&sData) != S_OK)
        {
            break;
        }
        if (!IS_A_MESSAGE(sData.m_ucDataType))
        {
            continue;   // error frames and bus states
        }
        if (s_bCapturePending.exchange(false))
        {
            /* as the message window: the time reference is read when the first message arrives */
            CaptureTimeBase();
        }
        const STCAN_MSG& sMsg = sData.m_uDataInfo.m_sCANMsg;
        canopen::Frame frame;
        frame.tick = sData.m_lTickCount.QuadPart;
        frame.channel = sMsg.m_ucChannel;
        frame.tx = IS_TX_MESSAGE(sData.m_ucDataType) != 0;
        frame.id = sMsg.m_unMsgID;
        frame.ext = sMsg.m_ucEXTENDED != 0;
        frame.rtr = sMsg.m_ucRTR != 0;
        frame.fd = sMsg.m_bCANFD;
        frame.len = sMsg.m_ucDataLen > 8 ? 8 : sMsg.m_ucDataLen;
        memcpy(frame.data, sMsg.m_ucData, frame.len);

        EnterCriticalSection(&m_cs);
        m_decoder.decode(frame, events);
        LeaveCriticalSection(&m_cs);
    }
    if (events.empty())
    {
        return;
    }

    EnterCriticalSection(&m_cs);
    m_pending.insert(m_pending.end(), std::make_move_iterator(events.begin()), std::make_move_iterator(events.end()));
    if (m_pending.size() > MAX_PENDING_EVENTS)
    {
        const size_t excess = m_pending.size() - MAX_PENDING_EVENTS;
        m_pending.erase(m_pending.begin(), m_pending.begin() + excess);
        m_dropped += excess;
    }
    LeaveCriticalSection(&m_cs);
}

void CCANopenMonitor::TakeEvents(std::vector<canopen::Event>& events, size_t& dropped)
{
    events.clear();
    EnterCriticalSection(&m_cs);
    events.swap(m_pending);
    dropped = m_dropped;
    m_dropped = 0;
    LeaveCriticalSection(&m_cs);
}

void CCANopenMonitor::GetNodes(std::vector<canopen::NodeInfo>& nodes)
{
    nodes.clear();
    EnterCriticalSection(&m_cs);
    for (const auto& item : m_decoder.nodes())
    {
        nodes.push_back(item.second);
    }
    LeaveCriticalSection(&m_cs);
}

void CCANopenMonitor::Clear()
{
    EnterCriticalSection(&m_cs);
    m_decoder.reset();
    m_pending.clear();
    m_dropped = 0;
    LeaveCriticalSection(&m_cs);
}

CCANopenMonitor::TimeBase CCANopenMonitor::s_timeBase;
std::atomic<bool> CCANopenMonitor::s_bCapturePending(false);
std::mutex CCANopenMonitor::s_timeBaseMutex;

void CCANopenMonitor::OnConnect()
{
    std::lock_guard<std::mutex> lock(s_timeBaseMutex);
    s_timeBase = TimeBase();
    s_bCapturePending = true;
}

void CCANopenMonitor::CaptureTimeBase()
{
    /* read once per connection, as the message window does (CMsgContainerCAN::InitTimeParams) */
    CBaseDIL_CAN* pDil = nullptr;
    if (DIL_GetInterface(CAN, (void**)&pDil) != S_OK || pDil == nullptr)
    {
        return;
    }
    SYSTEMTIME sConnectTime = {};
    UINT64 unConnectTick = 0;
    LARGE_INTEGER lnQueryTick = {};
    TimeBase base;
    if (pDil->DILC_GetTimeModeMapping(sConnectTime, unConnectTick, lnQueryTick) == S_OK)
    {
        /* same reference as CRefTimeKeeper, so that the times match the message window */
        base.valid = true;
        base.connectTime = sConnectTime.wHour * 36000000LL + sConnectTime.wMinute * 600000LL +
                           sConnectTime.wSecond * 10000LL + sConnectTime.wMilliseconds * 10LL;
        base.connectTick = (int64_t)unConnectTick;
    }
    std::lock_guard<std::mutex> lock(s_timeBaseMutex);
    s_timeBase = base;
}

CCANopenMonitor::TimeBase CCANopenMonitor::GetTimeBase() const
{
    std::lock_guard<std::mutex> lock(s_timeBaseMutex);
    return s_timeBase;
}
