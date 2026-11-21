/*
 * NEO9990 firmware, MSX bus engine (core1 side).
 *
 * Data flow
 *   MSX write : PIO monitor -> DMA ring -> core1 -> <chip>_write_port()
 *   MSX read  : the PIO monitor decodes the cycle and pulls /WAIT and /BUSDIR
 *               low; the PIO read machine then waits for core1 to say that
 *               every earlier access is done, fetches the answer byte out of
 *               read_table[] with a pair of DMA channels and drives D0-D7 on
 *               its own. core1 only keeps read_table[] current (through
 *               <chip>_peek_port()) and applies the side effects of the read
 *               once the byte has been taken.
 *
 * The port decode lives in the PIO (see bus.pio): a single 6-bit compare of
 * A7-A4, /M1 and /IORQ replaces the decoder chip of earlier designs.
 *
 * Port blocks (low nibble = port within the block)
 *   V9990 : 60h-6Fh. Nibbles 0-7 are P#0-P#7, 8-F answer FFh.
 *   V9968 : 80h-8Fh. Nibbles 8-C are the V9968 ports 0-4, D and F are the
 *           geo3d index and data ports, 0-7 and E answer FFh.
 *
 * The build defines exactly one of NEO_CHIP_V9990 and NEO_CHIP_V9968, so the
 * unused chip costs no code and no run-time test.
 */
#include <assert.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/structs/sio.h"
#include "hardware/timer.h"
#include "board_pins.h"
#include "bus.h"
#include "bus.pio.h"
#include "dvi_out.h"

#if defined(NEO_CHIP_V9990) && defined(NEO_CHIP_V9968)
#error "build either the V9990 or the V9968 firmware"
#elif defined(NEO_CHIP_V9990)
#include "v9990.h"
#define DECODE_FIELD_VALUE DECODE_V9990
#elif defined(NEO_CHIP_V9968)
#include "v9968.h"
#define DECODE_FIELD_VALUE DECODE_V9968
#else
#error "define NEO_CHIP_V9990 or NEO_CHIP_V9968"
#endif

static_assert(PIN_IORQ == neo_bus_monitor_PIN_IORQ, "bus.pio PIN_IORQ mismatch");
static_assert(PIN_IORQ == neo_bus_read_PIN_IORQ, "bus.pio PIN_IORQ mismatch");
static_assert(PIN_IORQ == PIN_A4 + 5 && PIN_M1 == PIN_A4 + 4, "decode field is A4 A5 A6 A7 /M1 /IORQ");
static_assert(PIN_D0 == PIN_A0 + 4, "data bus must follow A0-A3");
static_assert(PIN_BUSDIR == PIN_WAIT + 1, "/WAIT and /BUSDIR are the set pins");

#define RING_BITS   12                          /* 4KB = 1024 events */
#define RING_WORDS  ((1u << RING_BITS) / 4u)

/* Bus event = snapshot of GP0-GP31 taken by the monitor */
#define EV_PORT(ev)     ((ev) & 0x0Fu)
#define EV_DATA(ev)     (((ev) >> 4) & 0xFFu)
#define EV_IS_READ(ev)  (!((ev) & (1u << PIN_RD)))

#define V9968_BLOCK 8u                          /* V9968 ports at nibble 8-F */
#define V9968_CLK_PER_US 86u                    /* command engine: 85.9MHz */
#define V9968_SLICE 512u                        /* max clocks per loop pass */
#define V9990_CMD_SLICE 4u                      /* command engine units per loop pass */
#define V9990_CMD_FAST_SLICE 1024u              /* fast paths: they yield on bus events */

static uint32_t ring[RING_WORDS] __attribute__((aligned(1u << RING_BITS)));
static volatile uint32_t read_table[16] __attribute__((aligned(64)));

/* Diagnostics, written by core1 and read by bus_print_stats() on core0. */
static volatile struct {
    uint32_t loops;            /* core1 heartbeat */
    uint32_t events;
    uint32_t max_backlog;      /* ring words waiting at once (overrun at RING_WORDS) */
    uint32_t max_loop_us;      /* longest core1 loop pass */
    uint32_t writes[16], reads[16];
    uint32_t last_ev[8];       /* last events, newest at last_pos - 1 */
    uint32_t last_pos;
    uint32_t resets;
    uint32_t fault_pc, fault_lr, fault_core;
    uint32_t cmd_us, cmd_slices;   /* time in the command engine while busy */
    uint32_t read_timeouts;    /* ordered reads the read SM did not take */
} stats;

#ifdef NEO_CHIP_V9990
static v9990_t *v9990;
#else
static v9968_t *v9968;
#endif
static PIO bus_pio;
static uint sm_monitor, sm_read;
static int dma_ring, dma_read_addr, dma_read_data;

/* /INT is open collector style: the output latch stays 0, the pin is an output
 * (low) while the interrupt is asserted and an input (released) otherwise. */
static inline void __not_in_flash_func(int_assert)(bool on) {
    if (on) sio_hw->gpio_oe_set = 1u << PIN_INT;
    else    sio_hw->gpio_oe_clr = 1u << PIN_INT;
}

static void init_gpio(void) {
    const uint32_t oc_mask = (1u << PIN_WAIT) | (1u << PIN_BUSDIR);

    /* A0-A3, D0-D7: PIO controlled, inputs by default */
    for (uint pin = PIN_A0; pin < PIN_D0 + 8; pin++) {
        pio_gpio_init(bus_pio, pin);
        gpio_set_pulls(pin, false, false);
    }
    pio_sm_set_consecutive_pindirs(bus_pio, sm_read, PIN_A0, 12, false);

    /* decode field, /RD, /WR: plain inputs that the PIO reads */
    for (uint pin = PIN_A4; pin <= PIN_WR; pin++) {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        gpio_disable_pulls(pin);
    }
    gpio_init(PIN_RESET);
    gpio_set_dir(PIN_RESET, GPIO_IN);
    gpio_pull_up(PIN_RESET);

    /* /INT: SIO, output latch 0, released (input) */
    gpio_init(PIN_INT);
    gpio_put(PIN_INT, 0);
    gpio_set_dir(PIN_INT, GPIO_IN);
    gpio_disable_pulls(PIN_INT);

    /* /WAIT, /BUSDIR: PIO, output latch 0, released (input) */
    pio_sm_set_pins_with_mask(bus_pio, sm_read, 0, oc_mask);
    pio_sm_set_pindirs_with_mask(bus_pio, sm_read, 0, oc_mask);
    pio_gpio_init(bus_pio, PIN_WAIT);
    pio_gpio_init(bus_pio, PIN_BUSDIR);
    gpio_disable_pulls(PIN_WAIT);
    gpio_disable_pulls(PIN_BUSDIR);
}

static void init_monitor_sm(void) {
    uint offset = pio_add_program(bus_pio, &neo_bus_monitor_program);
    pio_sm_config c = neo_bus_monitor_program_get_default_config(offset);
    sm_config_set_in_pins(&c, PIN_A0);
    sm_config_set_in_shift(&c, false, false, 32);   /* shift left, no autopush */
    sm_config_set_out_shift(&c, true, false, 32);   /* shift right, no autopull */
    sm_config_set_jmp_pin(&c, PIN_RD);
    sm_config_set_set_pins(&c, PIN_WAIT, 2);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    pio_sm_init(bus_pio, sm_monitor, offset, &c);
    pio_sm_exec(bus_pio, sm_monitor, pio_encode_set(pio_y, DECODE_FIELD_VALUE));

    /* DMA: monitor RX FIFO -> ring buffer, forever */
    dma_channel_config dc = dma_channel_get_default_config(dma_ring);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_read_increment(&dc, false);
    channel_config_set_write_increment(&dc, true);
    channel_config_set_ring(&dc, true, RING_BITS);
    channel_config_set_dreq(&dc, pio_get_dreq(bus_pio, sm_monitor, false));
    dma_channel_configure(dma_ring, &dc, ring, &bus_pio->rxf[sm_monitor],
                          (DMA_CH0_TRANS_COUNT_MODE_VALUE_ENDLESS
                           << DMA_CH0_TRANS_COUNT_MODE_LSB) | 1u,
                          true);
}

static void init_read_sm(void) {
    uint offset = pio_add_program(bus_pio, &neo_bus_read_program);
    pio_sm_config c = neo_bus_read_program_get_default_config(offset);
    sm_config_set_in_pins(&c, PIN_A0);
    sm_config_set_out_pins(&c, PIN_D0, 8);
    sm_config_set_in_shift(&c, false, false, 32);   /* shift left */
    sm_config_set_out_shift(&c, true, false, 32);   /* shift right */
    sm_config_set_set_pins(&c, PIN_WAIT, 2);
    pio_sm_init(bus_pio, sm_read, offset, &c);

    /* X = table address >> 6 */
    pio_sm_put_blocking(bus_pio, sm_read, (uint32_t)(uintptr_t)read_table >> 6);
    pio_sm_exec(bus_pio, sm_read, pio_encode_pull(false, true));
    pio_sm_exec(bus_pio, sm_read, pio_encode_mov(pio_x, pio_osr));

    /* DMA 1: RX FIFO (response address) -> read_addr trigger of DMA 2 */
    dma_channel_config ca = dma_channel_get_default_config(dma_read_addr);
    channel_config_set_transfer_data_size(&ca, DMA_SIZE_32);
    channel_config_set_read_increment(&ca, false);
    channel_config_set_write_increment(&ca, false);
    channel_config_set_dreq(&ca, pio_get_dreq(bus_pio, sm_read, false));
    channel_config_set_high_priority(&ca, true);

    /* DMA 2: one word from the table -> TX FIFO */
    dma_channel_config cd = dma_channel_get_default_config(dma_read_data);
    channel_config_set_transfer_data_size(&cd, DMA_SIZE_32);
    channel_config_set_read_increment(&cd, false);
    channel_config_set_write_increment(&cd, false);
    channel_config_set_high_priority(&cd, true);
    dma_channel_configure(dma_read_data, &cd, &bus_pio->txf[sm_read],
                          read_table, 1, false);

    dma_channel_configure(dma_read_addr, &ca,
                          &dma_hw->ch[dma_read_data].al3_read_addr_trig,
                          &bus_pio->rxf[sm_read],
                          (DMA_CH0_TRANS_COUNT_MODE_VALUE_ENDLESS
                           << DMA_CH0_TRANS_COUNT_MODE_LSB) | 1u,
                          true);
}

#ifdef NEO_CHIP_V9990
static void __not_in_flash_func(refresh_vdp)(void) {
    for (uint port = 0; port < 8; port++)
        read_table[port] = v9990_peek_port(v9990, (uint8_t)port);
}
#else
static void __not_in_flash_func(refresh_vdp)(void) {
    for (uint port = 0; port < 8; port++)
        read_table[V9968_BLOCK + port] = v9968_peek_port(v9968, (uint8_t)port);
}
#endif

void bus_init(void *vdp) {
#ifdef NEO_CHIP_V9990
    v9990 = vdp;
#else
    v9968 = vdp;
#endif
    bus_pio = pio0;
    sm_monitor = (uint)pio_claim_unused_sm(bus_pio, true);
    sm_read = (uint)pio_claim_unused_sm(bus_pio, true);
    dma_ring = dma_claim_unused_channel(true);
    dma_read_addr = dma_claim_unused_channel(true);
    dma_read_data = dma_claim_unused_channel(true);

    for (uint port = 0; port < 16; port++) read_table[port] = 0xFF;
    refresh_vdp();
#ifdef NEO_CHIP_V9990
    v9990_cmd_watch = &dma_hw->ch[dma_ring].write_addr;
#else
    v9968_cmd_watch = &dma_hw->ch[dma_ring].write_addr;
#endif

    init_gpio();
    init_monitor_sm();
    init_read_sm();
    pio_set_sm_mask_enabled(bus_pio, (1u << sm_monitor) | (1u << sm_read), true);
}

void __not_in_flash_func(bus_refresh_status)(void) {
#ifdef NEO_CHIP_V9990
    if (v9990)
        read_table[V9990_PORT_STATUS] = v9990_peek_port(v9990, V9990_PORT_STATUS);
#endif
}

static inline uint32_t __not_in_flash_func(ring_head)(void) {
    return ((dma_hw->ch[dma_ring].write_addr - (uintptr_t)ring) / 4u) & (RING_WORDS - 1u);
}

/* Reads are answered in order. By the time core1 gets here the read machine
 * has been holding the Z80 on /WAIT, and every earlier access has been
 * applied and the table refreshed. Release the answer and wait until the byte
 * has actually been taken: only then may the side effects of this very read
 * (address counters, status flags) be applied. */
#define READ_IRQ_GO   4u
#define READ_IRQ_DONE 5u

static inline void __not_in_flash_func(ordered_read)(void) {
    bus_pio->irq_force = 1u << READ_IRQ_GO;
    uint32_t t0 = timer_hw->timerawl;
    while (!(bus_pio->irq & (1u << READ_IRQ_DONE))) {
        if (timer_hw->timerawl - t0 > 20u) {
            /* the read SM did not see this read */
            bus_pio->irq = 1u << READ_IRQ_GO;
            stats.read_timeouts++;
            return;
        }
    }
    bus_pio->irq = 1u << READ_IRQ_DONE;
}

static inline void __not_in_flash_func(stat_event)(uint32_t ev) {
    uint8_t port = (uint8_t)EV_PORT(ev);
    stats.events++;
    if (EV_IS_READ(ev)) stats.reads[port]++;
    else                stats.writes[port]++;
    stats.last_ev[stats.last_pos & 7u] = ev;
    stats.last_pos++;
}

static inline void __not_in_flash_func(stat_loop)(uint32_t head, uint32_t tail,
                                                  uint32_t *t0) {
    uint32_t backlog = (head - tail) & (RING_WORDS - 1u);
    if (backlog > stats.max_backlog) stats.max_backlog = backlog;
    uint32_t now = timer_hw->timerawl;
    if (now - *t0 > stats.max_loop_us) stats.max_loop_us = now - *t0;
    *t0 = now;
    stats.loops++;
}

#ifdef NEO_CHIP_V9990
static void __not_in_flash_func(loop_vdp)(void) {
    uint32_t tail = 0;
    bool in_reset = false;
    uint32_t t0 = timer_hw->timerawl;

    for (;;) {
        uint32_t head = ring_head();
        stat_loop(head, tail, &t0);
        while (tail != head) {
            uint32_t ev = ring[tail];
            tail = (tail + 1u) & (RING_WORDS - 1u);
            stat_event(ev);
            uint8_t port = (uint8_t)EV_PORT(ev);
            bool is_read = EV_IS_READ(ev);
            if (is_read) ordered_read();
            if (port < 8) {
                if (is_read)
                    (void)v9990_read_port(v9990, port);
                else
                    v9990_write_port(v9990, port, (uint8_t)EV_DATA(ev));
                refresh_vdp();
                /* Interrupt handlers return a few microseconds after
                 * clearing P#6, so drop /INT right away or the Z80 would
                 * be interrupted a second time. */
                int_assert(v9990_irq_pending(v9990));
            }
        }

        /* short slices keep bus events from waiting long */
        if (v9990->cmd.op) {
            uint32_t c0 = timer_hw->timerawl;
            if (v9990_cmd_step(v9990, v9990_cmd_fast(v9990) ? V9990_CMD_FAST_SLICE : V9990_CMD_SLICE))
                refresh_vdp();
            stats.cmd_us += timer_hw->timerawl - c0;
            stats.cmd_slices++;
        }
        read_table[V9990_PORT_STATUS] = v9990_peek_port(v9990, V9990_PORT_STATUS);
        read_table[V9990_PORT_INT_FLAG] = v9990_peek_port(v9990, V9990_PORT_INT_FLAG);

        int_assert(v9990_irq_pending(v9990));

        bool reset = !gpio_get(PIN_RESET);
        if (reset && !in_reset) {
            stats.resets++;
            v9990_reset(v9990);
            refresh_vdp();
        }
        in_reset = reset;
    }
}
#else
/* Set by the console to freeze the V9968 command engine while state is dumped */
volatile bool bus_cmd_hold;

static void __not_in_flash_func(loop_vdp)(void) {
    uint32_t tail = 0;
    bool in_reset = false;
    uint32_t last_us = timer_hw->timerawl;
    int32_t credit = 0;                 /* command engine clocks owed */
    uint32_t t0 = last_us;

    for (;;) {
        uint32_t head = ring_head();
        stat_loop(head, tail, &t0);
        while (tail != head) {
            uint32_t ev = ring[tail];
            tail = (tail + 1u) & (RING_WORDS - 1u);
            stat_event(ev);
            uint8_t port = (uint8_t)EV_PORT(ev);
            bool is_read = EV_IS_READ(ev);
            /* core1 is the only writer of read_table, so this is what the
             * read machine puts on the bus for this event */
            uint8_t seen = (uint8_t)read_table[port];
            if (is_read) ordered_read();
            if (port >= V9968_BLOCK) {
                if (is_read)
                    (void)v9968_read_port_seen(v9968, port - V9968_BLOCK, seen);
                else {
                    /* geo3d starts a model with a long set-up before it can
                     * report busy, yet the CPU polls the status a few
                     * microseconds after RUN. Mark it busy in the table first
                     * (refresh_vdp() below keeps it), otherwise software would
                     * see "done" and load the next model over the one that is
                     * still being drawn. */
                    if (port - V9968_BLOCK == V9968_PORT_GEO_DATA)
                        read_table[V9968_BLOCK + V9968_PORT_GEO_INDEX] |= 0x05;
                    v9968_write_port(v9968, port - V9968_BLOCK, (uint8_t)EV_DATA(ev));
                }
                refresh_vdp();
            }
        }

        /* the command engine is paced to the real cartridge's clock */
        uint32_t now = timer_hw->timerawl;
        credit += (int32_t)((now - last_us) * V9968_CLK_PER_US);
        last_us = now;
        if (credit > (int32_t)V9968_SLICE) credit = V9968_SLICE;
        if (credit > 0 && !bus_cmd_hold && (v9968_cmd_busy(v9968) || v9968->geo->running)) {
            /* The engine watches the ring through v9968_cmd_watch and gives up
             * within about 32 clocks once an event arrives, so a read parked
             * on /WAIT never waits for a whole slice. read_table is refreshed
             * once at the end; a parked read only proceeds after the ring has
             * been served, which happens after this. */
            do {
                unsigned b = (unsigned)credit;
                unsigned used = v9968_service(v9968, b);
                credit -= (int32_t)used;
                if (used < b) break;              /* done, or waiting for the CPU */
            } while (credit > 0 && ring_head() == tail);
            refresh_vdp();
        } else {
            read_table[V9968_BLOCK + V9968_PORT_CTRL] = v9968_peek_port(v9968, V9968_PORT_CTRL);
            read_table[V9968_BLOCK + V9968_PORT_INT] = v9968_peek_port(v9968, V9968_PORT_INT);
        }
        if (!v9968_cmd_busy(v9968) && !v9968->geo->running && credit > 0) credit = 0;

        int_assert(v9968_irq_pending(v9968));

        bool reset = !gpio_get(PIN_RESET);
        if (reset && !in_reset) {
            stats.resets++;
            v9968_reset(v9968);
            refresh_vdp();
        }
        in_reset = reset;
    }
}
#endif

void __not_in_flash_func(bus_core1_main)(void) {
    loop_vdp();
}

/* Hard fault: remember where it happened and park this core. The other core
 * keeps USB alive so that "stat" can report it. Every open collector line is
 * let go first so that the MSX is not held up. */
void __attribute__((used)) bus_fault_record(uint32_t *frame) {
    stats.fault_pc = frame[6];
    stats.fault_lr = frame[5];
    stats.fault_core = get_core_num() + 1u;
    int_assert(false);
    gpio_init(PIN_WAIT);                    /* from the read SM back to SIO, as inputs */
    gpio_init(PIN_BUSDIR);
    for (;;) __asm volatile("wfi");
}

void __attribute__((naked)) isr_hardfault(void) {
    __asm volatile(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "b bus_fault_record\n");
}

void bus_print_stats(bool reset) {
    static uint32_t prev_loops, prev_us;
    uint32_t now = timer_hw->timerawl;
    uint32_t loops = stats.loops;
    printf("core1: %lu loops (%lu/ms since last stat), max pass %lu us, "
           "max backlog %lu/%u, MSX resets %lu, ordered-read timeouts %lu\n",
           (unsigned long)loops,
           (unsigned long)((loops - prev_loops) / ((now - prev_us) / 1000u + 1u)),
           (unsigned long)stats.max_loop_us, (unsigned long)stats.max_backlog,
           (unsigned)RING_WORDS, (unsigned long)stats.resets,
           (unsigned long)stats.read_timeouts);
    prev_loops = loops;
    prev_us = now;
    if (stats.fault_core)
        printf("HARD FAULT on core%lu: pc %08lx lr %08lx\n",
               (unsigned long)stats.fault_core - 1u,
               (unsigned long)stats.fault_pc, (unsigned long)stats.fault_lr);
    printf("events %lu; port w/r:", (unsigned long)stats.events);
    for (uint p = 0; p < 16; p++)
        if (stats.writes[p] || stats.reads[p])
            printf(" %X:%lu/%lu", p, (unsigned long)stats.writes[p],
                   (unsigned long)stats.reads[p]);
    printf("\nlast events (old->new):");
    uint32_t pos = stats.last_pos;
    for (uint i = 0; i < 8; i++) {
        uint32_t ev = stats.last_ev[(pos + i) & 7u];
        printf(" %c%X=%02X", EV_IS_READ(ev) ? 'R' : 'W', (unsigned)EV_PORT(ev),
               (unsigned)EV_DATA(ev));
    }
    printf("\n/INT %s, /WAIT %s, /BUSDIR %s, /RESET %s\n",
           (sio_hw->gpio_oe & (1u << PIN_INT)) ? "ASSERTED" : "released",
           gpio_get(PIN_WAIT) ? "high" : "LOW",
           gpio_get(PIN_BUSDIR) ? "high" : "LOW",
           gpio_get(PIN_RESET) ? "high" : "LOW");
#ifdef NEO_CHIP_V9990
    if (v9990) {
        const v9990_t *v = v9990;
        printf("V9990: status %02X int_flags %02X int_enable(R9) %02X irq %d, "
               "cmd op %X remaining %u x %u, P#7 %02X\n",
               v->status, v->int_flags, v->regs[V9990_R_INT_ENABLE],
               v9990_irq_pending(v), v->cmd.op, v->cmd.remaining_x, v->cmd.remaining_y,
               v->sys_ctrl);
        printf("regs:");
        for (uint r = 0; r < 64; r++) printf("%s%02X", (r & 15) ? " " : "\n ", v->regs[r]);
        printf("\nread_table:");
        for (uint p = 0; p < 8; p++) printf(" %02lX", (unsigned long)read_table[p] & 0xFF);
        printf("\n");
    }
#endif
    static uint32_t prev_frame, prev_frame_us;
    uint32_t fr = dvi_lines.frame;
    printf("frames %lu in %lu ms; command engine busy %lu us (%lu slices)\n",
           (unsigned long)(fr - prev_frame), (unsigned long)((now - prev_frame_us) / 1000u),
           (unsigned long)stats.cmd_us, (unsigned long)stats.cmd_slices);
    printf("dvi: late IRQs %lu, output restarts %lu\n",
           (unsigned long)dvi_lines.dma_late, (unsigned long)dvi_lines.dma_restarts);
    dvi_print_debug();
    prev_frame = fr;
    prev_frame_us = now;
    uint32_t n = dvi_lines.render_lines;
    printf("render: %lu lines, avg %lu us, max %lu us, late %lu; picture %lu lines x%lu "
           "(budget %lu us per line)\n",
           (unsigned long)n, (unsigned long)(n ? dvi_lines.render_us / n : 0),
           (unsigned long)dvi_lines.render_max_us, (unsigned long)dvi_lines.render_late,
           (unsigned long)dvi_lines.src_height, (unsigned long)dvi_lines.scale,
           (unsigned long)(dvi_lines.scale * 3178u / 100u));
#if defined(NEO_CHIP_V9968) && defined(V9968_PROF)
    extern volatile uint32_t v9968_prof_us[6];
    printf("v9968 render stages (us total): background %lu, colours %lu, sprites %lu (select %lu, draw %lu), output %lu\n",
           (unsigned long)v9968_prof_us[0], (unsigned long)v9968_prof_us[1],
           (unsigned long)v9968_prof_us[2], (unsigned long)v9968_prof_us[4],
           (unsigned long)v9968_prof_us[5], (unsigned long)v9968_prof_us[3]);
    if (reset) for (int i = 0; i < 6; i++) v9968_prof_us[i] = 0;
#endif
    if (reset) {
        stats.cmd_us = 0;
        stats.cmd_slices = 0;
        stats.read_timeouts = 0;
        dvi_lines.render_lines = 0;
        dvi_lines.render_us = 0;
        dvi_lines.render_max_us = 0;
        dvi_lines.render_late = 0;
        stats.max_loop_us = 0;
        stats.max_backlog = 0;
    }
}

#ifdef NEO_CHIP_V9990
/* Console "vram": CRC32 of each 64KB VRAM block, for comparing with a replay
 * of the same program on a PC (an all-zero block gives d7978eeb). */
static uint32_t crc32_block(const volatile uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1u));
    }
    return ~c;
}

void bus_print_vram_crc(void) {
    printf("vram crc:");
    for (uint i = 0; i < 8; i++)
        printf(" %08lx", (unsigned long)crc32_block(
                   (i < 4 ? v9990->vram : v9990->vram1) + (i & 3u) * 65536u, 65536u));
    printf("\n");
}

void bus_debug_out(unsigned port, unsigned value) {
    v9990_write_port(v9990, (uint8_t)(port & 15u), (uint8_t)value);
    printf("P#%X <- %02X\n", port & 15u, value & 0xFFu);
}
#else
void bus_print_vram_crc(void) { printf("vram: V9990 only\n"); }

void bus_debug_out(unsigned port, unsigned value) {
    if (port < 8u) {
        v9968_write_port(v9968, (uint8_t)port, (uint8_t)value);
        printf("V9968 port %X <- %02X\n", port, value & 0xFFu);
    }
}
#endif
