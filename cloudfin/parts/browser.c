/* ---- the card: AERIA's own browser (research-card.md) ------------------------------------------
 * Listing: the firmware's own opendir/readdir (the calls stock DIRSCAN makes). Loading: stock
 * 0x40022610(kind, slot, path) clears the slot, writes the path into the project's sample list and
 * posts the engine's load; its popups sit under AERIA's screen. Done: the slot's status long
 * (+8: 0 loaded, 1 empty, 2 loading, 3 error). Preview: Static slot 128, played with
 * 0x40093678(0, 128, -1, 0, 1, 1), stopped with 0x40093e9c(0). All of it in the UI task. */
#define BR_MAX 192
#if defined(PREVIEW) && defined(AERIAOS)
#define BR_NAME 64             /* AERIA OS keeps long names (longer ones go by their 8.3 name) */
#else
#define BR_NAME 28
#endif
enum { BR_OFF, BR_LOOP, BR_STARS, BR_PONG, BR_SEQ };   /* SEQ: a folder dealt to the sequencer's kit (static slots 97-112, bank 7) */   /* PONG: a folder dealt to the balls (static slots 112-127) */
static int br_mode, br_count, br_top, br_cur, br_depth, br_busy, br_preview;
static char br_path[0x104];
static char br_names[BR_MAX][BR_NAME];
static uint8_t br_dir[BR_MAX];
static char br_status[24];
/* STARS: a folder's WAVs dealt into 16 slots, one load at a time */
static int deal_left, deal_slot, deal_waiting;
static int16_t deal_order[16];

#if defined(PREVIEW) && defined(AERIAOS)
/* AERIA OS: its own card (aeria-os/src/card.c, sampler.c) */
int aos_list(const char *path, void (*each)(const char *name, int dir));
int aos_slot_load(int kind, int slot, const char *path);
int aos_slot_status(int kind, int slot);
void aos_hear(int on);
static int ends_wav(const char *n)
{
    int k = 0;
    while (n[k]) k++;
    if (k < 4) return 0;
    const char *e = n + k - 4;
    return e[0] == '.' && (e[1] | 32) == 'w' && (e[2] | 32) == 'a' && (e[3] | 32) == 'v';
}
static void br_each(const char *name, int dir)
{
    if (br_count >= BR_MAX || (!dir && !ends_wav(name))) return;
    int j = 0;
    while (name[j] && j < BR_NAME - 1) { br_names[br_count][j] = name[j]; j++; }
    br_names[br_count][j] = 0;
    br_dir[br_count] = (uint8_t)dir;
    br_count++;
}
static int br_scan(void) { br_count = 0; return aos_list(br_path[0] ? br_path : "/", br_each) < 0 ? -1 : br_count; }
static void slot_load(int kind, int slot, const char *path) { aos_slot_load(kind, slot, path); }
static int slot_status(int kind, int slot) { return aos_slot_status(kind, slot); }
static void wav_preview_play(void) { aos_hear(1); }
static void wav_preview_stop(void) { aos_hear(0); }
static void set_path_root(void) { br_path[0] = 0; }
#elif defined(PREVIEW)
static int br_scan(void)
{
    static const char *const demo[] = {"Drum Machines", "Sample Packs", "Ambient", "kick 1.wav", "snare 2.wav", "hat.wav"};
    br_count = 6;
    for (int i = 0; i < 6; i++) { int j = 0; while (demo[i][j] && j < BR_NAME - 1) { br_names[i][j] = demo[i][j]; j++; } br_names[i][j] = 0; br_dir[i] = i < 3; }
    return 6;
}
static void slot_load(int kind, int slot, const char *path) { (void)kind; (void)slot; (void)path; }
static int slot_status(int kind, int slot) { (void)kind; (void)slot; return 0; }
static void wav_preview_play(void) {}
static void wav_preview_stop(void) {}
static void set_path_root(void) { const char *r = "/SET/AUDIO"; int i = 0; while (r[i]) { br_path[i] = r[i]; i++; } br_path[i] = 0; }
#else
/* Raw opendir/readdir through the file-function slots DIRSCAN itself calls (research-card.md 1.2 B):
 * no shared name pool to borrow, so no 64 KB save. A slot reads 0 while USB mode swaps in stubs. */
typedef int (*fs_opendir_fn)(const char *path, void *state);
typedef int (*fs_readdir_fn)(void *state, uint8_t *rec);
static uint8_t br_rec[0x140];
static int ends_wav(const char *n)
{
    int k = 0;
    while (n[k]) k++;
    if (k < 4) return 0;
    const char *e = n + k - 4;
    return e[0] == '.' && (e[1] | 32) == 'w' && (e[2] | 32) == 'a' && (e[3] | 32) == 'v';
}
static int br_scan(void)
{
    br_count = 0;
    fs_opendir_fn op = *(fs_opendir_fn volatile *)0x46c8240eu;
    fs_readdir_fn rd = *(fs_readdir_fn volatile *)0x46c8244eu;
    if (!op || !rd) return -1;
    static uint32_t st[4];
    st[0] = st[1] = st[2] = st[3] = 0;
    if (op(br_path, st) < 0) return -1;
    for (int guard = 0; guard < 4096 && br_count < BR_MAX; guard++) {
        if (rd(st, br_rec) != 0) break;
        const char *name = (const char *)br_rec;
        if (!name[0] || name[0] == '.') continue;
        int dir = (br_rec[0x10d] & 0x10) != 0;
        if (!dir && !ends_wav(name)) continue;
        int j = 0;
        while (name[j] && j < BR_NAME - 1) { br_names[br_count][j] = name[j]; j++; }
        br_names[br_count][j] = 0;
        br_dir[br_count] = (uint8_t)dir;
        br_count++;
    }
    return br_count;
}
static void slot_load(int kind, int slot, const char *path) { ((void (*)(uint32_t, uint32_t, const char *))0x40022610u)((uint32_t)kind, (uint32_t)slot, path); }
static int slot_status(int kind, int slot)
{
    uint32_t rec = (kind ? 0x46c922c4u : 0x46c90a78u) + 44u * (unsigned)slot;
    return (int)*(volatile uint32_t *)(rec + 8u);
}
static void wav_preview_play(void) { ((void (*)(uint32_t, uint32_t, int32_t, uint32_t, uint32_t, uint32_t))0x40093678u)(0, 128, -1, 0, 1, 1); }
static void wav_preview_stop(void) { ((void (*)(uint32_t))0x40093e9cu)(0); }
static void set_path_root(void)
{
    const volatile char *set = (const volatile char *)0x100f8480u;
    int i = 0;
    while (i < 0xc0 && set[i] >= ' ' && set[i] < 127) { br_path[i] = set[i]; i++; }
    const char *a = "/AUDIO";
    for (int k = 0; a[k]; k++) br_path[i++] = a[k];
    br_path[i] = 0;
}
#endif

static int path_len(void) { int n = 0; while (br_path[n]) n++; return n; }
static void path_push(const char *name)
{
    int n = path_len();
    if (n + 2 + BR_NAME >= (int)sizeof br_path) return;
    br_path[n++] = '/';
    for (int k = 0; name[k]; k++) br_path[n++] = name[k];
    br_path[n] = 0;
}
static void path_pop(void) { int n = path_len(); while (n > 0 && br_path[n - 1] != '/') n--; if (n > 0) br_path[n - 1] = 0; }
static void path_with(const char *name, char *out)
{
    int n = 0;
    for (; br_path[n]; n++) out[n] = br_path[n];
    out[n++] = '/';
    for (int k = 0; name[k] && n < 0x103; k++) out[n++] = name[k];
    out[n] = 0;
}
static int is_wav(int i) { return !br_dir[i]; }

static void browser_open(int mode)
{
    br_mode = mode; br_cur = br_top = br_depth = 0; br_preview = 0;
    set_path_root();
    br_scan();
    say(mode == BR_LOOP ? "PICK A SAMPLE" : "PICK A FOLDER", 0);
}
static void browser_close(void) { if (br_preview) { wav_preview_stop(); br_preview = 0; } br_mode = BR_OFF; }

static int wav_count(void) { int n = 0; for (int i = 0; i < br_count; i++) n += is_wav(i); return n; }

/* STARS: deal up to 16 random WAVs of the listed folder into bank b's Static slots */
static void deal_folder(int bank)
{
    int wavs[BR_MAX], n = 0;
    for (int i = 0; i < br_count; i++) if (is_wav(i)) wavs[n++] = i;
    if (!n) { say("NO WAVS HERE", 0); return; }
#if defined(PREVIEW) && defined(AERIAOS)
    /* AERIA OS: a seeded shuffle it can repeat at start, with the folder and the seed kept (sampler.c) */
    { void aos_shuffle(int16_t *idx, int n, uint32_t seed); void aos_note_deal(int bank, const char *folder, uint32_t seed);
      static int16_t order[BR_MAX]; uint32_t seed = (rnd() ^ (ticks << 8)) | 1u; if (bank == 6 && br_mode == BR_SEQ) seed = 0;
      for (int i = 0; i < n; i++) order[i] = (int16_t)i;
      aos_shuffle(order, n, seed);
      for (int k = 0; k < 16; k++) deal_order[k] = (int16_t)wavs[order[k % n]];
      aos_note_deal(bank, br_path, seed); }
#else
    for (int i = n - 1; i > 0; i--) { int j = range(0, i), t = wavs[i]; wavs[i] = wavs[j]; wavs[j] = t; }
    for (int k = 0; k < 16; k++) deal_order[k] = (int16_t)wavs[k % n];
#endif
    deal_left = 16; deal_slot = bank * 16; deal_waiting = 0;
    br_busy = 1;
}
static void deal_tick(void)
{
    if (!deal_left) return;
    int slot = deal_slot + (16 - deal_left);
    if (deal_waiting) {
        int st = slot_status(0, slot);
        if (st == 2 && deal_waiting < 600) { deal_waiting++; return; }
        deal_waiting = 0;
        deal_left--;
        if (!deal_left) { br_busy = 0; say(deal_slot == 96 ? "KIT LOADED" : "STARS LOADED", 0); }
        return;
    }
    static char full[0x104];
    path_with(br_names[deal_order[16 - deal_left]], full);
    slot_load(0, slot, full);
    deal_waiting = 1;
}

/* LOOP: the chosen WAV into Flex slot 1 (0-based 0), the sample T1 plays */
static int loop_waiting;
static void loop_load(int i)
{
    static char full[0x104];
    path_with(br_names[i], full);
    slot_load(1, 0, full);
    loop_waiting = 1;
    br_busy = 1;
    int k = 0; while (br_names[i][k] && k < 23) { br_status[k] = br_names[i][k]; k++; } br_status[k] = 0;
}
static void loop_tick(void)
{
    if (!loop_waiting) return;
    int st = slot_status(1, 0);
    if (st == 2 && loop_waiting < 1800) { loop_waiting++; return; }
    loop_waiting = 0; br_busy = 0;
    say(st == 0 ? "LOADED" : "LOAD FAILED", 0);
}
static int preview_waiting;
static void preview_tick(void)
{
    if (!preview_waiting) return;
    int st = slot_status(0, 128);
    if (st == 2 && preview_waiting < 600) { preview_waiting++; return; }
    preview_waiting = 0;
    if (st == 0) { wav_preview_play(); br_preview = 1; }
}

static void browser_key(int code)
{
    if (br_busy) return;
    if (code == KEY_UP) { if (br_cur > 0) br_cur--; }
    else if (code == KEY_DOWN) { if (br_cur < br_count - 1) br_cur++; }
    else if (code == KEY_LEFT || code == KEY_NO) {
        if (br_preview) { wav_preview_stop(); br_preview = 0; }
        if (br_depth > 0) { path_pop(); br_depth--; br_cur = br_top = 0; br_scan(); }
        else browser_close();
    } else if (code == KEY_RIGHT && br_count) {
        if (br_dir[br_cur]) { path_push(br_names[br_cur]); br_depth++; br_cur = br_top = 0; br_scan(); }
        else { static char full[0x104]; if (br_preview) wav_preview_stop(); path_with(br_names[br_cur], full); slot_load(0, 128, full); preview_waiting = 1; }
    } else if (code == KEY_YES && br_count) {
        /* YES opens a folder or loads (a WAV for LOOP; for STARS, the folder it sits in).
         * FUNC+YES plays the WAV in the headphones; on a folder in STARS it deals that folder. */
        static char full[0x104];
        if (func_held && !br_dir[br_cur]) { if (br_preview) wav_preview_stop(); path_with(br_names[br_cur], full); slot_load(0, 128, full); preview_waiting = 1; }
        else if (br_dir[br_cur] && !(func_held && (br_mode >= BR_STARS))) { path_push(br_names[br_cur]); br_depth++; br_cur = br_top = 0; br_scan(); }
        else if (br_mode >= BR_STARS) {
            if (br_preview) { wav_preview_stop(); br_preview = 0; }
            if (br_dir[br_cur]) { path_push(br_names[br_cur]); br_depth++; br_cur = br_top = 0; br_scan(); }
            if (wav_count()) { deal_folder(br_mode == BR_PONG ? 7 : br_mode == BR_SEQ ? 6 : pv(PG_STARS, 9)); browser_close(); } else say("NO WAVS HERE", 0);
        } else { if (br_preview) { wav_preview_stop(); br_preview = 0; } loop_load(br_cur); browser_close(); }
    }
    if (br_cur < br_top) br_top = br_cur;
    if (br_cur >= br_top + 6) br_top = br_cur - 5;
}
static void browser_enc(int delta) { for (int i = 0; i < (delta < 0 ? -delta : delta); i++) browser_key(delta > 0 ? KEY_DOWN : KEY_UP); }

static void draw_browser(void)
{
    for (int x = 0; x < W; x++) span(x, 9, 56, 0);
    /* the folder we are in, its last part */
    int n = path_len(), s = n;
    while (s > 0 && br_path[s - 1] != '/') s--;
    char here[30]; int k = 0;
    for (int i = s; i < n && k < 28; i++) { char c = br_path[i]; here[k++] = c >= 'a' && c <= 'z' ? (char)(c - 32) : c; }
    here[k] = 0;
    text(2, 10, here, 1);
    for (int x = 0; x < W; x += 2) px(x, 16);
    if (br_busy) { text(30, 32, "LOADING", 1); return; }
    for (int r = 0; r < 6 && br_top + r < br_count; r++) {
        int i = br_top + r, y = 19 + r * 6;
        char line[30]; int j = 0;
        if (br_dir[i]) line[j++] = '>';
        for (int c = 0; br_names[i][c] && j < 29; c++) { char ch = br_names[i][c]; line[j++] = ch >= 'a' && ch <= 'z' ? (char)(ch - 32) : ch; }
        line[j] = 0;
        if (i == br_cur) { for (int x = 0; x < W; x++) span(x, y - 1, y + 5, 1); text(2, y, line, 0); }
        else text(2, y, line, 1);
    }
    if (!br_count) text(2, 24, "EMPTY", 1);
    text(2, 57, br_mode != BR_LOOP ? "YES DEAL  FUNC+YES HEAR" : "YES LOAD  FUNC+YES HEAR", 1);
}

