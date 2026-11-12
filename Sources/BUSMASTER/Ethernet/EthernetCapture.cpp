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
 * \file      EthernetCapture.cpp
 * \brief     Capture and transmission of Ethernet frames through Npcap / WinPcap (no MFC)
 */

#ifndef NOMINMAX
#define NOMINMAX            // std::min / std::max
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <mmsystem.h>

#include "EthernetCapture.h"

#include <algorithm>
#include <cstring>
#include <map>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "winmm.lib")

namespace eth
{

namespace
{

/* The parts of the pcap API that are used, declared here so that no SDK is needed to build. The layouts are those
 * of the 32-bit wpcap.dll of Npcap and WinPcap. */
struct pcap_timeval
{
    long tv_sec;
    long tv_usec;
};

struct pcap_pkthdr
{
    pcap_timeval ts;
    uint32_t caplen;
    uint32_t len;
};

struct pcap_addr
{
    pcap_addr* next;
    sockaddr* addr;
    sockaddr* netmask;
    sockaddr* broadaddr;
    sockaddr* dstaddr;
};

struct pcap_if
{
    pcap_if* next;
    char* name;
    char* description;
    pcap_addr* addresses;
    uint32_t flags;
};

struct pcap_stat
{
    unsigned int ps_recv;
    unsigned int ps_drop;
    unsigned int ps_ifdrop;
    unsigned int ps_capt;       // Windows only
    unsigned int reserved[4];
};

struct bpf_program
{
    unsigned int bf_len;
    void* bf_insns;
};

typedef void pcap_t;
const int PCAP_ERRBUF_SIZE = 256;
const int DLT_EN10MB = 1;
const uint32_t PCAP_NETMASK_UNKNOWN = 0xffffffff;

typedef int (__cdecl* PFN_findalldevs)(pcap_if**, char*);
typedef void (__cdecl* PFN_freealldevs)(pcap_if*);
typedef pcap_t* (__cdecl* PFN_open_live)(const char*, int, int, int, char*);
typedef void (__cdecl* PFN_close)(pcap_t*);
typedef int (__cdecl* PFN_next_ex)(pcap_t*, pcap_pkthdr**, const unsigned char**);
typedef int (__cdecl* PFN_sendpacket)(pcap_t*, const unsigned char*, int);
typedef char* (__cdecl* PFN_geterr)(pcap_t*);
typedef int (__cdecl* PFN_compile)(pcap_t*, bpf_program*, const char*, int, uint32_t);
typedef int (__cdecl* PFN_setfilter)(pcap_t*, bpf_program*);
typedef void (__cdecl* PFN_freecode)(bpf_program*);
typedef int (__cdecl* PFN_stats)(pcap_t*, pcap_stat*);
typedef const char* (__cdecl* PFN_lib_version)(void);
typedef int (__cdecl* PFN_datalink)(pcap_t*);
typedef int (__cdecl* PFN_setmintocopy)(pcap_t*, int);

struct PcapApi
{
    HMODULE module = nullptr;
    PFN_findalldevs findalldevs = nullptr;
    PFN_freealldevs freealldevs = nullptr;
    PFN_open_live open_live = nullptr;
    PFN_close close = nullptr;
    PFN_next_ex next_ex = nullptr;
    PFN_sendpacket sendpacket = nullptr;
    PFN_geterr geterr = nullptr;
    PFN_compile compile = nullptr;
    PFN_setfilter setfilter = nullptr;
    PFN_freecode freecode = nullptr;
    PFN_stats stats = nullptr;
    PFN_lib_version lib_version = nullptr;
    PFN_datalink datalink = nullptr;
    PFN_setmintocopy setmintocopy = nullptr;    // WinPcap / Npcap extension, optional
};

PcapApi sg_api;

template <typename T>
bool Resolve(T& pfn, const char* name)
{
    pfn = reinterpret_cast<T>(::GetProcAddress(sg_api.module, name));
    return pfn != nullptr;
}

/** "{GUID}" of a pcap device name "\Device\NPF_{GUID}" */
std::string GuidOf(const std::string& device)
{
    const size_t pos = device.find('{');
    return pos == std::string::npos ? std::string() : device.substr(pos);
}

std::string Narrow(const wchar_t* text)
{
    if (text == nullptr)
    {
        return std::string();
    }
    const int n = ::WideCharToMultiByte(CP_ACP, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1)
    {
        ::WideCharToMultiByte(CP_ACP, 0, text, -1, &s[0], n, nullptr, nullptr);
    }
    return s;
}

int64_t NowMs()
{
    static LARGE_INTEGER frequency = {};
    if (frequency.QuadPart == 0)
    {
        ::QueryPerformanceFrequency(&frequency);
    }
    LARGE_INTEGER now;
    ::QueryPerformanceCounter(&now);
    return (int64_t)(now.QuadPart * 1000 / frequency.QuadPart);
}

}   // namespace

CEthernetCapture::CEthernetCapture()
    : m_pCapture(nullptr), m_pSend(nullptr), m_hCaptureThread(nullptr), m_hTransmitThread(nullptr),
      m_lRunning(0)
{
    ::InitializeCriticalSection(&m_csFrames);
    ::InitializeCriticalSection(&m_csSend);
    m_hStopEvent = ::CreateEvent(nullptr, TRUE, FALSE, nullptr);
    m_hCyclicChanged = ::CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

CEthernetCapture::~CEthernetCapture()
{
    Stop();
    ::CloseHandle(m_hStopEvent);
    ::CloseHandle(m_hCyclicChanged);
    ::DeleteCriticalSection(&m_csFrames);
    ::DeleteCriticalSection(&m_csSend);
}

bool CEthernetCapture::LoadPcap(std::string& error)
{
    if (sg_api.module != nullptr)
    {
        return true;
    }
    /* Npcap puts its DLLs in System32\Npcap (SysWOW64\Npcap for 32-bit programs, through the file system
     * redirection); wpcap.dll needs Packet.dll from the same folder. WinPcap puts them in System32. */
    char system[MAX_PATH] = {};
    ::GetSystemDirectoryA(system, MAX_PATH);
    const std::string npcap = std::string(system) + "\\Npcap\\wpcap.dll";
    HMODULE module = ::LoadLibraryExA(npcap.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (module == nullptr)
    {
        module = ::LoadLibraryExA((std::string(system) + "\\wpcap.dll").c_str(), nullptr,
                                  LOAD_WITH_ALTERED_SEARCH_PATH);
    }
    if (module == nullptr)
    {
        error = "Npcap is not installed. Ethernet needs Npcap (https://npcap.com), or WinPcap 4.1.3 on "
                "Windows 7 and older processors.";
        return false;
    }
    sg_api.module = module;
    const bool ok = Resolve(sg_api.findalldevs, "pcap_findalldevs") && Resolve(sg_api.freealldevs, "pcap_freealldevs")
                    && Resolve(sg_api.open_live, "pcap_open_live") && Resolve(sg_api.close, "pcap_close")
                    && Resolve(sg_api.next_ex, "pcap_next_ex") && Resolve(sg_api.sendpacket, "pcap_sendpacket")
                    && Resolve(sg_api.geterr, "pcap_geterr") && Resolve(sg_api.compile, "pcap_compile")
                    && Resolve(sg_api.setfilter, "pcap_setfilter") && Resolve(sg_api.freecode, "pcap_freecode")
                    && Resolve(sg_api.stats, "pcap_stats") && Resolve(sg_api.lib_version, "pcap_lib_version")
                    && Resolve(sg_api.datalink, "pcap_datalink");
    Resolve(sg_api.setmintocopy, "pcap_setmintocopy");
    if (!ok)
    {
        ::FreeLibrary(module);
        sg_api = PcapApi();
        error = "wpcap.dll is too old or damaged: install the current Npcap (https://npcap.com).";
        return false;
    }
    return true;
}

std::string CEthernetCapture::LibraryVersion()
{
    return sg_api.lib_version != nullptr ? sg_api.lib_version() : "";
}

bool CEthernetCapture::ListAdapters(std::vector<Adapter>& adapters, std::string& error)
{
    adapters.clear();
    if (!LoadPcap(error))
    {
        return false;
    }

    /* names, MAC addresses and state from Windows, by adapter GUID */
    std::map<std::string, Adapter> windows;
    ULONG size = 32 * 1024;
    std::vector<uint8_t> buffer;
    ULONG result = ERROR_BUFFER_OVERFLOW;
    for (int attempt = 0; attempt < 3 && result == ERROR_BUFFER_OVERFLOW; ++attempt)
    {
        buffer.resize(size);
        result = ::GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST
                                        | GAA_FLAG_SKIP_DNS_SERVER, nullptr,
                                        reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
    }
    if (result == NO_ERROR)
    {
        for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()); a != nullptr; a = a->Next)
        {
            Adapter info;
            info.name = Narrow(a->FriendlyName);
            info.description = Narrow(a->Description);
            info.up = a->OperStatus == IfOperStatusUp;
            if (a->PhysicalAddressLength == 6)
            {
                memcpy(info.mac, a->PhysicalAddress, 6);
                info.hasMac = true;
            }
            for (auto* u = a->FirstUnicastAddress; u != nullptr; u = u->Next)
            {
                const sockaddr* sa = u->Address.lpSockaddr;
                if (sa != nullptr && sa->sa_family == AF_INET)
                {
                    const uint8_t* ip = reinterpret_cast<const uint8_t*>(
                        &reinterpret_cast<const sockaddr_in*>(sa)->sin_addr);
                    char text[20];
                    sprintf_s(text, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
                    info.ipv4 += info.ipv4.empty() ? text : std::string(", ") + text;
                }
            }
            windows[a->AdapterName] = info;
        }
    }

    pcap_if* devices = nullptr;
    char errbuf[PCAP_ERRBUF_SIZE] = {};
    if (sg_api.findalldevs(&devices, errbuf) == -1)
    {
        error = std::string("Listing the network adapters failed: ") + errbuf;
        return false;
    }
    for (pcap_if* d = devices; d != nullptr; d = d->next)
    {
        Adapter adapter;
        adapter.device = d->name;
        const auto it = windows.find(GuidOf(adapter.device));
        if (it != windows.end())
        {
            adapter.name = it->second.name;
            adapter.description = it->second.description;
            adapter.ipv4 = it->second.ipv4;
            memcpy(adapter.mac, it->second.mac, 6);
            adapter.hasMac = it->second.hasMac;
            adapter.up = it->second.up;
        }
        else
        {
            /* the Npcap loopback adapter, or an adapter Windows does not list */
            adapter.name = d->description != nullptr ? d->description : d->name;
            adapter.up = true;
        }
        if (adapter.description.empty() && d->description != nullptr)
        {
            adapter.description = d->description;
        }
        adapters.push_back(adapter);
    }
    sg_api.freealldevs(devices);
    std::stable_sort(adapters.begin(), adapters.end(), [](const Adapter& a, const Adapter& b)
    {
        return a.up != b.up ? a.up : false;     // connected adapters first
    });
    return true;
}

bool CEthernetCapture::Start(const Adapter& adapter, bool promiscuous, const std::string& filter, std::string& error)
{
    Stop();
    if (!LoadPcap(error))
    {
        return false;
    }
    char errbuf[PCAP_ERRBUF_SIZE] = {};
    /* 50 ms read timeout: frames are delivered at least that often, and Stop does not wait longer */
    pcap_t* capture = sg_api.open_live(adapter.device.c_str(), 65536, promiscuous ? 1 : 0, 50, errbuf);
    if (capture == nullptr)
    {
        error = "The adapter cannot be opened: " + std::string(errbuf);
        return false;
    }
    if (sg_api.datalink(capture) != DLT_EN10MB)
    {
        sg_api.close(capture);
        error = "This adapter does not give Ethernet frames (loopback or other link type).";
        return false;
    }
    if (sg_api.setmintocopy != nullptr)
    {
        sg_api.setmintocopy(capture, 0);    // deliver each frame without waiting for the buffer to fill
    }
    if (!filter.empty())
    {
        bpf_program program = {};
        if (sg_api.compile(capture, &program, filter.c_str(), 1, PCAP_NETMASK_UNKNOWN) != 0)
        {
            error = "Capture filter: " + std::string(sg_api.geterr(capture));
            sg_api.close(capture);
            return false;
        }
        const int set = sg_api.setfilter(capture, &program);
        sg_api.freecode(&program);
        if (set != 0)
        {
            error = "Capture filter: " + std::string(sg_api.geterr(capture));
            sg_api.close(capture);
            return false;
        }
    }
    pcap_t* send = sg_api.open_live(adapter.device.c_str(), 64, 0, 1000, errbuf);
    if (send == nullptr)
    {
        error = "The adapter cannot be opened for sending: " + std::string(errbuf);
        sg_api.close(capture);
        return false;
    }

    m_adapter = adapter;
    m_pCapture = capture;
    m_pSend = send;
    ::EnterCriticalSection(&m_csFrames);
    m_pending.clear();
    m_stats = Stats();
    m_lastError.clear();
    ::LeaveCriticalSection(&m_csFrames);
    ::ResetEvent(m_hStopEvent);
    ::InterlockedExchange(&m_lRunning, 1);
    m_hCaptureThread = ::CreateThread(nullptr, 0, CaptureThread, this, 0, nullptr);
    m_hTransmitThread = ::CreateThread(nullptr, 0, TransmitThread, this, 0, nullptr);
    if (m_hCaptureThread == nullptr || m_hTransmitThread == nullptr)
    {
        Stop();
        error = "The capture thread cannot be started.";
        return false;
    }
    ::SetThreadPriority(m_hCaptureThread, THREAD_PRIORITY_ABOVE_NORMAL);
    ::SetThreadPriority(m_hTransmitThread, THREAD_PRIORITY_TIME_CRITICAL);
    return true;
}

void CEthernetCapture::Stop()
{
    ::InterlockedExchange(&m_lRunning, 0);
    ::SetEvent(m_hStopEvent);
    for (HANDLE* thread : { &m_hCaptureThread, &m_hTransmitThread })
    {
        if (*thread != nullptr)
        {
            ::WaitForSingleObject(*thread, 5000);
            ::CloseHandle(*thread);
            *thread = nullptr;
        }
    }
    if (m_pCapture != nullptr)
    {
        pcap_stat stats = {};
        if (sg_api.stats(m_pCapture, &stats) == 0)
        {
            ::EnterCriticalSection(&m_csFrames);
            m_stats.dropped = stats.ps_drop;
            ::LeaveCriticalSection(&m_csFrames);
        }
        sg_api.close(m_pCapture);
        m_pCapture = nullptr;
    }
    ::EnterCriticalSection(&m_csSend);
    if (m_pSend != nullptr)
    {
        sg_api.close(m_pSend);
        m_pSend = nullptr;
    }
    ::LeaveCriticalSection(&m_csSend);
}

bool CEthernetCapture::IsRunning() const
{
    return m_lRunning != 0;
}

std::string CEthernetCapture::LastError() const
{
    ::EnterCriticalSection(&m_csFrames);
    const std::string error = m_lastError;
    ::LeaveCriticalSection(&m_csFrames);
    return error;
}

bool CEthernetCapture::SendLocked(const uint8_t* data, size_t length, std::string& error)
{
    if (m_pSend == nullptr)
    {
        error = "Not connected.";
        return false;
    }
    if (sg_api.sendpacket(m_pSend, data, (int)length) != 0)
    {
        error = "Sending failed: " + std::string(sg_api.geterr(m_pSend));
        ::EnterCriticalSection(&m_csFrames);
        ++m_stats.sendErrors;
        ::LeaveCriticalSection(&m_csFrames);
        return false;
    }
    ::EnterCriticalSection(&m_csFrames);
    ++m_stats.sent;
    ::LeaveCriticalSection(&m_csFrames);
    return true;
}

bool CEthernetCapture::Send(const std::vector<uint8_t>& frame, std::string& error)
{
    ::EnterCriticalSection(&m_csSend);
    const bool ok = SendLocked(frame.data(), frame.size(), error);
    ::LeaveCriticalSection(&m_csSend);
    return ok;
}

void CEthernetCapture::SetCyclic(const std::vector<CyclicFrame>& frames)
{
    ::EnterCriticalSection(&m_csSend);
    m_cyclic = frames;
    ::LeaveCriticalSection(&m_csSend);
    ::SetEvent(m_hCyclicChanged);
}

void CEthernetCapture::Take(std::vector<CapturedFrame>& out)
{
    ::EnterCriticalSection(&m_csFrames);
    out.swap(m_pending);
    m_pending.clear();
    ::LeaveCriticalSection(&m_csFrames);
}

CEthernetCapture::Stats CEthernetCapture::GetStats() const
{
    ::EnterCriticalSection(&m_csFrames);
    const Stats stats = m_stats;
    ::LeaveCriticalSection(&m_csFrames);
    return stats;
}

DWORD WINAPI CEthernetCapture::CaptureThread(LPVOID param)
{
    static_cast<CEthernetCapture*>(param)->CaptureLoop();
    return 0;
}

DWORD WINAPI CEthernetCapture::TransmitThread(LPVOID param)
{
    static_cast<CEthernetCapture*>(param)->TransmitLoop();
    return 0;
}

void CEthernetCapture::CaptureLoop()
{
    int64_t lastStats = NowMs();
    while (m_lRunning != 0)
    {
        pcap_pkthdr* header = nullptr;
        const unsigned char* data = nullptr;
        const int result = sg_api.next_ex(m_pCapture, &header, &data);
        if (result == 1)
        {
            CapturedFrame frame;
            frame.timeUs = (int64_t)header->ts.tv_sec * 1000000 + header->ts.tv_usec;
            frame.length = header->len;
            const size_t stored = std::min<size_t>(header->caplen, MAX_STORED);
            frame.data.assign(data, data + stored);
            ::EnterCriticalSection(&m_csFrames);
            if (m_pending.size() >= MAX_PENDING)
            {
                m_pending.erase(m_pending.begin(), m_pending.begin() + MAX_PENDING / 10);
                m_stats.lost += MAX_PENDING / 10;
            }
            m_pending.push_back(std::move(frame));
            ++m_stats.frames;
            m_stats.bytes += header->len;
            ::LeaveCriticalSection(&m_csFrames);
        }
        else if (result < 0)
        {
            ::EnterCriticalSection(&m_csFrames);
            m_lastError = "The capture stopped: " + std::string(sg_api.geterr(m_pCapture));
            ::LeaveCriticalSection(&m_csFrames);
            ::InterlockedExchange(&m_lRunning, 0);
            ::SetEvent(m_hStopEvent);
            break;
        }
        const int64_t now = NowMs();
        if (now - lastStats >= 500)
        {
            lastStats = now;
            pcap_stat stats = {};
            if (sg_api.stats(m_pCapture, &stats) == 0)
            {
                ::EnterCriticalSection(&m_csFrames);
                m_stats.dropped = stats.ps_drop;
                ::LeaveCriticalSection(&m_csFrames);
            }
        }
    }
}

void CEthernetCapture::TransmitLoop()
{
    struct Entry
    {
        CyclicFrame frame;
        int64_t due;
    };
    std::vector<Entry> entries;
    bool timerResolution = false;
    const HANDLE events[] = { m_hStopEvent, m_hCyclicChanged };
    ::SetEvent(m_hCyclicChanged);           // take the current list
    DWORD wait = INFINITE;
    while (m_lRunning != 0)
    {
        const DWORD signaled = ::WaitForMultipleObjects(2, events, FALSE, wait);
        if (m_lRunning == 0 || signaled == WAIT_OBJECT_0)
        {
            break;
        }
        const int64_t now = NowMs();
        if (signaled == WAIT_OBJECT_0 + 1)
        {
            ::EnterCriticalSection(&m_csSend);
            entries.clear();
            for (const CyclicFrame& frame : m_cyclic)
            {
                if (frame.periodMs > 0 && !frame.data.empty())
                {
                    entries.push_back({ frame, now });
                }
            }
            ::LeaveCriticalSection(&m_csSend);
            /* 1 ms timer resolution while frames are sent cyclically */
            if (!entries.empty() && !timerResolution)
            {
                timerResolution = ::timeBeginPeriod(1) == TIMERR_NOERROR;
            }
            else if (entries.empty() && timerResolution)
            {
                ::timeEndPeriod(1);
                timerResolution = false;
            }
        }
        int64_t next = INT64_MAX;
        for (Entry& e : entries)
        {
            if (e.due <= now)
            {
                std::string error;
                ::EnterCriticalSection(&m_csSend);
                SendLocked(e.frame.data.data(), e.frame.data.size(), error);
                ::LeaveCriticalSection(&m_csSend);
                e.due += e.frame.periodMs;
                if (e.due <= now)
                {
                    e.due = now + e.frame.periodMs;     // late (system busy): no burst to catch up
                }
            }
            next = std::min(next, e.due);
        }
        wait = entries.empty() ? INFINITE : (DWORD)std::max<int64_t>(0, next - NowMs());
    }
    if (timerResolution)
    {
        ::timeEndPeriod(1);
    }
}

}
