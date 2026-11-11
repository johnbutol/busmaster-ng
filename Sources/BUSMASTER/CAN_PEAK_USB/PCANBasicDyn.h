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
 * \file      PCANBasicDyn.h
 * \brief     BUSMASTER-NG: the part of the PEAK PCAN-Basic API used by CAN_PEAK_USB, loaded at run time.
 *
 * PCANBasic.dll is installed with the PEAK device driver (and is free to redistribute). It is loaded with
 * LoadLibrary when the driver is selected, so nothing from PEAK is needed to build BUSMASTER-NG and the
 * program starts normally on a PC without PEAK software. Only the long-standing functions and parameters of
 * PCAN-Basic (version 4 and later) are declared here.
 */
#pragma once

#include <windows.h>

typedef WORD  TPCANHandle;
typedef DWORD TPCANStatus;
typedef BYTE  TPCANParameter;
typedef BYTE  TPCANMessageType;
typedef BYTE  TPCANType;
typedef WORD  TPCANBaudrate;

/* Channel handles */
#define PCAN_NONEBUS            0x00U
#define PCAN_ISABUS1            0x21U
#define PCAN_DNGBUS1            0x31U
#define PCAN_PCIBUS1            0x41U   /* PCAN_PCIBUS1..8  = 0x41..0x48 */
#define PCAN_PCIBUS9            0x409U  /* PCAN_PCIBUS9..16 = 0x409..0x410 */
#define PCAN_USBBUS1            0x51U   /* PCAN_USBBUS1..8  = 0x51..0x58 */
#define PCAN_USBBUS9            0x509U  /* PCAN_USBBUS9..16 = 0x509..0x510 */
#define PCAN_PCCBUS1            0x61U   /* PCAN_PCCBUS1..2  = 0x61..0x62 */
#define PCAN_LANBUS1            0x801U  /* PCAN_LANBUS1..16 = 0x801..0x810 */

/* Status / error codes (bit field) */
#define PCAN_ERROR_OK           0x00000U
#define PCAN_ERROR_XMTFULL      0x00001U
#define PCAN_ERROR_OVERRUN      0x00002U
#define PCAN_ERROR_BUSLIGHT     0x00004U
#define PCAN_ERROR_BUSHEAVY     0x00008U
#define PCAN_ERROR_BUSWARNING   PCAN_ERROR_BUSHEAVY
#define PCAN_ERROR_BUSPASSIVE   0x40000U
#define PCAN_ERROR_BUSOFF       0x00010U
#define PCAN_ERROR_ANYBUSERR    (PCAN_ERROR_BUSWARNING | PCAN_ERROR_BUSLIGHT | PCAN_ERROR_BUSHEAVY | PCAN_ERROR_BUSOFF | PCAN_ERROR_BUSPASSIVE)
#define PCAN_ERROR_QRCVEMPTY    0x00020U
#define PCAN_ERROR_QOVERRUN     0x00040U
#define PCAN_ERROR_QXMTFULL     0x00080U
#define PCAN_ERROR_NODRIVER     0x00200U
#define PCAN_ERROR_HWINUSE      0x00400U
#define PCAN_ERROR_NETINUSE     0x00800U
#define PCAN_ERROR_ILLHW        0x01400U
#define PCAN_ERROR_ILLNET       0x01800U
#define PCAN_ERROR_ILLCLIENT    0x01C00U
#define PCAN_ERROR_INITIALIZE   0x4000000U

/* Parameters */
#define PCAN_DEVICE_ID          0x01U   /* (PCAN_DEVICE_NUMBER in older versions) */
#define PCAN_RECEIVE_EVENT      0x03U
#define PCAN_API_VERSION        0x05U
#define PCAN_CHANNEL_VERSION    0x06U
#define PCAN_BUSOFF_AUTORESET   0x07U
#define PCAN_LISTEN_ONLY        0x08U
#define PCAN_CHANNEL_CONDITION  0x0DU
#define PCAN_HARDWARE_NAME      0x0EU

#define PCAN_PARAMETER_OFF      0x00U
#define PCAN_PARAMETER_ON       0x01U

/* Values of PCAN_CHANNEL_CONDITION */
#define PCAN_CHANNEL_UNAVAILABLE 0x00U
#define PCAN_CHANNEL_AVAILABLE   0x01U
#define PCAN_CHANNEL_OCCUPIED    0x02U
#define PCAN_CHANNEL_PCANVIEW    (PCAN_CHANNEL_AVAILABLE | PCAN_CHANNEL_OCCUPIED)

#define MAX_LENGTH_HARDWARE_NAME 33
#define MAX_LENGTH_VERSION_STRING 256

/* Message types (bit field) */
#define PCAN_MESSAGE_STANDARD   0x00U
#define PCAN_MESSAGE_RTR        0x01U
#define PCAN_MESSAGE_EXTENDED   0x02U
#define PCAN_MESSAGE_FD         0x04U
#define PCAN_MESSAGE_BRS        0x08U
#define PCAN_MESSAGE_ESI        0x10U
#define PCAN_MESSAGE_ECHO       0x20U
#define PCAN_MESSAGE_ERRFRAME   0x40U
#define PCAN_MESSAGE_STATUS     0x80U

/* Bit rates as BTR0/BTR1 of an SJA1000 at 16 MHz (the values PEAK recommends) */
#define PCAN_BAUD_1M            0x0014U
#define PCAN_BAUD_800K          0x0016U
#define PCAN_BAUD_500K          0x001CU
#define PCAN_BAUD_250K          0x011CU
#define PCAN_BAUD_125K          0x031CU
#define PCAN_BAUD_100K          0x432FU
#define PCAN_BAUD_95K           0xC34EU
#define PCAN_BAUD_83K           0x852BU
#define PCAN_BAUD_50K           0x472FU
#define PCAN_BAUD_47K           0x1414U
#define PCAN_BAUD_33K           0x8B2FU
#define PCAN_BAUD_20K           0x532FU
#define PCAN_BAUD_10K           0x672FU
#define PCAN_BAUD_5K            0x7F7FU

typedef struct tagTPCANMsg
{
    DWORD            ID;
    TPCANMessageType MSGTYPE;
    BYTE             LEN;
    BYTE             DATA[8];
} TPCANMsg;

typedef struct tagTPCANTimestamp
{
    DWORD millis;           /* milliseconds */
    WORD  millis_overflow;  /* times millis wrapped around */
    WORD  micros;           /* microseconds 0..999 */
} TPCANTimestamp;

typedef TPCANStatus (__stdcall* PFN_CAN_Initialize)(TPCANHandle Channel, TPCANBaudrate Btr0Btr1, TPCANType HwType, DWORD IOPort, WORD Interrupt);
typedef TPCANStatus (__stdcall* PFN_CAN_Uninitialize)(TPCANHandle Channel);
typedef TPCANStatus (__stdcall* PFN_CAN_Reset)(TPCANHandle Channel);
typedef TPCANStatus (__stdcall* PFN_CAN_GetStatus)(TPCANHandle Channel);
typedef TPCANStatus (__stdcall* PFN_CAN_Read)(TPCANHandle Channel, TPCANMsg* MessageBuffer, TPCANTimestamp* TimestampBuffer);
typedef TPCANStatus (__stdcall* PFN_CAN_Write)(TPCANHandle Channel, TPCANMsg* MessageBuffer);
typedef TPCANStatus (__stdcall* PFN_CAN_GetValue)(TPCANHandle Channel, TPCANParameter Parameter, void* Buffer, DWORD BufferLength);
typedef TPCANStatus (__stdcall* PFN_CAN_SetValue)(TPCANHandle Channel, TPCANParameter Parameter, void* Buffer, DWORD BufferLength);
typedef TPCANStatus (__stdcall* PFN_CAN_GetErrorText)(TPCANStatus Error, WORD Language, LPSTR Buffer);
