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

#include <Arduino.h>
#include "can3fd.h"
#include "TeensyProtocol.h"

namespace can3fd
{

// ---------------------------------------------------------------------------------------------
// Registers of FLEXCAN3 (i.MX RT1060 reference manual, chapter FlexCAN FD)

static const uint32_t BASE = 0x401D8000;

#define REG(offset)     (*(volatile uint32_t*)(BASE + (offset)))
#define MCR             REG(0x00)
#define CTRL1           REG(0x04)
#define TIMER           REG(0x08)
#define ESR1            REG(0x20)
#define IMASK2          REG(0x24)
#define IMASK1          REG(0x28)
#define IFLAG2          REG(0x2C)
#define IFLAG1          REG(0x30)
#define CTRL2           REG(0x34)
#define CBT             REG(0x50)
#define RXIMR(n)        REG(0x880 + 4 * (n))
#define FDCTRL          REG(0xC00)
#define FDCBT           REG(0xC04)

static const uint32_t MCR_MDIS = 1u << 31, MCR_FRZ = 1u << 30, MCR_FEN = 1u << 29, MCR_HALT = 1u << 28,
                      MCR_SOFT_RST = 1u << 25, MCR_FRZ_ACK = 1u << 24, MCR_WRN_EN = 1u << 21,
                      MCR_LPM_ACK = 1u << 20, MCR_SRX_DIS = 1u << 17, MCR_IRMQ = 1u << 16,
                      MCR_DMA = 1u << 15, MCR_FDEN = 1u << 11;
static const uint32_t CTRL1_BOFF_MSK = 1u << 15, CTRL1_ERR_MSK = 1u << 14, CTRL1_LPB = 1u << 12,
                      CTRL1_LOM = 1u << 3;
static const uint32_t CTRL2_ERRMSK_FAST = 1u << 31, CTRL2_RRS = 1u << 17, CTRL2_EACEN = 1u << 16,
                      CTRL2_ISOCANFDEN = 1u << 12;
static const uint32_t CBT_BTF = 1u << 31;
static const uint32_t FDCTRL_FDRATE = 1u << 31, FDCTRL_TDCEN = 1u << 15, FDCTRL_TDCFAIL = 1u << 14;
static const uint32_t FDCTRL_RESET = 0x80000100;
static const uint32_t ESR1_ERROR_BITS = 0x0000FC00;     // BIT1, BIT0, ACK, CRC, FRM, STF

// mailbox: control and status word
static const uint32_t CS_EDL = 1u << 31, CS_BRS = 1u << 30, CS_ESI = 1u << 29, CS_SRR = 1u << 22,
                      CS_IDE = 1u << 21, CS_RTR = 1u << 20;
static const uint32_t CODE_RX_EMPTY = 0x4, CODE_RX_FULL = 0x2, CODE_RX_OVERRUN = 0x6,
                      CODE_TX_INACTIVE = 0x8, CODE_TX_ONCE = 0xC;
#define CS_CODE(code)   ((uint32_t)(code) << 24)

// 64 data bytes for each mailbox: 7 mailboxes in each of the two memory regions
static const uint8_t MAILBOX_COUNT = 14;
static const uint8_t RX_COUNT = 13;
static const uint8_t TX_MAILBOX = 13;
static const uint32_t MAILBOX_MASK = (1u << MAILBOX_COUNT) - 1;

static volatile uint32_t* mailbox(uint8_t index)
{
    return (volatile uint32_t*)(index < 7 ? BASE + 0x80 + 0x48 * index : BASE + 0x280 + 0x48 * (index - 7));
}

// ---------------------------------------------------------------------------------------------

static void (*receivedHandler)(const CanFrame&) = nullptr;
static void (*transmittedHandler)() = nullptr;
static volatile bool active = false;
static volatile uint32_t overrunCount = 0;
static volatile uint32_t errorEsr1 = 0;
static volatile bool errorPending = false;

static const uint8_t DLC_TO_LENGTH[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64 };

static uint8_t lengthToDlc(uint8_t length)
{
    uint8_t dlc = 0;
    while (dlc < 15 && DLC_TO_LENGTH[dlc] < length)
    {
        dlc++;
    }
    return dlc;
}

uint8_t roundLength(uint8_t length)
{
    return DLC_TO_LENGTH[lengthToDlc(length)];
}

static bool waitFor(volatile uint32_t& reg, uint32_t mask, bool set)
{
    const uint32_t start = millis();
    while (((reg & mask) != 0) != set)
    {
        if (millis() - start > 100)
        {
            return false;
        }
    }
    return true;
}

static void enterFreeze()
{
    MCR |= MCR_FRZ | MCR_HALT;
    waitFor(MCR, MCR_FRZ_ACK, true);
}

// The clock of the FlexCAN controllers, in Hz; 0 when it is off.
static uint32_t canClock()
{
    static const uint32_t source[4] = { 60000000, 24000000, 80000000, 0 };
    return source[(CCM_CSCMR2 >> 8) & 3] / (((CCM_CSCMR2 >> 2) & 0x3F) + 1);
}

struct Timing
{
    uint32_t prescaler;     // from 1
    uint32_t propSeg;       // time quanta
    uint32_t phaseSeg1;
    uint32_t phaseSeg2;
    uint32_t jumpWidth;
};

/**
 * Bit timing for a bit rate: the smallest prescaler that gives at most maxQuanta time quanta in
 * a bit, the sample point near samplePercent.
 */
static bool findTiming(uint32_t clock, uint32_t bitRate, uint32_t minQuanta, uint32_t maxQuanta,
                       uint32_t samplePercent, uint32_t minPropSeg, uint32_t maxPropSeg, uint32_t maxPhaseSeg,
                       Timing& timing)
{
    if (bitRate == 0 || clock == 0)
    {
        return false;
    }
    for (uint32_t prescaler = 1; prescaler <= 1024; prescaler++)
    {
        const uint32_t quanta = (clock + (bitRate * prescaler) / 2) / (bitRate * prescaler);
        if (quanta > maxQuanta)
        {
            continue;
        }
        if (quanta < minQuanta)
        {
            return false;
        }
        // the bit rate this makes must be the one asked, within 0.5 %
        const uint32_t actual = clock / (prescaler * quanta);
        const uint32_t difference = actual > bitRate ? actual - bitRate : bitRate - actual;
        if (difference > bitRate / 200)
        {
            continue;
        }
        uint32_t phaseSeg2 = (quanta * (100 - samplePercent) + 50) / 100;
        if (phaseSeg2 < 2)
        {
            phaseSeg2 = 2;
        }
        if (phaseSeg2 > maxPhaseSeg)
        {
            phaseSeg2 = maxPhaseSeg;
        }
        const uint32_t before = quanta - 1 - phaseSeg2;     // propagation segment + phase segment 1
        uint32_t phaseSeg1 = before / 2;
        if (phaseSeg1 < 1)
        {
            phaseSeg1 = 1;
        }
        if (phaseSeg1 > maxPhaseSeg)
        {
            phaseSeg1 = maxPhaseSeg;
        }
        if (before < phaseSeg1 + minPropSeg || before - phaseSeg1 > maxPropSeg)
        {
            continue;
        }
        timing.prescaler = prescaler;
        timing.propSeg = before - phaseSeg1;
        timing.phaseSeg1 = phaseSeg1;
        timing.phaseSeg2 = phaseSeg2;
        timing.jumpWidth = phaseSeg2;
        return true;
    }
    return false;
}

static void interrupt();

uint8_t begin(uint32_t bitRate, uint32_t dataBitRate, bool listenOnly, bool loopback,
              void (*received)(const CanFrame&), void (*transmitted)())
{
    active = false;
    NVIC_DISABLE_IRQ(IRQ_CAN3);

    if ((CCM_CSCMR2 & 0x300) == 0x300)
    {
        // the clock of the CAN controllers is off: 24 MHz from the crystal
        CCM_CSCMR2 = (CCM_CSCMR2 & 0xFFFFFC03) | CCM_CSCMR2_CAN_CLK_SEL(1) | CCM_CSCMR2_CAN_CLK_PODF(0);
    }
    const uint32_t clock = canClock();

    // arbitration phase: 8 to 40 time quanta, sample point 80 %; data phase: 5 to 25, 75 %
    Timing nominal, data;
    if (!findTiming(clock, bitRate, 8, 40, 80, 1, 64, 32, nominal))
    {
        return BMT_ACK_BAD_BITRATE;
    }
    if (!findTiming(clock, dataBitRate, 5, 25, 75, 0, 31, 8, data))
    {
        return BMT_ACK_BAD_DATA_BITRATE;
    }

    CCM_CCGR0 |= CCM_CCGR0_LPUART3(CCM_CCGR_ON);    // the FD controller needs this clock too
    CCM_CCGR7 |= 0x3C0;                             // clocks of CAN3
    IOMUXC_SW_MUX_CTL_PAD_GPIO_EMC_36 = 0x19;       // pin 31: CAN3 TX
    IOMUXC_SW_PAD_CTL_PAD_GPIO_EMC_36 = 0x10B0;
    IOMUXC_CANFD_IPP_IND_CANRX_SELECT_INPUT = 0x00;
    IOMUXC_SW_MUX_CTL_PAD_GPIO_EMC_37 = 0x19;       // pin 30: CAN3 RX
    IOMUXC_SW_PAD_CTL_PAD_GPIO_EMC_37 = 0x10B0;

    MCR &= ~MCR_MDIS;
    waitFor(MCR, MCR_LPM_ACK, false);
    MCR |= MCR_SOFT_RST;
    waitFor(MCR, MCR_SOFT_RST, false);
    enterFreeze();

    uint32_t mcr = MCR & ~(0x7Fu | MCR_FEN | MCR_DMA | MCR_SRX_DIS);
    mcr |= MCR_IRMQ | MCR_FDEN | MCR_WRN_EN | (MAILBOX_COUNT - 1);
    if (!loopback)
    {
        mcr |= MCR_SRX_DIS;         // sent frames are not received
    }
    MCR = mcr;
    // remote frames are stored; the identifier type and the remote bit follow the masks (all 0:
    // every frame is accepted); ISO CAN FD
    CTRL2 |= CTRL2_RRS | CTRL2_EACEN | CTRL2_ISOCANFDEN | CTRL2_ERRMSK_FAST;
    CTRL1 = CTRL1_ERR_MSK | CTRL1_BOFF_MSK | (listenOnly ? CTRL1_LOM : 0) | (loopback ? CTRL1_LPB : 0);
    CBT = CBT_BTF | ((nominal.prescaler - 1) << 21) | ((nominal.jumpWidth - 1) << 16) |
          ((nominal.propSeg - 1) << 10) | ((nominal.phaseSeg1 - 1) << 5) | (nominal.phaseSeg2 - 1);
    FDCBT = ((data.prescaler - 1) << 20) | ((data.jumpWidth - 1) << 16) | (data.propSeg << 10) |
            ((data.phaseSeg1 - 1) << 5) | (data.phaseSeg2 - 1);
    // Transceiver delay compensation for fast data bits: the second sample point is placed at the
    // sample point of the data bit, counted in periods of the CAN clock (5 bit field).
    uint32_t fdctrl = FDCTRL_FDRATE | FDCTRL_TDCFAIL | (3u << 19) | (3u << 16);
    const uint32_t tdcOffset = (1 + data.propSeg + data.phaseSeg1) * data.prescaler;
    if (!loopback && dataBitRate >= 1000000 && tdcOffset <= 31)
    {
        fdctrl |= FDCTRL_TDCEN | (tdcOffset << 8);
    }
    FDCTRL = fdctrl;

    for (uint8_t index = 0; index < MAILBOX_COUNT; index++)
    {
        volatile uint32_t* box = mailbox(index);
        RXIMR(index) = 0;
        box[0] = 0;
        box[1] = 0;
        box[0] = CS_CODE(index < RX_COUNT ? CODE_RX_EMPTY : CODE_TX_INACTIVE);
    }
    IMASK2 = 0;
    IMASK1 = MAILBOX_MASK;
    IFLAG2 = 0xFFFFFFFF;
    IFLAG1 = 0xFFFFFFFF;
    ESR1 = ESR1;

    receivedHandler = received;
    transmittedHandler = transmitted;
    overrunCount = 0;
    errorPending = false;
    attachInterruptVector(IRQ_CAN3, interrupt);
    active = true;
    NVIC_ENABLE_IRQ(IRQ_CAN3);

    MCR &= ~MCR_HALT;
    waitFor(MCR, MCR_FRZ_ACK, false);
    return BMT_ACK_OK;
}

void end()
{
    if (!active)
    {
        return;
    }
    active = false;
    NVIC_DISABLE_IRQ(IRQ_CAN3);
    enterFreeze();
    IMASK1 = 0;
    IFLAG1 = 0xFFFFFFFF;
    // back to a classic controller: these registers survive the reset the FlexCAN_T4 library does
    CBT = 0;
    FDCBT = 0;
    FDCTRL = FDCTRL_RESET | FDCTRL_TDCFAIL;
    CTRL2 &= ~(CTRL2_ISOCANFDEN | CTRL2_ERRMSK_FAST);
    CTRL1 = CTRL1_LOM;
    MCR &= ~MCR_FDEN;
}

bool write(const CanFrame& frame)
{
    volatile uint32_t* box = mailbox(TX_MAILBOX);
    if (!active || ((box[0] >> 24) & 0xF) != CODE_TX_INACTIVE)
    {
        return false;
    }
    const bool extended = (frame.flags & BMT_FRAME_EXTENDED) != 0;
    const bool fd = (frame.flags & BMT_FRAME_FD) != 0;
    const bool remote = !fd && (frame.flags & BMT_FRAME_REMOTE) != 0;
    const uint8_t dlc = lengthToDlc(fd ? frame.length : (frame.length > 8 ? 8 : frame.length));
    const uint8_t length = remote ? 0 : DLC_TO_LENGTH[dlc];

    box[0] = CS_CODE(CODE_TX_INACTIVE);
    box[1] = extended ? (frame.id & 0x1FFFFFFF) : ((frame.id & 0x7FF) << 18);
    for (uint8_t word = 0; word * 4 < length; word++)
    {
        uint32_t value = 0;
        for (uint8_t byte = 0; byte < 4; byte++)
        {
            const uint8_t position = word * 4 + byte;
            value = (value << 8) | (position < frame.length ? frame.data[position] : 0);
        }
        box[2 + word] = value;
    }
    uint32_t cs = CS_CODE(CODE_TX_ONCE) | ((uint32_t)dlc << 16);
    if (extended)
    {
        cs |= CS_IDE | CS_SRR;
    }
    if (remote)
    {
        cs |= CS_RTR;
    }
    if (fd)
    {
        cs |= CS_EDL | ((frame.flags & BMT_FRAME_BRS) ? CS_BRS : 0);
    }
    box[0] = cs;
    return true;
}

bool takeError(uint32_t& esr1)
{
    if (!errorPending)
    {
        return false;
    }
    __disable_irq();
    esr1 = errorEsr1;
    errorPending = false;
    __enable_irq();
    return true;
}

uint32_t overruns()
{
    return overrunCount;
}

static void interrupt()
{
    if (active)
    {
        const uint32_t flags = IFLAG1 & IMASK1 & MAILBOX_MASK;

        // reception: read the mailboxes that have a frame, then give the frames in the order of
        // their time on the bus (frames can be waiting in several mailboxes)
        static CanFrame frames[RX_COUNT];
        uint16_t stamps[RX_COUNT];
        uint8_t order[RX_COUNT];
        uint8_t count = 0;
        for (uint8_t index = 0; index < RX_COUNT; index++)
        {
            if (!(flags & (1u << index)))
            {
                continue;
            }
            volatile uint32_t* box = mailbox(index);
            uint32_t cs = box[0];                   // locks the mailbox
            for (uint8_t attempt = 0; attempt < 100 && (cs & CS_CODE(1)); attempt++)
            {
                cs = box[0];                        // busy: a frame is being moved in
            }
            const uint32_t code = (cs >> 24) & 0xF;
            if (code == CODE_RX_FULL || code == CODE_RX_OVERRUN)
            {
                if (code == CODE_RX_OVERRUN)
                {
                    overrunCount++;
                }
                CanFrame& frame = frames[count];
                const bool extended = (cs & CS_IDE) != 0;
                const bool fd = (cs & CS_EDL) != 0;
                frame.id = extended ? (box[1] & 0x1FFFFFFF) : ((box[1] >> 18) & 0x7FF);
                frame.flags = (uint8_t)((extended ? BMT_FRAME_EXTENDED : 0) |
                                        ((!fd && (cs & CS_RTR)) ? BMT_FRAME_REMOTE : 0) |
                                        (fd ? BMT_FRAME_FD : 0) |
                                        ((fd && (cs & CS_BRS)) ? BMT_FRAME_BRS : 0) |
                                        ((fd && (cs & CS_ESI)) ? BMT_FRAME_ESI : 0));
                frame.length = DLC_TO_LENGTH[(cs >> 16) & 0xF];
                if (!fd && frame.length > 8)
                {
                    frame.length = 8;
                }
                const uint8_t dataLength = (frame.flags & BMT_FRAME_REMOTE) ? 0 : frame.length;
                for (uint8_t word = 0; word * 4 < dataLength; word++)
                {
                    const uint32_t value = box[2 + word];
                    frame.data[word * 4] = (uint8_t)(value >> 24);
                    frame.data[word * 4 + 1] = (uint8_t)(value >> 16);
                    frame.data[word * 4 + 2] = (uint8_t)(value >> 8);
                    frame.data[word * 4 + 3] = (uint8_t)value;
                }
                stamps[count] = (uint16_t)cs;
                order[count] = count;
                count++;
                box[0] = CS_CODE(CODE_RX_EMPTY);
            }
            (void)TIMER;                            // unlocks the mailbox
            IFLAG1 = 1u << index;
        }
        // insertion sort by the 16 bit time stamp of the controller, relative to the first frame
        for (uint8_t i = 1; i < count; i++)
        {
            const uint8_t item = order[i];
            const int16_t key = (int16_t)(stamps[item] - stamps[0]);
            int8_t j = (int8_t)i - 1;
            while (j >= 0 && (int16_t)(stamps[order[j]] - stamps[0]) > key)
            {
                order[j + 1] = order[j];
                j--;
            }
            order[j + 1] = item;
        }
        for (uint8_t i = 0; i < count; i++)
        {
            if (receivedHandler != nullptr)
            {
                receivedHandler(frames[order[i]]);
            }
        }

        // transmission: the frame is on the bus (after a remote frame the mailbox turns into a
        // reception mailbox: make it a transmit mailbox again)
        if (flags & (1u << TX_MAILBOX))
        {
            volatile uint32_t* box = mailbox(TX_MAILBOX);
            box[0] = CS_CODE(CODE_TX_INACTIVE);
            IFLAG1 = 1u << TX_MAILBOX;
            if (transmittedHandler != nullptr)
            {
                transmittedHandler();
            }
        }

        // errors: those of the data phase are reported on the bits of the arbitration phase
        const uint32_t esr1 = ESR1;
        const uint32_t errors = (esr1 | ((esr1 >> 16) & 0xDC00)) & ESR1_ERROR_BITS;
        if (errors != 0)
        {
            errorEsr1 = (esr1 & ~ESR1_ERROR_BITS) | errors;
            errorPending = true;
        }
        ESR1 = esr1;
    }
    else
    {
        IMASK1 = 0;
        ESR1 = ESR1;
    }
    asm volatile("dsb");
}

}
