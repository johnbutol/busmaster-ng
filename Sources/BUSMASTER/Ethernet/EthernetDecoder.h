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
 * \file      EthernetDecoder.h
 * \brief     Decoding and building of Ethernet frames (no MFC, no Windows)
 *
 * Decoding: MAC addresses, VLAN tags (802.1Q, 802.1ad), EtherType, IPv4 / IPv6 with UDP / TCP / ICMP, ARP, and
 * a short description of some protocols of the automotive and industrial networks (SOME/IP, DoIP, PTP / gPTP,
 * AVTP, LLDP). Building: raw Ethernet frames and UDP over IPv4, for the transmit window.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace eth
{

const size_t MAC_LEN = 6;
const size_t MIN_FRAME = 60;        ///< without the frame check sequence, which the adapter adds
const size_t MAX_PAYLOAD = 1500;

/** What the message window shows of a frame */
struct Decoded
{
    uint8_t  dst[MAC_LEN] = {};
    uint8_t  src[MAC_LEN] = {};
    int      vlanId = -1;           ///< VLAN ID of the outer tag, -1 without tag
    int      vlanPriority = 0;      ///< PCP of the outer tag
    int      vlanTags = 0;          ///< number of VLAN tags
    uint16_t etherType = 0;         ///< after the VLAN tags; 0 for an IEEE 802.3 frame with length field
    std::string protocol;           ///< "IPv4/UDP", "ARP", "PTP", ...
    std::string source;             ///< IP address and port when there is one, else empty
    std::string destination;
    std::string info;               ///< short description
    size_t   payloadOffset = 0;     ///< first byte after the Ethernet header and the VLAN tags
    uint64_t kindKey = 0;           ///< same for the frames of the same kind (overwrite mode of the window)
};

/** Decodes a frame; false if it is shorter than an Ethernet header (d then has what could be read) */
bool Decode(const uint8_t* frame, size_t length, Decoded& d);

/** "01:23:45:67:89:ab" */
std::string MacToString(const uint8_t* mac);

/** Accepts "01:23:45:67:89:AB", "01-23-45-67-89-ab" and "0123456789ab" */
bool ParseMac(const std::string& text, uint8_t mac[MAC_LEN]);

/** "a.b.c.d" -> address in network order in out[4] */
bool ParseIPv4(const std::string& text, uint8_t out[4]);

/** Hex bytes, with or without spaces between them ("01 02 0a", "01020A"); false on another character or an
 *  odd number of digits in a group */
bool ParseHexBytes(const std::string& text, std::vector<uint8_t>& out);

/** "01 02 0A ..." */
std::string HexBytes(const uint8_t* data, size_t length, size_t maxBytes);

/** Name of an EtherType, nullptr if unknown */
const char* EtherTypeName(uint16_t etherType);

/** Description of a frame to send */
struct FrameSpec
{
    enum Kind { Raw, UdpIPv4 };
    Kind     kind = Raw;
    uint8_t  dst[MAC_LEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    uint8_t  src[MAC_LEN] = {};
    int      vlanId = -1;           ///< -1: no tag
    int      vlanPriority = 0;
    uint16_t etherType = 0x88b5;    ///< raw frames (0x88b5: local experimental EtherType)
    uint8_t  srcIp[4] = {};
    uint8_t  dstIp[4] = {};
    uint16_t srcPort = 0;
    uint16_t dstPort = 0;
    uint8_t  ttl = 64;
    std::vector<uint8_t> data;      ///< payload of the frame, or of the UDP datagram
};

/** Builds the bytes of a frame (padded to 60 bytes); false with a reason if the data is too long */
bool Build(const FrameSpec& spec, std::vector<uint8_t>& frame, std::string& error);

}
