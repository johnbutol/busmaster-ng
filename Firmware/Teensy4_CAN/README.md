# Teensy 4.0 CAN interface for BUSMASTER-NG

Firmware that turns a Teensy 4.0 into a two-channel CAN interface for BUSMASTER-NG (driver *Teensy 4.0*,
`CAN_Teensy.dll`).

| BUSMASTER interface | Controller | Teensy pins | Transceiver standby |
|---|---|---|---|
| `... - CAN1` | CAN1 | 22 (CTX1), 23 (CRX1) | pin 5 |
| `... - CAN3` | CAN3 | 31 (CTX3), 30 (CRX3) | pin 6 |

Each controller needs a CAN transceiver. The firmware was written for the *Ultimate Breakout Board for Teensy 4.0*
(two MCP2558FD transceivers on CAN1 and CAN3, 120 ohm terminations enabled by solder jumpers), but any board with
transceivers on these pins works. Pins 5 and 6 drive the standby input of the transceivers (high = standby while
the channel is closed, low = normal while it is open); on another board leave them free or change `STANDBY_PIN` in
`src/main.cpp`.

On that breakout board the transceivers take their 5 V from the VIN pin of the Teensy. A Teensy 4.0 as delivered
joins VIN and VUSB, so the USB cable alone powers everything. If the VIN-VUSB trace under the Teensy was cut, the
transceivers need the power input of the board (J3).

## Build and load

The project is a [PlatformIO](https://platformio.org) project (framework Arduino, library FlexCAN_T4 of
Teensyduino). In this folder:

    pio run                 build
    pio run -t upload       build and load (press the button of the Teensy if it is asked)

The USB type must stay *Serial* (`-D USB_SERIAL` in `platformio.ini`): BUSMASTER-NG looks for that USB identity.

## Use

1. Plug the Teensy in USB. Windows 10 / 11 give it a COM port without any driver to install.
2. In BUSMASTER-NG choose *CAN → Driver Selection → Teensy 4.0*. The Hardware Selection dialog lists the two
   controllers of every Teensy found; give them to channels and choose the bit rate of each (5 kbit/s to 1 Mbit/s).
3. Connect.

The LED of the Teensy is on while a channel is open and flickers with the traffic. The COM port is used only while
BUSMASTER-NG lists the boards or is connected, so the firmware can be loaded again in between.

## What it does

- Classic CAN on both channels, standard and extended identifiers, remote frames. No CAN FD yet (CAN3 could).
- Frames are time stamped in the CAN interrupt with the microsecond clock of the Teensy.
- A sent frame comes back to BUSMASTER-NG when it is really on the bus, with its time stamp. Frames of a channel
  are sent one at a time, in the order BUSMASTER-NG gave them (256 can wait).
- Error counters and bus state every 100 ms, bus errors (bit, stuff, form, CRC, acknowledge) as error frames.
- Listen only mode (*passive* in BUSMASTER-NG).
- When the program that uses the port goes away (DTR drops), both channels are closed: the Teensy no longer
  acknowledges frames on the buses.

## Test without BUSMASTER

`tools/bmt_test.py` (Python with pyserial) talks to the firmware directly:

    python tools/bmt_test.py              board information and status
    python tools/bmt_test.py --loop       the two CAN ports wired together: frames must cross in both directions
    python tools/bmt_test.py --listen 10  print what both channels receive for 10 seconds

The serial protocol is described in
[Sources/BUSMASTER/CAN_Teensy/TeensyProtocol.h](../../Sources/BUSMASTER/CAN_Teensy/TeensyProtocol.h); the firmware
and the driver share that file.
