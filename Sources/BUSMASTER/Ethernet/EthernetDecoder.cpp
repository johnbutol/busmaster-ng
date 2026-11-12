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
 * \file      EthernetDecoder.cpp
 * \brief     Decoding and building of Ethernet frames (no MFC, no Windows)
 */

#include "EthernetDecoder.h"

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace eth
{

namespace
{

uint16_t Be16(const uint8_t* p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

uint32_t Be32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

void PutBe16(std::vector<uint8_t>& v, uint16_t x)
{
    v.push_back((uint8_t)(x >> 8));
    v.push_back((uint8_t)x);
}

std::string Format(const char* format, ...)
{
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    buffer[sizeof(buffer) - 1] = 0;
    return buffer;
}

std::string IPv4ToString(const uint8_t* a)
{
    return Format("%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
}

/** Shortest form of RFC 5952: the longest run of zero groups (at least 2) becomes "::" */
std::string IPv6ToString(const uint8_t* a)
{
    uint16_t g[8];
    for (int i = 0; i < 8; ++i)
    {
        g[i] = Be16(a + 2 * i);
    }
    int bestStart = -1, bestLen = 0;
    for (int i = 0; i < 8;)
    {
        if (g[i] != 0)
        {
            ++i;
            continue;
        }
        int j = i;
        while (j < 8 && g[j] == 0)
        {
            ++j;
        }
        if (j - i > bestLen)
        {
            bestStart = i;
            bestLen = j - i;
        }
        i = j;
    }
    if (bestLen < 2)
    {
        bestStart = -1;
    }
    std::string s;
    for (int i = 0; i < 8; ++i)
    {
        if (i == bestStart)
        {
            s += "::";
            i += bestLen - 1;
            continue;
        }
        if (!s.empty() && s.back() != ':')
        {
            s += ':';
        }
        s += Format("%x", g[i]);
    }
    return s;
}

/** FNV-1a, for the kind of a frame */
void Mix(uint64_t& h, const void* data, size_t length)
{
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < length; ++i)
    {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
}

void MixValue(uint64_t& h, uint64_t value)
{
    Mix(h, &value, sizeof(value));
}

const char* PtpMessageName(int type)
{
    switch (type)
    {
        case 0x0: return "Sync";
        case 0x1: return "Delay_Req";
        case 0x2: return "Pdelay_Req";
        case 0x3: return "Pdelay_Resp";
        case 0x8: return "Follow_Up";
        case 0x9: return "Delay_Resp";
        case 0xA: return "Pdelay_Resp_Follow_Up";
        case 0xB: return "Announce";
        case 0xC: return "Signaling";
        case 0xD: return "Management";
        default:  return "message";
    }
}

/** PTP (IEEE 1588 / 802.1AS) message, over Ethernet or UDP 319 / 320 */
void DecodePtp(const uint8_t* p, size_t n, Decoded& d, uint64_t& key)
{
    if (n < 34)
    {
        d.info = "PTP (truncated)";
        return;
    }
    const int type = p[0] & 0x0f;
    MixValue(key, (uint64_t)type);
    d.info = Format("%s, domain %u, seq %u", PtpMessageName(type), p[4], Be16(p + 30));
}

const char* SomeIpTypeName(uint8_t type)
{
    switch (type & ~0x20)       // 0x20: SOME/IP-TP segment
    {
        case 0x00: return "Request";
        case 0x01: return "Request (no return)";
        case 0x02: return "Notification";
        case 0x80: return "Response";
        case 0x81: return "Error";
        default:   return nullptr;
    }
}

/** SOME/IP header (16 bytes): message ID, length, request ID, protocol version 1, interface version, type, return
 *  code. Recognised by its content, as SOME/IP has no fixed port except 30490 for service discovery. */
bool DecodeSomeIp(const uint8_t* p, size_t n, Decoded& d, uint64_t& key)
{
    if (n < 16 || p[12] != 1 || SomeIpTypeName(p[14]) == nullptr)
    {
        return false;
    }
    const uint32_t length = Be32(p + 4);
    if (length < 8 || (size_t)length + 8 > n)
    {
        return false;
    }
    const uint16_t service = Be16(p);
    const uint16_t method = Be16(p + 2);
    MixValue(key, ((uint64_t)service << 16) | method);
    if (service == 0xffff && method == 0x8100)
    {
        d.protocol += "/SOME/IP-SD";
        d.info = Format("Service discovery, session 0x%04x", Be16(p + 10));
        return true;
    }
    d.protocol += "/SOME/IP";
    d.info = Format("Service 0x%04x Method 0x%04x %s%s, client 0x%04x session 0x%04x", service, method,
                    SomeIpTypeName(p[14]), (p[14] & 0x20) ? " (TP)" : "", Be16(p + 8), Be16(p + 10));
    if (p[15] != 0)
    {
        d.info += Format(", return code 0x%02x", p[15]);
    }
    return true;
}

const char* DoIpTypeName(uint16_t type)
{
    switch (type)
    {
        case 0x0000: return "Generic negative acknowledge";
        case 0x0001: return "Vehicle identification request";
        case 0x0002: return "Vehicle identification request (EID)";
        case 0x0003: return "Vehicle identification request (VIN)";
        case 0x0004: return "Vehicle announcement";
        case 0x0005: return "Routing activation request";
        case 0x0006: return "Routing activation response";
        case 0x0007: return "Alive check request";
        case 0x0008: return "Alive check response";
        case 0x4001: return "Entity status request";
        case 0x4002: return "Entity status response";
        case 0x4003: return "Power mode request";
        case 0x4004: return "Power mode response";
        case 0x8001: return "Diagnostic message";
        case 0x8002: return "Diagnostic message ACK";
        case 0x8003: return "Diagnostic message NACK";
        default:     return nullptr;
    }
}

/** DoIP (ISO 13400-2) header: version, inverted version, payload type, length; UDP or TCP port 13400 */
bool DecodeDoIp(const uint8_t* p, size_t n, Decoded& d, uint64_t& key)
{
    if (n < 8 || (uint8_t)(p[0] ^ p[1]) != 0xff)
    {
        return false;
    }
    const uint16_t type = Be16(p + 2);
    const char* name = DoIpTypeName(type);
    MixValue(key, type);
    d.protocol += "/DoIP";
    d.info = name != nullptr ? name : Format("Payload type 0x%04x", type);
    if (type >= 0x8001 && type <= 0x8003 && n >= 12)
    {
        d.info += Format(" 0x%04x -> 0x%04x", Be16(p + 8), Be16(p + 10));
        if (type == 0x8001 && n > 12)
        {
            d.info += ": " + HexBytes(p + 12, n - 12, 16);
        }
    }
    return true;
}

void DecodeUdp(const uint8_t* p, size_t n, Decoded& d, uint64_t& key)
{
    if (n < 8)
    {
        d.protocol += "/UDP";
        d.info = "UDP (truncated)";
        return;
    }
    const uint16_t srcPort = Be16(p);
    const uint16_t dstPort = Be16(p + 2);
    d.source += Format(":%u", srcPort);
    d.destination += Format(":%u", dstPort);
    MixValue(key, ((uint64_t)srcPort << 16) | dstPort);
    size_t length = Be16(p + 4);
    if (length < 8 || length > n)
    {
        length = n;
    }
    const uint8_t* data = p + 8;
    const size_t dataLength = length - 8;
    d.protocol += "/UDP";

    auto is = [&](uint16_t port) { return srcPort == port || dstPort == port; };
    if (is(13400) && DecodeDoIp(data, dataLength, d, key))
    {
        return;
    }
    if (is(319) || is(320))
    {
        d.protocol += "/PTP";
        DecodePtp(data, dataLength, d, key);
        return;
    }
    if (DecodeSomeIp(data, dataLength, d, key))
    {
        return;
    }
    const char* name = nullptr;
    if (is(53))
    {
        name = "DNS";
    }
    else if (is(5353))
    {
        name = "mDNS";
    }
    else if (is(67) || is(68))
    {
        name = "DHCP";
    }
    else if (is(123))
    {
        name = "NTP";
    }
    else if (is(1900))
    {
        name = "SSDP";
    }
    else if (is(161) || is(162))
    {
        name = "SNMP";
    }
    else if (is(69))
    {
        name = "TFTP";
    }
    else if (is(514))
    {
        name = "Syslog";
    }
    else if (is(137) || is(138))
    {
        name = "NetBIOS";
    }
    else if (is(5355))
    {
        name = "LLMNR";
    }
    if (name != nullptr)
    {
        d.protocol += "/";
        d.protocol += name;
    }
    d.info = Format("%u -> %u, %u bytes", srcPort, dstPort, (unsigned)dataLength);
}

void DecodeTcp(const uint8_t* p, size_t n, Decoded& d, uint64_t& key)
{
    d.protocol += "/TCP";
    if (n < 20)
    {
        d.info = "TCP (truncated)";
        return;
    }
    const uint16_t srcPort = Be16(p);
    const uint16_t dstPort = Be16(p + 2);
    d.source += Format(":%u", srcPort);
    d.destination += Format(":%u", dstPort);
    MixValue(key, ((uint64_t)srcPort << 16) | dstPort);
    size_t header = (size_t)(p[12] >> 4) * 4;
    if (header < 20 || header > n)
    {
        header = n;
    }
    const uint8_t flags = p[13];
    std::string f;
    static const struct { uint8_t bit; const char* name; } names[] =
    {
        { 0x02, "SYN" }, { 0x10, "ACK" }, { 0x01, "FIN" }, { 0x04, "RST" }, { 0x08, "PSH" }, { 0x20, "URG" }
    };
    for (const auto& x : names)
    {
        if (flags & x.bit)
        {
            f += f.empty() ? "" : ", ";
            f += x.name;
        }
    }
    const uint8_t* data = p + header;
    const size_t dataLength = n - header;
    if (dataLength > 0 && (srcPort == 13400 || dstPort == 13400) && DecodeDoIp(data, dataLength, d, key))
    {
        return;
    }
    if (dataLength > 0 && DecodeSomeIp(data, dataLength, d, key))
    {
        return;
    }
    d.info = Format("%u -> %u [%s] Seq=%lu Ack=%lu Len=%u", srcPort, dstPort, f.c_str(),
                    (unsigned long)Be32(p + 4), (unsigned long)Be32(p + 8), (unsigned)dataLength);
}

void DecodeIcmp(const uint8_t* p, size_t n, Decoded& d)
{
    d.protocol += "/ICMP";
    if (n < 4)
    {
        return;
    }
    switch (p[0])
    {
        case 0:  d.info = "Echo reply"; break;
        case 3:  d.info = Format("Destination unreachable (code %u)", p[1]); break;
        case 5:  d.info = "Redirect"; break;
        case 8:  d.info = "Echo request"; break;
        case 11: d.info = "Time exceeded"; break;
        default: d.info = Format("Type %u code %u", p[0], p[1]); break;
    }
    if ((p[0] == 0 || p[0] == 8) && n >= 8)
    {
        d.info += Format(", id %u seq %u", Be16(p + 4), Be16(p + 6));
    }
}

void DecodeIcmpV6(const uint8_t* p, size_t n, Decoded& d)
{
    d.protocol += "/ICMPv6";
    if (n < 4)
    {
        return;
    }
    switch (p[0])
    {
        case 1:   d.info = "Destination unreachable"; break;
        case 3:   d.info = "Time exceeded"; break;
        case 128: d.info = "Echo request"; break;
        case 129: d.info = "Echo reply"; break;
        case 133: d.info = "Router solicitation"; break;
        case 134: d.info = "Router advertisement"; break;
        case 135: d.info = "Neighbor solicitation"; break;
        case 136: d.info = "Neighbor advertisement"; break;
        case 143: d.info = "Multicast listener report v2"; break;
        default:  d.info = Format("Type %u code %u", p[0], p[1]); break;
    }
    if ((p[0] == 135 || p[0] == 136) && n >= 24)
    {
        d.info += " " + IPv6ToString(p + 8);
    }
}

void DecodeTransport(int protocol, const uint8_t* p, size_t n, Decoded& d, uint64_t& key)
{
    switch (protocol)
    {
        case 1:  DecodeIcmp(p, n, d); break;
        case 2:  d.protocol += "/IGMP"; d.info = "Group membership"; break;
        case 6:  DecodeTcp(p, n, d, key); break;
        case 17: DecodeUdp(p, n, d, key); break;
        case 58: DecodeIcmpV6(p, n, d); break;
        default: d.info = Format("IP protocol %d", protocol); break;
    }
}

void DecodeIPv4(const uint8_t* p, size_t n, Decoded& d, uint64_t& key)
{
    d.protocol = "IPv4";
    if (n < 20 || (p[0] >> 4) != 4)
    {
        d.info = "IPv4 (truncated or invalid)";
        return;
    }
    const size_t header = (size_t)(p[0] & 0x0f) * 4;
    size_t total = Be16(p + 2);
    if (header < 20 || header > n)
    {
        d.info = "IPv4 (invalid header length)";
        return;
    }
    if (total < header || total > n)
    {
        total = n;      // truncated capture, or padding of a short frame after the datagram
    }
    d.source = IPv4ToString(p + 12);
    d.destination = IPv4ToString(p + 16);
    Mix(key, p + 12, 8);
    MixValue(key, p[9]);
    const uint16_t fragment = Be16(p + 6);
    if ((fragment & 0x1fff) != 0)
    {
        d.info = Format("Fragment, offset %u", (unsigned)(fragment & 0x1fff) * 8);
        return;
    }
    DecodeTransport(p[9], p + header, total - header, d, key);
    if (fragment & 0x2000)
    {
        d.info += " (first fragment)";
    }
}

void DecodeIPv6(const uint8_t* p, size_t n, Decoded& d, uint64_t& key)
{
    d.protocol = "IPv6";
    if (n < 40 || (p[0] >> 4) != 6)
    {
        d.info = "IPv6 (truncated or invalid)";
        return;
    }
    d.source = IPv6ToString(p + 8);
    d.destination = IPv6ToString(p + 24);
    Mix(key, p + 8, 32);
    size_t end = 40 + (size_t)Be16(p + 4);
    if (end > n)
    {
        end = n;
    }
    int next = p[6];
    size_t pos = 40;
    /* extension headers: hop-by-hop, routing, fragment, destination options */
    while ((next == 0 || next == 43 || next == 44 || next == 60) && pos + 8 <= end)
    {
        const size_t length = next == 44 ? 8 : ((size_t)p[pos + 1] + 1) * 8;
        if (next == 44 && (Be16(p + pos + 2) & 0xfff8) != 0)
        {
            d.info = "Fragment";
            return;
        }
        next = p[pos];
        pos += length;
    }
    MixValue(key, (uint64_t)next);
    if (pos > end)
    {
        return;
    }
    DecodeTransport(next, p + pos, end - pos, d, key);
}

void DecodeArp(const uint8_t* p, size_t n, Decoded& d)
{
    d.protocol = "ARP";
    if (n < 28 || Be16(p) != 1 || Be16(p + 2) != 0x0800 || p[4] != 6 || p[5] != 4)
    {
        d.info = "ARP";
        return;
    }
    const uint16_t op = Be16(p + 6);
    d.source = IPv4ToString(p + 14);
    d.destination = IPv4ToString(p + 24);
    if (op == 1)
    {
        d.info = memcmp(p + 14, p + 24, 4) == 0 ? "Announcement " + d.source
                 : "Who has " + d.destination + "? Tell " + d.source;
    }
    else if (op == 2)
    {
        d.info = d.source + " is at " + MacToString(p + 8);
    }
    else
    {
        d.info = Format("Operation %u", op);
    }
}

const char* AvtpSubtypeName(uint8_t subtype)
{
    switch (subtype)
    {
        case 0x00: return "IEC 61883 / IIDC";
        case 0x02: return "AAF audio";
        case 0x03: return "CVF video";
        case 0x04: return "CRF clock reference";
        case 0x05: return "TSCF time-synchronous control";
        case 0x82: return "NTSCF non-time-synchronous control";
        case 0xFA: return "AECP";
        case 0xFB: return "ACMP";
        case 0xFC: return "ADP";
        case 0xFE: return "MAAP";
        default:   return nullptr;
    }
}

void DecodeAvtp(const uint8_t* p, size_t n, Decoded& d, uint64_t& key)
{
    if (n < 1)
    {
        return;
    }
    const char* name = AvtpSubtypeName(p[0]);
    MixValue(key, p[0]);
    d.info = name != nullptr ? name : Format("Subtype 0x%02x", p[0]);
    /* stream data PDUs: stream ID valid bit, then the stream ID at byte 4 */
    if ((p[1] & 0x80) != 0 && n >= 12 && p[0] < 0x80)
    {
        d.info += ", stream " + HexBytes(p + 4, 8, 8);
        Mix(key, p + 4, 8);
    }
}

/** LLDP: chassis ID, port ID and system name TLVs */
void DecodeLldp(const uint8_t* p, size_t n, Decoded& d)
{
    std::string port, system;
    size_t pos = 0;
    while (pos + 2 <= n)
    {
        const int type = p[pos] >> 1;
        const size_t length = ((size_t)(p[pos] & 1) << 8) | p[pos + 1];
        pos += 2;
        if (type == 0 || pos + length > n)
        {
            break;
        }
        auto text = [&](size_t skip)
        {
            std::string s;
            for (size_t i = skip; i < length; ++i)
            {
                const uint8_t c = p[pos + i];
                s += (c >= 0x20 && c < 0x7f) ? (char)c : '.';
            }
            return s;
        };
        if (type == 2 && length > 1)
        {
            port = p[pos] == 3 && length == 7 ? MacToString(p + pos + 1) : text(1);
        }
        else if (type == 5)
        {
            system = text(0);
        }
        pos += length;
    }
    d.info = system.empty() ? "Port " + port : system + ", port " + port;
}

}   // namespace

std::string MacToString(const uint8_t* mac)
{
    return Format("%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

bool ParseMac(const std::string& text, uint8_t mac[MAC_LEN])
{
    std::string hex;
    for (char c : text)
    {
        if (c == ':' || c == '-' || c == ' ' || c == '.')
        {
            continue;
        }
        if (!isxdigit((unsigned char)c))
        {
            return false;
        }
        hex += c;
    }
    if (hex.size() != 2 * MAC_LEN)
    {
        return false;
    }
    for (size_t i = 0; i < MAC_LEN; ++i)
    {
        mac[i] = (uint8_t)strtoul(hex.substr(2 * i, 2).c_str(), nullptr, 16);
    }
    return true;
}

bool ParseIPv4(const std::string& text, uint8_t out[4])
{
    unsigned a[4];
    char extra;
    if (sscanf(text.c_str(), "%u.%u.%u.%u%c", &a[0], &a[1], &a[2], &a[3], &extra) != 4)
    {
        return false;
    }
    for (int i = 0; i < 4; ++i)
    {
        if (a[i] > 255)
        {
            return false;
        }
        out[i] = (uint8_t)a[i];
    }
    return true;
}

bool ParseHexBytes(const std::string& text, std::vector<uint8_t>& out)
{
    out.clear();
    std::string group;
    auto flush = [&]()
    {
        if (group.size() % 2 != 0)
        {
            return false;
        }
        for (size_t i = 0; i < group.size(); i += 2)
        {
            out.push_back((uint8_t)strtoul(group.substr(i, 2).c_str(), nullptr, 16));
        }
        group.clear();
        return true;
    };
    for (char c : text)
    {
        if (isxdigit((unsigned char)c))
        {
            group += c;
        }
        else if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',' || c == ':' || c == '-')
        {
            if (!flush())
            {
                return false;
            }
        }
        else
        {
            return false;
        }
    }
    return flush();
}

std::string HexBytes(const uint8_t* data, size_t length, size_t maxBytes)
{
    std::string s;
    const size_t n = length < maxBytes ? length : maxBytes;
    s.reserve(n * 3 + 4);
    static const char digits[] = "0123456789ABCDEF";
    for (size_t i = 0; i < n; ++i)
    {
        if (i > 0)
        {
            s += ' ';
        }
        s += digits[data[i] >> 4];
        s += digits[data[i] & 0x0f];
    }
    if (length > n)
    {
        s += " ...";
    }
    return s;
}

const char* EtherTypeName(uint16_t etherType)
{
    switch (etherType)
    {
        case 0x0800: return "IPv4";
        case 0x0806: return "ARP";
        case 0x0842: return "Wake-on-LAN";
        case 0x22EA: return "SRP";
        case 0x22F0: return "AVTP";
        case 0x8035: return "RARP";
        case 0x8100: return "VLAN";
        case 0x86DD: return "IPv6";
        case 0x8808: return "Ethernet flow control";
        case 0x8809: return "Slow protocols (LACP)";
        case 0x8847: return "MPLS";
        case 0x8848: return "MPLS multicast";
        case 0x8863: return "PPPoE discovery";
        case 0x8864: return "PPPoE session";
        case 0x888E: return "EAPOL (802.1X)";
        case 0x8892: return "PROFINET";
        case 0x88A4: return "EtherCAT";
        case 0x88A8: return "VLAN (802.1ad)";
        case 0x88AB: return "POWERLINK";
        case 0x88B5: return "Local experimental 1";
        case 0x88B6: return "Local experimental 2";
        case 0x88CC: return "LLDP";
        case 0x88CD: return "SERCOS III";
        case 0x88E1: return "HomePlug";
        case 0x88E5: return "MACsec";
        case 0x88F5: return "MVRP";
        case 0x88F6: return "MMRP";
        case 0x88F7: return "PTP";
        case 0x88FB: return "PRP";
        case 0x8902: return "CFM (802.1ag)";
        case 0x892F: return "HSR";
        case 0x893A: return "IEEE 1905";
        case 0x9000: return "Loopback";
        default:     return nullptr;
    }
}

bool Decode(const uint8_t* frame, size_t length, Decoded& d)
{
    d = Decoded();
    if (length < 14)
    {
        d.protocol = "?";
        d.info = "Shorter than an Ethernet header";
        return false;
    }
    memcpy(d.dst, frame, MAC_LEN);
    memcpy(d.src, frame + MAC_LEN, MAC_LEN);
    uint64_t key = 1469598103934665603ULL;
    Mix(key, frame, 2 * MAC_LEN);

    size_t pos = 12;
    uint16_t type = Be16(frame + pos);
    while ((type == 0x8100 || type == 0x88a8 || type == 0x9100) && pos + 6 <= length)
    {
        const uint16_t tci = Be16(frame + pos + 2);
        if (d.vlanTags == 0)
        {
            d.vlanId = tci & 0x0fff;
            d.vlanPriority = tci >> 13;
        }
        ++d.vlanTags;
        pos += 4;
        type = Be16(frame + pos);
    }
    d.payloadOffset = pos + 2;
    MixValue(key, ((uint64_t)(d.vlanId & 0xffff) << 16) | type);
    const uint8_t* p = frame + d.payloadOffset;
    const size_t n = length > d.payloadOffset ? length - d.payloadOffset : 0;

    if (type < 0x0600)
    {
        /* IEEE 802.3 frame: length field, then LLC */
        d.etherType = 0;
        if (n >= 3 && p[0] == 0x42 && p[1] == 0x42)
        {
            d.protocol = "STP";
            d.info = n >= 7 && p[6] == 0x02 ? "Rapid spanning tree BPDU" : "Spanning tree BPDU";
        }
        else if (n >= 8 && p[0] == 0xaa && p[1] == 0xaa)
        {
            d.protocol = "LLC/SNAP";
            d.info = Format("OUI %02x-%02x-%02x, type 0x%04x", p[3], p[4], p[5], Be16(p + 6));
        }
        else
        {
            d.protocol = "LLC";
            d.info = n >= 2 ? Format("DSAP 0x%02x SSAP 0x%02x, %u bytes", p[0], p[1], (unsigned)type)
                     : std::string("802.3");
        }
        d.kindKey = key;
        return true;
    }

    d.etherType = type;
    switch (type)
    {
        case 0x0800:
            DecodeIPv4(p, n, d, key);
            break;
        case 0x86DD:
            DecodeIPv6(p, n, d, key);
            break;
        case 0x0806:
            DecodeArp(p, n, d);
            break;
        case 0x88F7:
            d.protocol = "PTP";
            DecodePtp(p, n, d, key);
            break;
        case 0x22F0:
            d.protocol = "AVTP";
            DecodeAvtp(p, n, d, key);
            break;
        case 0x88CC:
            d.protocol = "LLDP";
            DecodeLldp(p, n, d);
            break;
        default:
        {
            const char* name = EtherTypeName(type);
            d.protocol = name != nullptr ? name : Format("0x%04x", type);
            d.info = Format("EtherType 0x%04x, %u bytes", type, (unsigned)n);
            break;
        }
    }
    d.kindKey = key;
    return true;
}

namespace
{

uint16_t Checksum(const uint8_t* p, size_t n, uint32_t sum)
{
    for (size_t i = 0; i + 1 < n; i += 2)
    {
        sum += Be16(p + i);
    }
    if (n & 1)
    {
        sum += (uint32_t)p[n - 1] << 8;
    }
    while (sum >> 16)
    {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

}

bool Build(const FrameSpec& spec, std::vector<uint8_t>& frame, std::string& error)
{
    frame.clear();
    const size_t payload = spec.kind == FrameSpec::UdpIPv4 ? spec.data.size() + 28 : spec.data.size();
    if (payload > MAX_PAYLOAD)
    {
        error = Format("The data is too long: %u bytes, at most %u%s", (unsigned)spec.data.size(),
                       (unsigned)(spec.kind == FrameSpec::UdpIPv4 ? MAX_PAYLOAD - 28 : MAX_PAYLOAD),
                       spec.kind == FrameSpec::UdpIPv4 ? " in a UDP datagram (no IP fragmentation)" : "");
        return false;
    }
    frame.insert(frame.end(), spec.dst, spec.dst + MAC_LEN);
    frame.insert(frame.end(), spec.src, spec.src + MAC_LEN);
    if (spec.vlanId >= 0)
    {
        PutBe16(frame, 0x8100);
        PutBe16(frame, (uint16_t)(((spec.vlanPriority & 7) << 13) | (spec.vlanId & 0x0fff)));
    }
    if (spec.kind == FrameSpec::Raw)
    {
        PutBe16(frame, spec.etherType);
        frame.insert(frame.end(), spec.data.begin(), spec.data.end());
    }
    else
    {
        PutBe16(frame, 0x0800);
        const size_t ip = frame.size();
        const uint16_t total = (uint16_t)(20 + 8 + spec.data.size());
        frame.push_back(0x45);              // version 4, header of 5 words
        frame.push_back(0);                 // DSCP / ECN
        PutBe16(frame, total);
        PutBe16(frame, 0);                  // identification
        PutBe16(frame, 0x4000);             // don't fragment
        frame.push_back(spec.ttl);
        frame.push_back(17);                // UDP
        PutBe16(frame, 0);                  // header checksum, below
        frame.insert(frame.end(), spec.srcIp, spec.srcIp + 4);
        frame.insert(frame.end(), spec.dstIp, spec.dstIp + 4);
        const uint16_t ipSum = Checksum(&frame[ip], 20, 0);
        frame[ip + 10] = (uint8_t)(ipSum >> 8);
        frame[ip + 11] = (uint8_t)ipSum;

        const size_t udp = frame.size();
        const uint16_t udpLength = (uint16_t)(8 + spec.data.size());
        PutBe16(frame, spec.srcPort);
        PutBe16(frame, spec.dstPort);
        PutBe16(frame, udpLength);
        PutBe16(frame, 0);                  // checksum, below
        frame.insert(frame.end(), spec.data.begin(), spec.data.end());
        /* pseudo header: addresses, protocol, UDP length */
        uint32_t sum = 0;
        sum += Be16(spec.srcIp) + Be16(spec.srcIp + 2) + Be16(spec.dstIp) + Be16(spec.dstIp + 2);
        sum += 17 + udpLength;
        uint16_t udpSum = Checksum(&frame[udp], udpLength, sum);
        if (udpSum == 0)
        {
            udpSum = 0xffff;                // 0 means "no checksum"
        }
        frame[udp + 6] = (uint8_t)(udpSum >> 8);
        frame[udp + 7] = (uint8_t)udpSum;
    }
    if (frame.size() < MIN_FRAME)
    {
        frame.resize(MIN_FRAME, 0);
    }
    return true;
}

}
