# Raspberry Pi Pico CAN interface for BUSMASTER-NG

Firmware that turns a Raspberry Pi Pico 2 into a three-channel CAN interface for BUSMASTER-NG, or a Pico (the first
one) into a two-channel interface. BUSMASTER-NG uses it with the driver *Teensy / Pico* (`CAN_Teensy.dll`), the same
as for a Teensy 4.0.

The Pico has no CAN controller. The CAN protocol is done in software by
[can2040](https://github.com/KevinOConnor/can2040) (Kevin O'Connor), which uses one PIO block of the chip for each
bus: three on the RP2350 of the Pico 2, two on the RP2040 of the Pico.

| BUSMASTER interface | TX pin | RX pin | |
|---|---|---|---|
| `... - CAN0` | GP2 (pin 4) | GP3 (pin 5) | |
| `... - CAN1` | GP6 (pin 9) | GP7 (pin 10) | |
| `... - CAN2` | GP10 (pin 14) | GP11 (pin 15) | Pico 2 only |

## Hardware

Each channel needs a CAN transceiver that works with 3.3 V logic: the inputs of the Pico do not stand 5 V. The
simplest is a SN65HVD230 module, powered from the 3V3 pin of the Pico: 3V3, GND, the TX pin of the channel to the
`TX` / `D` input of the module and the RX pin to its `RX` / `R` output. Many of these modules have a 120 ohm
termination resistor soldered: remove it when the bus is already terminated at both ends.

### Trying it without transceivers

Two or three channels of the same Pico can be wired together without any transceiver:

- join the RX pins of the channels: this is the "bus"; pull it up to 3V3 with a resistor of about 1 kOhm;
- connect each TX pin to the bus through a diode (1N4148 for example), the cathode on the TX pin.

A channel that sends a dominant bit pulls the bus low through its diode, as a transceiver would. This tests the
firmware and the driver, not the connection to a real bus.

## Build and load

The project is a [PlatformIO](https://platformio.org) project. The first build downloads the Arduino core for the
Pico (arduino-pico, through the platform of Max Gerhardt, because the platform of PlatformIO does not know the
RP2350) and can2040. In this folder:

    pio run -e pico2                build for a Pico 2          (-e pico for a Pico)
    pio run -e pico2 -t upload      build and load

The first time, hold the BOOTSEL button while plugging the board, so it shows up as a USB drive; afterwards the
upload restarts the board by itself.

## Use

1. Plug the Pico in USB. Windows 10 / 11 give it a COM port without any driver to install.
2. In BUSMASTER-NG choose *CAN → Driver Selection → Teensy / Pico*. The Hardware Selection dialog lists the
   channels of every board found; give them to BUSMASTER channels and choose the bit rate of each (5 kbit/s to
   1 Mbit/s).
3. Connect.

The LED of the Pico is on while a channel is open and flickers with the traffic. The COM port is used only while
BUSMASTER-NG lists the boards or is connected.

## What it does, and what a software CAN node does not

- Classic CAN, standard and extended identifiers, remote frames, any bit rate up to 1 Mbit/s. No CAN FD.
- Frames are time stamped with the microsecond clock of the Pico, when the interrupt of the PIO block handles
  them.
- A sent frame comes back to BUSMASTER-NG when it is really on the bus. Frames of a channel are sent in the order
  BUSMASTER-NG gave them (256 can wait).
- **No listen only mode**: can2040 always acknowledges the frames it receives. Connecting in *passive* mode is
  refused with a message.
- **No error counters, no bus off**: the counters stay at 0 and the bus state stays "active". can2040 does not send
  error frames either. Frames it cannot read (stuffing, CRC or form error) are reported to BUSMASTER-NG as bus
  errors, without their kind.
- With nobody to acknowledge, a frame is sent again and again, as on any CAN node; the frames behind it wait.
- When the program that uses the port goes away (DTR drops), all channels are closed.

## License

The files of this folder are under the LGPL v3 as the rest of BUSMASTER-NG. can2040 is under the GPL v3 and is
not in this repository: PlatformIO fetches it. A firmware binary built from both is under the GPL v3.

The serial protocol is described in
[Sources/BUSMASTER/CAN_Teensy/TeensyProtocol.h](../../Sources/BUSMASTER/CAN_Teensy/TeensyProtocol.h), shared with
the Teensy firmware. `Firmware/Teensy4_CAN/tools/bmt_test.py` also talks to a Pico: board information, and `--loop` between all its
channels wired together (`--listen` is refused: no listen only mode).
