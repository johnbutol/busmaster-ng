#!/usr/bin/env python3
"""Test of the BUSMASTER-NG CAN firmware of the Teensy 4.0, without BUSMASTER.

Needs pyserial (pip install pyserial).

    python bmt_test.py                 information of the board, status of both channels
    python bmt_test.py --loop          the two CAN ports wired together (CANH-CANH, CANL-CANL,
                                       both terminations on): frames sent on one channel must
                                       arrive on the other, in both directions. If other nodes
                                       are on that bus, give their bit rate with --bitrate
    python bmt_test.py --listen 10     prints what both channels receive for 10 seconds
    options: --port COM7   --bitrate 500000

The protocol is in Sources/BUSMASTER/CAN_Teensy/TeensyProtocol.h.
"""

import argparse
import struct
import sys
import time

import serial
import serial.tools.list_ports

SOF = 0xA5
CMD_HELLO, CMD_OPEN, CMD_CLOSE, CMD_TX, CMD_GET_STATUS = 1, 2, 3, 4, 5
MSG_INFO, MSG_ACK, MSG_FRAME, MSG_STATUS, MSG_ERROR = 0x81, 0x82, 0x83, 0x84, 0x85
FRAME_EXTENDED, FRAME_REMOTE, FRAME_TX = 1, 2, 4
BUS_STATES = ["active", "warning", "passive", "bus off"]
ACK_RESULTS = ["ok", "bad channel", "bad bit rate", "bad parameter"]


def crc8(data):
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def packet(kind, payload=b""):
    body = bytes([kind, len(payload)]) + payload
    return bytes([SOF]) + body + bytes([crc8(body)])


class Board:
    def __init__(self, port):
        self.serial = serial.Serial(port, 115200, timeout=0.05)     # DTR is set: the firmware answers
        self.buffer = b""

    def send(self, kind, payload=b""):
        self.serial.write(packet(kind, payload))

    def packets(self, duration):
        """Yields (type, payload) for the packets that arrive during 'duration' seconds."""
        end = time.time() + duration
        while time.time() < end:
            self.buffer += self.serial.read(4096)
            while True:
                start = self.buffer.find(bytes([SOF]))
                if start < 0:
                    self.buffer = b""
                    break
                self.buffer = self.buffer[start:]
                if len(self.buffer) < 4:
                    break
                length = self.buffer[2]
                if length > 64:
                    self.buffer = self.buffer[1:]
                    continue
                if len(self.buffer) < length + 4:
                    break
                if crc8(self.buffer[1:3 + length]) == self.buffer[3 + length]:
                    yield self.buffer[1], self.buffer[3:3 + length]
                    self.buffer = self.buffer[length + 4:]
                else:
                    self.buffer = self.buffer[1:]

    def wait(self, kind, duration=1.0, match=lambda payload: True):
        for received, payload in self.packets(duration):
            if received == kind and match(payload):
                return payload
        return None

    def hello(self):
        for _ in range(5):
            self.send(CMD_HELLO)
            info = self.wait(MSG_INFO, 0.3)
            if info:
                return info
        return None

    def open(self, channel, bitrate, listen_only=False):
        self.send(CMD_OPEN, struct.pack("<BBI", channel, 1 if listen_only else 0, bitrate))
        ack = self.wait(MSG_ACK, 1.5, lambda p: p[0] == CMD_OPEN and p[1] == channel)
        return ack[2] if ack else None

    def close(self, channel):
        self.send(CMD_CLOSE, bytes([channel]))
        return self.wait(MSG_ACK, 1.0, lambda p: p[0] == CMD_CLOSE and p[1] == channel) is not None

    def transmit(self, channel, can_id, data, extended=False, remote=False):
        flags = (FRAME_EXTENDED if extended else 0) | (FRAME_REMOTE if remote else 0)
        self.send(CMD_TX, struct.pack("<BBIB", channel, flags, can_id, len(data)) + (b"" if remote else bytes(data)))

    def status(self, channel):
        self.send(CMD_GET_STATUS, bytes([channel]))
        return self.wait(MSG_STATUS, 1.0, lambda p: p[0] == channel)


def decode_frame(payload):
    channel, flags, timestamp, can_id, dlc = struct.unpack("<BBIIB", payload[:11])
    return channel, flags, timestamp, can_id, dlc, payload[11:]


def frame_text(payload):
    channel, flags, timestamp, can_id, dlc, data = decode_frame(payload)
    kind = "Tx" if flags & FRAME_TX else "Rx"
    ident = "%08X" % can_id if flags & FRAME_EXTENDED else "%03X" % can_id
    body = "remote" if flags & FRAME_REMOTE else data.hex(" ")
    return "%10.4f s  ch%d %s %s [%d] %s" % (timestamp / 1e6, channel, kind, ident, dlc, body)


def status_text(payload):
    channel, is_open, state, tec, rec, timestamp, rx_lost, tx_dropped, pending = struct.unpack("<BBBBBIIIH", payload[:19])
    return "ch%d %s, bus %s, TEC %d, REC %d, rx lost %d, tx dropped %d, tx pending %d" % (
        channel, "open" if is_open else "closed", BUS_STATES[min(state, 3)], tec, rec, rx_lost, tx_dropped, pending)


def find_port():
    ports = [p.device for p in serial.tools.list_ports.comports() if p.vid == 0x16C0 and p.pid == 0x0483]
    if not ports:
        sys.exit("No Teensy serial port found (USB 16C0:0483). Is it plugged, with the firmware loaded?")
    return ports[0]


def loop_test(board, bitrate):
    """Both channels on the same bus: every frame sent on one must be received on the other."""
    sent = []
    for index in range(200):
        channel = index % 2
        extended = index % 4 >= 2
        can_id = (0x18DA0000 + index) if extended else (0x100 + index)
        data = bytes((index + k) & 0xFF for k in range(index % 9))
        board.transmit(channel, can_id, data, extended)
        sent.append((channel, can_id, extended, data))
    echoes, received, errors, foreign = [], [], 0, 0
    known = set((i, e) for _, i, e, _ in sent)
    # 200 frames of at most 160 bits, and time for the frames of other nodes on the bus
    for kind, payload in board.packets(1.0 + 200 * 160 * 2.0 / bitrate):
        if kind == MSG_FRAME:
            channel, flags, _, can_id, dlc, data = decode_frame(payload)
            entry = (channel, can_id, bool(flags & FRAME_EXTENDED), data)
            if (entry[1], entry[2]) not in known:
                foreign += 1            # another node of the bus
            else:
                (echoes if flags & FRAME_TX else received).append(entry)
        elif kind == MSG_ERROR:
            errors += 1
    expected = [(1 - c, i, e, d) for c, i, e, d in sent]
    print("sent %d, echoes %d, received on the other channel %d, bus errors %d, frames of other nodes %d" % (
        len(sent), len(echoes), len(received), errors, foreign))
    ok = True
    for channel in (0, 1):
        if [e for e in echoes if e[0] == channel] != [s for s in sent if s[0] == channel]:
            print("FAIL: echoes of channel %d differ from what was sent (or are in another order)" % channel)
            ok = False
        if [r for r in received if r[0] == channel] != [x for x in expected if x[0] == channel]:
            print("FAIL: frames received on channel %d differ from what the other channel sent" % channel)
            ok = False
    # remote frame
    board.transmit(0, 0x7F0, bytes(4), remote=True)
    remote = [decode_frame(p) for k, p in board.packets(0.5 + 400.0 / bitrate) if k == MSG_FRAME]
    if not any(f[0] == 1 and f[1] & FRAME_REMOTE and f[3] == 0x7F0 and f[4] == 4 for f in remote):
        print("FAIL: the remote frame was not received on channel 1")
        ok = False
    print("PASS" if ok else "FAILED")
    return ok


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port")
    parser.add_argument("--bitrate", type=int, default=500000)
    parser.add_argument("--loop", action="store_true")
    parser.add_argument("--listen", type=float, metavar="SECONDS")
    args = parser.parse_args()

    port = args.port or find_port()
    board = Board(port)
    info = board.hello()
    if not info:
        sys.exit("%s does not answer: the Teensy does not run the BUSMASTER-NG CAN firmware" % port)
    protocol, major, minor, channels = info[0], info[1], info[2], info[3]
    serial_number = struct.unpack("<I", info[4 + channels:8 + channels])[0]
    print("%s: %s, serial %d, firmware %d.%d, protocol %d" % (
        port, info[8 + channels:].decode("ascii", "replace"), serial_number, major, minor, protocol))

    ok = True
    if args.loop or args.listen:
        for channel in range(channels):
            result = board.open(channel, args.bitrate, listen_only=bool(args.listen))
            print("open ch%d at %d bit/s: %s" % (channel, args.bitrate, "no answer" if result is None else ACK_RESULTS[min(result, 3)]))
            ok = ok and result == 0
    if args.loop and ok:
        ok = loop_test(board, args.bitrate)
    if args.listen and ok:
        for kind, payload in board.packets(args.listen):
            if kind == MSG_FRAME:
                print(frame_text(payload))
            elif kind == MSG_ERROR:
                channel, timestamp, esr1, tec, rec = struct.unpack("<BIIBB", payload[:11])
                print("%10.4f s  ch%d bus error ESR1=%08X TEC %d REC %d" % (timestamp / 1e6, channel, esr1, tec, rec))
    for channel in range(channels):
        status = board.status(channel)
        print(status_text(status) if status else "ch%d: no status" % channel)
    for channel in range(channels):
        board.close(channel)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
