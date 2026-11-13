#!/usr/bin/env python3
"""Test of the BUSMASTER-NG CAN firmware of the Teensy 4.0, without BUSMASTER.

Needs pyserial (pip install pyserial).

    python bmt_test.py                 information of the board, status of both channels
    python bmt_test.py --selftest      nothing needs to be wired: frames are looped inside the
                                       controllers (classic CAN on both channels, CAN FD with
                                       every frame length on channel 1), nothing goes to the bus
    python bmt_test.py --loop          the two CAN ports wired together (CANH-CANH, CANL-CANL,
                                       both terminations on): frames sent on one channel must
                                       arrive on the other, in both directions. If other nodes
                                       are on that bus, give their bit rate with --bitrate.
                                       With --fd channel 1 runs its CAN FD controller (classic
                                       frames only: channel 0 is a classic controller)
    python bmt_test.py --fdalone       channel 1 alone on its bus (nothing else connected, the
                                       termination on): CAN FD frames with bit rate switch are
                                       sent; nobody acknowledges them, but any other error
                                       means the fast data bits are not read back correctly
    python bmt_test.py --listen 10     prints what both channels receive for 10 seconds
                                       (with --fd channel 1 listens in CAN FD mode)
    options: --port COM7   --bitrate 500000   --databitrate 2000000

The protocol is in Sources/BUSMASTER/CAN_Teensy/TeensyProtocol.h.
"""

import argparse
import struct
import sys
import time

import serial
import serial.tools.list_ports

PROTOCOL = 2
SOF = 0xA5
MAX_PAYLOAD = 80
CMD_HELLO, CMD_OPEN, CMD_CLOSE, CMD_TX, CMD_GET_STATUS = 1, 2, 3, 4, 5
MSG_INFO, MSG_ACK, MSG_FRAME, MSG_STATUS, MSG_ERROR = 0x81, 0x82, 0x83, 0x84, 0x85
MODE_LISTEN_ONLY, MODE_FD, MODE_LOOPBACK = 1, 2, 4
FRAME_EXTENDED, FRAME_REMOTE, FRAME_TX, FRAME_FD, FRAME_BRS, FRAME_ESI = 1, 2, 4, 8, 0x10, 0x20
BUS_STATES = ["active", "warning", "passive", "bus off"]
ACK_RESULTS = ["ok", "bad channel", "bad bit rate", "bad parameter", "no CAN FD on this channel", "bad data bit rate"]
FD_LENGTHS = [0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64]
ESR1_ERRORS = [(0x8000, "bit1"), (0x4000, "bit0"), (0x2000, "ack"), (0x1000, "crc"), (0x0800, "form"), (0x0400, "stuff")]


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
                if length > MAX_PAYLOAD:
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

    def open(self, channel, bitrate, mode=0, data_bitrate=0):
        payload = struct.pack("<BBI", channel, mode, bitrate)
        if mode & MODE_FD:
            payload += struct.pack("<I", data_bitrate)
        self.send(CMD_OPEN, payload)
        ack = self.wait(MSG_ACK, 1.5, lambda p: p[0] == CMD_OPEN and p[1] == channel)
        return ack[2] if ack else None

    def close(self, channel):
        self.send(CMD_CLOSE, bytes([channel]))
        return self.wait(MSG_ACK, 1.0, lambda p: p[0] == CMD_CLOSE and p[1] == channel) is not None

    def transmit(self, channel, can_id, data, flags=0):
        self.send(CMD_TX, struct.pack("<BBIB", channel, flags, can_id, len(data)) + (b"" if flags & FRAME_REMOTE else bytes(data)))

    def status(self, channel):
        self.send(CMD_GET_STATUS, bytes([channel]))
        return self.wait(MSG_STATUS, 1.0, lambda p: p[0] == channel)


def decode_frame(payload):
    channel, flags, timestamp, can_id, length = struct.unpack("<BBIIB", payload[:11])
    return channel, flags, timestamp, can_id, length, payload[11:]


def frame_text(payload):
    channel, flags, timestamp, can_id, length, data = decode_frame(payload)
    kind = "Tx" if flags & FRAME_TX else "Rx"
    ident = "%08X" % can_id if flags & FRAME_EXTENDED else "%03X" % can_id
    body = "remote" if flags & FRAME_REMOTE else data.hex(" ")
    fd = (" FD" + ("+BRS" if flags & FRAME_BRS else "")) if flags & FRAME_FD else ""
    return "%10.4f s  ch%d %s %s%s [%d] %s" % (timestamp / 1e6, channel, kind, ident, fd, length, body)


def error_text(payload):
    channel, timestamp, esr1, tec, rec = struct.unpack("<BIIBB", payload[:11])
    names = "+".join(name for bit, name in ESR1_ERRORS if esr1 & bit) or "?"
    return "%10.4f s  ch%d bus error %s (ESR1=%08X) TEC %d REC %d" % (timestamp / 1e6, channel, names, esr1, tec, rec)


def status_text(payload):
    channel, is_open, state, tec, rec, timestamp, rx_lost, tx_dropped, pending = struct.unpack("<BBBBBIIIH", payload[:19])
    return "ch%d %s, bus %s, TEC %d, REC %d, rx lost %d, tx dropped %d, tx pending %d" % (
        channel, "open" if is_open else "closed", BUS_STATES[min(state, 3)], tec, rec, rx_lost, tx_dropped, pending)


def ack_text(result):
    return "no answer" if result is None else ACK_RESULTS[result] if result < len(ACK_RESULTS) else "code %d" % result


def find_port():
    ports = [p.device for p in serial.tools.list_ports.comports() if p.vid == 0x16C0 and p.pid == 0x0483]
    if not ports:
        sys.exit("No Teensy serial port found (USB 16C0:0483). Is it plugged, with the firmware loaded?")
    return ports[0]


def test_frames(channel, fd):
    """Frames to send on a channel: (channel, id, flags, data)."""
    frames = []
    for index in range(64):
        extended = index % 4 >= 2
        can_id = (0x18DA0000 + index) if extended else (0x100 + index)
        flags = FRAME_EXTENDED if extended else 0
        if fd:
            length = FD_LENGTHS[index % len(FD_LENGTHS)]
            flags |= FRAME_FD | (FRAME_BRS if index % 3 else 0)
        else:
            length = index % 9
        frames.append((channel, can_id, flags, bytes((index * 3 + k) & 0xFF for k in range(length))))
    return frames


def collect(board, duration):
    echoes, received, errors = [], [], []
    for kind, payload in board.packets(duration):
        if kind == MSG_FRAME:
            channel, flags, _, can_id, length, data = decode_frame(payload)
            entry = (channel, can_id, flags & ~(FRAME_TX | FRAME_ESI), data)
            (echoes if flags & FRAME_TX else received).append(entry)
        elif kind == MSG_ERROR:
            errors.append(payload)
    return echoes, received, errors


def self_test(board, channels, bitrate, data_bitrate):
    """Internal loopback: every frame sent must come back as echo and as received frame."""
    ok = True
    cases = [(channel, False) for channel in range(channels)] + [(1, True)]
    for channel, fd in cases:
        name = "ch%d %s" % (channel, "CAN FD %d/%d" % (bitrate, data_bitrate) if fd else "classic %d" % bitrate)
        result = board.open(channel, bitrate, MODE_LOOPBACK | (MODE_FD if fd else 0), data_bitrate)
        if result != 0:
            print("%s: open failed: %s" % (name, ack_text(result)))
            ok = False
            continue
        sent = test_frames(channel, fd)
        if not fd:
            sent.append((channel, 0x7F0, FRAME_REMOTE, b""))
        for _, can_id, flags, data in sent:
            board.transmit(channel, can_id, data if not flags & FRAME_REMOTE else bytes(4), flags)
        if not fd:
            sent[-1] = (channel, 0x7F0, FRAME_REMOTE, b"")
        echoes, received, errors = collect(board, 1.0 + len(sent) * 700.0 / bitrate)
        good = echoes == sent and received == sent and not errors
        print("%s: sent %d, echoes %d, received %d, bus errors %d: %s" % (
            name, len(sent), len(echoes), len(received), len(errors), "ok" if good else "FAILED"))
        if not good:
            ok = False
            for label, got in (("echo", echoes), ("received", received)):
                for index, expected in enumerate(sent):
                    if index >= len(got) or got[index] != expected:
                        print("   first difference in %s at frame %d: expected %s, got %s" % (
                            label, index, expected, got[index] if index < len(got) else None))
                        break
            for payload in errors[:3]:
                print("  ", error_text(payload))
        board.close(channel)
    print("PASS" if ok else "FAILED")
    return ok


def loop_test(board, bitrate):
    """Both channels on the same bus: every frame sent on one must be received on the other."""
    sent = []
    for index in range(200):
        channel = index % 2
        extended = index % 4 >= 2
        can_id = (0x18DA0000 + index) if extended else (0x100 + index)
        data = bytes((index + k) & 0xFF for k in range(index % 9))
        board.transmit(channel, can_id, data, FRAME_EXTENDED if extended else 0)
        sent.append((channel, can_id, extended, data))
    echoes, received, errors, foreign = [], [], 0, 0
    known = set((i, e) for _, i, e, _ in sent)
    # 200 frames of at most 160 bits, and time for the frames of other nodes on the bus
    for kind, payload in board.packets(1.0 + 200 * 160 * 2.0 / bitrate):
        if kind == MSG_FRAME:
            channel, flags, _, can_id, length, data = decode_frame(payload)
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
    # remote frames, one in each direction
    for channel in (0, 1):
        board.transmit(channel, 0x7F0 + channel, bytes(4), FRAME_REMOTE)
        remote = [decode_frame(p) for k, p in board.packets(0.5 + 400.0 / bitrate) if k == MSG_FRAME]
        if not any(f[0] == 1 - channel and f[1] & FRAME_REMOTE and f[3] == 0x7F0 + channel and f[4] == 4 for f in remote):
            print("FAIL: the remote frame of channel %d was not received on channel %d" % (channel, 1 - channel))
            ok = False
    print("PASS" if ok else "FAILED")
    return ok


def fd_alone_test(board, bitrate, data_bitrate):
    """Channel 1 alone on its bus: only acknowledge errors are expected."""
    result = board.open(1, bitrate, MODE_FD, data_bitrate)
    print("open ch1 CAN FD %d / %d bit/s: %s" % (bitrate, data_bitrate, ack_text(result)))
    if result != 0:
        return False
    board.transmit(1, 0x555, bytes([0x55, 0xAA, 0x00, 0xFF] * 16), FRAME_FD | FRAME_BRS)
    kinds = {}
    frames = 0
    for kind, payload in board.packets(1.5):
        if kind == MSG_ERROR:
            esr1 = struct.unpack("<I", payload[5:9])[0]
            for bit, name in ESR1_ERRORS:
                if esr1 & bit:
                    kinds[name] = kinds.get(name, 0) + 1
        elif kind == MSG_FRAME:
            frames += 1
    print("errors reported:", kinds or "none", "- frames:", frames)
    status = board.status(1)
    print(status_text(status) if status else "no status")
    board.close(1)
    others = [name for name in kinds if name != "ack"]
    if frames:
        print("FAILED: a frame was acknowledged or received, so another node is on this bus")
        return False
    if others or "ack" not in kinds:
        print("FAILED: expected acknowledge errors only")
        return False
    print("PASS: the frame went out complete, with its fast data bits read back correctly; nobody acknowledged it")
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port")
    parser.add_argument("--bitrate", type=int, default=500000)
    parser.add_argument("--databitrate", type=int, default=2000000)
    parser.add_argument("--fd", action="store_true")
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--loop", action="store_true")
    parser.add_argument("--fdalone", action="store_true")
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
    if protocol != PROTOCOL:
        sys.exit("This script speaks protocol %d: load the firmware of the same version" % PROTOCOL)

    ok = True
    if args.selftest:
        ok = self_test(board, channels, args.bitrate, args.databitrate)
    elif args.fdalone:
        ok = fd_alone_test(board, args.bitrate, args.databitrate)
    elif args.loop or args.listen:
        for channel in range(channels):
            mode = (MODE_LISTEN_ONLY if args.listen else 0) | (MODE_FD if args.fd and channel == 1 else 0)
            result = board.open(channel, args.bitrate, mode, args.databitrate)
            print("open ch%d at %d bit/s%s: %s" % (channel, args.bitrate, " (CAN FD controller)" if mode & MODE_FD else "", ack_text(result)))
            ok = ok and result == 0
        if args.loop and ok:
            ok = loop_test(board, args.bitrate)
        if args.listen and ok:
            for kind, payload in board.packets(args.listen):
                if kind == MSG_FRAME:
                    print(frame_text(payload))
                elif kind == MSG_ERROR:
                    print(error_text(payload))
    for channel in range(channels):
        status = board.status(channel)
        print(status_text(status) if status else "ch%d: no status" % channel)
    for channel in range(channels):
        board.close(channel)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
