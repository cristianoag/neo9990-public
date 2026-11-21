/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NEO9990_PARALLEL_H
#define NEO9990_PARALLEL_H

#include <stdbool.h>
#include <stdint.h>

enum neo9990_register {
    NEO9990_STATUS = 0,
    NEO9990_ADDRESS = 1,
    NEO9990_REPLY_TICKET = 2,
    NEO9990_CONFIG = 3
};

enum neo9990_device {
    NEO9990_DEVICE_V9990 = 1,
    NEO9990_DEVICE_ROM = 2,
    NEO9990_DEVICE_OPL4 = 3
};

enum neo9990_feature {
    NEO9990_ENABLE_V9990 = 1,
    NEO9990_ENABLE_ROM = 2,
    NEO9990_ENABLE_OPL4 = 4
};

enum neo9990_decode_result {
    NEO9990_NO_REQUEST = 0,
    NEO9990_REQUEST = 1,
    NEO9990_BAD_ARGUMENT = -1,
    NEO9990_RESET_ACTIVE = -2,
    NEO9990_PROTOCOL_ERROR = -3,
    NEO9990_BAD_SIGNATURE = -4,
    NEO9990_BAD_DEVICE = -5
};

struct neo9990_request {
    uint16_t address;
    uint8_t write_data;
    uint8_t token;
    enum neo9990_device device;
    bool is_read;
    bool is_memory;
};

#define NEO9990_SIGNATURE UINT16_C(0x00d4)
#define NEO9990_STATUS_PENDING UINT16_C(0x8000)
#define NEO9990_STATUS_READ UINT16_C(0x4000)
#define NEO9990_STATUS_MEMORY UINT16_C(0x2000)
#define NEO9990_STATUS_RESET_N UINT16_C(0x1000)
#define NEO9990_STATUS_ERROR UINT16_C(0x0800)
#define NEO9990_CONFIG_BOOT_JUMPER UINT16_C(0x8000)
#define NEO9990_CONFIG_ARMED UINT16_C(0x4000)
#define NEO9990_CONFIG_PAGES UINT16_C(0x0f00)
#define NEO9990_CONFIG_FEATURES UINT16_C(0x0007)

static inline enum neo9990_decode_result neo9990_decode_request(
    uint16_t status, uint16_t address, uint16_t ticket,
    struct neo9990_request *request)
{
    unsigned int device = (status >> 8) & 7u;
    bool memory = (status & NEO9990_STATUS_MEMORY) != 0;
    if (request == 0)
        return NEO9990_BAD_ARGUMENT;
    if ((status & NEO9990_STATUS_RESET_N) == 0)
        return NEO9990_RESET_ACTIVE;
    if ((status & NEO9990_STATUS_ERROR) != 0)
        return NEO9990_PROTOCOL_ERROR;
    if ((status & NEO9990_STATUS_PENDING) == 0)
        return NEO9990_NO_REQUEST;
    if ((ticket & UINT16_C(0x00ff)) != NEO9990_SIGNATURE)
        return NEO9990_BAD_SIGNATURE;
    if (device < NEO9990_DEVICE_V9990 || device > NEO9990_DEVICE_OPL4 ||
        memory != (device == NEO9990_DEVICE_ROM))
        return NEO9990_BAD_DEVICE;
    request->address = address;
    request->write_data = (uint8_t)status;
    request->token = (uint8_t)(ticket >> 8);
    request->device = (enum neo9990_device)device;
    request->is_read = (status & NEO9990_STATUS_READ) != 0;
    request->is_memory = memory;
    return NEO9990_REQUEST;
}

static inline uint16_t neo9990_reply_word(uint8_t token, uint8_t data)
{
    return (uint16_t)(((uint16_t)token << 8) | data);
}

#endif
