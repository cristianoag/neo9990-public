/*
 * NEO9990 board bring-up shared by the V9990 and V9968 firmwares: releasing
 * the bus pins, the clock tree and the USB console.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pll.h"
#include "hardware/structs/qmi.h"
#include "hardware/structs/sio.h"
#include "hardware/vreg.h"
#include "board_pins.h"
#include "bus.h"
#include "platform.h"
#if defined(NEO_CHIP_V9990)
#include "psram.h"
#endif

/* Clock plan (the renderers need the extra speed):
 *   pll_sys 252MHz -> clk_hstx / 2 = 126MHz, i.e. 5 HSTX clocks per 25.2MHz
 *                     DVI pixel (two bits shifted per clock)
 *   pll_usb 1536MHz / 4 = 384MHz -> clk_sys, and clk_usb = 384MHz / 8 = 48MHz
 * clk_hstx only has an integer divider with no duty-cycle fix-up and HSTX
 * shifts on both edges, so an odd divider (clk_sys / 3) gave a 33% duty cycle
 * and a black picture on the monitor. An even divider keeps it at 50%. */
#define PLL_SYS_KHZ    252000u
#define SYS_CLOCK_KHZ  384000u
#define HSTX_CLOCK_HZ  126000000u   /* 2 bits per clock, 5 clocks per pixel */

void neo_release_bus_outputs(void) {
    /* Every bus pin becomes a plain input without pulls. The output latch of
     * /INT, /WAIT and /BUSDIR is cleared so that turning one of them into an
     * output later pulls the line low. The HSTX pins are left alone. */
    for (uint pin = PIN_A0; pin <= PIN_RESET; pin++) {
        if (pin >= PIN_D0 + 8 && pin < PIN_A4) continue;   /* HSTX pins */
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        gpio_disable_pulls(pin);
    }
    gpio_put(PIN_INT, 0);
    gpio_put(PIN_WAIT, 0);
    gpio_put(PIN_BUSDIR, 0);
    gpio_pull_up(PIN_RESET);
}

/* Run the flash at a safe divider before clk_sys goes up (executes from RAM). */
static void __no_inline_not_in_flash_func(set_flash_divisor)(uint32_t clkdiv, uint32_t rxdelay) {
    uint32_t t = qmi_hw->m[0].timing;
    t &= ~(QMI_M0_TIMING_CLKDIV_BITS | QMI_M0_TIMING_RXDELAY_BITS);
    t |= clkdiv << QMI_M0_TIMING_CLKDIV_LSB | rxdelay << QMI_M0_TIMING_RXDELAY_LSB;
    qmi_hw->m[0].timing = t;
}

void neo_init_clocks(void) {
    vreg_set_voltage(VREG_VOLTAGE_1_30);
    sleep_ms(10);
    set_flash_divisor(5, 3);                     /* 76.8MHz flash at 384MHz */
    set_sys_clock_khz(PLL_SYS_KHZ, true);        /* clk_sys = pll_sys for now */
    clock_configure(clk_hstx, 0, CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                    PLL_SYS_KHZ * 1000u, HSTX_CLOCK_HZ);
    /* pll_usb 1536MHz VCO / 4 / 1 = 384MHz (USB is not running yet) */
    pll_init(pll_usb, 1, 1536u * MHZ, 4, 1);
    clock_configure(clk_usb, 0, CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
                    SYS_CLOCK_KHZ * 1000u, 48u * MHZ);
    clock_configure(clk_sys, CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
                    CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
                    SYS_CLOCK_KHZ * 1000u, SYS_CLOCK_KHZ * 1000u);
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                    SYS_CLOCK_KHZ * 1000u, SYS_CLOCK_KHZ * 1000u);
}

static void command(const char *line, int (*chip_cmd)(const char *line)) {
    while (*line == ' ') line++;
    if (!*line) return;
    if (strncmp(line, "stat", 4) == 0) {
        bus_print_stats(strstr(line, "reset") != NULL);
    } else if (strncmp(line, "vram", 4) == 0) {
        bus_print_vram_crc();
    } else if (strncmp(line, "out ", 4) == 0) {
        char *end;
        unsigned long port = strtoul(line + 4, &end, 16);
        unsigned long value = strtoul(end, &end, 16);
        bus_debug_out((unsigned)port, (unsigned)value);
#if defined(NEO_CHIP_V9990)
    } else if (strncmp(line, "psram", 5) == 0) {
        psram_diag(strstr(line, "sweep") != NULL);
#endif
    } else if (chip_cmd && chip_cmd(line)) {
        /* handled by the firmware */
    } else {
        printf("NEO9990 " NEO_FW_VERSION " commands: stat [reset], vram, out <port> <val>"
#if defined(NEO_CHIP_V9990)
               ", psram [sweep]"
#endif
               "\n");
    }
}

void neo_console_poll(int (*chip_cmd)(const char *line)) {
    static char buf[32];
    static unsigned len;
    int c;
    while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        if (c == '\r' || c == '\n') {
            buf[len] = 0;
            command(buf, chip_cmd);
            len = 0;
        } else if (len < sizeof buf - 1) {
            buf[len++] = (char)c;
        }
    }
}
