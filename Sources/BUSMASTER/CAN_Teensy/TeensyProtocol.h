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
 * @file  TeensyProtocol.h
 * @brief Serial protocol between BUSMASTER-NG (CAN_Teensy.dll) and the Teensy 4.0 CAN firmware
 *        (Firmware/Teensy4_CAN). Plain C, shared by both sides.
 *
 * The Teensy is a USB serial device (virtual COM port; the baud rate of the port does not matter).
 * Both directions use the same packet:
 *
 *     0xA5 | type | length | payload (length bytes, 0..64) | crc8
 *
 * crc8 (polynomial 0x07, start 0) covers type, length and payload. Numbers are little endian.
 * The host must raise DTR; when DTR drops (port closed, program gone) the firmware closes both
 * channels, so the Teensy stops acknowledging frames on the buses.
 *
 * Host -> Teensy
 *   HELLO       (no payload)                          closes all channels, answered by INFO
 *   OPEN        channel u8, mode u8, bit rate u32     answered by ACK
 *   CLOSE       channel u8                            answered by ACK
 *   TX          channel u8, flags u8, id u32, dlc u8, data[dlc]
 *               not answered: the frame comes back as FRAME with BMT_FRAME_TX once it is on the
 *               bus; a frame that cannot be queued is counted in STATUS (txDropped)
 *   GET_STATUS  channel u8                            answered by STATUS
 *
 * Teensy -> host
 *   INFO        protocol u8, firmware major u8, minor u8, channel count u8,
 *               capabilities u8 for each channel, serial number u32,
 *               text "board;name of channel 0;name of channel 1" (not terminated)
 *   ACK         command u8, channel u8, result u8 (BMT_ACK_...)
 *   FRAME       channel u8, flags u8, timestamp u32, id u32, dlc u8, data[dlc]
 *               (no data for a remote frame)
 *   STATUS      channel u8, open u8, bus state u8, TEC u8, REC u8, timestamp u32,
 *               rxLost u32, txDropped u32, txPending u16
 *               sent every 100 ms for each open channel
 *   ERROR       channel u8, timestamp u32, ESR1 u32 (FlexCAN error and status register),
 *               TEC u8, REC u8          a bus error was detected (at most one every 10 ms)
 *
 * Timestamps are microseconds of the Teensy clock, 32 bit (wrap after 71.6 minutes; the host
 * extends them, STATUS arrives often enough).
 */

#pragma once

#define BMT_SOF                 0xA5
#define BMT_PROTOCOL_VERSION    1
#define BMT_MAX_PAYLOAD         64
#define BMT_CHANNEL_COUNT       2

/* USB identity of a Teensy in "Serial" mode (PJRC) */
#define BMT_USB_VID             0x16C0
#define BMT_USB_PID             0x0483

/* host -> Teensy */
#define BMT_CMD_HELLO           0x01
#define BMT_CMD_OPEN            0x02
#define BMT_CMD_CLOSE           0x03
#define BMT_CMD_TX              0x04
#define BMT_CMD_GET_STATUS      0x05

/* Teensy -> host */
#define BMT_MSG_INFO            0x81
#define BMT_MSG_ACK             0x82
#define BMT_MSG_FRAME           0x83
#define BMT_MSG_STATUS          0x84
#define BMT_MSG_ERROR           0x85

/* OPEN: mode */
#define BMT_MODE_NORMAL         0x00
#define BMT_MODE_LISTEN_ONLY    0x01

/* TX and FRAME: flags */
#define BMT_FRAME_EXTENDED      0x01
#define BMT_FRAME_REMOTE        0x02
#define BMT_FRAME_TX            0x04    /* FRAME only: sent by this interface */

/* INFO: capabilities of a channel */
#define BMT_CAP_CANFD           0x01    /* the controller can do CAN FD (not used by this firmware yet) */

/* ACK: result */
#define BMT_ACK_OK              0
#define BMT_ACK_BAD_CHANNEL     1
#define BMT_ACK_BAD_BITRATE     2       /* the bit rate cannot be made from the controller clock */
#define BMT_ACK_BAD_PARAMETER   3

/* STATUS: bus state */
#define BMT_BUS_ACTIVE          0
#define BMT_BUS_WARNING         1
#define BMT_BUS_PASSIVE         2
#define BMT_BUS_OFF             3

/* ERROR: bits of ESR1 that tell the kind of error */
#define BMT_ESR1_BIT1_ERROR     0x00008000u
#define BMT_ESR1_BIT0_ERROR     0x00004000u
#define BMT_ESR1_ACK_ERROR      0x00002000u
#define BMT_ESR1_CRC_ERROR      0x00001000u
#define BMT_ESR1_FORM_ERROR     0x00000800u
#define BMT_ESR1_STUFF_ERROR    0x00000400u
#define BMT_ESR1_ERROR_MASK     0x0000FC00u
#define BMT_ESR1_TRANSMITTING   0x00000040u    /* the error happened while this interface was sending */

/* sizes of the fixed part of the payloads */
#define BMT_SIZE_OPEN           6
#define BMT_SIZE_TX_HEADER      7
#define BMT_SIZE_FRAME_HEADER   11
#define BMT_SIZE_STATUS         19
#define BMT_SIZE_ERROR          11
#define BMT_SIZE_ACK            3
#define BMT_SIZE_INFO_HEADER    (4 + BMT_CHANNEL_COUNT + 4)

static inline unsigned char bmt_crc8(unsigned char crc, const unsigned char* data, unsigned int length)
{
    unsigned int i;
    int bit;
    for (i = 0; i < length; i++)
    {
        crc ^= data[i];
        for (bit = 0; bit < 8; bit++)
        {
            crc = (unsigned char)((crc & 0x80) ? ((crc << 1) ^ 0x07) : (crc << 1));
        }
    }
    return crc;
}
