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
 * BUSMASTER-NG CAN interface on a Teensy 4.0: two classic CAN channels over the USB serial port.
 *
 *   channel 0 = CAN1 (pin 22 CTX1, pin 23 CRX1)
 *   channel 1 = CAN3 (pin 31 CTX3, pin 30 CRX3)
 *
 * The protocol is described in Sources/BUSMASTER/CAN_Teensy/TeensyProtocol.h.
 *
 * Received frames and the echo of sent frames are time stamped in the CAN interrupt and put in
 * a ring buffer; loop() turns them into packets. Frames are sent one at a time for each channel
 * (one hardware mailbox, a queue in software), so they leave in the order they were given.
 */

#include <Arduino.h>
#include <FlexCAN_T4.h>
#include "TeensyProtocol.h"

#define FW_VERSION_MAJOR 1
#define FW_VERSION_MINOR 0

static const char INFO_TEXT[] = "Teensy 4.0;CAN1;CAN3";

static FlexCAN_T4<CAN1, RX_SIZE_16, TX_SIZE_16> can1;
static FlexCAN_T4<CAN3, RX_SIZE_16, TX_SIZE_16> can3;

// ---------------------------------------------------------------------------------------------
// Channels

static const uint32_t TX_QUEUE_SIZE = 256;     // power of 2

struct Channel
{
    uint32_t base;                  // address of the FlexCAN registers
    FlexCAN_T4_Base* can;
    bool started;                   // begin() was called once: the registers can be used
    volatile bool open;
    bool listenOnly;
    // transmission: txCurrent is in the hardware mailbox while txBusy
    volatile bool txBusy;
    CAN_message_t txCurrent;
    CAN_message_t txQueue[TX_QUEUE_SIZE];
    volatile uint32_t txHead;       // written by loop()
    volatile uint32_t txTail;       // written by the interrupt
    volatile uint32_t rxLost;       // frames that did not fit in the event buffer
    uint32_t txDropped;             // frames that could not be queued for sending
    uint32_t lastStatusMs;
    uint32_t lastErrorMs;
};

static Channel channels[BMT_CHANNEL_COUNT];

// ---------------------------------------------------------------------------------------------
// Events from the CAN interrupts to loop()

struct Event
{
    uint32_t timestamp;
    uint32_t id;
    uint8_t channel;
    uint8_t flags;
    uint8_t dlc;
    uint8_t data[8];
};

static const uint32_t EVENT_COUNT = 4096;      // power of 2
static Event events[EVENT_COUNT];
static volatile uint32_t eventHead = 0;        // written by the interrupts (same priority, not nested)
static volatile uint32_t eventTail = 0;        // written by loop()

static void pushEvent(uint8_t channel, const CAN_message_t& msg, bool sent)
{
    const uint32_t head = eventHead;
    const uint32_t next = (head + 1) & (EVENT_COUNT - 1);
    if (next == eventTail)
    {
        channels[channel].rxLost++;
        return;
    }
    Event& event = events[head];
    event.timestamp = micros();
    event.id = msg.id;
    event.channel = channel;
    event.flags = (uint8_t)((msg.flags.extended ? BMT_FRAME_EXTENDED : 0) |
                            (msg.flags.remote ? BMT_FRAME_REMOTE : 0) |
                            (sent ? BMT_FRAME_TX : 0));
    event.dlc = msg.len > 8 ? 8 : msg.len;
    memcpy(event.data, msg.buf, 8);
    eventHead = next;
}

// Called in the CAN interrupt.
static void onReceived(uint8_t index, const CAN_message_t& msg)
{
    Channel& channel = channels[index];
    if (channel.open)
    {
        pushEvent(index, msg, false);
    }
}

// Called in the CAN interrupt when the frame in the mailbox is on the bus.
static void onTransmitted(uint8_t index)
{
    Channel& channel = channels[index];
    if (!channel.open || !channel.txBusy)
    {
        return;
    }
    pushEvent(index, channel.txCurrent, true);
    const uint32_t tail = channel.txTail;
    if (tail == channel.txHead)
    {
        channel.txBusy = false;
        return;
    }
    channel.txCurrent = channel.txQueue[tail];
    channel.txTail = (tail + 1) & (TX_QUEUE_SIZE - 1);
    if (channel.can->write(channel.txCurrent) != 1)
    {
        channel.txBusy = false;
        channel.txDropped++;
    }
}

static void onReceived0(const CAN_message_t& msg) { onReceived(0, msg); }
static void onReceived1(const CAN_message_t& msg) { onReceived(1, msg); }
static void onTransmitted0(const CAN_message_t&)  { onTransmitted(0); }
static void onTransmitted1(const CAN_message_t&)  { onTransmitted(1); }

// ---------------------------------------------------------------------------------------------
// Packets to the host

static uint8_t outBuffer[2048];
static uint32_t outLength = 0;

static void flushOutput()
{
    if (outLength != 0)
    {
        Serial.write(outBuffer, outLength);
        outLength = 0;
    }
}

static void sendPacket(uint8_t type, const uint8_t* payload, uint8_t length)
{
    if (outLength + length + 4u > sizeof(outBuffer))
    {
        flushOutput();
    }
    uint8_t* packet = outBuffer + outLength;
    packet[0] = BMT_SOF;
    packet[1] = type;
    packet[2] = length;
    memcpy(packet + 3, payload, length);
    packet[3 + length] = bmt_crc8(0, packet + 1, length + 2u);
    outLength += length + 4u;
}

static void put32(uint8_t* p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static uint32_t get32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void sendAck(uint8_t command, uint8_t channel, uint8_t result)
{
    const uint8_t payload[BMT_SIZE_ACK] = { command, channel, result };
    sendPacket(BMT_MSG_ACK, payload, sizeof(payload));
}

static void sendInfo()
{
    uint8_t payload[BMT_SIZE_INFO_HEADER + sizeof(INFO_TEXT) - 1];
    uint32_t serial = HW_OCOTP_MAC0 & 0xFFFFFF;    // the number in the USB serial number string
    if (serial < 10000000)
    {
        serial *= 10;
    }
    payload[0] = BMT_PROTOCOL_VERSION;
    payload[1] = FW_VERSION_MAJOR;
    payload[2] = FW_VERSION_MINOR;
    payload[3] = BMT_CHANNEL_COUNT;
    payload[4] = 0;                 // CAN1: classic CAN
    payload[5] = BMT_CAP_CANFD;     // CAN3: the controller can do CAN FD
    put32(payload + 6, serial);
    memcpy(payload + BMT_SIZE_INFO_HEADER, INFO_TEXT, sizeof(INFO_TEXT) - 1);
    sendPacket(BMT_MSG_INFO, payload, sizeof(payload));
}

static uint8_t busState(uint32_t esr1)
{
    const uint32_t confinement = (esr1 >> 4) & 3;
    if (confinement >= 2)
    {
        return BMT_BUS_OFF;
    }
    if (confinement == 1)
    {
        return BMT_BUS_PASSIVE;
    }
    return (esr1 & 0x300) ? BMT_BUS_WARNING : BMT_BUS_ACTIVE;   // TX_WRN, RX_WRN
}

static void sendStatus(uint8_t index)
{
    Channel& channel = channels[index];
    uint8_t payload[BMT_SIZE_STATUS];
    memset(payload, 0, sizeof(payload));
    payload[0] = index;
    payload[1] = channel.open ? 1 : 0;
    if (channel.open)
    {
        const uint32_t ecr = FLEXCANb_ECR(channel.base);
        payload[2] = busState(FLEXCANb_ESR1(channel.base));
        payload[3] = (uint8_t)ecr;
        payload[4] = (uint8_t)(ecr >> 8);
    }
    put32(payload + 5, micros());
    put32(payload + 9, channel.rxLost);
    put32(payload + 13, channel.txDropped);
    const uint32_t pending = ((channel.txHead - channel.txTail) & (TX_QUEUE_SIZE - 1)) + (channel.txBusy ? 1 : 0);
    payload[17] = (uint8_t)pending;
    payload[18] = (uint8_t)(pending >> 8);
    sendPacket(BMT_MSG_STATUS, payload, sizeof(payload));
    channel.lastStatusMs = millis();
}

// ---------------------------------------------------------------------------------------------
// Opening and closing a channel

static void resetChannelState(Channel& channel)
{
    channel.txBusy = false;
    channel.txHead = 0;
    channel.txTail = 0;
    channel.rxLost = 0;
    channel.txDropped = 0;
}

template <class Controller> static void closeChannel(Controller& can, uint8_t index)
{
    Channel& channel = channels[index];
    channel.open = false;
    if (channel.started)
    {
        // freeze: the controller leaves the bus (no acknowledge, nothing sent)
        can.FLEXCAN_EnterFreezeMode();
    }
    resetChannelState(channel);
}

// The clock of the FlexCAN controllers, in Hz.
static uint32_t canClock()
{
    static const uint32_t source[4] = { 60000000, 24000000, 80000000, 0 };
    return source[(CCM_CSCMR2 >> 8) & 3] / (((CCM_CSCMR2 >> 2) & 0x3F) + 1);
}

// The bit rate the controller is really set to, from its timing register.
static uint32_t actualBitRate(uint32_t base)
{
    const uint32_t ctrl = FLEXCANb_CTRL1(base);
    const uint32_t prescaler = ((ctrl >> 24) & 0xFF) + 1;
    const uint32_t quanta = 1 + ((ctrl & 7) + 1) + (((ctrl >> 19) & 7) + 1) + (((ctrl >> 16) & 7) + 1);
    return canClock() / (prescaler * quanta);
}

template <class Controller>
static uint8_t openChannel(Controller& can, uint8_t index, uint32_t bitRate, uint8_t mode,
                           void (*received)(const CAN_message_t&), void (*transmitted)(const CAN_message_t&))
{
    Channel& channel = channels[index];
    closeChannel(can, index);
    if (bitRate < 5000 || bitRate > 1000000)
    {
        return BMT_ACK_BAD_BITRATE;
    }
    can.begin();                    // resets the controller, listen only until the bit rate is set
    channel.started = true;
    can.setBaudRate(bitRate, (mode & BMT_MODE_LISTEN_ONLY) ? LISTEN_ONLY : TX);
    // setBaudRate does nothing when it finds no timing: check what the controller really has
    const uint32_t actual = actualBitRate(channel.base);
    const uint32_t difference = actual > bitRate ? actual - bitRate : bitRate - actual;
    if (difference > bitRate / 200)                 // more than 0.5 %
    {
        can.FLEXCAN_EnterFreezeMode();
        return BMT_ACK_BAD_BITRATE;
    }
    can.setMaxMB(16);
    can.enableFIFO();               // all frames in the receive FIFO, mailboxes 8..15 for sending
    can.onReceive(received);
    can.onTransmit(transmitted);
    channel.listenOnly = (mode & BMT_MODE_LISTEN_ONLY) != 0;
    channel.lastStatusMs = millis();
    channel.lastErrorMs = millis();
    channel.open = true;
    can.enableFIFOInterrupt();
    return BMT_ACK_OK;
}

static void closeAll()
{
    closeChannel(can1, 0);
    closeChannel(can3, 1);
}

static bool anyOpen()
{
    return channels[0].open || channels[1].open;
}

// ---------------------------------------------------------------------------------------------
// Sending a frame

static void transmit(uint8_t index, const CAN_message_t& msg)
{
    Channel& channel = channels[index];
    if (!channel.open || channel.listenOnly)
    {
        channel.txDropped++;
        return;
    }
    // the transmit interrupt takes the next frame from the queue: keep it out while deciding
    __disable_irq();
    if (!channel.txBusy)
    {
        channel.txCurrent = msg;
        channel.txBusy = true;
        if (channel.can->write(channel.txCurrent) != 1)
        {
            channel.txBusy = false;
            channel.txDropped++;
        }
    }
    else
    {
        const uint32_t head = channel.txHead;
        const uint32_t next = (head + 1) & (TX_QUEUE_SIZE - 1);
        if (next == channel.txTail)
        {
            channel.txDropped++;        // queue full: the bus does not take the frames
        }
        else
        {
            channel.txQueue[head] = msg;
            channel.txHead = next;
        }
    }
    __enable_irq();
}

// ---------------------------------------------------------------------------------------------
// Commands of the host

static void handleCommand(uint8_t type, const uint8_t* payload, uint8_t length)
{
    switch (type)
    {
        case BMT_CMD_HELLO:
            closeAll();
            eventTail = eventHead;
            sendInfo();
            break;

        case BMT_CMD_OPEN:
        {
            if (length < BMT_SIZE_OPEN)
            {
                sendAck(type, length > 0 ? payload[0] : 0xFF, BMT_ACK_BAD_PARAMETER);
                break;
            }
            const uint8_t index = payload[0];
            const uint32_t bitRate = get32(payload + 2);
            uint8_t result = BMT_ACK_BAD_CHANNEL;
            if (index == 0)
            {
                result = openChannel(can1, 0, bitRate, payload[1], onReceived0, onTransmitted0);
            }
            else if (index == 1)
            {
                result = openChannel(can3, 1, bitRate, payload[1], onReceived1, onTransmitted1);
            }
            sendAck(type, index, result);
            break;
        }

        case BMT_CMD_CLOSE:
        {
            const uint8_t index = length > 0 ? payload[0] : 0xFF;
            if (index == 0)
            {
                closeChannel(can1, 0);
            }
            else if (index == 1)
            {
                closeChannel(can3, 1);
            }
            sendAck(type, index, index < BMT_CHANNEL_COUNT ? BMT_ACK_OK : BMT_ACK_BAD_CHANNEL);
            break;
        }

        case BMT_CMD_TX:
        {
            if (length < BMT_SIZE_TX_HEADER || payload[0] >= BMT_CHANNEL_COUNT || payload[6] > 8)
            {
                break;
            }
            CAN_message_t msg;
            msg.flags.extended = (payload[1] & BMT_FRAME_EXTENDED) != 0;
            msg.flags.remote = (payload[1] & BMT_FRAME_REMOTE) != 0;
            msg.id = get32(payload + 2) & (msg.flags.extended ? 0x1FFFFFFF : 0x7FF);
            msg.len = payload[6];
            const uint8_t dataLength = (uint8_t)(length - BMT_SIZE_TX_HEADER);
            memcpy(msg.buf, payload + BMT_SIZE_TX_HEADER, dataLength < msg.len ? dataLength : msg.len);
            transmit(payload[0], msg);
            break;
        }

        case BMT_CMD_GET_STATUS:
            if (length > 0 && payload[0] < BMT_CHANNEL_COUNT)
            {
                sendStatus(payload[0]);
            }
            break;

        default:
            break;
    }
}

static void readCommands()
{
    static uint8_t state = 0;       // 0 start, 1 type, 2 length, 3 payload, 4 crc
    static uint8_t type = 0;
    static uint8_t length = 0;
    static uint8_t received = 0;
    static uint8_t payload[BMT_MAX_PAYLOAD + 2];

    int budget = 4096;              // leave time for the frames of the buses
    while (budget-- > 0 && Serial.available() > 0)
    {
        const uint8_t byte = (uint8_t)Serial.read();
        switch (state)
        {
            case 0:
                if (byte == BMT_SOF)
                {
                    state = 1;
                }
                break;
            case 1:
                type = byte;
                state = 2;
                break;
            case 2:
                length = byte;
                received = 0;
                if (length > BMT_MAX_PAYLOAD)
                {
                    state = 0;
                }
                else
                {
                    state = length == 0 ? 4 : 3;
                }
                break;
            case 3:
                payload[received++] = byte;
                if (received == length)
                {
                    state = 4;
                }
                break;
            default:
            {
                const uint8_t header[2] = { type, length };
                if (bmt_crc8(bmt_crc8(0, header, 2), payload, length) == byte)
                {
                    handleCommand(type, payload, length);
                }
                state = 0;
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Frames and errors to the host

static void sendEvents()
{
    int budget = 512;
    while (budget-- > 0 && eventTail != eventHead)
    {
        const Event& event = events[eventTail];
        uint8_t payload[BMT_SIZE_FRAME_HEADER + 8];
        const uint8_t dataLength = (event.flags & BMT_FRAME_REMOTE) ? 0 : event.dlc;
        payload[0] = event.channel;
        payload[1] = event.flags;
        put32(payload + 2, event.timestamp);
        put32(payload + 6, event.id);
        payload[10] = event.dlc;
        memcpy(payload + BMT_SIZE_FRAME_HEADER, event.data, dataLength);
        sendPacket(BMT_MSG_FRAME, payload, (uint8_t)(BMT_SIZE_FRAME_HEADER + dataLength));
        eventTail = (eventTail + 1) & (EVENT_COUNT - 1);
    }
}

template <class Controller> static void sendErrors(Controller& can, uint8_t index)
{
    Channel& channel = channels[index];
    if (!channel.open)
    {
        return;
    }
    CAN_error_t error;
    int budget = 16;
    while (budget-- > 0 && can.error(error, false))
    {
        // the library records every change of the status register: keep the bus errors
        if ((error.ESR1 & BMT_ESR1_ERROR_MASK) == 0 || millis() - channel.lastErrorMs < 10)
        {
            continue;
        }
        channel.lastErrorMs = millis();
        uint8_t payload[BMT_SIZE_ERROR];
        payload[0] = index;
        put32(payload + 1, micros());
        put32(payload + 5, error.ESR1);
        payload[9] = error.TX_ERR_COUNTER;
        payload[10] = error.RX_ERR_COUNTER;
        sendPacket(BMT_MSG_ERROR, payload, sizeof(payload));
    }
}

// ---------------------------------------------------------------------------------------------

void setup()
{
    channels[0].base = CAN1;
    channels[0].can = &can1;
    channels[1].base = CAN3;
    channels[1].can = &can3;
    pinMode(LED_BUILTIN, OUTPUT);
    Serial.begin(115200);           // USB: the baud rate is not used
}

void loop()
{
    static uint32_t lastActivityMs = 0;
    static uint32_t lastEventHead = 0;

    if (!Serial.dtr())
    {
        // nobody has the port open: leave the buses
        if (anyOpen())
        {
            closeAll();
        }
        eventTail = eventHead;
        outLength = 0;
        digitalWriteFast(LED_BUILTIN, LOW);
        return;
    }

    readCommands();
    sendEvents();
    sendErrors(can1, 0);
    sendErrors(can3, 1);
    for (uint8_t index = 0; index < BMT_CHANNEL_COUNT; index++)
    {
        if (channels[index].open && millis() - channels[index].lastStatusMs >= 100)
        {
            sendStatus(index);
        }
    }
    flushOutput();

    // LED: on while a channel is open, flickers with the traffic
    if (eventHead != lastEventHead)
    {
        lastEventHead = eventHead;
        lastActivityMs = millis();
    }
    const bool traffic = millis() - lastActivityMs < 40;
    digitalWriteFast(LED_BUILTIN, anyOpen() && !(traffic && ((millis() >> 5) & 1)) ? HIGH : LOW);
}
