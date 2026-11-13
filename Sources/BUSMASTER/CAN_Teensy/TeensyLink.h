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
 * @file  TeensyLink.h
 * @brief Serial link to a Teensy 4.0 with the BUSMASTER-NG CAN firmware (see TeensyProtocol.h).
 *        Win32 only, no MFC.
 */

#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include "TeensyProtocol.h"

namespace teensy
{

/** A serial port that belongs to a Teensy (USB identity of PJRC, "Serial" mode). */
struct Port
{
    std::string name;           // "COM7"
    std::string usbSerial;      // serial number of the USB device, may be empty
};

/** The Teensy serial ports that are present now. The firmware is not checked. */
std::vector<Port> EnumeratePorts();

/** The answer of the firmware to HELLO. */
struct Info
{
    int protocol;
    int firmwareMajor;
    int firmwareMinor;
    int channelCount;
    unsigned char capabilities[BMT_CHANNEL_COUNT];
    unsigned long serial;
    std::string board;                              // "Teensy 4.0"
    std::string channelName[BMT_CHANNEL_COUNT];     // "CAN1", "CAN3"

    Info() : protocol(0), firmwareMajor(0), firmwareMinor(0), channelCount(0), serial(0)
    {
        for (int i = 0; i < BMT_CHANNEL_COUNT; i++)
        {
            capabilities[i] = 0;
        }
    }
};

/** A CAN frame received, or sent by the interface (BMT_FRAME_TX). */
struct Frame
{
    unsigned char channel;
    unsigned char flags;                // BMT_FRAME_...
    unsigned long long timeUs;          // clock of the Teensy, extended to 64 bit
    unsigned long id;
    unsigned char length;               // data bytes: 0..8, up to 64 for a CAN FD frame
    unsigned char data[BMT_MAX_DATA];
};

struct Status
{
    unsigned char channel;
    bool open;
    unsigned char busState;             // BMT_BUS_...
    unsigned char tec;
    unsigned char rec;
    unsigned long long timeUs;
    unsigned long rxLost;
    unsigned long txDropped;
    unsigned short txPending;
};

struct BusError
{
    unsigned char channel;
    unsigned long long timeUs;
    unsigned long esr1;                 // BMT_ESR1_...
    unsigned char tec;
    unsigned char rec;
};

class CLink;

/** Called in the read thread of the link. */
class IListener
{
public:
    virtual ~IListener() {}
    virtual void OnFrame(CLink& link, const Frame& frame) = 0;
    virtual void OnStatus(CLink& link, const Status& status) = 0;
    virtual void OnBusError(CLink& link, const BusError& error) = 0;
    /** The port does not answer any more (cable removed). */
    virtual void OnDisconnected(CLink& link) = 0;
};

struct Packet
{
    unsigned char type;
    unsigned char length;
    unsigned char payload[BMT_MAX_PAYLOAD];
};

/** Finds the packets in a stream of bytes. */
class CParser
{
public:
    CParser();
    void Reset();
    /** Gives one byte; true when it completes a packet with a good checksum. */
    bool Feed(unsigned char byte, Packet& packet);

private:
    int m_state;
    unsigned char m_received;
    Packet m_packet;
};

/** Writes a packet in out (length + 4 bytes); returns its size. */
size_t BuildPacket(unsigned char* out, unsigned char type, const unsigned char* payload, unsigned char length);

/** Extends the 32 bit timestamps of one Teensy to 64 bit. */
class CTimeExtender
{
public:
    CTimeExtender();
    void Reset();
    unsigned long long Extend(unsigned long timestamp);

private:
    bool m_started;
    unsigned long long m_last;
};

class CLink
{
public:
    CLink();
    ~CLink();

    /** Opens the port and asks the firmware who it is. All channels are closed after this. */
    bool Open(const std::string& port, std::string& error);
    void Close();
    bool IsOpen() const;
    /** False when the port was lost after Open. */
    bool IsAlive() const;

    const Info& GetInfo() const;
    const std::string& PortName() const;
    void SetListener(IListener* listener);

    /** mode: BMT_MODE_... bits; dataBitRate is used with BMT_MODE_FD. */
    bool OpenChannel(unsigned char channel, unsigned long bitRate, unsigned char mode, unsigned long dataBitRate,
                     std::string& error);
    bool CloseChannel(unsigned char channel);
    /** flags: BMT_FRAME_EXTENDED, _REMOTE, _FD, _BRS; length: data bytes (the DLC of a remote frame). */
    bool Transmit(unsigned char channel, unsigned long id, unsigned char flags,
                  unsigned char length, const unsigned char* data);
    /** Asks the status of a channel and waits for it. */
    bool QueryStatus(unsigned char channel, Status& status, DWORD timeoutMs = 500);

private:
    CLink(const CLink&);
    CLink& operator=(const CLink&);

    static DWORD WINAPI ReadThread(LPVOID parameter);
    void ReadLoop();
    void Handle(const Packet& packet);
    bool Send(unsigned char type, const unsigned char* payload, unsigned char length);
    bool Write(const unsigned char* data, DWORD length);
    bool WaitAck(unsigned char command, unsigned char channel, unsigned char& result);

    HANDLE m_port;
    HANDLE m_thread;
    HANDLE m_stopEvent;
    HANDLE m_readEvent;
    HANDLE m_writeEvent;
    HANDLE m_infoEvent;
    HANDLE m_ackEvent;
    HANDLE m_statusEvent;
    CRITICAL_SECTION m_writeLock;
    CRITICAL_SECTION m_replyLock;
    volatile bool m_alive;
    std::string m_portName;
    Info m_info;
    unsigned char m_ack[BMT_SIZE_ACK];
    Status m_status;
    volatile int m_statusWanted;        // channel a QueryStatus waits for, -1 when none
    IListener* volatile m_listener;
    CParser m_parser;
    CTimeExtender m_time;
};

} // namespace teensy
