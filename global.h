/*
 * VIBE DJ – GLOBAL (29.10.2026, Sven): the settings for the whole player, K5 from everywhere (↩ / K5 / F6 close it).
 * Included at the end of main.c, after dj.h (one unit).
 *
 *   SCAN LIBRARY: every track without a kept analysis is measured, one after the other, in the draw – the same measurement
 *   as when it is loaded into a deck (tempo, first beat, loudness, waveform, MP3 seek points), kept in djan/; the lists
 *   show the BPM, and a deck has it all at once. A deck that measures goes first; while one plays the scan takes a small
 *   slice a frame (as a deck does), while nothing plays two big ones. It runs on with the page closed.
 *   AUTO VOL · READ THE LIST AGAIN (also the media sources: a stick, the card's /root/vibe-media – library.h)
 *   The last line: the sources the music comes from.
 */

static deck_t SC; static uint8_t SCB[CB], SAB[DAB];   /* the scan's own deck (never heard) */
static int glob_sel, scan_run, scan_i, scan_have, scan_new, scan_none, scan_bad;

static void scan_reset(void) { SC.t = -1; scan_i = 0; scan_have = scan_new = scan_none = scan_bad = 0; }   /* (the list read again: other indices) */
static int scan_running(void) { return scan_run; }
static void scan_start(void) { scan_reset(); scan_run = 1; say("SCAN LIBRARY"); }
static void scan_stop(void) { scan_run = 0; SC.t = -1; say("SCAN STOPPED"); }
static void scan_service(void)                   /* the draw: one step of the library scan */
{
    if (!scan_run) return;
    for (int k = 0; k < 2; k++) if (D[k].t >= 0 && (D[k].an == 1 || !D[k].wf_done)) return;   /* (a deck measures: it first) */
    if (SC.t < 0) {
        for (int look = 0; look < 16 && scan_i < ntr; look++) {   /* (the next track without a kept analysis – a few looks a frame) */
            int ti = scan_i, size = TR[ti].size; char nm[24];
            if (TR[ti].gone || size <= 0) { scan_i++; scan_bad++; continue; }
            an_name(an_key(TR[ti].name, size), nm);
            if (vibe_store_size(nm) > 0) { scan_i++; scan_have++; continue; }
            dj_quiet = 1; int ok = dj_open(&SC, ti, SCB, SAB); dj_quiet = 0;
            if (ok == VIBE_MEDIA_PENDING) return;   /* (its file is not here yet: the same track again, READY or the next frame) */
            scan_i++;
            if (!ok) { SC.t = -1; scan_bad++; continue; }
            if (SC.an_saved) { SC.t = -1; scan_have++; continue; }   /* (kept after all) */
            return;                              /* (measured from the next frame on) */
        }
        if (scan_i >= ntr) { scan_run = 0; say("LIBRARY MEASURED"); }
        return;
    }
    int quiet = !dj_active() && st != ST_PLAY;
    for (int n = 0; n < (quiet ? 2 : 1); n++) dj_analyse(&SC);
    if (SC.gone) { scan_bad++; SC.t = -1; return; }   /* (its stick was pulled) */
    if (SC.an != 1 && SC.wf_done && SC.an_saved) { if (SC.an == 2) scan_new++; else scan_none++; SC.t = -1; }
}

static void glob_toggle(void) { glob_on = !glob_on; if (glob_on) glob_sel = 0; }
static void glob_use(int r)
{
    if (r == 0) { if (scan_run) scan_stop(); else scan_start(); }
    else if (r == 1) { C.no_auto ^= 1; cfg_touch(); say(C.no_auto ? "AUTO VOL OFF" : "AUTO VOL ON"); }
    else if (r == 2) { scan(); scan_reset(); say("LIST READ AGAIN"); }
    else if (r == 3) { C.split ^= 1; cfg_touch(); say(C.split ? "SPLIT ON: L MASTER, R PHONES" : "SPLIT OFF: THE MASTER IN STEREO"); }
}
static int glob_event(const vibe_event *e)       /* 1 = taken (the page open: everything but HOME and SHIFT) */
{
    if (!glob_on) return 0;
    if (e->type == VIBE_EV_BUTTON && (e->id == VB_SHIFT || e->id == VB_HOME)) return 0;
    if (e->type == VIBE_EV_ENCODER && e->id == VE_DATA) { list_acc += e->value; while (list_acc >= 1) { list_acc -= 1; if (glob_sel < 3) glob_sel++; } while (list_acc <= -1) { list_acc += 1; if (glob_sel > 0) glob_sel--; } return 1; }
    if (e->type != VIBE_EV_BUTTON || !e->down) return 1;
    switch (e->id) {
        case VB_ENTER: case VB_DATA_PUSH: glob_use(glob_sel); break;
        case VB_F1: glob_use(0); break;
        case VB_F2: glob_use(1); break;
        case VB_F3: glob_use(2); break;
        case VB_F4: glob_use(3); break;
        case VB_F6: case VB_RETURN: case VB_K5: glob_on = 0; break;
    }
    return 1;
}
static void glob_draw(void)
{
    vg_clear(C_BG);
    { int nw = vg_text("VIBE DJ", 10, 8, VG_MED, C_TEXT, VG_LEFT); vg_text("GLOBAL", 20 + nw, 8, VG_MED, C_TEXT, VG_LEFT); }
    static const char *const R[4] = { "SCAN LIBRARY", "AUTO VOL", "READ THE LIST AGAIN", "HEADPHONES SPLIT (Y cable)" };   /* (2.5: SPLIT) */
    for (int r = 0; r < 4; r++) {
        int y = 38 + r * 34, s = r == glob_sel;
        vg_round(10, y, 460, 30, 5, s ? C_BUTTON : C_PANEL);
        if (s) vg_rect(10, y, 4, 30, C_WARN);
        vg_text(R[r], 24, y + 6, VG_MED, C_TEXT, VG_LEFT);
        const char *v = r == 0 ? (scan_run ? "RUNS - STOP" : "START") : r == 1 ? (C.no_auto ? "OFF" : "ON") : r == 3 ? (C.split ? "ON" : "OFF") : "READ";
        uint32_t c = r == 0 ? (scan_run ? C_ACTIVE : C_TEXT_OFF) : r == 1 ? (C.no_auto ? C_TEXT_OFF : C_ACTIVE) : r == 3 ? (C.split ? C_ACTIVE : C_TEXT_OFF) : C_TEXT_OFF;
        vg_text(v, 458, y + 7, VG_MED, c, VG_RIGHT);
    }
    {   /* the scan: how far, what it measures now */
        int y = 174; char b[64], n[12]; int q;
        vg_text("LIBRARY", 12, y, VG_SMALL, C_TEXT2, VG_LEFT);
        int done = scan_have + scan_new + scan_none + scan_bad;
        q = 0; vg_itoa(ntr, n); scopy(b, n, 12); q = slen(b); scopy(b + q, " tracks", 16);
        if (scan_run || done) { q = slen(b); scopy(b + q, "  \xC2\xB7  ", 8); q = slen(b); vg_itoa(done, n); scopy(b + q, n, 12); q = slen(b); scopy(b + q, " looked at", 16); }
        vg_text(b, 12, y + 18, VG_MED, C_TEXT, VG_LEFT);
        if (ntr > 0) { int w = 456 * (scan_run || done ? done : 0) / ntr; vg_rect(12, y + 42, 456, 6, C_BUTTON); vg_rect(12, y + 42, w, 6, scan_run ? C_ACTIVE : C_TEXT2); }
        q = 0; vg_itoa(scan_new, n); scopy(b, n, 12); q = slen(b); scopy(b + q, " measured now  \xC2\xB7  ", 24); q = slen(b);
        vg_itoa(scan_have, n); scopy(b + q, n, 12); q = slen(b); scopy(b + q, " kept before  \xC2\xB7  ", 24); q = slen(b);
        vg_itoa(scan_none, n); scopy(b + q, n, 12); q = slen(b); scopy(b + q, " no tempo", 12);
        if (scan_run || done) vg_text(b, 12, y + 54, VG_SMALL, C_TEXT_OFF, VG_LEFT);
        if (scan_run && SC.t >= 0) {
            char t[80]; scopy(b, "NOW: ", 8); scopy(b + 5, TR[SC.t].title, 58); fit(b, t, 80, VG_SMALL, 456);
            vg_text(t, 12, y + 72, VG_SMALL, C_ACTIVE, VG_LEFT);
        } else if (!scan_run) vg_text("Measures every track once: BPM, beat, waveform", 12, y + 72, VG_SMALL, C_TEXT2, VG_LEFT);
    }
    {   /* where the music comes from (SDK 13: the host's media sources) */
        char b[160]; lib_sources_text(b, (int)sizeof b);
        vg_text(b, 12, 264, VG_SMALL, lib_reading() ? C_ACTIVE : C_TEXT2, VG_LEFT);
    }
    if (toast && ms_now - toast_t < 1200u) { int w = vg_text_width(toast, VG_MED) + 32; vg_round(240 - w / 2, 126, w, 30, 6, C_BUTTON); vg_text(toast, 240, 133, VG_MED, C_TEXT, VG_CENTER); }
    const char *F[6] = { scan_run ? "STOP" : "SCAN", "AUTO VOL", "LIST", "SPLIT", "", "CLOSE" };
    vg_rect(0, 282, VIBE_SCREEN_W, 38, C_BG);
    for (int i = 0; i < 6; i++) {
        int x = 40 + i * 80; vibe_label(VB_F1 + i, F[i]);
        if (!F[i][0]) { vibe_led_button(VB_F1 + i, 0, 0); continue; }
        uint32_t on = i == 0 && scan_run ? C_ACTIVE : i == 1 && !C.no_auto ? C_ACTIVE : i == 3 && C.split ? C_ACTIVE : 0;
        vg_round(x - 37, 286, 74, 30, 5, on ? on : C_BUTTON);
        vg_text(F[i], x, 293, VG_MED, on ? C_ON : C_TEXT, VG_CENTER);
        vibe_led_button(VB_F1 + i, on ? on : C_TEXT2, on ? 0.9f : 0.1f);
    }
    for (int i = 0; i < 6; i++) { vibe_label(VB_K1 + i, i == 4 ? "GLOBAL" : ""); vibe_led_button(VB_K1 + i, i == 4 ? C_TEXT : 0, i == 4 ? 0.9f : 0); }
}
