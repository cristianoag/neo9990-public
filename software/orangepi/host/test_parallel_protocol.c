#include "neo9990_parallel.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    struct neo9990_request request;
    unsigned int checks = 0;
    for (unsigned int address = 0; address < 65536; ++address) {
        assert(neo9990_decode_request(0xf200, (uint16_t)address, 0x37d4, &request) == NEO9990_REQUEST);
        assert(request.address == address && request.is_read && request.is_memory);
        assert(request.token == 0x37 && request.device == NEO9990_DEVICE_ROM);
        assert(neo9990_reply_word(request.token, (uint8_t)address) == (uint16_t)(0x3700 | (address & 255)));
        checks += 4;
    }
    assert(neo9990_decode_request(0x916a, 0xab60, 0x11d4, &request) == NEO9990_REQUEST);
    assert(request.device == NEO9990_DEVICE_V9990 && !request.is_read && !request.is_memory && request.write_data == 0x6a);
    assert(neo9990_decode_request(0xd300, 0x007f, 0x22d4, &request) == NEO9990_REQUEST);
    assert(request.device == NEO9990_DEVICE_OPL4 && request.is_read);
    assert(neo9990_decode_request(0x1000, 0, 0, &request) == NEO9990_NO_REQUEST);
    assert(neo9990_decode_request(0, 0, 0, &request) == NEO9990_RESET_ACTIVE);
    assert(neo9990_decode_request(0xf200, 0, 0, &request) == NEO9990_BAD_SIGNATURE);
    assert(neo9990_decode_request(0xf201, 0, 0xd4, 0) == NEO9990_BAD_ARGUMENT);
    assert(neo9990_decode_request(0xfa00, 0, 0xd4, &request) == NEO9990_PROTOCOL_ERROR);
    assert(neo9990_decode_request(0xf100, 0, 0xd4, &request) == NEO9990_BAD_DEVICE);
    assert(neo9990_decode_request(0xd200, 0, 0xd4, &request) == NEO9990_BAD_DEVICE);
    assert(neo9990_decode_request(0xd700, 0, 0xd4, &request) == NEO9990_BAD_DEVICE);
    checks += 12;
    printf("PASS: %u parallel host protocol checks\n", checks);
    return 0;
}
