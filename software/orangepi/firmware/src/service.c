/* SPDX-License-Identifier: GPL-2.0-only */
#include "neo_service.h"

void neo_service_stop(struct neo_service *s)
{
    s->bus.ready(s->bus.context, false);
    s->bus.irq(s->bus.context, false);
    s->running = s->awaiting = false;
}

void neo_service_init(struct neo_service *s, struct neo_bus bus, struct neo_v9990 *vdp)
{
    *s = (struct neo_service){.bus=bus, .vdp=vdp, .fault=NEO_SERVICE_IDLE};
    neo_service_stop(s);
}

static enum neo_service_result fault(struct neo_service *s, enum neo_service_result error)
{
    neo_service_stop(s);
    s->fault = error;
    return error;
}

static enum neo_service_result reset(struct neo_service *s, uint64_t ticks)
{
    neo_service_stop(s);
    neo_vdp_reset(s->vdp, ticks);
    return NEO_SERVICE_RESET;
}

enum neo_service_result neo_service_poll(struct neo_service *s, uint64_t ticks)
{
    void *c = s->bus.context;
    if (s->fault != NEO_SERVICE_IDLE) return s->fault;
    if (!s->bus.reset_high(c)) return reset(s, ticks);
    uint16_t config = s->bus.read(c, NEO9990_CONFIG);
    if (!s->bus.reset_high(c)) return reset(s, ticks);
    if (s->running && !(config & NEO9990_CONFIG_ARMED))
        return reset(s, ticks);
    if (!s->running) {
        uint16_t signature = s->bus.read(c, NEO9990_REPLY_TICKET);
        if (!s->bus.reset_high(c)) return reset(s, ticks);
        if ((signature & 0xff) != NEO9990_SIGNATURE)
            return fault(s, NEO_SERVICE_BAD_SIGNATURE);
        if (config & NEO9990_CONFIG_BOOT_JUMPER) return fault(s, NEO_SERVICE_BOOT_JUMPER);
        if (s->bus.read(c, NEO9990_STATUS) & NEO9990_STATUS_ERROR)
            return fault(s, NEO_SERVICE_PROTOCOL);
        neo_vdp_reset(s->vdp, ticks);
        s->bus.write(c, NEO9990_CONFIG, NEO9990_ENABLE_V9990);
        config = s->bus.read(c, NEO9990_CONFIG);
        if (!s->bus.reset_high(c)) return reset(s, ticks);
        if ((config & (NEO9990_CONFIG_FEATURES | NEO9990_CONFIG_PAGES)) != NEO9990_ENABLE_V9990)
            return fault(s, NEO_SERVICE_CONFIG_ERROR);
        s->bus.ready(c, true);
        s->running = true;
    }
    if (neo_vdp_tick(s->vdp, ticks) != NEO_VDP_OK)
        return fault(s, NEO_SERVICE_VDP_FAULT);
    s->bus.irq(c, neo_vdp_irq(s->vdp));
    uint16_t status = s->bus.read(c, NEO9990_STATUS);
    if (!(status & NEO9990_STATUS_RESET_N) || !s->bus.reset_high(c))
        return reset(s, ticks);
    if (status & NEO9990_STATUS_ERROR) return fault(s, NEO_SERVICE_PROTOCOL);
    if (!(status & NEO9990_STATUS_PENDING)) {
        s->awaiting = false;
        return NEO_SERVICE_IDLE;
    }
    uint16_t addr = s->bus.read(c, NEO9990_ADDRESS);
    uint16_t ticket = s->bus.read(c, NEO9990_REPLY_TICKET);
    if (!s->bus.reset_high(c)) return reset(s, ticks);
    struct neo9990_request request;
    if (neo9990_decode_request(status, addr, ticket, &request) != NEO9990_REQUEST)
        return fault(s, NEO_SERVICE_PROTOCOL);
    if (s->awaiting && request.token == s->token) {
        if (ticks - s->reply_time > NEO_UC_HZ / 10)
            return fault(s, NEO_SERVICE_ACK_TIMEOUT);
        return NEO_SERVICE_IDLE;
    }
    if (request.device != NEO9990_DEVICE_V9990 || request.is_memory ||
        (request.address & 0xf0) != 0x60)
        return fault(s, NEO_SERVICE_BAD_DEVICE);
    uint8_t data = 0;
    enum neo_vdp_result result = request.is_read ?
        neo_vdp_read(s->vdp, request.address, ticks, &data) :
        neo_vdp_write(s->vdp, request.address, request.write_data, ticks);
    if (result != NEO_VDP_OK) return fault(s, NEO_SERVICE_VDP_FAULT);
    config = s->bus.read(c, NEO9990_CONFIG);
    if (!s->bus.reset_high(c) || !(config & NEO9990_CONFIG_ARMED))
        return reset(s, ticks);
    s->bus.irq(c, neo_vdp_irq(s->vdp));
    s->bus.write(c, NEO9990_REPLY_TICKET, neo9990_reply_word(request.token, data));
    s->awaiting = true; s->token = request.token; s->reply_time = ticks;
    return NEO_SERVICE_REPLIED;
}
