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
 * \file      CANopenMonitor.h
 * \brief     Receives the CAN frames of the bus and decodes them
 *
 * Adds its own buffer to the monitor client of the CAN driver, like the Signal Watch window, so it sees
 * every frame sent and received, before any filter of the message window. A thread reads the buffer and
 * passes the frames to the decoder; the window takes the decoded events with a timer.
 */

#pragma once

#include <atomic>
#include <mutex>

#include "CANopenDecoder.h"

class CBaseDIL_CAN;
class STCANDATA;
template <typename T> class CMsgBufFSE;

class CCANopenMonitor
{
public:
    /** Clock of the time stamps of BUSMASTER: 100 us */
    static const int64_t TICKS_PER_SECOND = 10000;

    CCANopenMonitor();
    ~CCANopenMonitor();

    /** Adds the buffer to the monitor client of the current CAN driver and starts the reading thread.
     *  Can be called again after another driver was selected. */
    bool Attach(std::string& error);

    /** Removes the buffer from the driver and stops the thread */
    void Detach();

    bool IsAttached() const { return m_bAttached; }

    /** Moves the events decoded since the last call to events; dropped is the number of events lost
     *  because nobody took them in time */
    void TakeEvents(std::vector<canopen::Event>& events, size_t& dropped);

    void GetNodes(std::vector<canopen::NodeInfo>& nodes);

    /** Forgets the events not yet taken, the nodes and the transfers in progress */
    void Clear();

    /** Mapping between the time stamps and the local time, given by the driver at the connection */
    struct TimeBase
    {
        bool     valid = false;
        int64_t  connectTime = 0;   ///< local time of the connection, in 100 us since midnight (as the message window)
        int64_t  connectTick = 0;   ///< time stamp of the connection
    };
    TimeBase GetTimeBase() const;

    /** Time from a time stamp to a later one, in 100 us. Some drivers (PEAK) give 32 bit time stamps with a 64 bit
     *  reference: as the message window (CFormatMsgCommon, DWORD), the difference is taken on 32 bits, which
     *  gives up to 119 hours. */
    static int64_t TicksSince(int64_t later, int64_t earlier)
    {
        return (int64_t)(uint32_t)(later - earlier);
    }

    /** Signed difference between two time stamps close to each other, on 32 bits as TicksSince */
    static int64_t TicksBetween(int64_t a, int64_t b)
    {
        return (int64_t)(int32_t)(uint32_t)(a - b);
    }

    /** To call when the bus is connected: the mapping of the driver is read when the first message arrives */
    static void OnConnect();

private:
    static void CaptureTimeBase();
    static TimeBase s_timeBase;     ///< of the last connection
    static std::atomic<bool> s_bCapturePending;
    static std::mutex s_timeBaseMutex;
    static DWORD WINAPI ThreadProc(LPVOID pParam);
    void ReadBuffer();

    CMsgBufFSE<STCANDATA>* m_pBuffer;
    CBaseDIL_CAN* m_pDil;
    DWORD m_dwClientId;
    bool m_bAttached;
    HANDLE m_hThread;
    HANDLE m_hStop;

    CRITICAL_SECTION m_cs;          ///< protects what follows
    canopen::Decoder m_decoder;
    std::vector<canopen::Event> m_pending;
    size_t m_dropped;
};
