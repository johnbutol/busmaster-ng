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
 * \file      CANopenDecoder.h
 * \brief     Passive decoder of CANopen CC (CiA 301) traffic
 *
 * Interprets classic CAN frames with the predefined connection set of CiA 301 (NMT, SYNC, TIME, EMCY,
 * PDO, SDO including block transfer, NMT error control and LSS) and keeps the state of every node seen
 * on each channel. Plain C++, no MFC and no BUSMASTER type, so it can be tested on its own.
 */

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace canopen
{

/** One CAN frame as seen on the bus */
struct Frame
{
    int64_t  tick    = 0;       ///< time stamp, in ticks of the clock given to the decoder
    uint8_t  channel = 1;       ///< BUSMASTER channel, 1 based
    bool     tx      = false;   ///< sent by BUSMASTER
    uint32_t id      = 0;
    bool     ext     = false;   ///< 29 bit identifier
    bool     rtr     = false;   ///< remote frame
    bool     fd      = false;   ///< CAN FD frame
    uint8_t  len     = 0;       ///< number of data bytes (at most 8 are kept)
    uint8_t  data[8] = {};
};

enum class EventType
{
    Nmt,            ///< NMT module control (COB-ID 0x000)
    Sync,
    Time,
    Emcy,
    Tpdo,           ///< process data sent by the node
    Rpdo,           ///< process data received by the node
    SdoRequest,     ///< client to server (0x600 + node)
    SdoResponse,    ///< server to client (0x580 + node)
    SdoAbort,
    SdoResult,      ///< summary of a finished segmented or block transfer (no frame of its own)
    BootUp,
    Heartbeat,
    GuardRequest,   ///< node guarding remote frame
    GuardResponse,
    Lss,
    Other           ///< not part of the predefined connection set
};

/** NMT states, with the values of the heartbeat protocol */
enum class NmtState : uint8_t
{
    BootUp         = 0,
    Stopped        = 4,
    Operational    = 5,
    PreOperational = 127,
    Unknown        = 255
};

struct Event
{
    Frame       frame;
    EventType   type = EventType::Other;
    uint8_t     node = 0;           ///< 0 if the frame is not for or from one node
    std::string text;               ///< decoded fields
};

/** What is known of one node */
struct NodeInfo
{
    uint8_t  channel = 0;
    uint8_t  node    = 0;
    NmtState state   = NmtState::Unknown;
    bool     guarded = false;       ///< node guarding was seen for this node
    int64_t  firstSeen     = 0;
    int64_t  lastSeen      = 0;     ///< last frame sent by the node
    int64_t  lastHeartbeat = 0;     ///< last heartbeat or boot-up, to see when the heartbeat stops
    int64_t  lastGuarding  = 0;     ///< last node guarding response
    double   heartbeatMs   = 0;     ///< observed heartbeat period, 0 while unknown
    int64_t  periodStart   = 0;     ///< previous heartbeat, 0 after a boot-up: measures the period
    uint32_t heartbeats    = 0;
    uint32_t frames        = 0;     ///< frames sent by the node
    uint32_t emcyCount     = 0;
    uint16_t lastEmcyCode  = 0;
    uint8_t  errorRegister = 0;
};

class Decoder
{
public:
    /** \param ticksPerSecond frequency of the clock of Frame::tick */
    explicit Decoder(int64_t ticksPerSecond);

    /** Decodes one frame and appends its event(s) to out: the frame itself, plus a SdoResult when it ends a
     *  segmented or block transfer. */
    void decode(const Frame& frame, std::vector<Event>& out);

    /** Forgets the nodes and the transfers in progress */
    void reset();

    /** Nodes seen so far, by key (channel << 8) | node */
    const std::map<uint16_t, NodeInfo>& nodes() const { return m_nodes; }

    static const char* typeName(EventType type);
    static const char* stateName(NmtState state);
    /** Text of an SDO abort code (CiA 301), nullptr if unknown */
    static const char* abortText(uint32_t code);
    /** Name of an object of the communication profile area (CiA 301), nullptr if unknown */
    static const char* objectName(uint16_t index);
    /** Meaning of an emergency error code (CiA 301), never nullptr */
    static const char* emcyText(uint16_t code);
    static std::string hexBytes(const uint8_t* data, size_t len);

private:
    /** SDO transfer in progress between a client and one server */
    struct Transfer
    {
        enum Phase { Idle, Segmented, BlockInit, BlockSub, BlockEnd };
        /** Segment of a sub-block, counted only once the receiver acknowledges it */
        struct Segment
        {
            uint8_t seq  = 0;
            bool    last = false;
            std::vector<uint8_t> data;
        };
        Phase    phase  = Idle;
        bool     upload = false;
        uint16_t index  = 0;
        uint8_t  sub    = 0;
        bool     sizeKnown = false;
        uint32_t size   = 0;
        uint32_t count  = 0;            ///< bytes transferred
        bool     lastSegment = false;   ///< block transfer: segment with the c bit seen
        std::vector<uint8_t> head;      ///< first bytes, for the summary
        std::vector<Segment> pending;   ///< block transfer: segments of the current sub-block
    };

    NodeInfo& node(uint8_t channel, uint8_t id, int64_t tick);
    void nodeSent(const Frame& f, uint8_t id);
    void decodeNmt(const Frame& f, Event& e);
    void decodeEmcy(const Frame& f, Event& e);
    void decodeTime(const Frame& f, Event& e);
    void decodeErrorControl(const Frame& f, Event& e);
    void decodeLss(const Frame& f, Event& e);
    void decodeSdo(const Frame& f, Event& e, bool fromClient, std::vector<Event>& out);
    bool decodeBlockSegment(const Frame& f, Event& e, Transfer& t);
    void addData(Transfer& t, const uint8_t* data, size_t len);
    void finish(const Frame& f, uint8_t id, Transfer& t, std::vector<Event>& out);
    std::string address(uint16_t index, uint8_t sub) const;

    int64_t m_ticksPerSecond;
    std::map<uint16_t, NodeInfo> m_nodes;
    std::map<uint16_t, Transfer> m_transfers;     ///< by (channel << 8) | node
    std::map<uint16_t, int64_t>  m_guardRequests; ///< pending node guarding requests, by (channel << 8) | node
};

} // namespace canopen
