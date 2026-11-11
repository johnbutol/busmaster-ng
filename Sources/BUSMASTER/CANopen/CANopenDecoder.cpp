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
 * \file      CANopenDecoder.cpp
 * \brief     Passive decoder of CANopen CC (CiA 301) traffic
 */

#include "CANopenDecoder.h"

#include <cstdarg>
#include <cstdio>
#include <cmath>
#include <ctime>

namespace canopen
{

namespace
{

/* Function codes of the predefined connection set (bits 10..7 of the COB-ID) */
const uint32_t FC_NMT_SYNC_EMCY = 0x1;  // 0x080 SYNC, 0x081..0x0FF EMCY
const uint32_t FC_TIME          = 0x2;  // 0x100
const uint32_t FC_SDO_RESPONSE  = 0xB;  // 0x580 + node
const uint32_t FC_SDO_REQUEST   = 0xC;  // 0x600 + node
const uint32_t FC_ERROR_CONTROL = 0xE;  // 0x700 + node

const uint32_t COBID_LSS_SLAVE  = 0x7E4;
const uint32_t COBID_LSS_MASTER = 0x7E5;

const size_t SUMMARY_BYTES = 32;        // bytes kept for the summary of a transfer

/** A node guarding response must come within this time after the request */
const double GUARD_RESPONSE_S = 1.0;

uint16_t u16(const uint8_t* p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

uint32_t u32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

std::string format(const char* fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return buf;
}

bool printable(const uint8_t* data, size_t len)
{
    if (len == 0)
    {
        return false;
    }
    for (size_t i = 0; i < len; i++)
    {
        /* a trailing NUL is common in strings of the object dictionary */
        if (data[i] == 0 && i == len - 1 && i > 0)
        {
            continue;
        }
        if (data[i] < 0x20 || data[i] > 0x7E)
        {
            return false;
        }
    }
    return true;
}

/** "XX XX ...", then "= value" for 1 to 4 bytes and the text if they are all printable */
std::string valueText(const uint8_t* data, size_t len)
{
    std::string s = Decoder::hexBytes(data, len);
    if (len >= 1 && len <= 4)
    {
        uint32_t v = 0;
        for (size_t i = 0; i < len; i++)
        {
            v |= (uint32_t)data[i] << (8 * i);
        }
        s += format(" = %u", v);
    }
    if (len >= 3 && printable(data, len))
    {
        s += " \"";
        for (size_t i = 0; i < len && data[i] != 0; i++)
        {
            s += (char)data[i];
        }
        s += "\"";
    }
    return s;
}

const char* nmtCommandName(uint8_t cs)
{
    switch (cs)
    {
        case 0x01: return "Start remote node";
        case 0x02: return "Stop remote node";
        case 0x80: return "Enter pre-operational";
        case 0x81: return "Reset node";
        case 0x82: return "Reset communication";
        default:   return nullptr;
    }
}

const char* lssCommandName(uint8_t cs)
{
    switch (cs)
    {
        case 0x04: return "Switch state global";
        case 0x11: return "Configure node-ID";
        case 0x13: return "Configure bit timing";
        case 0x15: return "Activate bit timing";
        case 0x17: return "Store configuration";
        case 0x40: return "Switch state selective, vendor-ID";
        case 0x41: return "Switch state selective, product code";
        case 0x42: return "Switch state selective, revision number";
        case 0x43: return "Switch state selective, serial number";
        case 0x44: return "Switch state selective response";
        case 0x46: return "Identify remote slave, vendor-ID";
        case 0x47: return "Identify remote slave, product code";
        case 0x48: return "Identify remote slave, revision number low";
        case 0x49: return "Identify remote slave, revision number high";
        case 0x4A: return "Identify remote slave, serial number low";
        case 0x4B: return "Identify remote slave, serial number high";
        case 0x4C: return "Identify non-configured remote slave";
        case 0x4F: return "Identify slave response";
        case 0x50: return "Identify non-configured slave response";
        case 0x51: return "Fastscan";
        case 0x5A: return "Inquire vendor-ID";
        case 0x5B: return "Inquire product code";
        case 0x5C: return "Inquire revision number";
        case 0x5D: return "Inquire serial number";
        case 0x5E: return "Inquire node-ID";
        default:   return nullptr;
    }
}

const char* lssBitRate(uint8_t index)
{
    static const char* const rates[] =
    {
        "1000 kbit/s", "800 kbit/s", "500 kbit/s", "250 kbit/s", "125 kbit/s", "reserved", "50 kbit/s",
        "20 kbit/s", "10 kbit/s", "automatic"
    };
    return index < sizeof(rates) / sizeof(rates[0]) ? rates[index] : "unknown";
}

} // namespace

Decoder::Decoder(int64_t ticksPerSecond)
    : m_ticksPerSecond(ticksPerSecond > 0 ? ticksPerSecond : 1)
{
}

void Decoder::reset()
{
    m_nodes.clear();
    m_transfers.clear();
    m_guardRequests.clear();
}

const char* Decoder::typeName(EventType type)
{
    switch (type)
    {
        case EventType::Nmt:           return "NMT";
        case EventType::Sync:          return "SYNC";
        case EventType::Time:          return "TIME";
        case EventType::Emcy:          return "EMCY";
        case EventType::Tpdo:          return "TPDO";
        case EventType::Rpdo:          return "RPDO";
        case EventType::SdoRequest:    return "SDO request";
        case EventType::SdoResponse:   return "SDO response";
        case EventType::SdoAbort:      return "SDO abort";
        case EventType::SdoResult:     return "SDO result";
        case EventType::BootUp:        return "Boot-up";
        case EventType::Heartbeat:     return "Heartbeat";
        case EventType::GuardRequest:  return "Guarding request";
        case EventType::GuardResponse: return "Guarding response";
        case EventType::Lss:           return "LSS";
        default:                       return "Other";
    }
}

const char* Decoder::stateName(NmtState state)
{
    switch (state)
    {
        case NmtState::BootUp:         return "Boot-up";
        case NmtState::Stopped:        return "Stopped";
        case NmtState::Operational:    return "Operational";
        case NmtState::PreOperational: return "Pre-operational";
        default:                       return "Unknown";
    }
}

const char* Decoder::abortText(uint32_t code)
{
    switch (code)
    {
        case 0x05030000: return "Toggle bit not alternated";
        case 0x05040000: return "SDO protocol timed out";
        case 0x05040001: return "Client/server command specifier not valid or unknown";
        case 0x05040002: return "Invalid block size";
        case 0x05040003: return "Invalid sequence number";
        case 0x05040004: return "CRC error";
        case 0x05040005: return "Out of memory";
        case 0x06010000: return "Unsupported access to an object";
        case 0x06010001: return "Attempt to read a write only object";
        case 0x06010002: return "Attempt to write a read only object";
        case 0x06020000: return "Object does not exist in the object dictionary";
        case 0x06040041: return "Object cannot be mapped to the PDO";
        case 0x06040042: return "The number and length of the objects to be mapped would exceed the PDO length";
        case 0x06040043: return "General parameter incompatibility";
        case 0x06040047: return "General internal incompatibility in the device";
        case 0x06060000: return "Access failed due to a hardware error";
        case 0x06070010: return "Data type does not match, length of service parameter does not match";
        case 0x06070012: return "Data type does not match, length of service parameter too high";
        case 0x06070013: return "Data type does not match, length of service parameter too low";
        case 0x06090011: return "Sub-index does not exist";
        case 0x06090030: return "Invalid value for parameter";
        case 0x06090031: return "Value of parameter written too high";
        case 0x06090032: return "Value of parameter written too low";
        case 0x06090036: return "Maximum value is less than minimum value";
        case 0x060A0023: return "Resource not available: SDO connection";
        case 0x08000000: return "General error";
        case 0x08000020: return "Data cannot be transferred or stored to the application";
        case 0x08000021: return "Data cannot be transferred or stored to the application because of local control";
        case 0x08000022: return "Data cannot be transferred or stored to the application because of the present device state";
        case 0x08000023: return "Object dictionary dynamic generation fails or no object dictionary is present";
        case 0x08000024: return "No data available";
        default:         return nullptr;
    }
}

const char* Decoder::objectName(uint16_t index)
{
    switch (index)
    {
        case 0x1000: return "Device type";
        case 0x1001: return "Error register";
        case 0x1002: return "Manufacturer status register";
        case 0x1003: return "Pre-defined error field";
        case 0x1005: return "COB-ID SYNC";
        case 0x1006: return "Communication cycle period";
        case 0x1007: return "Synchronous window length";
        case 0x1008: return "Manufacturer device name";
        case 0x1009: return "Manufacturer hardware version";
        case 0x100A: return "Manufacturer software version";
        case 0x100C: return "Guard time";
        case 0x100D: return "Life time factor";
        case 0x1010: return "Store parameters";
        case 0x1011: return "Restore default parameters";
        case 0x1012: return "COB-ID TIME";
        case 0x1013: return "High resolution time stamp";
        case 0x1014: return "COB-ID EMCY";
        case 0x1015: return "Inhibit time EMCY";
        case 0x1016: return "Consumer heartbeat time";
        case 0x1017: return "Producer heartbeat time";
        case 0x1018: return "Identity object";
        case 0x1019: return "Synchronous counter overflow value";
        case 0x1020: return "Verify configuration";
        case 0x1021: return "Store EDS";
        case 0x1022: return "Store format";
        case 0x1023: return "OS command";
        case 0x1024: return "OS command mode";
        case 0x1025: return "OS debugger interface";
        case 0x1026: return "OS prompt";
        case 0x1027: return "Module list";
        case 0x1028: return "Emergency consumer object";
        case 0x1029: return "Error behavior object";
        case 0x1F50: return "Program data";
        case 0x1F51: return "Program control";
        case 0x1F56: return "Program software identification";
        case 0x1F57: return "Flash status identification";
        case 0x1F80: return "NMT startup";
        default:     break;
    }
    if (index >= 0x1200 && index <= 0x127F) return "SDO server parameter";
    if (index >= 0x1280 && index <= 0x12FF) return "SDO client parameter";
    if (index >= 0x1400 && index <= 0x15FF) return "RPDO communication parameter";
    if (index >= 0x1600 && index <= 0x17FF) return "RPDO mapping parameter";
    if (index >= 0x1800 && index <= 0x19FF) return "TPDO communication parameter";
    if (index >= 0x1A00 && index <= 0x1BFF) return "TPDO mapping parameter";
    if (index >= 0x2000 && index <= 0x5FFF) return "Manufacturer specific";
    if (index >= 0x6000 && index <= 0x9FFF) return "Device profile";
    return nullptr;
}

const char* Decoder::emcyText(uint16_t code)
{
    switch (code)
    {
        case 0x0000: return "Error reset or no error";
        case 0x2100: return "Current, device input side";
        case 0x2200: return "Current inside the device";
        case 0x2300: return "Current, device output side";
        case 0x3100: return "Mains voltage";
        case 0x3200: return "Voltage inside the device";
        case 0x3300: return "Output voltage";
        case 0x4100: return "Ambient temperature";
        case 0x4200: return "Device temperature";
        case 0x6100: return "Internal software";
        case 0x6200: return "User software";
        case 0x6300: return "Data set";
        case 0x8100: return "Communication";
        case 0x8110: return "CAN overrun (objects lost)";
        case 0x8120: return "CAN in error passive mode";
        case 0x8130: return "Life guard error or heartbeat error";
        case 0x8140: return "Recovered from bus off";
        case 0x8150: return "CAN-ID collision";
        case 0x8200: return "Protocol error";
        case 0x8210: return "PDO not processed due to length error";
        case 0x8220: return "PDO length exceeded";
        case 0x8230: return "DAM MPDO not processed, destination object not available";
        case 0x8240: return "Unexpected SYNC data length";
        case 0x8250: return "RPDO timeout";
        default:     break;
    }
    if ((code & 0xFF00) == 0xFF00) return "Device specific";
    switch (code & 0xF000)
    {
        case 0x1000: return "Generic error";
        case 0x2000: return "Current";
        case 0x3000: return "Voltage";
        case 0x4000: return "Temperature";
        case 0x5000: return "Device hardware";
        case 0x6000: return "Device software";
        case 0x7000: return "Additional modules";
        case 0x8000: return "Monitoring";
        case 0x9000: return "External error";
        case 0xF000: return "Additional functions";
        default:     return "Unknown error code";
    }
}

std::string Decoder::hexBytes(const uint8_t* data, size_t len)
{
    std::string s;
    for (size_t i = 0; i < len; i++)
    {
        char b[4];
        snprintf(b, sizeof(b), i ? " %02X" : "%02X", data[i]);
        s += b;
    }
    return s;
}

std::string Decoder::address(uint16_t index, uint8_t sub) const
{
    const char* name = objectName(index);
    return name ? format("0x%04X:%02X %s", index, sub, name) : format("0x%04X:%02X", index, sub);
}

NodeInfo& Decoder::node(uint8_t channel, uint8_t id, int64_t tick)
{
    NodeInfo& n = m_nodes[(uint16_t)((channel << 8) | id)];
    if (n.node == 0)
    {
        n.channel = channel;
        n.node = id;
        n.firstSeen = tick;
    }
    return n;
}

void Decoder::nodeSent(const Frame& f, uint8_t id)
{
    NodeInfo& n = node(f.channel, id, f.tick);
    n.lastSeen = f.tick;
    n.frames++;
}

void Decoder::decode(const Frame& f, std::vector<Event>& out)
{
    out.emplace_back();
    Event& e = out.back();
    e.frame = f;
    if (e.frame.len > 8)
    {
        e.frame.len = 8;
    }
    const Frame& fr = e.frame;

    if (fr.fd)
    {
        e.text = "CAN FD frame, not CANopen CC";
        return;
    }
    if (fr.ext)
    {
        e.text = "29 bit identifier, not CANopen CC";
        return;
    }

    const uint32_t id = fr.id & 0x7FF;
    const uint32_t fc = id >> 7;
    const uint8_t nid = (uint8_t)(id & 0x7F);

    if (id == 0x000)
    {
        e.type = EventType::Nmt;
        decodeNmt(fr, e);
        return;
    }
    if (fc == FC_NMT_SYNC_EMCY)
    {
        if (nid == 0)
        {
            e.type = EventType::Sync;
            if (fr.rtr)
            {
                e.text = "remote frame";
            }
            else if (fr.len == 0)
            {
                e.text = "";
            }
            else if (fr.len == 1)
            {
                e.text = format("counter %u", fr.data[0]);
            }
            else
            {
                e.text = format("unexpected length %u", fr.len);
            }
        }
        else
        {
            e.type = EventType::Emcy;
            e.node = nid;
            decodeEmcy(fr, e);
        }
        return;
    }
    if (fc == FC_TIME && nid == 0)
    {
        e.type = EventType::Time;
        decodeTime(fr, e);
        return;
    }
    if (fc >= 0x3 && fc <= 0xA && nid != 0)
    {
        const bool tpdo = (fc & 1) != 0;    // 0x180, 0x280, 0x380, 0x480
        const unsigned number = tpdo ? (fc - 1) / 2 : (fc - 2) / 2;
        e.type = tpdo ? EventType::Tpdo : EventType::Rpdo;
        e.node = nid;
        if (fr.rtr)
        {
            e.text = format("%s%u remote request", tpdo ? "TPDO" : "RPDO", number);
        }
        else
        {
            e.text = format("%s%u, %u byte%s", tpdo ? "TPDO" : "RPDO", number, fr.len, fr.len == 1 ? "" : "s");
            if (tpdo)
            {
                nodeSent(fr, nid);
            }
        }
        return;
    }
    if ((fc == FC_SDO_RESPONSE || fc == FC_SDO_REQUEST) && nid != 0)
    {
        e.node = nid;
        const bool fromClient = (fc == FC_SDO_REQUEST);
        if (!fromClient)
        {
            nodeSent(fr, nid);
        }
        decodeSdo(fr, e, fromClient, out);
        return;
    }
    if (fc == FC_ERROR_CONTROL && nid != 0)
    {
        e.node = nid;
        decodeErrorControl(fr, e);
        return;
    }
    if (id == COBID_LSS_SLAVE || id == COBID_LSS_MASTER)
    {
        e.type = EventType::Lss;
        decodeLss(fr, e);
        return;
    }
    e.text = "Not in the predefined connection set";
}

void Decoder::decodeNmt(const Frame& f, Event& e)
{
    if (f.rtr || f.len < 2)
    {
        e.text = format("unexpected %s, length %u", f.rtr ? "remote frame" : "frame", f.len);
        return;
    }
    const char* name = nmtCommandName(f.data[0]);
    e.node = f.data[1];
    std::string target = f.data[1] == 0 ? "all nodes" : format("node %u", f.data[1]);
    e.text = name ? format("%s, %s", name, target.c_str())
                  : format("unknown command 0x%02X, %s", f.data[0], target.c_str());
}

void Decoder::decodeEmcy(const Frame& f, Event& e)
{
    if (f.rtr)
    {
        e.text = "remote frame";
        return;
    }
    nodeSent(f, e.node);
    if (f.len < 3)
    {
        e.text = format("unexpected length %u", f.len);
        return;
    }
    const uint16_t code = u16(f.data);
    const uint8_t reg = f.data[2];
    e.text = format("0x%04X %s, error register 0x%02X", code, emcyText(code), reg);

    static const char* const bits[] =
    {
        "generic", "current", "voltage", "temperature", "communication", "device profile", "reserved",
        "manufacturer"
    };
    std::string set;
    for (int i = 0; i < 8; i++)
    {
        if (reg & (1 << i))
        {
            set += set.empty() ? "" : ", ";
            set += bits[i];
        }
    }
    if (!set.empty())
    {
        e.text += " (" + set + ")";
    }
    if (f.len > 3)
    {
        e.text += ", data " + hexBytes(f.data + 3, f.len - 3);
    }

    NodeInfo& n = node(f.channel, e.node, f.tick);
    n.emcyCount++;
    n.lastEmcyCode = code;
    n.errorRegister = reg;
}

void Decoder::decodeTime(const Frame& f, Event& e)
{
    if (f.rtr || f.len < 6)
    {
        e.text = format("unexpected %s, length %u", f.rtr ? "remote frame" : "frame", f.len);
        return;
    }
    const uint32_t ms = u32(f.data) & 0x0FFFFFFF;
    const uint16_t days = u16(f.data + 4);

    /* days since 1 January 1984, converted with the civil calendar */
    const long long z = 5113LL + days + 719468;     // 1984-01-01 is day 5113 after 1970-01-01
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned day = doy - (153 * mp + 2) / 5 + 1;
    const unsigned month = mp < 10 ? mp + 3 : mp - 9;
    const long long year = yoe + era * 400 + (month <= 2);

    e.text = format("%04lld-%02u-%02u %02u:%02u:%02u.%03u", year, month, day, ms / 3600000, ms / 60000 % 60,
                    ms / 1000 % 60, ms % 1000);
}

void Decoder::decodeErrorControl(const Frame& f, Event& e)
{
    const uint16_t key = (uint16_t)((f.channel << 8) | e.node);
    if (f.rtr)
    {
        e.type = EventType::GuardRequest;
        e.text = "";
        m_guardRequests[key] = f.tick;
        return;
    }
    if (f.len < 1)
    {
        e.type = EventType::Heartbeat;
        e.text = "empty frame";
        return;
    }

    const uint8_t value = f.data[0] & 0x7F;
    const unsigned toggle = f.data[0] >> 7;
    const NmtState state = (NmtState)value;
    const char* name = (value == 0 || value == 4 || value == 5 || value == 127) ? stateName(state) : nullptr;
    std::string stateText = name ? name : format("state 0x%02X", value);

    NodeInfo& n = node(f.channel, e.node, f.tick);
    n.lastSeen = f.tick;
    n.frames++;

    auto guard = m_guardRequests.find(key);
    if (guard != m_guardRequests.end() &&
        (double)(f.tick - guard->second) / (double)m_ticksPerSecond <= GUARD_RESPONSE_S)
    {
        m_guardRequests.erase(guard);
        e.type = EventType::GuardResponse;
        e.text = format("%s, toggle %u", stateText.c_str(), toggle);
        n.guarded = true;
        n.state = state;
        n.lastGuarding = f.tick;
        return;
    }

    if (value == 0)
    {
        e.type = EventType::BootUp;
        e.text = "";
        /* after initialisation the node enters pre-operational by itself */
        n.state = NmtState::PreOperational;
        n.lastHeartbeat = f.tick;
        n.periodStart = 0;
        m_transfers.erase(key);
        return;
    }

    e.type = EventType::Heartbeat;
    e.text = stateText;
    if (toggle)
    {
        e.text += ", toggle bit set";
    }
    /* the period is measured between two heartbeats only: the first one after a boot-up follows its own delay */
    if (n.periodStart != 0 && f.tick > n.periodStart)
    {
        const double interval = (double)(f.tick - n.periodStart) * 1000.0 / (double)m_ticksPerSecond;
        /* follow a new period at once, smooth the jitter of the same period */
        if (n.heartbeatMs == 0 || std::fabs(interval - n.heartbeatMs) > n.heartbeatMs / 2)
        {
            n.heartbeatMs = interval;
        }
        else
        {
            n.heartbeatMs = 0.75 * n.heartbeatMs + 0.25 * interval;
        }
    }
    n.lastHeartbeat = f.tick;
    n.periodStart = f.tick;
    n.heartbeats++;
    n.state = state;
}

void Decoder::decodeLss(const Frame& f, Event& e)
{
    const bool master = (f.id & 0x7FF) == COBID_LSS_MASTER;
    if (f.rtr || f.len < 1)
    {
        e.text = format("%s, unexpected %s", master ? "master" : "slave", f.rtr ? "remote frame" : "empty frame");
        return;
    }
    const uint8_t cs = f.data[0];
    const char* name = lssCommandName(cs);
    e.text = format("%s: %s", master ? "master" : "slave", name ? name : format("command 0x%02X", cs).c_str());

    const uint8_t* p = f.data + 1;
    const bool full = f.len >= 5;
    switch (cs)
    {
        case 0x04:
            if (f.len >= 2)
            {
                e.text += p[0] == 1 ? ", configuration" : p[0] == 0 ? ", waiting" : format(", mode %u", p[0]);
            }
            break;
        case 0x11:
            if (f.len >= 2)
            {
                e.text += master ? format(", node-ID %u", p[0]) : format(", error %u", p[0]);
            }
            break;
        case 0x13:
            if (master && f.len >= 3)
            {
                e.text += format(", table %u, %s", p[0], p[0] == 0 ? lssBitRate(p[1]) : format("index %u", p[1]).c_str());
            }
            else if (!master && f.len >= 2)
            {
                e.text += format(", error %u", p[0]);
            }
            break;
        case 0x15:
            if (f.len >= 3)
            {
                e.text += format(", switch delay %u ms", u16(p));
            }
            break;
        case 0x17:
            if (!master && f.len >= 2)
            {
                e.text += format(", error %u", p[0]);
            }
            break;
        case 0x5E:
            if (!master && f.len >= 2)
            {
                e.text += format(", node-ID %u", p[0]);
            }
            break;
        default:
            if (full && ((cs >= 0x40 && cs <= 0x43) || (cs >= 0x46 && cs <= 0x4B) ||
                         (!master && cs >= 0x5A && cs <= 0x5D)))
            {
                e.text += format(", 0x%08X", u32(p));
            }
            break;
    }
}

void Decoder::addData(Transfer& t, const uint8_t* data, size_t len)
{
    t.count += (uint32_t)len;
    for (size_t i = 0; i < len && t.head.size() < SUMMARY_BYTES; i++)
    {
        t.head.push_back(data[i]);
    }
}

void Decoder::finish(const Frame& f, uint8_t id, Transfer& t, std::vector<Event>& out)
{
    Event r;
    r.frame = f;
    r.type = EventType::SdoResult;
    r.node = id;
    const bool block = t.phase >= Transfer::BlockInit;
    r.text = format("%s %s %s complete, %u byte%s", block ? "Block" : "Segmented", t.upload ? "upload" : "download",
                    address(t.index, t.sub).c_str(), t.count, t.count == 1 ? "" : "s");
    if (t.sizeKnown && t.size != t.count)
    {
        r.text += format(" (%u announced)", t.size);
    }
    if (!t.head.empty())
    {
        const size_t shown = t.head.size() < t.count ? t.head.size() : t.count;
        r.text += ": " + hexBytes(t.head.data(), shown);
        if (shown < t.count)
        {
            r.text += " ...";
        }
        if (shown == t.count && printable(t.head.data(), shown))
        {
            r.text += " \"";
            for (size_t i = 0; i < shown && t.head[i] != 0; i++)
            {
                r.text += (char)t.head[i];
            }
            r.text += "\"";
        }
    }
    t = Transfer();
    out.push_back(r);
}

bool Decoder::decodeBlockSegment(const Frame& f, Event& e, Transfer& t)
{
    const uint8_t seq = f.data[0] & 0x7F;
    const bool last = (f.data[0] & 0x80) != 0;
    if (seq == 0)
    {
        return false;
    }
    const size_t len = f.len > 1 ? f.len - 1u : 0u;
    e.text = format("Block %s segment %u%s: %s", t.upload ? "upload" : "download", seq, last ? ", last" : "",
                    hexBytes(f.data + 1, len).c_str());

    /* kept until the acknowledge says which segments the receiver took */
    t.pending.push_back(Transfer::Segment());
    Transfer::Segment& s = t.pending.back();
    s.seq = seq;
    s.last = last;
    s.data.assign(f.data + 1, f.data + 1 + len);
    return true;
}

void Decoder::decodeSdo(const Frame& f, Event& e, bool fromClient, std::vector<Event>& out)
{
    e.type = fromClient ? EventType::SdoRequest : EventType::SdoResponse;
    const uint16_t key = (uint16_t)((f.channel << 8) | e.node);
    Transfer& t = m_transfers[key];

    if (f.rtr)
    {
        e.text = "remote frame, not valid for SDO";
        return;
    }
    if (f.len < 1)
    {
        e.text = "empty frame";
        return;
    }
    const uint8_t* d = f.data;
    const uint8_t b0 = d[0];

    /* Sub-block of a block transfer: the side that holds the data sends sequence numbers, not commands */
    if (t.phase == Transfer::BlockSub && fromClient != t.upload && b0 != 0x80)
    {
        if (decodeBlockSegment(f, e, t))
        {
            return;
        }
    }

    if (b0 == 0x80)
    {
        e.type = EventType::SdoAbort;
        if (f.len < 8)
        {
            e.text = format("Abort by %s, unexpected length %u", fromClient ? "client" : "server", f.len);
        }
        else
        {
            const uint32_t code = u32(d + 4);
            const char* reason = abortText(code);
            e.text = format("Abort %s by %s: 0x%08X %s", address(u16(d + 1), d[3]).c_str(),
                            fromClient ? "client" : "server", code, reason ? reason : "(unknown abort code)");
        }
        t = Transfer();
        return;
    }

    const uint8_t cs = b0 >> 5;
    const bool hasAddress = f.len >= 4;
    const uint16_t index = hasAddress ? u16(d + 1) : 0;
    const uint8_t sub = hasAddress ? d[3] : 0;
    const std::string where = hasAddress ? address(index, sub) : std::string("(short frame)");

    if (fromClient)
    {
        switch (cs)
        {
            case 1:     /* initiate download */
            {
                t = Transfer();
                t.upload = false;
                t.index = index;
                t.sub = sub;
                const bool expedited = (b0 & 0x02) != 0;
                const bool sized = (b0 & 0x01) != 0;
                if (expedited)
                {
                    size_t n = sized ? 4u - ((b0 >> 2) & 3u) : 4u;
                    if (f.len < 4 + n)
                    {
                        n = f.len > 4 ? f.len - 4u : 0u;
                    }
                    e.text = format("Download %s, expedited: %s", where.c_str(), valueText(d + 4, n).c_str());
                }
                else
                {
                    t.phase = Transfer::Segmented;
                    t.sizeKnown = sized && f.len >= 8;
                    t.size = t.sizeKnown ? u32(d + 4) : 0;
                    e.text = t.sizeKnown ? format("Download %s, segmented, %u bytes", where.c_str(), t.size)
                                         : format("Download %s, segmented", where.c_str());
                }
                break;
            }
            case 0:     /* download segment */
            {
                const unsigned toggle = (b0 >> 4) & 1;
                const bool last = (b0 & 1) != 0;
                size_t n = 7u - ((b0 >> 1) & 7u);
                if (f.len < 1 + n)
                {
                    n = f.len - 1u;
                }
                e.text = format("Download segment, toggle %u%s: %s", toggle, last ? ", last" : "",
                                hexBytes(d + 1, n).c_str());
                if (t.phase == Transfer::Segmented && !t.upload)
                {
                    addData(t, d + 1, n);
                    t.lastSegment = last;
                }
                break;
            }
            case 2:     /* initiate upload */
                t = Transfer();
                t.upload = true;
                t.index = index;
                t.sub = sub;
                e.text = format("Upload %s", where.c_str());
                break;
            case 3:     /* upload segment */
                e.text = format("Upload segment request, toggle %u", (b0 >> 4) & 1);
                break;
            case 5:     /* block upload */
                switch (b0 & 3)
                {
                    case 0:
                        t = Transfer();
                        t.upload = true;
                        t.index = index;
                        t.sub = sub;
                        t.phase = Transfer::BlockInit;
                        e.text = format("Block upload %s, block size %u, CRC %s", where.c_str(),
                                        f.len >= 5 ? d[4] : 0, (b0 & 4) ? "yes" : "no");
                        if (f.len >= 6 && d[5] != 0)
                        {
                            e.text += format(", protocol switch above %u bytes", d[5]);
                        }
                        break;
                    case 3:
                        e.text = "Block upload start";
                        if (t.phase == Transfer::BlockInit && t.upload)
                        {
                            t.phase = Transfer::BlockSub;
                        }
                        break;
                    case 2:
                    {
                        const uint8_t ackseq = f.len >= 2 ? d[1] : 0;
                        e.text = format("Block upload acknowledge, sequence %u, next block size %u", ackseq,
                                        f.len >= 3 ? d[2] : 0);
                        if (t.upload && (t.phase == Transfer::BlockSub))
                        {
                            for (const Transfer::Segment& s : t.pending)
                            {
                                if (s.seq <= ackseq)
                                {
                                    addData(t, s.data.data(), s.data.size());
                                    t.lastSegment = t.lastSegment || s.last;
                                }
                            }
                            t.pending.clear();
                            t.phase = t.lastSegment ? Transfer::BlockEnd : Transfer::BlockSub;
                        }
                        break;
                    }
                    case 1:
                        e.text = "Block upload end, confirmed";
                        if (t.upload && t.phase == Transfer::BlockEnd)
                        {
                            finish(f, e.node, t, out);
                        }
                        break;
                }
                break;
            case 6:     /* block download */
                if ((b0 & 1) == 0)
                {
                    t = Transfer();
                    t.upload = false;
                    t.index = index;
                    t.sub = sub;
                    t.phase = Transfer::BlockInit;
                    t.sizeKnown = (b0 & 2) != 0 && f.len >= 8;
                    t.size = t.sizeKnown ? u32(d + 4) : 0;
                    e.text = format("Block download %s", where.c_str());
                    if (t.sizeKnown)
                    {
                        e.text += format(", %u bytes", t.size);
                    }
                    e.text += format(", CRC %s", (b0 & 4) ? "yes" : "no");
                }
                else
                {
                    const unsigned unused = (b0 >> 2) & 7;
                    e.text = format("Block download end, %u unused byte%s in the last segment", unused,
                                    unused == 1 ? "" : "s");
                    if (f.len >= 3)
                    {
                        e.text += format(", CRC 0x%04X", u16(d + 1));
                    }
                    if (!t.upload && t.phase == Transfer::BlockEnd)
                    {
                        t.count = t.count >= unused ? t.count - unused : 0;
                    }
                }
                break;
            default:
                e.text = format("Unknown client command 0x%02X", b0);
                break;
        }
        return;
    }

    switch (cs)
    {
        case 3:     /* initiate download response */
            e.text = format("Download %s, confirmed", where.c_str());
            break;
        case 1:     /* download segment response */
            e.text = format("Download segment confirmed, toggle %u", (b0 >> 4) & 1);
            if (t.phase == Transfer::Segmented && !t.upload && t.lastSegment)
            {
                finish(f, e.node, t, out);
            }
            break;
        case 2:     /* initiate upload response */
        {
            const bool expedited = (b0 & 0x02) != 0;
            const bool sized = (b0 & 0x01) != 0;
            const bool block = t.phase == Transfer::BlockInit;
            t = Transfer();
            t.upload = true;
            t.index = index;
            t.sub = sub;
            if (expedited)
            {
                size_t n = sized ? 4u - ((b0 >> 2) & 3u) : 4u;
                if (f.len < 4 + n)
                {
                    n = f.len > 4 ? f.len - 4u : 0u;
                }
                e.text = format("Upload %s%s, expedited: %s", where.c_str(), block ? " (switched from block)" : "",
                                valueText(d + 4, n).c_str());
            }
            else
            {
                t.phase = Transfer::Segmented;
                t.sizeKnown = sized && f.len >= 8;
                t.size = t.sizeKnown ? u32(d + 4) : 0;
                e.text = format("Upload %s%s, segmented", where.c_str(), block ? " (switched from block)" : "");
                if (t.sizeKnown)
                {
                    e.text += format(", %u bytes", t.size);
                }
            }
            break;
        }
        case 0:     /* upload segment response */
        {
            const unsigned toggle = (b0 >> 4) & 1;
            const bool last = (b0 & 1) != 0;
            size_t n = 7u - ((b0 >> 1) & 7u);
            if (f.len < 1 + n)
            {
                n = f.len - 1u;
            }
            e.text = format("Upload segment, toggle %u%s: %s", toggle, last ? ", last" : "",
                            hexBytes(d + 1, n).c_str());
            if (t.phase == Transfer::Segmented && t.upload)
            {
                addData(t, d + 1, n);
                if (last)
                {
                    finish(f, e.node, t, out);
                }
            }
            break;
        }
        case 5:     /* block download response */
            switch (b0 & 3)
            {
                case 0:
                    e.text = format("Block download %s, accepted, block size %u, CRC %s", where.c_str(),
                                    f.len >= 5 ? d[4] : 0, (b0 & 4) ? "yes" : "no");
                    if (!t.upload && t.phase == Transfer::BlockInit)
                    {
                        t.phase = Transfer::BlockSub;
                    }
                    break;
                case 2:
                {
                    const uint8_t ackseq = f.len >= 2 ? d[1] : 0;
                    e.text = format("Block download acknowledge, sequence %u, next block size %u", ackseq,
                                    f.len >= 3 ? d[2] : 0);
                    if (!t.upload && (t.phase == Transfer::BlockSub))
                    {
                        for (const Transfer::Segment& s : t.pending)
                        {
                            if (s.seq <= ackseq)
                            {
                                addData(t, s.data.data(), s.data.size());
                                t.lastSegment = t.lastSegment || s.last;
                            }
                        }
                        t.pending.clear();
                        t.phase = t.lastSegment ? Transfer::BlockEnd : Transfer::BlockSub;
                    }
                    break;
                }
                case 1:
                    e.text = "Block download end, confirmed";
                    if (!t.upload && t.phase == Transfer::BlockEnd)
                    {
                        finish(f, e.node, t, out);
                    }
                    break;
                default:
                    e.text = format("Unknown server command 0x%02X", b0);
                    break;
            }
            break;
        case 6:     /* block upload response */
            if ((b0 & 1) == 0)
            {
                const bool sized = (b0 & 2) != 0 && f.len >= 8;
                e.text = format("Block upload %s, accepted", where.c_str());
                if (sized)
                {
                    e.text += format(", %u bytes", u32(d + 4));
                }
                e.text += format(", CRC %s", (b0 & 4) ? "yes" : "no");
                if (t.upload && t.phase == Transfer::BlockInit)
                {
                    t.sizeKnown = sized;
                    t.size = sized ? u32(d + 4) : 0;
                }
            }
            else
            {
                const unsigned unused = (b0 >> 2) & 7;
                e.text = format("Block upload end, %u unused byte%s in the last segment", unused,
                                unused == 1 ? "" : "s");
                if (f.len >= 3)
                {
                    e.text += format(", CRC 0x%04X", u16(d + 1));
                }
                if (t.upload && t.phase == Transfer::BlockEnd)
                {
                    t.count = t.count >= unused ? t.count - unused : 0;
                }
            }
            break;
        default:
            e.text = format("Unknown server command 0x%02X", b0);
            break;
    }
}

} // namespace canopen
