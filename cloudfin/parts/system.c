/* ---- SYSTEM: the stock routines behind PROJECT > SYSTEM, under AERIA's screen (research-card.md) ---- */
static int usb_mode;
#if defined(PREVIEW) && defined(AERIAOS)
/* AERIA OS: its own USB (aeria-os/src/usb.c): the card goes to the computer while this is on */
void aos_usb_disk(int on);
int aos_disk_busy(void);
static int popup_open(void) { return 0; }
static void sync_to_card(void) {}
static void usb_enter(void) { aos_usb_disk(1); usb_mode = aos_disk_busy(); say(usb_mode ? "USB DISK MODE" : "NO CARD", 0); }
static void usb_leave(void) { aos_usb_disk(0); usb_mode = 0; say("BACK", 0); }
static void usb_tick(void) { if (usb_mode && !aos_disk_busy()) { usb_mode = 0; say("EJECTED", 0); } }
static void os_upgrade(void) { say("NOT YET", 0); }
static void transport_start(void) { preview_play = 1; }
#elif defined(PREVIEW)
static int popup_open(void) { return 0; }
static void sync_to_card(void) {}
static void usb_enter(void) { usb_mode = 1; }
static void usb_leave(void) { usb_mode = 0; }
static void usb_tick(void) {}
static void os_upgrade(void) { say("OS UPGRADE", 0); }
static void transport_start(void) { preview_play = 1; }
#else
/* USB DISK MODE: the menu's YES body 0x4007eb34 (stop, cut voices, sync, then the deferred
 * USB switch, which sets 0x460e76a0 = 1); leaving: 0x4007ebf0(0) (USB off, remount, reload). */
static void usb_enter(void) { ((void (*)(void))0x4007eb34u)(); say("USB DISK MODE", 0); }
static void usb_leave(void) { ((void (*)(uint32_t))0x4007ebf0u)(0); usb_mode = 0; say("BACK", 0); }
static void usb_tick(void) { usb_mode = *(volatile uint32_t *)0x460e76a0u == 1u; }
/* a stock or base popup is open (the popup record 0x46c7d34c, flags +0x20: 0x21 open): its keys are its own */
static int popup_open(void) { return *(volatile uint32_t *)(0x46c7d34cu + 0x20u) == 0x21u; }
/* SYNC TO CARD: the project, its Parts and the base's effect map, written as stock's menu does */
static void sync_to_card(void) { ((void (*)(uint32_t))0x40022cd4u)(1); }
/* OS UPGRADE: the menu's YES body 0x40063660(0): stop, sync, then the newest .BIN on the card */
static void os_upgrade(void) { ((void (*)(uint32_t))0x40063660u)(0); }
/* the PLAY path: 0x4009b964(0), or 0x400a030c(0) in arranger mode; only from a stop */
static void transport_start(void)
{
    if (*(volatile uint32_t *)0x800065b8u != 0u) return;
    if (*(volatile uint32_t *)0x460d1aecu) ((void (*)(uint32_t))0x400a030cu)(0);
    else ((void (*)(uint32_t))0x4009b964u)(0);
}
#endif

