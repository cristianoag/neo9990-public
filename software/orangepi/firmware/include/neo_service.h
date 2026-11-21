/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NEO_SERVICE_H
#define NEO_SERVICE_H

#include "neo_v9990.h"
#include "../../host/neo9990_parallel.h"

struct neo_bus {
    void *context;
    uint16_t (*read)(void *, enum neo9990_register);
    void (*write)(void *, enum neo9990_register, uint16_t);
    void (*ready)(void *, bool);
    void (*irq)(void *, bool);
    bool (*reset_high)(void *);
};

enum neo_service_result {
    NEO_SERVICE_IDLE, NEO_SERVICE_RESET, NEO_SERVICE_REPLIED,
    NEO_SERVICE_BAD_SIGNATURE, NEO_SERVICE_BOOT_JUMPER, NEO_SERVICE_PROTOCOL,
    NEO_SERVICE_BAD_DEVICE, NEO_SERVICE_VDP_FAULT, NEO_SERVICE_ACK_TIMEOUT,
    NEO_SERVICE_CONFIG_ERROR
};

struct neo_service {
    struct neo_bus bus;
    struct neo_v9990 *vdp;
    bool running, awaiting;
    uint8_t token;
    uint64_t reply_time;
    enum neo_service_result fault;
};

void neo_service_init(struct neo_service *service, struct neo_bus bus,
                      struct neo_v9990 *vdp);
enum neo_service_result neo_service_poll(struct neo_service *service, uint64_t ticks);
void neo_service_stop(struct neo_service *service);

#endif
