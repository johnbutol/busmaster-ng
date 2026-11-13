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
 * CAN FD on the CAN3 controller of the Teensy 4.0 (FlexCAN with flexible data rate), on the
 * registers of the controller: the FD part of the FlexCAN_T4 library has no notification of
 * sent frames, no remote frames and no error report.
 *
 * 14 mailboxes with 64 data bytes: 13 for reception (any identifier), 1 for transmission, so
 * frames leave in the order they are given.
 */

#pragma once

#include <stdint.h>

/** A CAN frame; flags are the BMT_FRAME_... bits of TeensyProtocol.h. */
struct CanFrame
{
    uint32_t id;
    uint8_t flags;
    uint8_t length;         // data bytes (the DLC for a remote frame)
    uint8_t data[64];
};

namespace can3fd
{

/** The number of data bytes a frame can really have: 0..8, 12, 16, 20, 24, 32, 48, 64. */
uint8_t roundLength(uint8_t length);

/**
 * Starts the controller in CAN FD mode. The handlers are called in the CAN interrupt.
 * Returns BMT_ACK_OK, BMT_ACK_BAD_BITRATE or BMT_ACK_BAD_DATA_BITRATE.
 */
uint8_t begin(uint32_t bitRate, uint32_t dataBitRate, bool listenOnly, bool loopback,
              void (*received)(const CanFrame&), void (*transmitted)());

/** Stops the controller (it leaves the bus) and takes it out of FD mode. */
void end();

/** Puts a frame in the transmit mailbox; false when the mailbox is still busy. */
bool write(const CanFrame& frame);

/** A bus error seen since the last call: the ESR1 register, data phase errors on the usual bits. */
bool takeError(uint32_t& esr1);

/** Frames lost because the reception mailboxes were full. */
uint32_t overruns();

}
