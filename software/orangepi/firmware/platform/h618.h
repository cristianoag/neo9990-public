/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NEO_H618_H
#define NEO_H618_H
#include "neo_service.h"

uint64_t h618_counter(void);
uint64_t h618_frequency(void);
void h618_delay_us(unsigned microseconds);
void h618_puts(const char *text);
void h618_hex(uint64_t number);
_Noreturn void h618_halt(void);
struct neo_bus h618_bus_init(void);
void neo_kernel(uint64_t fdt_address);
_Noreturn void neo_exception(uint64_t syndrome, uint64_t pc, uint64_t address);

#endif
