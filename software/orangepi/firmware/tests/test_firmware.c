/* SPDX-License-Identifier: GPL-2.0-only */
#include "neo_fdt.h"
#include "neo_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); exit(1); } } while (0)

static struct neo_v9990 v;
static uint32_t line[NEO_WIDTH];

static void wr(uint16_t port, uint8_t data)
{
    CHECK(neo_vdp_write(&v,port,data,v.now) == NEO_VDP_OK);
}

static uint8_t rd(uint16_t port)
{
    uint8_t result = 0;
    CHECK(neo_vdp_read(&v,port,v.now,&result) == NEO_VDP_OK);
    return result;
}

static void reg(unsigned number, uint8_t data)
{
    wr(0x64,(uint8_t)(0xc0 | number));
    wr(0x63,data);
}

static void addr(unsigned base, unsigned value, bool inhibit)
{
    reg(base,(uint8_t)value);
    reg(base+1,(uint8_t)(value >> 8));
    reg(base+2,(uint8_t)((value >> 16) | (inhibit ? 0x80 : 0)));
}

static uint8_t pattern(unsigned x)
{
    return (uint8_t)((x*31u) ^ (x >> 8) ^ (x >> 16));
}

static void test_vram(void)
{
    for (unsigned mode = 0; mode < 3; ++mode) {
        neo_vdp_power_on(&v,0);
        reg(6,(uint8_t)(mode == 0 ? 0 : mode == 1 ? 0x40 : 0x82));
        addr(0,0,false);
        for (unsigned i = 0; i < NEO_VRAM_SIZE; ++i) wr(0x60,pattern(i));
        CHECK(v.regs[0] == 0 && v.regs[1] == 0 && v.regs[2] == 0);
        addr(3,0,false);
        for (unsigned i = 0; i < NEO_VRAM_SIZE; ++i) CHECK(rd(0x60) == pattern(i));
        CHECK(rd(0x60) == pattern(0));
        for (unsigned i = 0; i < NEO_VRAM_SIZE; ++i) {
            unsigned logical;
            if (mode == 0) logical = i;
            else if (mode == 1 && i >= 0x3c000 && i < 0x40000) logical = i+0x3c000;
            else if (mode == 1 && i >= 0x7c000) logical = i;
            else logical = ((i & 0x3ffff) << 1) | (i >> 18);
            CHECK(v.vram[i] == pattern(logical));
        }
        addr(0,12345,true);
        wr(0x60,0x12); wr(0x60,0x34);
        addr(3,12345,true);
        CHECK(rd(0x60) == 0x34 && rd(0x60) == 0x34);
        reg(3,99);
        CHECK(rd(0x60) == 0x34); /* Changing R3 alone does not reload the buffer. */
        addr(3,12345,false);
        wr(0x60,0x56);
        CHECK(rd(0x60) == 0x34); /* CPU reads are buffered, not direct VRAM reads. */
    }
}

static void test_registers(void)
{
    neo_vdp_power_on(&v,0);
    for (unsigned i = 0; i < NEO_VRAM_SIZE; ++i)
        CHECK(v.vram[i] == ((i & 512) ? 255 : 0));
    reg(18,255); CHECK(v.regs[18] == 0xdf);
    reg(19,255); CHECK(v.regs[19] == 7);
    reg(22,255); CHECK(v.regs[22] == 0xc1);
    reg(24,255); CHECK(v.regs[24] == 0x3f);
    reg(29,255); CHECK(v.regs[29] == 0);
    reg(53,255); CHECK(v.regs[53] == 0);
    wr(0x64,0x80 | 6);
    wr(0x63,0x82); CHECK(v.select == (0x80 | 6));
    CHECK(rd(0x63) == 0x82 && v.select == (0x80 | 7));
    wr(0x64,0x40 | 6);
    CHECK(rd(0x63) == 0x82 && v.select == (0x40 | 6));
    wr(0x63,0x81); CHECK(v.select == (0x40 | 7));
    wr(0x64,0);
    CHECK(rd(0x63) == 0xff && v.select == 1);
    wr(0x64,0x3f); wr(0x63,255); CHECK(v.select == 0);
    wr(0x64,0xbf); CHECK(rd(0x63) == 255 && v.select == 0x80);
    CHECK(rd(0xff69) == 0xff); /* High address byte is ignored. */
    CHECK(neo_vdp_read(&v,0x70,0,&v.read_buffer) == NEO_VDP_BAD_ARGUMENT);
    CHECK(neo_vdp_read(&v,0x60,0,NULL) == NEO_VDP_BAD_ARGUMENT);
    reg(14,0xff); wr(0x61,255); CHECK(v.palette[255] == 0 && v.regs[14] == 252);
    reg(14,0); wr(0x61,255); wr(0x61,255); wr(0x61,255);
    CHECK(v.palette[0] == 0x9f && v.palette[1] == 31 && v.palette[2] == 31);
    CHECK(v.regs[14] == 4);
    reg(14,0); reg(13,0x10); CHECK(rd(0x61) == 0x9f && v.regs[14] == 0);
    wr(0x61,15); CHECK(v.regs[14] == 1); /* Inhibit applies to palette reads only. */
    addr(0,0,false); wr(0x60,123);
    neo_vdp_reset(&v,42);
    CHECK(v.vram[0] == 123 && v.palette[0] == 15 && v.now == 42);
    wr(0x67,2); CHECK(v.system_reset);
    wr(0x64,6); CHECK(v.select == 0);
    CHECK(rd(0x63) == 255 && v.select == 0);
    wr(0x60,99); CHECK(v.vram[0] == 123);
    wr(0x67,0); CHECK(!v.system_reset && v.vram[0] == 123);
    reg(52,0); CHECK((rd(0x65) & 0x81) == 0);
    CHECK(neo_vdp_write(&v,0x64,52,42) == NEO_VDP_OK);
    CHECK(neo_vdp_write(&v,0x63,0x20,42) == NEO_VDP_UNSUPPORTED_COMMAND);
    CHECK(v.fault_register == 52 && v.fault_value == 0x20);
    CHECK(neo_vdp_read(&v,0x65,42,&v.read_buffer) == NEO_VDP_UNSUPPORTED_COMMAND);
}

static void test_timing_and_render(void)
{
    neo_vdp_power_on(&v,0);
    reg(9,1);
    uint64_t frame = 262u*NEO_LINE_TICKS;
    uint64_t bottom = 240u*NEO_LINE_TICKS; /* R16=0 gives top=28, not 29. */
    CHECK(neo_vdp_tick(&v,bottom-1) == NEO_VDP_OK && !neo_vdp_irq(&v));
    CHECK(neo_vdp_tick(&v,bottom) == NEO_VDP_OK && neo_vdp_irq(&v));
    CHECK(rd(0x66) == 1);
    wr(0x66,1); CHECK(!neo_vdp_irq(&v));
    CHECK(neo_vdp_tick(&v,bottom+1) == NEO_VDP_OK && !neo_vdp_irq(&v));
    CHECK(neo_vdp_tick(&v,frame) == NEO_VDP_OK && !neo_vdp_irq(&v));
    CHECK(neo_vdp_tick(&v,frame+bottom) == NEO_VDP_OK && neo_vdp_irq(&v));
    CHECK(neo_vdp_tick(&v,v.now-1) == NEO_VDP_TIME_REVERSED);
    neo_vdp_power_on(&v,0);
    CHECK(neo_vdp_demo(&v) == NEO_VDP_OK);
    uint32_t colors[] = {0xffffff,0xffff00,0x00ffff,0x00ff00,0xff00ff,0xff0000,0x0000ff,0};
    for (unsigned y = 0; y < NEO_HEIGHT; ++y) {
        CHECK(neo_vdp_render_line(&v,y,line,NEO_WIDTH) == NEO_VDP_OK);
        for (unsigned x = 0; x < NEO_WIDTH; ++x) CHECK(line[x] == colors[x/32]);
    }
    CHECK(neo_vdp_render_line(&v,0,line,255) == NEO_VDP_BAD_ARGUMENT);
    CHECK(neo_vdp_render_line(&v,212,line,256) == NEO_VDP_BAD_ARGUMENT);
    reg(6,0x81); addr(0,0,false); wr(0x60,0x56);
    CHECK(neo_vdp_render_line(&v,0,line,NEO_WIDTH) == NEO_VDP_OK);
    CHECK(line[0] == 0xff0000 && line[1] == 0xff);
    reg(7,8); CHECK(neo_vdp_tick(&v,2*frame) == NEO_VDP_OK && v.pal);
    uint64_t pal_frame = 313u*NEO_LINE_TICKS;
    CHECK(neo_vdp_tick(&v,2*frame+pal_frame) == NEO_VDP_OK);
    CHECK(v.frame_start == 2*frame+pal_frame);
    wr(0x64,0xc6);
    CHECK(neo_vdp_write(&v,0x63,0,v.now) == NEO_VDP_UNSUPPORTED_DISPLAY);
    CHECK(neo_vdp_render_line(&v,0,line,NEO_WIDTH) == NEO_VDP_UNSUPPORTED_DISPLAY);
    neo_vdp_power_on(&v,0); reg(9,1);
    CHECK(neo_vdp_tick(&v,1000*frame) == NEO_VDP_OK && neo_vdp_irq(&v));
    CHECK(neo_vdp_write(&v,0x64,9,v.now) == NEO_VDP_OK);
    CHECK(neo_vdp_write(&v,0x63,2,v.now) == NEO_VDP_UNSUPPORTED_IRQ);
}

struct fake {
    uint16_t regs[4], reply;
    bool ready, irq, reset;
    unsigned replies, ready_highs, writes;
};

static uint16_t fake_read(void *context, enum neo9990_register regno)
{
    struct fake *f = context;
    return f->regs[regno];
}

static void fake_write(void *context, enum neo9990_register regno, uint16_t value)
{
    struct fake *f = context;
    ++f->writes;
    if (regno == NEO9990_REPLY_TICKET) { f->reply=value; ++f->replies; }
    else f->regs[regno]=value;
}

static void fake_ready(void *context, bool high)
{
    struct fake *f = context; f->ready=high;
    if (high) { ++f->ready_highs; f->regs[3] |= NEO9990_CONFIG_ARMED; }
}
static void fake_irq(void *context, bool high) { ((struct fake *)context)->irq=high; }
static bool fake_reset(void *context) { return ((struct fake *)context)->reset; }

static struct neo_bus fake_bus(struct fake *f)
{
    return (struct neo_bus){f,fake_read,fake_write,fake_ready,fake_irq,fake_reset};
}

static void setup(struct fake *f, struct neo_service *s)
{
    *f = (struct fake){.regs={0x1000,0,0x00d4,0},.reset=true};
    neo_vdp_power_on(&v,0);
    neo_service_init(s,fake_bus(f),&v);
    CHECK(neo_service_poll(s,0) == NEO_SERVICE_IDLE);
    CHECK(f->ready && (f->regs[3] & 7) == NEO9990_ENABLE_V9990);
}

static void request(struct fake *f, uint8_t token, uint16_t address, bool read, uint8_t data)
{
    f->regs[0] = (uint16_t)(0x9100 | (read ? 0x4000 : 0) | data);
    f->regs[1] = address;
    f->regs[2] = (uint16_t)(((uint16_t)token << 8) | 0xd4);
}

static void test_service(void)
{
    struct fake f;
    struct neo_service s;
    setup(&f,&s);
    for (unsigned i = 0; i < 768; ++i) {
        request(&f,(uint8_t)i,0x64,false,0xc6);
        CHECK(neo_service_poll(&s,i) == NEO_SERVICE_REPLIED);
        CHECK(f.reply == (uint16_t)(i << 8) && f.replies == i+1);
        CHECK(neo_service_poll(&s,i) == NEO_SERVICE_IDLE && f.replies == i+1);
        f.regs[0]=0x1000;
        CHECK(neo_service_poll(&s,i) == NEO_SERVICE_IDLE);
    }
    f.reset=false;
    CHECK(neo_service_poll(&s,800) == NEO_SERVICE_RESET && !f.ready);
    f.reset=true;
    CHECK(neo_service_poll(&s,801) == NEO_SERVICE_IDLE && f.ready_highs == 2);
    f.regs[3] &= (uint16_t)~NEO9990_CONFIG_ARMED;
    CHECK(neo_service_poll(&s,801) == NEO_SERVICE_RESET && !f.ready);
    CHECK(neo_service_poll(&s,801) == NEO_SERVICE_IDLE && f.ready_highs == 3);
    request(&f,1,0x64,false,6);
    CHECK(neo_service_poll(&s,802) == NEO_SERVICE_REPLIED);
    /* A new token proves the previous request completed, even if its low pulse was missed. */
    request(&f,2,0x63,false,0x82);
    CHECK(neo_service_poll(&s,803) == NEO_SERVICE_REPLIED && v.regs[6] == 0x82);
    CHECK(neo_service_poll(&s,NEO_UC_HZ) == NEO_SERVICE_ACK_TIMEOUT && !f.ready);
    CHECK(neo_service_poll(&s,NEO_UC_HZ+1) == NEO_SERVICE_ACK_TIMEOUT);
    setup(&f,&s); request(&f,1,0x63,false,0); f.regs[0]|=0x800;
    CHECK(neo_service_poll(&s,1) == NEO_SERVICE_PROTOCOL && !f.ready && !f.replies);
    setup(&f,&s); request(&f,1,0x60,true,0); f.regs[2]=0x00c3;
    CHECK(neo_service_poll(&s,1) == NEO_SERVICE_PROTOCOL && !f.ready);
    setup(&f,&s); request(&f,1,0x60,true,0); f.regs[0]=0xf200;
    CHECK(neo_service_poll(&s,1) == NEO_SERVICE_BAD_DEVICE && !f.replies);
    setup(&f,&s); request(&f,1,0x62,false,0x55);
    CHECK(neo_service_poll(&s,1) == NEO_SERVICE_VDP_FAULT && !f.ready && !f.replies);
    setup(&f,&s); neo_service_stop(&s); f.regs[3]=0x8000;
    CHECK(neo_service_poll(&s,1) == NEO_SERVICE_BOOT_JUMPER && !f.ready);
    setup(&f,&s); neo_service_stop(&s); f.regs[2]=0;
    CHECK(neo_service_poll(&s,1) == NEO_SERVICE_BAD_SIGNATURE && !f.ready);
}

static uint8_t tree[4096], names[512];
static unsigned cursor, name_cursor;

static void word(uint32_t value)
{
    CHECK(cursor+4 <= sizeof tree);
    tree[cursor++]=(uint8_t)(value >> 24); tree[cursor++]=(uint8_t)(value >> 16);
    tree[cursor++]=(uint8_t)(value >> 8); tree[cursor++]=(uint8_t)value;
}

static void align_tree(void) { while (cursor & 3) tree[cursor++]=0; }
static void begin(const char *name)
{
    word(1);
    do { tree[cursor++]=(uint8_t)*name; } while (*name++);
    align_tree();
}
static void property(const char *name, const void *data, unsigned size)
{
    word(3); word(size); word(name_cursor);
    do { names[name_cursor++]=(uint8_t)*name; } while (*name++);
    memcpy(tree+cursor,data,size); cursor+=size; align_tree();
}
static void integer(const char *name, uint32_t value)
{
    uint8_t bytes[4]={(uint8_t)(value >> 24),(uint8_t)(value >> 16),(uint8_t)(value >> 8),(uint8_t)value};
    property(name,bytes,4);
}
static void string(const char *name, const char *value)
{
    property(name,value,(unsigned)strlen(value)+1);
}

static unsigned make_tree(const char *board, const char *format, const char *mode,
                          unsigned stride, bool framebuffer)
{
    memset(tree,0,sizeof tree); memset(names,0,sizeof names); cursor=56; name_cursor=0;
    begin(""); string("compatible",board); integer("#address-cells",2); integer("#size-cells",2);
    begin("chosen"); integer("#address-cells",2); integer("#size-cells",2);
    string("neo9990,mode",mode);
    if (framebuffer) {
        begin("framebuffer"); string("compatible","simple-framebuffer");
        uint8_t range[]={0,0,0,0,0x50,0,0,0,0,0,0,0,0,0x40,0,0};
        property("reg",range,sizeof range); string("format",format);
        integer("width",640); integer("height",480); integer("stride",stride);
        string("status","okay"); word(2);
    }
    word(2); word(2); word(9);
    unsigned struct_size=cursor-56, string_offset=cursor;
    memcpy(tree+cursor,names,name_cursor); cursor+=name_cursor;
    unsigned total=cursor;
    cursor=0; word(0xd00dfeed); word(total); word(56); word(string_offset);
    word(40); word(17); word(16); word(0); word(name_cursor); word(struct_size);
    return total;
}

static void test_fdt(void)
{
    struct neo_boot boot;
    unsigned size=make_tree("xunlong,orangepi-zero2w","x8r8g8b8","demo",2560,true);
    CHECK(neo_fdt_parse(tree,size,&boot) == NEO_BOOT_OK);
    CHECK(!boot.cartridge && boot.fb.address == 0x50000000 && boot.fb.size == 0x400000);
    CHECK(boot.fb.width == 640 && boot.fb.height == 480 && !boot.fb.swap_red_blue);
    for (unsigned i = 0; i < size; ++i) CHECK(neo_fdt_parse(tree,i,&boot) == NEO_BOOT_BAD_FDT);
    CHECK(neo_fb_overlaps(&boot.fb,0x50000000,1));
    CHECK(!neo_fb_overlaps(&boot.fb,0x50400000,1));
    CHECK(neo_fb_overlaps(&boot.fb,UINT64_MAX,10));
    size=make_tree("wrong-board","x8r8g8b8","demo",2560,true);
    CHECK(neo_fdt_parse(tree,size,&boot) == NEO_BOOT_WRONG_BOARD);
    size=make_tree("xunlong,orangepi-zero2w","x8b8g8r8","cartridge",2560,true);
    CHECK(neo_fdt_parse(tree,size,&boot) == NEO_BOOT_OK && boot.cartridge && boot.fb.swap_red_blue);
    size=make_tree("xunlong,orangepi-zero2w","r5g6b5","demo",2560,true);
    CHECK(neo_fdt_parse(tree,size,&boot) == NEO_BOOT_BAD_FRAMEBUFFER);
    size=make_tree("xunlong,orangepi-zero2w","x8r8g8b8","demo",100,true);
    CHECK(neo_fdt_parse(tree,size,&boot) == NEO_BOOT_BAD_FRAMEBUFFER);
    size=make_tree("xunlong,orangepi-zero2w","x8r8g8b8","typo",2560,true);
    CHECK(neo_fdt_parse(tree,size,&boot) == NEO_BOOT_BAD_MODE);
    size=make_tree("xunlong,orangepi-zero2w","x8r8g8b8","demo",2560,false);
    CHECK(neo_fdt_parse(tree,size,&boot) == NEO_BOOT_NO_FRAMEBUFFER);
    CHECK(neo_fdt_parse(NULL,size,&boot) == NEO_BOOT_BAD_FDT);
    CHECK(neo_fdt_parse(tree,size,NULL) == NEO_BOOT_BAD_FDT);
}

int main(int argc, char **argv)
{
    test_vram(); test_registers(); test_timing_and_render(); test_service(); test_fdt();
    if (argc == 2) {
        neo_vdp_power_on(&v,0); CHECK(neo_vdp_demo(&v) == NEO_VDP_OK);
        FILE *file=fopen(argv[1],"wb"); CHECK(file != NULL);
        CHECK(fprintf(file,"P6\n256 212\n255\n") > 0);
        for (unsigned y=0; y<NEO_HEIGHT; ++y) {
            CHECK(neo_vdp_render_line(&v,y,line,NEO_WIDTH) == NEO_VDP_OK);
            for (unsigned x=0; x<NEO_WIDTH; ++x) {
                uint8_t bytes[]={(uint8_t)(line[x] >> 16),(uint8_t)(line[x] >> 8),(uint8_t)line[x]};
                CHECK(fwrite(bytes,1,3,file) == 3);
            }
        }
        CHECK(fclose(file) == 0);
    }
    printf("PASS: %u firmware checks\n",checks);
    return 0;
}
