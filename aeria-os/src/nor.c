/* AERIA OS: updating itself in NOR. Our own code. The board's NOR (16-bit, AMD command set, at 0) holds the
 * Elektron bootloader below 0x4000 -- never touched here -- and the OS as Elektron's plain container from 0x4000
 * (what the bootloader checks and unpacks into 0x40000400). Command words and the sector starts as Modwerk's
 * flash.c names them (its notes, read for understanding): unlock 0xAAAA at byte 0xAAAA, 0x5555 at 0x5554; program
 * 0xA0A0; sector erase 0x8080 + unlock + 0x3030 at the sector; reset 0xF0F0. Sectors: 8 KB from 0x4000 to 0x10000,
 * then 64 KB to 0x1F0000.
 * The write goes back to front, the container's first sector last, so a cut write never looks like a whole OS.
 * Everything runs from SDRAM with interrupts masked (AERIA OS never unmasks them). */
#define NOR(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define NOR_OS 0x4000u
#define NOR_END 0x200000u
static uint32_t nor_sector_start(uint32_t a) { return a < 0x10000u ? a & ~0x1FFFu : a & ~0xFFFFu; }
static uint32_t nor_sector_end(uint32_t a) { return a < 0x10000u ? (a & ~0x1FFFu) + 0x2000u : (a & ~0xFFFFu) + 0x10000u; }
static void nor_unlock(void) { NOR(0xAAAAu) = 0xAAAAu; NOR(0x5554u) = 0x5555u; }
static int nor_erase_sector(uint32_t a)
{
    if (a < NOR_OS || a >= NOR_END) return 0;
    nor_unlock(); NOR(0xAAAAu) = 0x8080u; nor_unlock(); NOR(a) = 0x3030u;
    uint32_t t = now();
    while (NOR(a) != 0xFFFFu) if (now() - t > MS(8000)) { NOR(0xAAAAu) = 0xF0F0u; return 0; }
    for (uint32_t p = a; p < nor_sector_end(a); p += 2) if (NOR(p) != 0xFFFFu) return 0;
    return 1;
}
static int nor_program_word(uint32_t a, uint16_t v)
{
    if (a < NOR_OS || a >= NOR_END || (a & 1)) return 0;
    if (v == 0xFFFFu) return NOR(a) == 0xFFFFu;
    nor_unlock(); NOR(0xAAAAu) = 0xA0A0u; NOR(a) = v;
    for (uint32_t n = 0; n < 2000000u; n++) if (NOR(a) == v) return 1;
    NOR(0xAAAAu) = 0xF0F0u;
    return 0;
}
/* [a, a+len) from src: erase the sectors, program, verify. 1 if NOR then holds exactly src. */
volatile uint32_t nor_result, nor_progress;   /* 0 idle, 1 working, 2 done, 0xEn failed at step n */
static int nor_write(uint32_t a, const uint8_t *src, uint32_t len)
{
    if (a < NOR_OS || a + len > NOR_END || (a & 0x1FFFu) || !len) return 0;
    uint32_t end = (a + len + 1u) & ~1u;
    /* back to front, a sector at a time: erase it, program its share */
    uint32_t s = nor_sector_start(end - 1u);
    for (;;) {
        if (!nor_erase_sector(s)) { nor_result = 0xE1; return 0; }
        uint32_t from = s > a ? s : a, to = nor_sector_end(s) < end ? nor_sector_end(s) : end;
        for (uint32_t p = from; p < to; p += 2) {
            uint32_t o = p - a;
            uint16_t v = (uint16_t)(src[o] << 8 | (o + 1 < len ? src[o + 1] : 0xFF));
            if (!nor_program_word(p, v)) { nor_result = 0xE2; return 0; }
        }
        nor_progress = end - s;
        if (s <= a) break;
        s = nor_sector_start(s - 1u);
    }
    for (uint32_t o = 0; o < len; o++) if (*(volatile uint8_t *)(uintptr_t)(a + o) != src[o]) { nor_result = 0xE3; return 0; }
    return 1;
}
