/* SPDX-License-Identifier: GPL-2.0-only */
#include "neo_fdt.h"

struct node {
    unsigned address_cells, size_cells, parent_address, parent_size;
    bool framebuffer, chosen, disabled, translated;
    const uint8_t *reg;
    uint32_t reg_size, width, height, stride;
    const uint8_t *format;
    uint32_t format_size;
};

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static bool eq(const uint8_t *p, size_t size, const char *text)
{
    size_t n = 0;
    while (text[n]) ++n;
    if (size != n + 1) return false;
    for (size_t i = 0; i <= n; ++i) if (p[i] != (uint8_t)text[i]) return false;
    return true;
}

static bool has_string(const uint8_t *p, size_t size, const char *text)
{
    size_t i = 0;
    while (i < size) {
        size_t end = i;
        while (end < size && p[end]) ++end;
        if (end == size) return false;
        if (eq(p+i, end-i+1, text)) return true;
        i = end + 1;
    }
    return false;
}

static bool bounded(uint32_t offset, uint32_t size, uint32_t total)
{
    return offset <= total && size <= total-offset;
}

static uint64_t cells(const uint8_t *p, unsigned count)
{
    return count == 2 ? ((uint64_t)be32(p) << 32) | be32(p+4) : be32(p);
}

bool neo_fb_overlaps(const struct neo_framebuffer *fb, uint64_t start, uint64_t size)
{
    if (!size || !fb->size) return false;
    if (start > UINT64_MAX-size || fb->address > UINT64_MAX-fb->size) return true;
    return fb->address < start+size && start < fb->address+fb->size;
}

enum neo_boot_result neo_fdt_parse(const void *data, size_t available, struct neo_boot *boot)
{
    if (!data || !boot || available < 40) return NEO_BOOT_BAD_FDT;
    const uint8_t *fdt = data;
    uint32_t total = be32(fdt+4), structure = be32(fdt+8), strings = be32(fdt+12);
    uint32_t string_size = be32(fdt+32), structure_size = be32(fdt+36);
    if (be32(fdt) != 0xd00dfeed || total < 40 || total > available ||
        be32(fdt+20) < 17 || be32(fdt+24) > 17 || structure < 40 ||
        strings < 40 || (structure & 3) || !bounded(structure, structure_size, total) ||
        !bounded(strings, string_size, total)) return NEO_BOOT_BAD_FDT;
    struct node stack[16] = {{0}};
    *boot = (struct neo_boot){0};
    unsigned depth = 0, framebuffers = 0;
    bool board = false, root_seen = false, root_closed = false;
    uint32_t pos = structure, end = structure+structure_size;
    while (bounded(pos, 4, end)) {
        uint32_t token = be32(fdt+pos);
        pos += 4;
        if (token == 1) {
            if (depth == 16 || root_closed) return NEO_BOOT_BAD_FDT;
            uint32_t name = pos;
            while (pos < end && fdt[pos]) ++pos;
            if (pos == end) return NEO_BOOT_BAD_FDT;
            uint32_t length = pos-name+1;
            if (!depth && (root_seen || length != 1)) return NEO_BOOT_BAD_FDT;
            struct node *n = &stack[depth];
            *n = (struct node){.address_cells=2, .size_cells=1};
            if (depth) {
                n->parent_address = stack[depth-1].address_cells;
                n->parent_size = stack[depth-1].size_cells;
                n->translated = stack[depth-1].translated;
                n->chosen = depth == 1 && eq(fdt+name, length, "chosen");
            } else root_seen = true;
            ++depth;
            if (pos > UINT32_MAX-4) return NEO_BOOT_BAD_FDT;
            pos = (pos+4) & ~3u;
        } else if (token == 2) {
            if (!depth) return NEO_BOOT_BAD_FDT;
            struct node *n = &stack[--depth];
            if (!depth) root_closed = true;
            if (n->framebuffer && !n->disabled) {
                if (++framebuffers != 1 || (depth != 1 &&
                    !(depth == 2 && stack[1].chosen)) || n->translated || !n->reg ||
                    n->parent_address < 1 || n->parent_address > 2 ||
                    n->parent_size < 1 || n->parent_size > 2 ||
                    n->reg_size != 4*(n->parent_address+n->parent_size))
                    return NEO_BOOT_BAD_FRAMEBUFFER;
                struct neo_framebuffer *fb = &boot->fb;
                fb->address = cells(n->reg, n->parent_address);
                fb->size = cells(n->reg+4*n->parent_address, n->parent_size);
                fb->width = n->width; fb->height = n->height; fb->stride = n->stride;
                bool rgb = n->format && (eq(n->format,n->format_size,"x8r8g8b8") ||
                                        eq(n->format,n->format_size,"a8r8g8b8"));
                bool bgr = n->format && (eq(n->format,n->format_size,"x8b8g8r8") ||
                                        eq(n->format,n->format_size,"a8b8g8r8"));
                fb->swap_red_blue = bgr;
                if ((!rgb && !bgr) || fb->width < 256 || fb->height < 212 ||
                    fb->width > 4096 || fb->height > 2160 || (fb->address & 3) ||
                    (fb->stride & 3) || fb->stride < fb->width*4 ||
                    (uint64_t)fb->stride*fb->height > fb->size ||
                    fb->address < UINT64_C(0x40000000) ||
                    fb->address >= UINT64_C(0xc0000000) ||
                    fb->size > UINT64_C(0xc0000000)-fb->address)
                    return NEO_BOOT_BAD_FRAMEBUFFER;
            }
        } else if (token == 3) {
            if (!depth || !bounded(pos, 8, end)) return NEO_BOOT_BAD_FDT;
            uint32_t length = be32(fdt+pos), key = be32(fdt+pos+4);
            pos += 8;
            if (!bounded(pos, length, end) || key >= string_size) return NEO_BOOT_BAD_FDT;
            uint32_t limit = key;
            while (limit < string_size && fdt[strings+limit]) ++limit;
            if (limit == string_size) return NEO_BOOT_BAD_FDT;
            const uint8_t *name = fdt+strings+key, *value = fdt+pos;
            size_t size = limit-key+1;
            struct node *n = &stack[depth-1];
            if (eq(name,size,"compatible")) {
                if (depth == 1) board = has_string(value,length,"xunlong,orangepi-zero2w");
                n->framebuffer = has_string(value,length,"simple-framebuffer");
            } else if (eq(name,size,"#address-cells") || eq(name,size,"#size-cells")) {
                if (length != 4) return NEO_BOOT_BAD_FDT;
                if (name[1] == 'a') n->address_cells = be32(value);
                else n->size_cells = be32(value);
            } else if (eq(name,size,"ranges") && length) n->translated = true;
            else if (eq(name,size,"status"))
                n->disabled = !eq(value,length,"okay") && !eq(value,length,"ok");
            else if (eq(name,size,"reg")) { n->reg = value; n->reg_size = length; }
            else if (eq(name,size,"format")) { n->format = value; n->format_size = length; }
            else if (eq(name,size,"width") || eq(name,size,"height") || eq(name,size,"stride")) {
                if (length != 4) return NEO_BOOT_BAD_FDT;
                if (name[0] == 'w') n->width = be32(value);
                else if (name[0] == 'h') n->height = be32(value);
                else n->stride = be32(value);
            } else if (n->chosen && eq(name,size,"neo9990,mode")) {
                if (eq(value,length,"cartridge")) boot->cartridge = true;
                else if (!eq(value,length,"demo")) return NEO_BOOT_BAD_MODE;
            }
            if (pos > UINT32_MAX-3 || length > UINT32_MAX-pos-3) return NEO_BOOT_BAD_FDT;
            pos = (pos+length+3) & ~3u;
        } else if (token == 4) continue;
        else if (token == 9) {
            if (depth || !root_closed) return NEO_BOOT_BAD_FDT;
            if (!board) return NEO_BOOT_WRONG_BOARD;
            return framebuffers ? NEO_BOOT_OK : NEO_BOOT_NO_FRAMEBUFFER;
        } else return NEO_BOOT_BAD_FDT;
    }
    return NEO_BOOT_BAD_FDT;
}
