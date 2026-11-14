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
 * BUSMASTER-NG CAN interface on a Raspberry Pi Pico: classic CAN channels over the USB serial
 * port, with the same protocol as the Teensy firmware
 * (Sources/BUSMASTER/CAN_Teensy/TeensyProtocol.h).
 *
 * The Pico has no CAN controller: the CAN protocol is done by the can2040 library of Kevin
 * O'Connor (GPL v3) with one PIO block for each bus, so a Pico 2 (RP2350, 3 PIO blocks) has
 * three channels and a Pico (RP2040, 2 PIO blocks) has two. Each channel needs a CAN transceiver
 * for 3.3 V logic (SN65HVD230 for example):
 *
 *   channel 0: TX GP2,  RX GP3
 *   channel 1: TX GP6,  RX GP7
 *   channel 2: TX GP10, RX GP11   (Pico 2 only)
 *
 * What a software CAN node does not do: no listen only mode (a received frame is always
 * acknowledged), no error counters, no error frames, no bus off.
 *
 * Received frames and the echo of sent frames are time stamped in the PIO interrupt and put in
 * a ring buffer; loop() turns them into packets and feeds the frames to send, in the order they
 * were given, to the queue of can2040.
 */

#include <Arduino.h>
#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/irq.h>
#include <hardware/pio.h>
#include <hardware/sync.h>
#include <pico/unique_id.h>

extern "C" {
#include "can2040.h"
}
#include "TeensyProtocol.h"

#define FW_VERSION_MAJOR 1
#define FW_VERSION_MINOR 0

#if NUM_PIOS >= 3
#define CHANNEL_COUNT 3
static const char INFO_TEXT[] = "Pico 2;CAN0;CAN1;CAN2";
#else
#define CHANNEL_COUNT 2
static const char INFO_TEXT[] = "Pico;CAN0;CAN1";
#endif

static const uint8_t TX_PIN[3] = { 2, 6, 10 };
static const uint8_t RX_PIN[3] = { 3, 7, 11 };

// ---------------------------------------------------------------------------------------------
// Channels

static const uint32_t TX_QUEUE_SIZE = 256;     // power of 2

struct Channel
{
    struct can2040 can;
    volatile bool open;
    // frames to send: written and read by loop() only
    struct can2040_msg txQueue[TX_QUEUE_SIZE];
    uint32_t txHead;
    uint32_t txTail;
    volatile uint32_t txInController;   // given to can2040 and not on the bus yet
    volatile uint32_t rxLost;           // frames lost: buffers of can2040 or the event buffer full
    uint32_t txDropped;                 // frames that could not be queued for sending
    uint32_t parseErrors;               // errors of the bus already reported
    uint32_t lastStatusMs;
    uint32_t lastErrorMs;
};

static Channel channels[CHANNEL_COUNT];

// ---------------------------------------------------------------------------------------------
// Events from the PIO interrupts to loop()

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

// Called in the PIO interrupt of a channel.
static void onCanEvent(uint8_t index, uint32_t notify, struct can2040_msg* msg)
{
    Channel& channel = channels[index];
    if (notify == CAN2040_NOTIFY_ERROR)
    {
        channel.rxLost++;
        return;
    }
    const bool sent = notify == CAN2040_NOTIFY_TX;
    if (sent && channel.txInController != 0)
    {
        channel.txInController--;
    }
    if (!channel.open)
    {
        return;
    }
    const uint32_t head = eventHead;
    const uint32_t next = (head + 1) & (EVENT_COUNT - 1);
    if (next == eventTail)
    {
        channel.rxLost++;
        return;
    }
    Event& event = events[head];
    const bool extended = (msg->id & CAN2040_ID_EFF) != 0;
    event.timestamp = micros();
    event.id = msg->id & (extended ? 0x1FFFFFFF : 0x7FF);
    event.channel = index;
    event.flags = (uint8_t)((extended ? BMT_FRAME_EXTENDED : 0) |
                            ((msg->id & CAN2040_ID_RTR) ? BMT_FRAME_REMOTE : 0) |
                            (sent ? BMT_FRAME_TX : 0));
    event.dlc = msg->dlc > 8 ? 8 : (uint8_t)msg->dlc;
    memcpy(event.data, msg->data, 8);
    eventHead = next;
}

static void onCanEvent0(struct can2040*, uint32_t notify, struct can2040_msg* msg) { onCanEvent(0, notify, msg); }
static void onCanEvent1(struct can2040*, uint32_t notify, struct can2040_msg* msg) { onCanEvent(1, notify, msg); }
static void pioInterrupt0() { can2040_pio_irq_handler(&channels[0].can); }
static void pioInterrupt1() { can2040_pio_irq_handler(&channels[1].can); }
#if CHANNEL_COUNT >= 3
static void onCanEvent2(struct can2040*, uint32_t notify, struct can2040_msg* msg) { onCanEvent(2, notify, msg); }
static void pioInterrupt2() { can2040_pio_irq_handler(&channels[2].can); }
#endif

static const can2040_rx_cb CAN_CALLBACK[CHANNEL_COUNT] =
{
    onCanEvent0, onCanEvent1,
#if CHANNEL_COUNT >= 3
    onCanEvent2,
#endif
};

static const irq_handler_t PIO_HANDLER[CHANNEL_COUNT] =
{
    pioInterrupt0, pioInterrupt1,
#if CHANNEL_COUNT >= 3
    pioInterrupt2,
#endif
};

static const uint PIO_IRQ[CHANNEL_COUNT] =
{
    PIO0_IRQ_0, PIO1_IRQ_0,
#if CHANNEL_COUNT >= 3
    PIO2_IRQ_0,
#endif
};

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
    uint8_t payload[BMT_SIZE_INFO(CHANNEL_COUNT) + sizeof(INFO_TEXT) - 1];
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    // a number of 9 digits at most from the last bytes of the identifier of the board
    const uint32_t serial = (((uint32_t)id.id[4] << 24) | ((uint32_t)id.id[5] << 16) |
                             ((uint32_t)id.id[6] << 8) | id.id[7]) % 1000000000u;
    payload[0] = BMT_PROTOCOL_VERSION;
    payload[1] = FW_VERSION_MAJOR;
    payload[2] = FW_VERSION_MINOR;
    payload[3] = CHANNEL_COUNT;
    for (uint8_t index = 0; index < CHANNEL_COUNT; index++)
    {
        payload[4 + index] = BMT_CAP_NO_LISTEN_ONLY | BMT_CAP_NO_ERROR_COUNTERS;
    }
    put32(payload + 4 + CHANNEL_COUNT, serial);
    memcpy(payload + BMT_SIZE_INFO(CHANNEL_COUNT), INFO_TEXT, sizeof(INFO_TEXT) - 1);
    sendPacket(BMT_MSG_INFO, payload, sizeof(payload));
}

static void sendStatus(uint8_t index)
{
    Channel& channel = channels[index];
    uint8_t payload[BMT_SIZE_STATUS];
    memset(payload, 0, sizeof(payload));
    payload[0] = index;
    payload[1] = channel.open ? 1 : 0;
    payload[2] = BMT_BUS_ACTIVE;        // no error counters: bytes 3 and 4 stay 0
    put32(payload + 5, micros());
    put32(payload + 9, channel.rxLost);
    put32(payload + 13, channel.txDropped);
    const uint32_t pending = ((channel.txHead - channel.txTail) & (TX_QUEUE_SIZE - 1)) + channel.txInController;
    payload[17] = (uint8_t)pending;
    payload[18] = (uint8_t)(pending >> 8);
    sendPacket(BMT_MSG_STATUS, payload, sizeof(payload));
    channel.lastStatusMs = millis();
}

// ---------------------------------------------------------------------------------------------
// Opening and closing a channel

static void closeChannel(uint8_t index)
{
    Channel& channel = channels[index];
    if (channel.open)
    {
        channel.open = false;
        irq_set_enabled(PIO_IRQ[index], false);
        can2040_stop(&channel.can);
        // the transmit pin floats: the transceiver sees its pull-up (recessive)
        gpio_set_function(TX_PIN[index], GPIO_FUNC_SIO);
        gpio_set_dir(TX_PIN[index], GPIO_IN);
    }
    channel.txHead = channel.txTail = 0;
    channel.txInController = 0;
    channel.rxLost = 0;
    channel.txDropped = 0;
    channel.parseErrors = 0;
}

static uint8_t openChannel(uint8_t index, uint32_t bitRate, uint8_t mode)
{
    Channel& channel = channels[index];
    closeChannel(index);
    if (mode & BMT_MODE_LISTEN_ONLY)
    {
        return BMT_ACK_NO_LISTEN_ONLY;
    }
    if (bitRate < 5000 || bitRate > 1000000)
    {
        return BMT_ACK_BAD_BITRATE;
    }
    can2040_setup(&channel.can, index);         // also forgets the frames that were waiting
    can2040_callback_config(&channel.can, CAN_CALLBACK[index]);
    irq_set_exclusive_handler(PIO_IRQ[index], PIO_HANDLER[index]);
    // above the USB interrupt: can2040 needs a short interrupt latency; the channels have the
    // same priority, so their interrupts do not nest
    irq_set_priority(PIO_IRQ[index], 0);
    channel.lastStatusMs = millis();
    channel.lastErrorMs = millis();
    channel.open = true;
    irq_set_enabled(PIO_IRQ[index], true);
    can2040_start(&channel.can, clock_get_hz(clk_sys), bitRate, RX_PIN[index], TX_PIN[index]);
    return BMT_ACK_OK;
}

static void closeAll()
{
    for (uint8_t index = 0; index < CHANNEL_COUNT; index++)
    {
        closeChannel(index);
    }
}

static bool anyOpen()
{
    for (uint8_t index = 0; index < CHANNEL_COUNT; index++)
    {
        if (channels[index].open)
        {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------------------------
// Sending frames

// Gives the waiting frames to can2040 while it has room (it keeps 4, sent in this order).
static void feedTransmit(uint8_t index)
{
    Channel& channel = channels[index];
    while (channel.open && channel.txTail != channel.txHead && can2040_check_transmit(&channel.can))
    {
        const uint32_t state = save_and_disable_interrupts();
        channel.txInController++;
        restore_interrupts(state);
        if (can2040_transmit(&channel.can, &channel.txQueue[channel.txTail]) < 0)
        {
            const uint32_t again = save_and_disable_interrupts();
            channel.txInController--;
            restore_interrupts(again);
            break;
        }
        channel.txTail = (channel.txTail + 1) & (TX_QUEUE_SIZE - 1);
    }
}

static void queueTransmit(uint8_t index, const struct can2040_msg& msg)
{
    Channel& channel = channels[index];
    const uint32_t next = (channel.txHead + 1) & (TX_QUEUE_SIZE - 1);
    if (!channel.open || next == channel.txTail)
    {
        channel.txDropped++;            // closed, or the bus does not take the frames
        return;
    }
    channel.txQueue[channel.txHead] = msg;
    channel.txHead = next;
    feedTransmit(index);
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
            sendAck(type, index, index < CHANNEL_COUNT ? openChannel(index, get32(payload + 2), payload[1])
                    : BMT_ACK_BAD_CHANNEL);
            break;
        }

        case BMT_CMD_CLOSE:
        {
            const uint8_t index = length > 0 ? payload[0] : 0xFF;
            if (index < CHANNEL_COUNT)
            {
                closeChannel(index);
            }
            sendAck(type, index, index < CHANNEL_COUNT ? BMT_ACK_OK : BMT_ACK_BAD_CHANNEL);
            break;
        }

        case BMT_CMD_TX:
        {
            if (length < BMT_SIZE_TX_HEADER || payload[0] >= CHANNEL_COUNT || payload[6] > 8)
            {
                break;
            }
            struct can2040_msg msg;
            const bool extended = (payload[1] & BMT_FRAME_EXTENDED) != 0;
            msg.id = (get32(payload + 2) & (extended ? 0x1FFFFFFF : 0x7FF)) |
                     (extended ? (uint32_t)CAN2040_ID_EFF : 0) |
                     ((payload[1] & BMT_FRAME_REMOTE) ? (uint32_t)CAN2040_ID_RTR : 0);
            msg.dlc = payload[6];
            msg.data32[0] = msg.data32[1] = 0;
            const uint8_t dataLength = (uint8_t)(length - BMT_SIZE_TX_HEADER);
            memcpy(msg.data, payload + BMT_SIZE_TX_HEADER, dataLength < msg.dlc ? dataLength : msg.dlc);
            queueTransmit(payload[0], msg);
            break;
        }

        case BMT_CMD_GET_STATUS:
            if (length > 0 && payload[0] < CHANNEL_COUNT)
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

// can2040 counts the frames it could not read (bit stuffing, CRC, form): one error message for
// new ones, at most every 10 ms. The kind of error is not known.
static void sendErrors(uint8_t index)
{
    Channel& channel = channels[index];
    if (!channel.open || millis() - channel.lastErrorMs < 10)
    {
        return;
    }
    struct can2040_stats stats;
    can2040_get_statistics(&channel.can, &stats);
    if (stats.parse_error == channel.parseErrors)
    {
        return;
    }
    channel.parseErrors = stats.parse_error;
    channel.lastErrorMs = millis();
    uint8_t payload[BMT_SIZE_ERROR];
    memset(payload, 0, sizeof(payload));
    payload[0] = index;
    put32(payload + 1, micros());
    sendPacket(BMT_MSG_ERROR, payload, sizeof(payload));
}

// ---------------------------------------------------------------------------------------------

void setup()
{
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
        digitalWrite(LED_BUILTIN, LOW);
        return;
    }

    readCommands();
    sendEvents();
    for (uint8_t index = 0; index < CHANNEL_COUNT; index++)
    {
        feedTransmit(index);
        sendErrors(index);
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
    digitalWrite(LED_BUILTIN, anyOpen() && !(traffic && ((millis() >> 5) & 1)) ? HIGH : LOW);
}
