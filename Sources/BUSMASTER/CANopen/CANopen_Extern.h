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
 * \file      CANopen_Extern.h
 * \brief     Functions of CANopen.dll called by the application
 */

#pragma once

#include <windows.h>

#if defined CANOPEN_EXPORT
#define CANOPEN_API __declspec(dllexport)
#else
#define CANOPEN_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Shows the CANopen window; the first call starts the analysis of the CAN traffic */
CANOPEN_API HRESULT CANopen_ShowWindow(HWND hMainFrame);

/** To call after a CAN driver was selected: attaches the analysis to the new driver if it is running */
CANOPEN_API HRESULT CANopen_DoInitialization(void);

/** To call when the CAN bus is connected, after the driver started: its time reference is read with the
 *  first message, as the message window does, so that both windows show the same times */
CANOPEN_API void CANopen_OnConnect(void);

/** Stops the analysis and closes the window; to call before the CAN driver is released */
CANOPEN_API void CANopen_Shutdown(void);

/** Commands of the CANopen drop-down of the ribbon, as those of the message window */
enum ECANopenCommand
{
    CANOPEN_CMD_ACTIVATE,           ///< shows the window, or hides it if it is shown
    CANOPEN_CMD_CLEAR,
    CANOPEN_CMD_SYSTEM_TIME,
    CANOPEN_CMD_ABSOLUTE_TIME,
    CANOPEN_CMD_RELATIVE_TIME,
    CANOPEN_CMD_OVERWRITE,
    CANOPEN_CMD_SHOW_LAST,
    CANOPEN_CMD_PAUSE,
    CANOPEN_CMD_COUNT
};

#define CANOPEN_STATE_ENABLED   0x01
#define CANOPEN_STATE_CHECKED   0x02

/** Runs a command of ECANopenCommand */
CANOPEN_API HRESULT CANopen_Command(HWND hMainFrame, int nCommand);

/** State of a command of ECANopenCommand: CANOPEN_STATE_ENABLED, CANOPEN_STATE_CHECKED */
CANOPEN_API int CANopen_GetCommandState(int nCommand);

#ifdef __cplusplus
}
#endif
