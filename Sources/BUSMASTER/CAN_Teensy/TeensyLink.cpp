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
 * @file  TeensyLink.cpp
 * @brief Serial link to a Teensy 4.0 with the BUSMASTER-NG CAN firmware.
 */

#include "TeensyLink.h"

#include <setupapi.h>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "advapi32.lib")

namespace teensy
{

// GUID_DEVCLASS_PORTS: the "Ports (COM & LPT)" device class
static const GUID PORTS_CLASS = { 0x4D36E978, 0xE325, 0x11CE, { 0xBF, 0xC1, 0x08, 0x00, 0x2B, 0xE1, 0x03, 0x18 } };

static unsigned long Get32(const unsigned char* p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static void Put32(unsigned char* p, unsigned long value)
{
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
    p[2] = (unsigned char)(value >> 16);
    p[3] = (unsigned char)(value >> 24);
}

// ---------------------------------------------------------------------------------------------

std::vector<Port> EnumeratePorts()
{
    std::vector<Port> ports;
    char wanted[40];
    sprintf_s(wanted, "VID_%04X&PID_%04X", BMT_USB_VID, BMT_USB_PID);

    HDEVINFO devices = SetupDiGetClassDevsA(&PORTS_CLASS, nullptr, nullptr, DIGCF_PRESENT);
    if (devices == INVALID_HANDLE_VALUE)
    {
        return ports;
    }
    SP_DEVINFO_DATA device;
    device.cbSize = sizeof(device);
    for (DWORD index = 0; SetupDiEnumDeviceInfo(devices, index, &device); index++)
    {
        char id[512] = { 0 };
        if (!SetupDiGetDeviceInstanceIdA(devices, &device, id, sizeof(id) - 1, nullptr))
        {
            continue;
        }
        _strupr_s(id);
        if (strstr(id, wanted) == nullptr)
        {
            continue;
        }
        HKEY key = SetupDiOpenDevRegKey(devices, &device, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (key == INVALID_HANDLE_VALUE)
        {
            continue;
        }
        char name[64] = { 0 };
        DWORD size = sizeof(name) - 1;
        DWORD type = 0;
        if (RegQueryValueExA(key, "PortName", nullptr, &type, (LPBYTE)name, &size) == ERROR_SUCCESS
                && type == REG_SZ && name[0] != 0)
        {
            Port port;
            port.name = name;
            // USB\VID_16C0&PID_0483\<serial number>
            const char* last = strrchr(id, '\\');
            if (last != nullptr && strchr(last, '&') == nullptr)
            {
                port.usbSerial = last + 1;
            }
            ports.push_back(port);
        }
        RegCloseKey(key);
    }
    SetupDiDestroyDeviceInfoList(devices);
    return ports;
}

// ---------------------------------------------------------------------------------------------

CParser::CParser()
{
    Reset();
}

void CParser::Reset()
{
    m_state = 0;
    m_received = 0;
}

bool CParser::Feed(unsigned char byte, Packet& packet)
{
    switch (m_state)
    {
        case 0:
            if (byte == BMT_SOF)
            {
                m_state = 1;
            }
            break;
        case 1:
            m_packet.type = byte;
            m_state = 2;
            break;
        case 2:
            m_packet.length = byte;
            m_received = 0;
            if (byte > BMT_MAX_PAYLOAD)
            {
                m_state = 0;
            }
            else
            {
                m_state = (byte == 0) ? 4 : 3;
            }
            break;
        case 3:
            m_packet.payload[m_received++] = byte;
            if (m_received == m_packet.length)
            {
                m_state = 4;
            }
            break;
        default:
        {
            m_state = 0;
            const unsigned char header[2] = { m_packet.type, m_packet.length };
            if (bmt_crc8(bmt_crc8(0, header, 2), m_packet.payload, m_packet.length) == byte)
            {
                packet = m_packet;
                return true;
            }
            break;
        }
    }
    return false;
}

size_t BuildPacket(unsigned char* out, unsigned char type, const unsigned char* payload, unsigned char length)
{
    out[0] = BMT_SOF;
    out[1] = type;
    out[2] = length;
    if (length != 0)
    {
        memcpy(out + 3, payload, length);
    }
    out[3 + length] = bmt_crc8(0, out + 1, length + 2u);
    return length + 4u;
}

// ---------------------------------------------------------------------------------------------

CTimeExtender::CTimeExtender()
{
    Reset();
}

void CTimeExtender::Reset()
{
    m_started = false;
    m_last = 0;
}

unsigned long long CTimeExtender::Extend(unsigned long timestamp)
{
    if (!m_started)
    {
        m_started = true;
        m_last = timestamp;
        return m_last;
    }
    // Frames are stamped in the interrupt and status packets in the main loop of the Teensy, so a
    // timestamp can be a little older than the one before: the difference is signed.
    const long difference = (long)(timestamp - (unsigned long)m_last);
    if (difference < 0 && (unsigned long long)(-(long long)difference) > m_last)
    {
        return 0;
    }
    const unsigned long long value = m_last + difference;
    if (value > m_last)
    {
        m_last = value;
    }
    return value;
}

// ---------------------------------------------------------------------------------------------

CLink::CLink()
    : m_port(INVALID_HANDLE_VALUE), m_thread(nullptr), m_alive(false), m_statusWanted(-1), m_listener(nullptr)
{
    m_stopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    m_readEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    m_writeEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    m_infoEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    m_ackEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    m_statusEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    InitializeCriticalSection(&m_writeLock);
    InitializeCriticalSection(&m_replyLock);
    memset(m_ack, 0, sizeof(m_ack));
    memset(&m_status, 0, sizeof(m_status));
}

CLink::~CLink()
{
    Close();
    CloseHandle(m_stopEvent);
    CloseHandle(m_readEvent);
    CloseHandle(m_writeEvent);
    CloseHandle(m_infoEvent);
    CloseHandle(m_ackEvent);
    CloseHandle(m_statusEvent);
    DeleteCriticalSection(&m_writeLock);
    DeleteCriticalSection(&m_replyLock);
}

bool CLink::IsOpen() const
{
    return m_port != INVALID_HANDLE_VALUE;
}

bool CLink::IsAlive() const
{
    return IsOpen() && m_alive;
}

const Info& CLink::GetInfo() const
{
    return m_info;
}

const std::string& CLink::PortName() const
{
    return m_portName;
}

void CLink::SetListener(IListener* listener)
{
    m_listener = listener;
}

bool CLink::Open(const std::string& port, std::string& error)
{
    Close();
    const std::string path = "\\\\.\\" + port;
    m_port = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                         FILE_FLAG_OVERLAPPED, nullptr);
    if (m_port == INVALID_HANDLE_VALUE)
    {
        const DWORD code = GetLastError();
        error = port + (code == ERROR_ACCESS_DENIED ? " is used by another program" : " cannot be opened");
        return false;
    }
    m_portName = port;

    DCB dcb;
    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    GetCommState(m_port, &dcb);
    dcb.BaudRate = 115200;                  // not used by a USB serial port
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fNull = FALSE;
    dcb.fAbortOnError = FALSE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;   // the firmware works only while DTR is set
    SetCommState(m_port, &dcb);
    SetupComm(m_port, 1 << 16, 1 << 12);

    // a read returns as soon as there are bytes, or empty after 100 ms
    COMMTIMEOUTS timeouts;
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = MAXDWORD;
    timeouts.ReadTotalTimeoutConstant = 100;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 1000;
    SetCommTimeouts(m_port, &timeouts);
    PurgeComm(m_port, PURGE_RXCLEAR | PURGE_TXCLEAR);
    EscapeCommFunction(m_port, SETDTR);

    m_parser.Reset();
    m_time.Reset();
    m_statusWanted = -1;
    m_alive = true;
    ResetEvent(m_stopEvent);
    ResetEvent(m_infoEvent);
    m_thread = CreateThread(nullptr, 0, ReadThread, this, 0, nullptr);
    if (m_thread == nullptr)
    {
        error = "The read thread cannot be started";
        Close();
        return false;
    }

    // The firmware sees DTR a moment after the port is opened: ask more than once.
    bool answered = false;
    for (int attempt = 0; attempt < 5 && !answered && m_alive; attempt++)
    {
        Send(BMT_CMD_HELLO, nullptr, 0);
        answered = WaitForSingleObject(m_infoEvent, 200) == WAIT_OBJECT_0;
    }
    if (!answered)
    {
        error = "The Teensy on " + port + " does not answer: it does not run the BUSMASTER-NG CAN firmware";
        Close();
        return false;
    }
    if (m_info.protocol != BMT_PROTOCOL_VERSION)
    {
        char text[160];
        sprintf_s(text, "The firmware of the Teensy on %s speaks protocol %d, this BUSMASTER-NG needs %d: "
                  "load the firmware of this version", port.c_str(), m_info.protocol, BMT_PROTOCOL_VERSION);
        error = text;
        Close();
        return false;
    }
    return true;
}

void CLink::Close()
{
    if (m_thread != nullptr)
    {
        SetEvent(m_stopEvent);
        WaitForSingleObject(m_thread, 3000);
        CloseHandle(m_thread);
        m_thread = nullptr;
    }
    if (m_port != INVALID_HANDLE_VALUE)
    {
        EscapeCommFunction(m_port, CLRDTR);     // the firmware leaves the buses
        CloseHandle(m_port);
        m_port = INVALID_HANDLE_VALUE;
    }
    m_alive = false;
}

DWORD WINAPI CLink::ReadThread(LPVOID parameter)
{
    static_cast<CLink*>(parameter)->ReadLoop();
    return 0;
}

void CLink::ReadLoop()
{
    unsigned char buffer[4096];
    Packet packet;
    bool lost = false;
    while (WaitForSingleObject(m_stopEvent, 0) != WAIT_OBJECT_0)
    {
        OVERLAPPED overlapped;
        memset(&overlapped, 0, sizeof(overlapped));
        overlapped.hEvent = m_readEvent;
        ResetEvent(m_readEvent);
        DWORD count = 0;
        if (!ReadFile(m_port, buffer, sizeof(buffer), &count, &overlapped))
        {
            if (GetLastError() != ERROR_IO_PENDING)
            {
                lost = true;
                break;
            }
            const HANDLE handles[2] = { m_readEvent, m_stopEvent };
            if (WaitForMultipleObjects(2, handles, FALSE, INFINITE) != WAIT_OBJECT_0)
            {
                CancelIo(m_port);
                GetOverlappedResult(m_port, &overlapped, &count, TRUE);
                break;
            }
            if (!GetOverlappedResult(m_port, &overlapped, &count, FALSE))
            {
                lost = true;
                break;
            }
        }
        for (DWORD i = 0; i < count; i++)
        {
            if (m_parser.Feed(buffer[i], packet))
            {
                Handle(packet);
            }
        }
    }
    if (lost)
    {
        m_alive = false;
        IListener* listener = m_listener;
        if (listener != nullptr)
        {
            listener->OnDisconnected(*this);
        }
    }
}

void CLink::Handle(const Packet& packet)
{
    const unsigned char* p = packet.payload;
    IListener* listener = m_listener;
    switch (packet.type)
    {
        case BMT_MSG_FRAME:
        {
            if (packet.length < BMT_SIZE_FRAME_HEADER || p[10] > 8)
            {
                break;
            }
            Frame frame;
            frame.channel = p[0];
            frame.flags = p[1];
            frame.timeUs = m_time.Extend(Get32(p + 2));
            frame.id = Get32(p + 6);
            frame.dlc = p[10];
            memset(frame.data, 0, sizeof(frame.data));
            const unsigned char available = (unsigned char)(packet.length - BMT_SIZE_FRAME_HEADER);
            memcpy(frame.data, p + BMT_SIZE_FRAME_HEADER, available < frame.dlc ? available : frame.dlc);
            if (listener != nullptr)
            {
                listener->OnFrame(*this, frame);
            }
            break;
        }

        case BMT_MSG_STATUS:
        {
            if (packet.length < BMT_SIZE_STATUS)
            {
                break;
            }
            Status status;
            status.channel = p[0];
            status.open = p[1] != 0;
            status.busState = p[2];
            status.tec = p[3];
            status.rec = p[4];
            status.timeUs = m_time.Extend(Get32(p + 5));
            status.rxLost = Get32(p + 9);
            status.txDropped = Get32(p + 13);
            status.txPending = (unsigned short)(p[17] | (p[18] << 8));
            if (m_statusWanted == status.channel)
            {
                EnterCriticalSection(&m_replyLock);
                m_status = status;
                m_statusWanted = -1;
                LeaveCriticalSection(&m_replyLock);
                SetEvent(m_statusEvent);
            }
            if (listener != nullptr)
            {
                listener->OnStatus(*this, status);
            }
            break;
        }

        case BMT_MSG_ERROR:
        {
            if (packet.length < BMT_SIZE_ERROR)
            {
                break;
            }
            BusError error;
            error.channel = p[0];
            error.timeUs = m_time.Extend(Get32(p + 1));
            error.esr1 = Get32(p + 5);
            error.tec = p[9];
            error.rec = p[10];
            if (listener != nullptr)
            {
                listener->OnBusError(*this, error);
            }
            break;
        }

        case BMT_MSG_ACK:
            if (packet.length >= BMT_SIZE_ACK)
            {
                EnterCriticalSection(&m_replyLock);
                memcpy(m_ack, p, BMT_SIZE_ACK);
                LeaveCriticalSection(&m_replyLock);
                SetEvent(m_ackEvent);
            }
            break;

        case BMT_MSG_INFO:
        {
            if (packet.length < BMT_SIZE_INFO_HEADER)
            {
                break;
            }
            Info info;
            info.protocol = p[0];
            info.firmwareMajor = p[1];
            info.firmwareMinor = p[2];
            info.channelCount = p[3] > BMT_CHANNEL_COUNT ? BMT_CHANNEL_COUNT : p[3];
            for (int i = 0; i < BMT_CHANNEL_COUNT; i++)
            {
                info.capabilities[i] = p[4 + i];
            }
            info.serial = Get32(p + 4 + BMT_CHANNEL_COUNT);
            // "board;channel 0;channel 1"
            const std::string text((const char*)p + BMT_SIZE_INFO_HEADER, packet.length - BMT_SIZE_INFO_HEADER);
            size_t start = 0;
            for (int field = 0; field <= BMT_CHANNEL_COUNT; field++)
            {
                size_t end = text.find(';', start);
                if (end == std::string::npos)
                {
                    end = text.size();
                }
                const std::string value = text.substr(start, end - start);
                if (field == 0)
                {
                    info.board = value;
                }
                else
                {
                    info.channelName[field - 1] = value;
                }
                start = end < text.size() ? end + 1 : text.size();
            }
            if (info.board.empty())
            {
                info.board = "Teensy";
            }
            for (int i = 0; i < BMT_CHANNEL_COUNT; i++)
            {
                if (info.channelName[i].empty())
                {
                    char name[16];
                    sprintf_s(name, "CAN %d", i + 1);
                    info.channelName[i] = name;
                }
            }
            m_info = info;
            SetEvent(m_infoEvent);
            break;
        }

        default:
            break;
    }
}

bool CLink::Write(const unsigned char* data, DWORD length)
{
    if (!IsAlive())
    {
        return false;
    }
    bool ok = false;
    EnterCriticalSection(&m_writeLock);
    OVERLAPPED overlapped;
    memset(&overlapped, 0, sizeof(overlapped));
    overlapped.hEvent = m_writeEvent;
    ResetEvent(m_writeEvent);
    DWORD written = 0;
    if (WriteFile(m_port, data, length, &written, &overlapped))
    {
        ok = written == length;
    }
    else if (GetLastError() == ERROR_IO_PENDING)
    {
        if (WaitForSingleObject(m_writeEvent, 2000) != WAIT_OBJECT_0)
        {
            CancelIo(m_port);
        }
        ok = GetOverlappedResult(m_port, &overlapped, &written, TRUE) && written == length;
    }
    LeaveCriticalSection(&m_writeLock);
    return ok;
}

bool CLink::Send(unsigned char type, const unsigned char* payload, unsigned char length)
{
    unsigned char packet[BMT_MAX_PAYLOAD + 4];
    const size_t size = BuildPacket(packet, type, payload, length);
    return Write(packet, (DWORD)size);
}

bool CLink::WaitAck(unsigned char command, unsigned char channel, unsigned char& result)
{
    const DWORD start = GetTickCount();
    while (GetTickCount() - start < 1500)
    {
        if (WaitForSingleObject(m_ackEvent, 100) != WAIT_OBJECT_0)
        {
            if (!m_alive)
            {
                return false;
            }
            continue;
        }
        EnterCriticalSection(&m_replyLock);
        const bool match = m_ack[0] == command && m_ack[1] == channel;
        result = m_ack[2];
        LeaveCriticalSection(&m_replyLock);
        if (match)
        {
            return true;
        }
    }
    return false;
}

bool CLink::OpenChannel(unsigned char channel, unsigned long bitRate, bool listenOnly, std::string& error)
{
    unsigned char payload[BMT_SIZE_OPEN];
    payload[0] = channel;
    payload[1] = listenOnly ? BMT_MODE_LISTEN_ONLY : BMT_MODE_NORMAL;
    Put32(payload + 2, bitRate);
    ResetEvent(m_ackEvent);
    unsigned char result = BMT_ACK_BAD_PARAMETER;
    if (!Send(BMT_CMD_OPEN, payload, sizeof(payload)) || !WaitAck(BMT_CMD_OPEN, channel, result))
    {
        error = "The Teensy on " + m_portName + " does not answer";
        return false;
    }
    char text[160];
    switch (result)
    {
        case BMT_ACK_OK:
            return true;
        case BMT_ACK_BAD_BITRATE:
            sprintf_s(text, "The Teensy cannot make the bit rate %lu bit/s", bitRate);
            break;
        case BMT_ACK_BAD_CHANNEL:
            sprintf_s(text, "The Teensy has no channel %d", channel + 1);
            break;
        default:
            sprintf_s(text, "The Teensy refused to open channel %d (code %d)", channel + 1, result);
            break;
    }
    error = text;
    return false;
}

bool CLink::CloseChannel(unsigned char channel)
{
    ResetEvent(m_ackEvent);
    unsigned char result = 0;
    return Send(BMT_CMD_CLOSE, &channel, 1) && WaitAck(BMT_CMD_CLOSE, channel, result) && result == BMT_ACK_OK;
}

bool CLink::Transmit(unsigned char channel, unsigned long id, bool extended, bool remote,
                     unsigned char dlc, const unsigned char* data)
{
    if (dlc > 8)
    {
        return false;
    }
    unsigned char payload[BMT_SIZE_TX_HEADER + 8];
    payload[0] = channel;
    payload[1] = (unsigned char)((extended ? BMT_FRAME_EXTENDED : 0) | (remote ? BMT_FRAME_REMOTE : 0));
    Put32(payload + 2, id);
    payload[6] = dlc;
    const unsigned char dataLength = remote ? 0 : dlc;
    if (dataLength != 0)
    {
        memcpy(payload + BMT_SIZE_TX_HEADER, data, dataLength);
    }
    return Send(BMT_CMD_TX, payload, (unsigned char)(BMT_SIZE_TX_HEADER + dataLength));
}

bool CLink::QueryStatus(unsigned char channel, Status& status, DWORD timeoutMs)
{
    ResetEvent(m_statusEvent);
    m_statusWanted = channel;
    if (!Send(BMT_CMD_GET_STATUS, &channel, 1) || WaitForSingleObject(m_statusEvent, timeoutMs) != WAIT_OBJECT_0)
    {
        m_statusWanted = -1;
        return false;
    }
    EnterCriticalSection(&m_replyLock);
    status = m_status;
    LeaveCriticalSection(&m_replyLock);
    return true;
}

} // namespace teensy
