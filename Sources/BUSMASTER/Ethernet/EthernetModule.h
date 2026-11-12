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
 * \file      EthernetModule.h
 * \brief     State of the Ethernet module shared by its windows: settings, connection, frames to send
 */

#pragma once

#include "EthernetCapture.h"
#include "EthernetDecoder.h"

/** Adapter and capture settings, kept in the registry */
struct EthSettings
{
    std::string device;             ///< pcap name of the adapter, empty: none selected
    std::string adapterName;        ///< for the status line while the adapter is not connected
    bool promiscuous = true;
    std::string filter;
};

/** Options of the message window, set from the ribbon */
struct EthDisplayOptions
{
    enum TimeMode { SystemTime, AbsoluteTime, RelativeTime };
    TimeMode timeMode = SystemTime;
    bool overwrite = false;         ///< one row per kind of frame (addresses, protocol, ports), updated in place
    bool showLast = true;           ///< scroll to the newest frame
    bool pause = false;             ///< frames are not taken while paused
};

/** A frame of the transmit window */
struct EthTxEntry
{
    std::string name;
    eth::FrameSpec spec;
    bool srcMacFromAdapter = true;  ///< source MAC: the one of the adapter
    bool srcIpFromAdapter = true;   ///< source IP (UDP): the first IPv4 address of the adapter
    unsigned cycleMs = 0;           ///< 0: sent only with Send
    bool cyclic = false;            ///< checked in the list: sent every cycle while connected
};

namespace ethmodule
{
eth::CEthernetCapture& Capture();
EthSettings& Settings();
EthDisplayOptions& Options();
std::vector<EthTxEntry>& TxEntries();

/** Saves the settings and the frames of the transmit window */
void SaveSettings();

/** Starts the capture on the adapter of the settings; false with a reason */
bool Connect(std::string& error);
void Disconnect();
bool IsConnected();

/** UTC microseconds of the connection, the time base of the absolute times */
int64_t ConnectTimeUs();

/** Why the capture stopped by itself, empty otherwise */
std::string StopReason();

/** Bytes of a frame of the transmit window, with the addresses of the adapter where they are left empty */
bool BuildTxFrame(const EthTxEntry& entry, std::vector<uint8_t>& frame, std::string& error);

/** To call after the frames to send or their checks changed: updates the cyclic transmission */
void TxChanged();
}
