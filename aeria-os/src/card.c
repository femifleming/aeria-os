/* AERIA OS: the CompactFlash card. Our own code: the MCF54455's ATA controller in PIO mode (polled, no DMA, no
 * interrupts) and a FAT16/FAT32 reader with long file names.
 *
 * The controller (register use read from 1.40C's card code for understanding, 0x40015ffa and 0x40014b94):
 *   0x90000002-08 PIO timing, 0x90000024 ATA_CR (bit 6 releases the card's reset line),
 *   0x900000A0 data (16 bits), A4 error/feature, A8 sector count, AC/B0/B4 LBA 7-0/15-8/23-16,
 *   B8 device (0xE0 | LBA 27-24), BC status (read) / command (write), D8 alternate status (BSY 7, DRDY 6, DRQ 3, ERR 0).
 * Card data arrives little-endian within each 16-bit word: card_read swaps it into byte order. */
#ifndef CARD_HOST   /* the host test (fat_test.c) supplies card_read itself */
#define C8(a)  (*(volatile uint8_t *)(a))
#define C16(a) (*(volatile uint16_t *)(a))
#define ATA_DATA 0x900000A0u
#define ATA_COUNT 0x900000A8u
#define ATA_LBA0 0x900000ACu
#define ATA_LBA1 0x900000B0u
#define ATA_LBA2 0x900000B4u
#define ATA_DEV 0x900000B8u
#define ATA_CMD 0x900000BCu
#define ATA_ALT 0x900000D8u

#define CMS(n) MS(n)

int card_ok;
uint32_t card_act_n;   /* counts every read or write: the card lamp flickers with it */
static int card_swap = 1;     /* 1: swap the bytes of each data word (found from the boot sector's 55 AA) */

static int ata_wait(int bits_set, uint32_t ms)
{
    uint32_t t = now();
    for (;;) {
        uint8_t s = C8(ATA_ALT);
        if (!(s & 0x80) && (s & bits_set) == bits_set) return s & 1 ? -1 : 1;
        if (now() - t > CMS(ms)) return 0;
    }
}
/* the controller as stock starts it: timing, the card's reset released, then wait for the card */
int card_init(void)
{
    C8(0x90000002u) = 10; C8(0x90000003u) = 22; C8(0x90000004u) = 22; C8(0x90000005u) = 5;
    C8(0x90000006u) = 0;  C8(0x90000007u) = 4;  C8(0x90000008u) = 3;
    C8(0x90000024u) = 0x40;
    { uint32_t t = now(); while (now() - t < CMS(20)) {} }
    C8(0x9000002Cu) = 0;                 /* no interrupts: AERIA OS polls */
    if (ata_wait(0x40, 3000) <= 0) return card_ok = 0;
    return card_ok = 1;
}
static int ata_cmd(uint32_t lba, int count, uint8_t cmd)
{
    if (ata_wait(0x40, 1000) <= 0) return 0;
    C8(ATA_DEV) = (uint8_t)(0xE0 | ((lba >> 24) & 0x0F));
    if (ata_wait(0x40, 1000) <= 0) return 0;
    C8(ATA_COUNT) = (uint8_t)count;
    C8(ATA_LBA0) = (uint8_t)lba; C8(ATA_LBA1) = (uint8_t)(lba >> 8); C8(ATA_LBA2) = (uint8_t)(lba >> 16);
    C8(ATA_CMD) = cmd;
    return 1;
}
/* IDENTIFY: the card's size in sectors (words 60-61), 0 if it does not answer */
uint32_t card_capacity(void)
{
    if (!card_ok || ata_wait(0x40, 1000) <= 0) return 0;
    C8(ATA_DEV) = 0xE0;
    if (ata_wait(0x40, 1000) <= 0) return 0;
    C8(ATA_CMD) = 0xEC;
    if (ata_wait(0x08, 1000) <= 0) return 0;
    uint32_t lo = 0, hi = 0;
    for (int i = 0; i < 256; i++) {
        uint16_t w = C16(ATA_DATA);
        /* identify words are little-endian in the card's byte stream: as read here they need the swap the data does not */
        if (!card_swap) w = (uint16_t)(w >> 8 | w << 8);
        if (i == 60) lo = w; else if (i == 61) hi = w;
    }
    (void)C8(ATA_CMD);
    return hi << 16 | lo;
}
/* n sectors (1..255) from lba into buf (512 n bytes) */
int card_read(uint32_t lba, int n, uint8_t *buf)
{
    if (!card_ok || n < 1 || n > 255 || !ata_cmd(lba, n, 0x20)) return 0;
    card_act_n++;
    for (int s = 0; s < n; s++) {
        if (ata_wait(0x08, 1000) <= 0) return 0;
        for (int i = 0; i < 256; i++) {
            uint16_t w = C16(ATA_DATA);
            if (card_swap) { *buf++ = (uint8_t)w; *buf++ = (uint8_t)(w >> 8); }
            else { *buf++ = (uint8_t)(w >> 8); *buf++ = (uint8_t)w; }
        }
    }
    (void)C8(ATA_CMD);
    return 1;
}
int card_write(uint32_t lba, int n, const uint8_t *buf)
{
    if (!card_ok || n < 1 || n > 255 || !ata_cmd(lba, n, 0x30)) return 0;
    card_act_n++;
    for (int s = 0; s < n; s++) {
        if (ata_wait(0x08, 1000) <= 0) return 0;
        for (int i = 0; i < 256; i++) {
            uint16_t w = card_swap ? (uint16_t)(buf[0] | buf[1] << 8) : (uint16_t)(buf[0] << 8 | buf[1]);
            C16(ATA_DATA) = w; buf += 2;
        }
    }
    if (ata_wait(0x40, 2000) <= 0) return 0;
    (void)C8(ATA_CMD);
    return 1;
}

#endif

/* ---- FAT16 / FAT32 ---- */
static void card_idle(void);   /* sampler.c: keeps the sample stream fed during long reads */
static uint8_t sec[512] __attribute__((aligned(4)));
static uint32_t fat_start, data_start, root_cluster, root_start, root_secs, spc, fat_type, fat_cached = 0xFFFFFFFFu;
static uint8_t fat_buf[512] __attribute__((aligned(4)));
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }

int fat_mount(void)
{
    if (!card_ok) return 0;
    if (!card_read(0, 1, sec)) return 0;
    if (!(sec[510] == 0x55 && sec[511] == 0xAA)) {        /* the other byte order? */
        card_swap = !card_swap;
        if (!card_read(0, 1, sec) || !(sec[510] == 0x55 && sec[511] == 0xAA)) return 0;
    }
    uint32_t vbr = 0;
    if (!(sec[0] == 0xEB || sec[0] == 0xE9) || le16(sec + 11) != 512) {   /* an MBR: the first partition */
        vbr = le32(sec + 446 + 8);
        if (!card_read(vbr, 1, sec)) return 0;
    }
    uint32_t bps = le16(sec + 11), rsv = le16(sec + 14), nfat = sec[16], rootent = le16(sec + 17);
    uint32_t fatsz = le16(sec + 22) ? le16(sec + 22) : le32(sec + 36);
    uint32_t tot = le16(sec + 19) ? le16(sec + 19) : le32(sec + 32);
    if (bps != 512 || !sec[13] || !nfat) return 0;
    spc = sec[13];
    fat_start = vbr + rsv;
    root_secs = (rootent * 32 + 511) / 512;
    root_start = fat_start + nfat * fatsz;
    data_start = root_start + root_secs;
    uint32_t clusters = (tot - (data_start - vbr)) / spc;
    fat_type = clusters < 65525 ? 16 : 32;
    root_cluster = fat_type == 32 ? le32(sec + 44) : 0;
    fat_cached = 0xFFFFFFFFu;
    return (int)fat_type;
}
static uint32_t fat_next(uint32_t cl)
{
    uint32_t off = cl * (fat_type == 32 ? 4 : 2), s = fat_start + off / 512;
    if (s != fat_cached) { if (!card_read(s, 1, fat_buf)) return 0x0FFFFFFFu; fat_cached = s; }
    uint32_t v = fat_type == 32 ? le32(fat_buf + off % 512) & 0x0FFFFFFFu : le16(fat_buf + off % 512);
    if (fat_type == 16 && v >= 0xFFF8u) v = 0x0FFFFFFFu;
    return v;
}
static uint32_t clus_lba(uint32_t cl) { return data_start + (cl - 2) * spc; }

/* a directory walk: dir_open(cluster; 0 = the root), then dir_next fills name (long name when there is one),
 * attr, first cluster and size; 0 at the end */
typedef struct { uint32_t cl, lba, idx, secs_left; int root16; } Dir;
static Dir dir;
static char lfn[256];
static char dir_short[13];
static int lfn_ok;
void dir_open(uint32_t cl)
{
    dir.idx = 0; lfn_ok = 0;
    if (!cl && fat_type == 16) { dir.root16 = 1; dir.lba = root_start; dir.secs_left = root_secs; dir.cl = 0; }
    else { dir.root16 = 0; dir.cl = cl ? cl : root_cluster; dir.lba = clus_lba(dir.cl); dir.secs_left = spc; }
}
int dir_next(char *name, int max, uint8_t *attr, uint32_t *cluster, uint32_t *size)
{
    for (;;) {
        if (dir.idx % 16 == 0) {
            if (dir.idx && --dir.secs_left == 0) {
                if (dir.root16) return 0;
                dir.cl = fat_next(dir.cl);
                if (dir.cl < 2 || dir.cl >= 0x0FFFFFF8u) return 0;
                dir.lba = clus_lba(dir.cl); dir.secs_left = spc;
            } else if (dir.idx) dir.lba++;
            if (!card_read(dir.lba, 1, sec)) return 0;
        }
        const uint8_t *e = sec + 32 * (dir.idx % 16);
        dir.idx++;
        if (e[0] == 0) return 0;
        if (e[0] == 0xE5) { lfn_ok = 0; continue; }
        if (e[11] == 0x0F) {                         /* a long-name piece: 13 UCS-2 characters, order in e[0] */
            int ord = (e[0] & 0x1F) - 1;
            if (e[0] & 0x40) { for (int i = 0; i < 256; i++) lfn[i] = 0; lfn_ok = 1; }
            static const uint8_t at[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
            for (int i = 0; i < 13 && ord * 13 + i < 255; i++) {
                uint16_t c = (uint16_t)(e[at[i]] | e[at[i] + 1] << 8);
                if (c == 0 || c == 0xFFFF) break;
                lfn[ord * 13 + i] = c < 128 ? (char)c : '?';
            }
            continue;
        }
        if (e[11] & 0x08) { lfn_ok = 0; continue; }  /* the volume label */
        { int m = 0; for (int i = 0; i < 8 && e[i] != ' '; i++) dir_short[m++] = (char)e[i];
          if (e[8] != ' ') { dir_short[m++] = '.'; for (int i = 8; i < 11 && e[i] != ' '; i++) dir_short[m++] = (char)e[i]; }
          dir_short[m] = 0; }
        int n = 0;
        if (lfn_ok && lfn[0]) { while (lfn[n] && n < max - 1) { name[n] = lfn[n]; n++; } }
        else {
            for (int i = 0; i < 8 && e[i] != ' '; i++) if (n < max - 1) name[n++] = (char)e[i];
            if (e[8] != ' ') { if (n < max - 1) name[n++] = '.'; for (int i = 8; i < 11 && e[i] != ' '; i++) if (n < max - 1) name[n++] = (char)e[i]; }
        }
        name[n] = 0; lfn_ok = 0;
        *attr = e[11];
        *cluster = le16(e + 26) | (fat_type == 32 ? le16(e + 20) << 16 : 0);
        *size = le32(e + 28);
        return 1;
    }
}
/* case-blind name match */
static int name_eq(const char *a, const char *b)
{
    for (;; a++, b++) {
        char x = *a >= 'a' && *a <= 'z' ? (char)(*a - 32) : *a, y = *b >= 'a' && *b <= 'z' ? (char)(*b - 32) : *b;
        if (x != y) return 0;
        if (!x) return 1;
    }
}
/* a path ("/AERIA/LOOPS/x.wav") to its first cluster, size and attributes; 0 if not found */
int fat_find(const char *path, uint32_t *cluster, uint32_t *size, uint8_t *attr)
{
    uint32_t cl = 0; char part[256], nm[256]; uint8_t a = 0x10; uint32_t c, sz = 0;
    while (*path == '/') path++;
    if (!*path) { *cluster = 0; *size = 0; *attr = 0x10; return 1; }
    while (*path) {
        int n = 0; while (*path && *path != '/' && n < 255) part[n++] = *path++;
        part[n] = 0; while (*path == '/') path++;
        dir_open(cl);
        int found = 0;
        while (dir_next(nm, sizeof nm, &a, &c, &sz)) if (name_eq(nm, part) || name_eq(dir_short, part)) { found = 1; break; }
        if (!found) return 0;
        cl = c;
        if (*path && !(a & 0x10)) return 0;
    }
    *cluster = cl; *size = sz; *attr = a;
    return 1;
}
/* read a whole file's first `max` bytes into dst (cluster by cluster); bytes read */
uint32_t fat_read(uint32_t cl, uint32_t size, uint8_t *dst, uint32_t max)
{
    uint32_t got = 0, want = size < max ? size : max;
    while (got < want && cl >= 2 && cl < 0x0FFFFFF8u) {
        uint32_t lba = clus_lba(cl);
        for (uint32_t s = 0; s < spc && got < want; s++) {
            if (want - got >= 512) { if (!card_read(lba + s, 1, dst + got)) return got; got += 512; }
            else { if (!card_read(lba + s, 1, sec)) return got; for (uint32_t i = 0; got < want; i++) dst[got++] = sec[i]; }
        }
        cl = fat_next(cl);
    }
    return got;
}

/* `len` bytes of a file from byte `off` into dst: whole runs of a cluster's sectors in one command where it can */
uint32_t fat_read_at(uint32_t cl, uint32_t off, uint8_t *dst, uint32_t len)
{
    uint32_t cbytes = spc * 512u, got = 0;
    while (off >= cbytes && cl >= 2 && cl < 0x0FFFFFF8u) { off -= cbytes; cl = fat_next(cl); }
    while (got < len && cl >= 2 && cl < 0x0FFFFFF8u) {
        uint32_t lba = clus_lba(cl), s = off / 512u, b = off % 512u;
        while (s < spc && got < len) {
            if (b == 0 && len - got >= 512u) {
                uint32_t n = (len - got) / 512u;
                if (n > spc - s) n = spc - s;
                if (n > 64) n = 64;
                if (!card_read(lba + s, (int)n, dst + got)) return got;
                got += n * 512u; s += n;
            } else {
                if (!card_read(lba + s, 1, sec)) return got;
                while (b < 512u && got < len) dst[got++] = sec[b++];
                b = 0; s++;
            }
            card_idle();
        }
        off = 0;
        cl = fat_next(cl);
    }
    return got;
}
