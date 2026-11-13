/* ---- the SYSTEM page's saver settings ---- */
static const uint8_t saver_wait[8] = {1, 2, 3, 5, 10, 15, 30, 60}; /* minutes */
static const char *saver_name(int s) { return s <= SS_OFF ? "OFF" : s >= SS_COUNT ? "RANDOM" : ss_name[s]; }
