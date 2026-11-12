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
 * \file      Ethernet_Extern.h
 * \brief     Functions of Ethernet.dll called by the application
 *
 * Ethernet.dll captures and sends Ethernet frames on a network adapter through Npcap (or WinPcap), which is
 * loaded at run time: BUSMASTER starts without it. It has its own windows (message window, transmit window)
 * and does not use the bus drivers of BUSMASTER.
 */

#pragma once

#include <windows.h>

#if defined ETHERNET_EXPORT
#define ETHERNET_API __declspec(dllexport)
#else
#define ETHERNET_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Commands of the Ethernet tab of the ribbon, in the order of their command IDs */
enum EEthernetCommand
{
    ETH_CMD_CONNECT,            ///< starts the capture on the selected adapter, or stops it
    ETH_CMD_ADAPTER,            ///< adapter selection dialog
    ETH_CMD_MSG_WINDOW,         ///< shows the message window, or hides it if it is shown
    ETH_CMD_TX_WINDOW,          ///< shows the transmit window, or hides it if it is shown
    ETH_CMD_CLEAR,
    ETH_CMD_SYSTEM_TIME,
    ETH_CMD_ABSOLUTE_TIME,
    ETH_CMD_RELATIVE_TIME,
    ETH_CMD_OVERWRITE,
    ETH_CMD_SHOW_LAST,
    ETH_CMD_PAUSE,
    ETH_CMD_SAVE_PCAP,          ///< saves the frames of the message window in a pcap file (Wireshark)
    ETH_CMD_COUNT
};

#define ETH_STATE_ENABLED   0x01
#define ETH_STATE_CHECKED   0x02

/** Runs a command of EEthernetCommand; hMainFrame is the main window (CMDIFrameWnd) */
ETHERNET_API HRESULT Ethernet_Command(HWND hMainFrame, int nCommand);

/** State of a command of EEthernetCommand: ETH_STATE_ENABLED, ETH_STATE_CHECKED */
ETHERNET_API int Ethernet_GetCommandState(int nCommand);

/** True while the capture runs */
ETHERNET_API BOOL Ethernet_IsConnected(void);

/** Stops the capture and the transmission and closes the windows; to call when the main window closes */
ETHERNET_API void Ethernet_Shutdown(void);

#ifdef __cplusplus
}
#endif
