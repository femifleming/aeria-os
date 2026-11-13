/* More pictures for the manual (12 Nov 2026, the Octatrack-format edition): the sequencer's newer pages and states,
 * and dream visits in the sky. Included by manual_shots.c; extra_shots() runs after the first sequencer tour, from
 * the sky. Everything is driven through the keys and knobs, as on the unit; only the demo's data and a dream visit's
 * moment are set by hand. */
static void seq_face(int on) { if ((int)seq_on != on) { down(K_FUNC); key(K_MIDI); up(K_FUNC); run(4); } }
/* a knob page, on its first six knobs (0) or its second six (1): the same key again goes between the two */
static void sqp(int k, int second) { key(k); if ((int)func_latch != second) key(k); run(2); }
static void dream_shot(int kind, int flip, int var, int at, const char *name)
{
#ifdef DREAM_SKY
    dv.kind = (uint8_t)kind; dv.len = 2400; dv.t = (uint16_t)at; dv.flip = (uint8_t)flip; dv.var = (uint8_t)var;
    run(40); shot(name);
    dv.kind = DV_NONE; dv.wait = 60000;
#endif
}
/* 12 Nov 2026 (the build of 15:52): the FFT SMEAR's knobs, the longer shelf, and the weathers that are a sound of
 * their own -- WIND, HAIL, AURORA on trigs 9 to 11 of SKY, the TWISTER on trig 12. All through the keys; wx.c's own
 * state (xw_on, tw_until) is only read back here to be sure every one of them is off again at the end. */
static void more_weather_shots(void)
{
    /* SMEAR in slot 5: its six knobs under CLOUDY; FRZ turned to HOLD; what SNOW does to SMR and FRZ */
    key(K_AMP); key(0); key(1); key(K_SRC); key(4); run(200); shot("chain_smear");
    enc(1, 127); run(4); shot("chain_smear_hold"); run(90);
    key(KEY_PUSH0 + 1); run(4);               /* FRZ pushed: back to the weather */
    key(K_AMP); key(0); key(4); key(K_SRC); key(4); run(400); shot("chain_smear_snow");
    key(K_AMP); key(0); key(1); run(200);
    /* the shelf's end: the five weather effects */
    key(K_SRC); key(5); run(90); key(K_BANK); enc(0, -20); enc(0, 13); run(3); shot("shelf3"); key(K_NO); run(90);
    /* the three balloons of the first tour would hang in front of the weather: BALLOONS off on SOURCE for these
     * pictures. And the sky CLEAR: with WIND and CLOUDY a twister may come by itself, into the wrong picture */
    key(K_LFO); key(2); key(K_AMP); key(0); run(200);
    /* SKY: WIND on (the popup), then the sky alone */
    key(K_AMP); key(8); shot("sky_wind_on"); run(6); shot("sky_wind_in"); run(700); shot("sky_wind");
    /* HAIL as well: the sky has put both effects into the chain's last empty slots */
    key(K_AMP); key(9); run(700); shot("sky_hail");
    key(K_SRC); key(5); run(90); shot("chain_hail"); key(6); run(90); shot("chain_wind");
    /* AURORA (hail goes), at night */
    key(K_AMP); key(8); key(10); key(K_YES); run(700); shot("sky_aurora");
    key(K_AMP); run(90); shot("sky_page_more");
    key(K_YES); key(10); run(900);                                                    /* day; the aurora's tail has gone */
    /* a TWISTER half way across, the wind blowing */
    key(K_AMP); key(8); run(200); key(11); shot("sky_twister_on"); run(640); shot("sky_twister");
    key(K_SRC); key(5); run(90); shot("chain_twister");
    key(K_AMP); key(8); run(1200);
    key(K_LFO); key(2); key(K_AMP); key(0); key(1); run(200);                         /* BALLOONS on again, CLOUDY */
    if (xw_on || ticks < tw_until) printf("more weather: still on (%d)\n", xw_on);
    for (int i = 5; i < 7; i++) if (slots[i].row > 0) printf("more weather: slot %d still holds %s\n", i + 1, slots[i].name);
    key(K_SRC); key(0); run(90);
}
static void extra_shots(void)
{
    SqHead *h; SqStep *s;
    seq_face(1); h = SQH;
    /* the demo, brought up to date: T4 a bouncing ratchet, T6 an FM voice, T8 a plucked string, T7 a MIDI chord */
    s = SQ_STEP(0, 3, 10); s->flags = (uint8_t)(SF_ON | ((5 & 15) << 4)); s->vel = 100; s->ratch = 8;
    h->trk[5][TP_MACH] = SQM_FM; h->trk2[5][TP_FMR - SQ_TP] = 56; h->trk2[5][TP_FMI - SQ_TP] = 70; h->trk2[5][TP_FMD - SQ_TP] = 34; h->trk[5][TP_DEC] = 50;
    h->trk[7][TP_MACH] = SQM_PLUK; h->trk2[7][TP_FMI - SQ_TP] = 80; h->trk2[7][TP_FMD - SQ_TP] = 100; h->trk[7][TP_DEC] = 110;
    h->trk[6][TP_MCH] = 3; h->trk[6][TP_MNOT] = 57; h->trk2[6][TP_CHRD - SQ_TP] = 8; h->trk2[6][TP_GATE - SQ_TP] = 12; h->trk2[6][TP_PROG - SQ_TP] = 5;
    h->trk2[3][TP_DIR - SQ_TP] = 2; h->trk2[3][TP_ROT - SQ_TP] = 3; h->trk2[2][TP_DENS - SQ_TP] = 100; h->trk2[2][TP_WARP - SQ_TP] = 76;
    h->trk2[1][TP_RATE - SQ_TP] = 7; h->trk2[2][TP_MODW - SQ_TP] = 2;
    { static const int pk[8] = {13, 0, 9, 5, 11, 7, 4, 6}; for (int i = 0; i < 8; i++) h_peak[i] = pk[i]; }
    run(90);
    /* the pictures of the first tour whose page key now also toggles the second set: again, on the first set */
    key(0x12); sqp(K_SRC, 0); run(90); shot("seq_grid");
    key(K_PLAY); h_at = 8; run(90); shot("seq_play"); key(K_STOP);
    down(K_FUNC); run(90); shot("seq_src_func"); up(K_FUNC);
    down(6); run(92); shot("seq_step"); enc(3, 2); run(4); shot("seq_step_cond"); enc(3, -2);
    key(K_FX1); run(90); enc(1, -6); run(4); shot("seq_lock"); enc(1, 6); up(6); run(90);
    sqp(K_AMP, 0); run(90); shot("seq_amp"); sqp(K_AMP, 1); run(90); shot("seq_amp_func"); sqp(K_AMP, 0);
    sqp(K_LFO, 0); enc(0, -2); run(90); shot("seq_mod"); enc(0, 2);
    sqp(K_FX1, 0); run(90); shot("seq_flt");
    sqp(KEY_PAGE, 0); run(90); shot("seq_ptn");
    key(K_PLAY); h_at = 6; key(K_BANK); key(5); run(90); shot("seq_mixer"); key(5); key(K_BANK);
    key(K_REC); run(94); shot("seq_live"); key(K_REC); key(K_STOP);
    key(0x16); key(K_RIGHT); sqp(K_SRC, 0); run(90); shot("seq_page2"); key(K_LEFT); key(0x12);
    down(K_PATTERN); run(4); shot("seq_pattern"); key(0); key(0); key(1); key(0); up(K_PATTERN); run(4); shot("seq_chain"); run(90);
    down(K_PATTERN); key(0); up(K_PATTERN); run(90);
    /* the annotated screen: T3 in hand, playing */
    key(0x12); sqp(K_SRC, 0); key(K_PLAY); h_at = 4; run(90); shot("seq_anno"); key(K_STOP); run(90);
    /* a page key again: its second six knobs */
    key(0x15); sqp(K_SRC, 0); run(90); shot("seq_fm"); key(K_SRC); run(4); shot("seq_src2_popup"); run(90); shot("seq_src2");
    enc(1, 6); run(4); shot("seq_fm_indx"); enc(1, -6); sqp(K_SRC, 0); run(90);
    key(0x17); sqp(K_SRC, 0); run(90); shot("seq_pluk"); sqp(K_SRC, 1); run(90); shot("seq_pluk2"); sqp(K_SRC, 0);
    key(0x12); sqp(K_SRC, 0); enc(4, 3); run(4); shot("seq_mach_popup"); enc(4, -3); run(90);
    /* the STEP knobs: ratchets and their curve; YES turns a held step round */
    key(0x13); sqp(K_SRC, 0); run(90); down(10); run(92); shot("seq_step_rtch"); enc(4, 2); run(4); shot("seq_rtch_popup"); enc(4, -2);
    enc(5, 1); run(4); shot("seq_curv_popup"); enc(5, -1); run(90); up(10); run(4);
    key(0x12); down(2); key(K_YES); run(4); shot("seq_step_rev"); key(K_YES); up(2); run(90);
    down(6); key(K_NO); run(4); shot("seq_locks_cleared"); up(6);
    s = SQ_STEP(0, 2, 6); s->lp[0] = TP_LP + 1; s->lv[0] = 96; s->lp[1] = TP_DLY + 1; s->lv[1] = 90; run(90);
    /* GEN (the ARR key) */
    key(0x13); sqp(K_ARR, 0); run(90); shot("seq_gen"); enc(1, 1); run(4); shot("seq_gen_dir"); enc(1, -1); run(90);
    key(0x12); sqp(K_ARR, 0); run(90); enc(3, 2); run(4); shot("seq_gen_warp"); enc(3, -2); run(90);
    key(0x11); enc(4, 1); run(4); shot("seq_gen_rate"); enc(4, -1); run(90);
    /* MIDI (the MIDI key): notes, chords, programs; its second set: SYNC and PCCH */
    key(0x16); sqp(K_MIDI, 0); run(90); shot("seq_midi"); enc(2, 1); run(4); shot("seq_midi_chord"); enc(2, -1); run(90);
    sqp(K_MIDI, 1); run(90); shot("seq_midi2"); enc(0, 2); run(4); shot("seq_midi_sync"); run(90);
    key(0x12); sqp(K_SRC, 0); key(K_PLAY); h_at = 2; h_ext = 1; run(90); shot("seq_ext"); h_ext = 0; key(K_STOP);
    sqp(K_MIDI, 1); enc(0, -2); sqp(K_MIDI, 0); run(4);
    /* MOD with the envelope follower; FLT; AMP */
    key(0x12); sqp(K_LFO, 0); run(90); shot("seq_mod2"); enc(0, -1); run(4); shot("seq_mod_wave"); enc(0, 1); run(90);
    enc(1, 5); run(4); shot("seq_mod_dest"); enc(1, -5); run(90);
    sqp(K_FX1, 0); run(90); shot("seq_flt2"); enc(3, 1); run(4); shot("seq_flt_out"); enc(3, -1); run(90);
    sqp(KEY_PAGE, 0); run(90); shot("seq_ptn_a"); sqp(KEY_PAGE, 1); run(90); shot("seq_ptn_b"); sqp(KEY_PAGE, 0);
    /* copy, paste, clear; the tempo; mute and solo; fill */
    key(0x12); sqp(K_SRC, 0); run(90);
    down(K_FUNC); key(K_REC); run(4); shot("seq_copy"); up(K_FUNC); run(90);
    down(K_PATTERN); down(K_FUNC); key(K_REC); run(4); shot("seq_copy_pattern"); up(K_FUNC); up(K_PATTERN); run(90);
    down(K_PATTERN); key(0); up(K_PATTERN); run(90);
    key(0x13); down(K_FUNC); key(K_STOP); run(4); shot("seq_paste_pattern"); up(K_FUNC); run(90);
    key(0x12); down(K_FUNC); key(K_REC); up(K_FUNC); run(90);
    down(0x18); enc(6, 4); run(4); shot("seq_tempo"); enc(6, -4); up(0x18); run(90);
    down(K_FUNC); key(0x15); run(4); shot("seq_mute"); key(0x15); up(K_FUNC); run(90);
    down(K_CUE); key(0x12); run(4); shot("seq_solo"); key(0x12); up(K_CUE); run(90);
    key(K_PLAY); h_at = 13; down(K_YES); run(4); shot("seq_fill"); up(K_YES); key(K_STOP); run(90);
    enc(6, 5); run(4); shot("seq_level"); enc(6, -5); run(90);
    /* the master page from the sequencer */
    key(K_MIX); enc(5, 60); run(90); shot("seq_master"); enc(5, -60); sqp(K_SRC, 0); run(90);
    /* quick start: a first beat from nothing */
    { SqStep keep[SQ_TRACKS][SQ_STEPS]; uint8_t tk[SQ_TRACKS][SQ_TP], tk2[SQ_TRACKS][16], ln[SQ_TRACKS], fxr[SQ_TRACKS];
      for (int t = 0; t < 8; t++) { for (int n = 0; n < 64; n++) keep[t][n] = *SQ_STEP(0, t, n); memcpy(tk[t], h->trk[t], SQ_TP); memcpy(tk2[t], h->trk2[t], 16); ln[t] = h->len[0][t]; fxr[t] = h->fx_row[t]; }
      int sw = h->swing;
      for (int t = 0; t < 8; t++) { for (int n = 0; n < 64; n++) memset(SQ_STEP(0, t, n), 0, sizeof(SqStep)); h->len[0][t] = 16; h->fx_row[t] = 0;
          memset(h->trk[t], 0, SQ_TP); h->trk[t][TP_SMPL] = (uint8_t)(96 + t); h->trk[t][TP_PTCH] = 64; h->trk[t][TP_DEC] = 127; h->trk[t][TP_VOL] = 100; h->trk[t][TP_LP] = 127; h->trk[t][TP_PAN] = 64; h->trk[t][TP_PDEC] = 40; h->trk[t][TP_LFOR] = 20; h->trk[t][TP_MNOT] = (uint8_t)(36 + t);
          memset(h->trk2[t], 0, 16); h->trk2[t][TP_DENS - SQ_TP] = 127; h->trk2[t][TP_RATE - SQ_TP] = 5; h->trk2[t][TP_WARP - SQ_TP] = 64; h->trk2[t][TP_FMR - SQ_TP] = 16; h->trk2[t][TP_FMI - SQ_TP] = 40; h->trk2[t][TP_FMD - SQ_TP] = 50; }
      h->swing = 50; h->mutes = 0; h->solo = 0; sq_tempo_set(120);
      for (int i = 0; i < 8; i++) h_peak[i] = 0;
      key(0x10); sqp(K_SRC, 0); run(90); shot("qs_seq_empty");
      key(0); key(4); key(8); key(12); run(90); shot("qs_seq_kick");
      key(K_PLAY); h_at = 5; h_peak[0] = 12; run(90); shot("qs_seq_play");
      key(0x11); key(4); key(12); key(0x12); key(2); key(6); key(10); key(14); h_peak[1] = 8; h_peak[2] = 10; h_at = 10; run(90); shot("qs_seq_hats");
      down(14); enc(4, 2); run(4); shot("qs_seq_ratchet"); up(14); run(90);
      sqp(KEY_PAGE, 0); enc(2, 8); run(4); shot("qs_seq_swing"); run(90);
      sqp(K_FX1, 0); enc(0, 70); run(4); shot("qs_seq_hp"); run(90);
      down(6); key(K_FX1); enc(1, -8); run(4); shot("qs_seq_lock"); up(6); run(90);
      sqp(K_FX2, 0); key(K_YES); sq_fxrow = (uint8_t)fx_row_named("ECHO"); run(4); shot("qs_seq_fxlist"); key(K_YES); run(90); shot("qs_seq_echo");
      key(K_BANK); run(90); shot("qs_seq_mixer"); key(K_BANK); key(K_STOP); sqp(K_SRC, 0); run(90);
      for (int t = 0; t < 8; t++) { for (int n = 0; n < 64; n++) *SQ_STEP(0, t, n) = keep[t][n]; memcpy(h->trk[t], tk[t], SQ_TP); memcpy(h->trk2[t], tk2[t], 16); h->len[0][t] = ln[t]; h->fx_row[t] = fxr[t]; }
      h->swing = (uint8_t)sw; sq_tempo_set(124); }
    seq_face(0); run(90); key(K_SRC); key(0); run(90);
    /* the sky's quick start: an effect brought in, a weather turned on, the delay, a balloon */
    key(K_SRC); key(0); run(90); shot("qs_sky_chain");
    key(2); run(90); enc(5, 70); run(4); shot("qs_sky_mix"); run(90);
    key(K_AMP); key(0); key(2); run(4); shot("qs_sky_rain"); run(90);
    key(K_MIX); enc(5, 70); run(4); shot("qs_sky_delay"); run(90); enc(5, -70);
    key(K_AMP); key(0); key(1); key(K_SRC); key(2); enc(5, -70); key(0); run(90);
    /* dream visits: the sky alone (the page has faded), a visit half way through */
    key(K_AMP); key(0); key(1); run(700);
    dream_shot(DV_ISLAND, 0, 40, 1200, "dream_island");
    dream_shot(DV_TRAIN, 0, 90, 1100, "dream_train");
    dream_shot(DV_LAMP, 0, 10, 1200, "dream_lamp");
    dream_shot(DV_DOOR, 1, 77, 1200, "dream_door");
    dream_shot(DV_TORPEDO, 0, 30, 1300, "dream_torpedo");
    dream_shot(DV_COLUMNS, 0, 60, 1200, "dream_columns");
    dream_shot(DV_FROGS, 0, 120, 1200, "dream_frogs");
    dream_shot(DV_PORTRAIT, 1, 200, 1200, "dream_portrait");
    dream_shot(DV_REDCARS, 0, 5, 1200, "dream_redcars");
    dream_shot(DV_WALKWAY, 0, 150, 1200, "dream_walkway");
    key(K_SRC); key(0); run(90);
}
