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
 * \file      EthernetCapture.h
 * \brief     Capture and transmission of Ethernet frames through Npcap / WinPcap (no MFC)
 *
 * wpcap.dll is loaded at run time: Npcap (System32\Npcap) first, then WinPcap (System32). It is not
 * distributed with BUSMASTER: Npcap may not be redistributed without a licence of its own, and the user
 * installs it from https://npcap.com.
 */

#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace eth
{

/** A network adapter that pcap can open */
struct Adapter
{
    std::string device;         ///< pcap name, "\Device\NPF_{GUID}"
    std::string name;           ///< name in Windows ("Ethernet 2"), else the pcap description
    std::string description;    ///< adapter model
    std::string ipv4;           ///< IPv4 addresses, comma separated
    uint8_t mac[6] = {};
    bool hasMac = false;
    bool up = false;            ///< connected (operational status of Windows)
};

/** A frame read from the adapter */
struct CapturedFrame
{
    int64_t timeUs = 0;         ///< time stamp of the driver, microseconds since 1970 (UTC)
    uint32_t length = 0;        ///< length on the wire, without the frame check sequence
    std::vector<uint8_t> data;  ///< captured bytes, at most MAX_STORED
};

/** A frame sent again and again */
struct CyclicFrame
{
    std::vector<uint8_t> data;
    unsigned periodMs = 0;
};

class CEthernetCapture
{
public:
    static const size_t MAX_STORED = 4096;      ///< bytes kept of each frame
    static const size_t MAX_PENDING = 200000;   ///< frames waiting for Take, the older ones are lost

    CEthernetCapture();
    ~CEthernetCapture();

    /** Loads wpcap.dll; false with a reason (Npcap missing) */
    static bool LoadPcap(std::string& error);

    /** Version of the loaded library ("Npcap version 1.80, based on libpcap version 1.10.5") */
    static std::string LibraryVersion();

    /** Ethernet adapters, with their Windows names and MAC addresses */
    static bool ListAdapters(std::vector<Adapter>& adapters, std::string& error);

    /** Opens the adapter and starts the capture thread. filter: pcap capture filter, may be empty. */
    bool Start(const Adapter& adapter, bool promiscuous, const std::string& filter, std::string& error);

    /** Stops the capture and the cyclic transmission */
    void Stop();

    /** False after Stop, or when the capture failed (adapter removed): see LastError */
    bool IsRunning() const;

    /** Reason why the capture stopped by itself, empty otherwise */
    std::string LastError() const;

    const Adapter& CurrentAdapter() const { return m_adapter; }

    /** Sends a frame once */
    bool Send(const std::vector<uint8_t>& frame, std::string& error);

    /** Frames sent cyclically while the capture runs; replaces the previous list */
    void SetCyclic(const std::vector<CyclicFrame>& frames);

    /** Moves the frames captured since the previous call to out */
    void Take(std::vector<CapturedFrame>& out);

    struct Stats
    {
        uint64_t frames = 0;        ///< frames captured
        uint64_t bytes = 0;
        uint64_t sent = 0;          ///< frames sent by Send and by the cyclic transmission
        uint64_t sendErrors = 0;
        uint64_t lost = 0;          ///< frames dropped because Take was not called often enough
        uint32_t dropped = 0;       ///< frames dropped by the driver (pcap_stats)
    };
    Stats GetStats() const;

private:
    static DWORD WINAPI CaptureThread(LPVOID param);
    static DWORD WINAPI TransmitThread(LPVOID param);
    void CaptureLoop();
    void TransmitLoop();
    bool SendLocked(const uint8_t* data, size_t length, std::string& error);

    Adapter m_adapter;
    void* m_pCapture;               ///< pcap_t of the capture
    void* m_pSend;                  ///< pcap_t for sending: a second handle, so that the capture sees what is sent
    HANDLE m_hCaptureThread;
    HANDLE m_hTransmitThread;
    HANDLE m_hStopEvent;
    HANDLE m_hCyclicChanged;
    volatile LONG m_lRunning;

    mutable CRITICAL_SECTION m_csFrames;
    std::vector<CapturedFrame> m_pending;
    Stats m_stats;
    std::string m_lastError;

    CRITICAL_SECTION m_csSend;
    std::vector<CyclicFrame> m_cyclic;
};

}
