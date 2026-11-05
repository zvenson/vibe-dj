/*
 * PLAYER – SPOTIFY (28.10.2026, SDK 12): Spotify Connect in the PLAYER, nowhere else. Included at the end of main.c.
 *
 *   The list's first row (in ALL MUSIC) is SPOTIFY: ENTER opens it – the VIBE asks the host for Spotify Connect
 *   (vibe_request(VIBE_REQ_SPOTIFY)), and "VIBE FM" shows up as a speaker in the Spotify app (Premium). What plays there
 *   comes in as the host's stream (vibe_stream_read, 44.1 kHz) and goes through the PLAYER's own way: resampled to the
 *   output's rate, VOLUME, BASS / MID / HIGH, the DJ filter (encoder B), the limiter. Its title, artist, album and the
 *   time from the stream's info. ↩ (or F6 CLOSE) ends it – the host stops Spotify Connect, the VIBE leaves the app.
 *   Play, pause and skip are the app's. Hosts without it (the browser, the plugin) leave the state at 0: a note says so.
 */
#define SPN 8192
static float SPL[SPN], SPR[SPN]; static uint32_t sp_w; static double sp_rp; static float sp_tl[512], sp_tr[512];
static void spot_open(void)
{
    if (dj_on) { say("SPOTIFY: IN THE LIST (K6), NOT ON A DECK"); return; }
    if (dj_active()) { say("THE DJ PLAYS - STOP ITS DECKS FIRST"); return; }
    if (st == ST_PLAY) st = ST_PAUSE;            /* (the track waits; it plays on with PLAY after Spotify) */
    sp_w = 0; sp_rp = 0; sp_on = 1;
    vibe_request(VIBE_REQ_SPOTIFY);
}
static void spot_close(void)
{
    sp_on = 0;
    vibe_request(VIBE_REQ_STREAM_OFF);
    say("SPOTIFY OFF");
}
/* the audio: up to n frames out, read from the stream as far as the resampling needs (silence while none comes) */
static void spot_audio(float *l, float *r, int n)
{
    const vibe_stream_info *in = vibe_stream();
    double ratio = in->rate > 0 ? (double)in->rate / SR : 1.0;
    uint32_t need = (uint32_t)(n * ratio) + 4;
    while (sp_w - (uint32_t)sp_rp < need + 4) {
        int room = SPN - 8 - (int)(sp_w - (uint32_t)sp_rp); if (room > 512) room = 512;
        int k = room > 0 ? vibe_stream_read(sp_tl, sp_tr, room) : 0;
        if (k <= 0) break;
        for (int i = 0; i < k; i++) { SPL[sp_w & (SPN - 1)] = sp_tl[i]; SPR[sp_w & (SPN - 1)] = sp_tr[i]; sp_w++; }
    }
    if (eq_dirty) eq_update();
    int djm = C.dj <= -2 ? 1 : C.dj >= 2 ? 2 : 0;
    if (C.dj != dj_was) { if (djm) vd_svf_set(&DJC, dj_hz(C.dj), 1.1f, SR); dj_was = C.dj; }
    float kg = vd_coef(0.02f, SR), tv = C.vol * C.vol;
    lim_rel = 1.0f - vd_coef(0.2f, SR);
    for (int i = 0; i < n; i++) {
        float a = 0, b = 0;
        if (sp_w - (uint32_t)sp_rp >= 4) {       /* (4-point Hermite, as the decks; at 44.1 -> 48 kHz plenty) */
            uint32_t ip = (uint32_t)sp_rp; float f = (float)(sp_rp - ip);
            float x0 = SPL[(ip - 1) & (SPN - 1)], x1 = SPL[ip & (SPN - 1)], x2 = SPL[(ip + 1) & (SPN - 1)], x3 = SPL[(ip + 2) & (SPN - 1)];
            a = ((((0.5f * (x3 - x0) + 1.5f * (x1 - x2)) * f + (x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3)) * f + 0.5f * (x2 - x0)) * f) + x1;
            x0 = SPR[(ip - 1) & (SPN - 1)]; x1 = SPR[ip & (SPN - 1)]; x2 = SPR[(ip + 1) & (SPN - 1)]; x3 = SPR[(ip + 2) & (SPN - 1)];
            b = ((((0.5f * (x3 - x0) + 1.5f * (x1 - x2)) * f + (x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3)) * f + 0.5f * (x2 - x0)) * f) + x1;
            sp_rp += ratio;
        }
        if (eq_on[0]) { a = bq(&EQ[0], &EQS[0][0], a); b = bq(&EQ[0], &EQS[1][0], b); }
        if (eq_on[1]) { a = bq(&EQ[1], &EQS[0][1], a); b = bq(&EQ[1], &EQS[1][1], b); }
        if (eq_on[2]) { a = bq(&EQ[2], &EQS[0][2], a); b = bq(&EQ[2], &EQS[1][2], b); }
        if (djm) { vd_svf_out ol = vd_svf_tick(&DJL, &DJC, a), orr = vd_svf_tick(&DJR, &DJC, b); a = djm == 1 ? ol.lp : ol.hp; b = djm == 1 ? orr.lp : orr.hp; }
        ANA[aw++ & 2047] = (a + b) * 0.5f;
        gv = tv + (gv - tv) * kg;
        a *= gv; b *= gv; limit(&a, &b);
        if (vd_absf(a) > pk_l) pk_l = vd_absf(a);
        if (vd_absf(b) > pk_r) pk_r = vd_absf(b);
        l[i] = a; r[i] = b;
    }
}
static int spot_event(const vibe_event *e)       /* 1 = taken; the encoders (sound) and HOME go on as always */
{
    if (e->type != VIBE_EV_BUTTON) return 0;
    if (e->id == VB_RETURN || e->id == VB_F6) { if (e->down) spot_close(); return 1; }
    if (e->id == VB_HOME || e->id == VB_SHIFT || e->id == VB_MENU) return 0;
    return 1;                                    /* (the rest: the app plays, pauses and skips) */
}
static void spot_draw(void)
{
    const vibe_stream_info *in = vibe_stream();
    char b[128], t[128];
    vg_clear(C_BG);
    int nw = vg_text("VIBE DJ", 10, 8, VG_MED, C_TEXT, VG_LEFT); vg_text("SPOTIFY", 20 + nw, 8, VG_MED, C_ACTIVE, VG_LEFT);
    float ml = vd_minf(1, vd_sqrt(pk_l)), mr = vd_minf(1, vd_sqrt(pk_r));
    vg_rect(402, 9, 66, 5, C_BUTTON); vg_rect(402, 9, (int)(66 * ml), 5, pk_l > 0.95f ? C_CLIP : C_ACTIVE);
    vg_rect(402, 17, 66, 5, C_BUTTON); vg_rect(402, 17, (int)(66 * mr), 5, pk_r > 0.95f ? C_CLIP : C_ACTIVE);
    pk_l *= 0.7f; pk_r *= 0.7f;
    vg_rect(0, 31, VIBE_SCREEN_W, 1, C_ACTIVE);
    if (in->state == 0) {
        vg_text("SPOTIFY PLAYS ON THE VIBE", 240, 96, VG_BIG, C_TEXT, VG_CENTER);
        vg_text("Spotify Connect runs on the VIBE FM itself –", 240, 140, VG_MED, C_TEXT2, VG_CENTER);
        vg_text("not in the browser and not in the plugin.", 240, 164, VG_MED, C_TEXT2, VG_CENTER);
    } else if (in->state == 1 || !in->title[0]) {
        vg_text("OPEN SPOTIFY", 240, 70, VG_BIG, C_TEXT, VG_CENTER);
        vg_text("On your phone or computer: play something,", 240, 116, VG_MED, C_TEXT2, VG_CENTER);
        vg_text("tap the speaker icon and choose", 240, 140, VG_MED, C_TEXT2, VG_CENTER);
        { int k = 0; b[k++] = '"'; for (int i = 0; in->name[i] && k < 40; i++) b[k++] = in->name[i]; b[k++] = '"'; b[k] = 0; vg_text(b, 240, 170, VG_BIG, C_ACTIVE, VG_CENTER); }
        vg_text("Spotify Premium needed", 240, 214, VG_SMALL, C_TEXT_OFF, VG_CENTER);
    } else {
        fit(in->title, b, 128, VG_BIG, 460); vg_text(b, 10, 40, VG_BIG, C_TEXT, VG_LEFT);
        { int q = 0; for (int z = 0; in->artist[z] && q < 60; z++) t[q++] = in->artist[z];
          if (in->album[0]) { if (q) { t[q++] = ' '; t[q++] = (char)0xC2; t[q++] = (char)0xB7; t[q++] = ' '; } for (int z = 0; in->album[z] && q < 120; z++) t[q++] = in->album[z]; }
          t[q] = 0; fit(t, b, 128, VG_MED, 460); vg_text(b, 10, 68, VG_MED, C_TEXT2, VG_LEFT); }
        char tm[16], d[16]; float pos = in->pos_ms / 1000.0f, dur = in->dur_ms / 1000.0f; if (dur > 0 && pos > dur) pos = dur;
        mmss(pos, tm); int tw = vg_text(tm, 10, 92, VG_HUGE, in->state == 2 ? C_TEXT : C_TEXT2, VG_LEFT);
        if (dur > 0) { char x[24] = "/ "; mmss(dur, d); scopy(x + 2, d, 16); vg_text(x, 24 + tw, 124, VG_BIG, C_TEXT2, VG_LEFT); }
        vg_text(in->state == 2 ? "\xE2\x96\xB6 PLAYING" : "PAUSED", 470, 124, VG_MED, in->state == 2 ? C_ACTIVE : C_TEXT2, VG_RIGHT);
        vg_round(10, 160, 460, 8, 4, C_BUTTON);
        if (dur > 0) vg_round(10, 160, (int)(460 * vd_clamp(pos / dur, 0, 1)), 8, 4, C_ACTIVE);
        spectrum();
        for (int k = 0; k < NB; k++) {
            int x = 10 + k * 460 / NB, segs = (int)(lvl[k] * 15 + 0.5f);
            for (int j = 0; j < 15; j++) { uint32_t col = j < 8 ? C_ACTIVE : j < 12 ? C_WARN : j < 14 ? C_WARN : C_CLIP; vg_rect(x, 274 - j * 6 - 5, 460 / NB - 3, 5, j < segs ? col : C_PANEL); }
        }
    }
    if (toast && ms_now - toast_t < 1200u) { int w = vg_text_width(toast, VG_MED) + 32; vg_round(240 - w / 2, 126, w, 30, 6, C_BUTTON); vg_text(toast, 240, 133, VG_MED, C_TEXT, VG_CENTER); }
    static const char *const F[6] = { "", "", "", "", "", "CLOSE" };
    vg_rect(0, 282, VIBE_SCREEN_W, 38, C_BG);
    for (int i = 0; i < 6; i++) {
        vibe_label(VB_F1 + i, F[i]);
        if (!F[i][0]) { vibe_led_button(VB_F1 + i, 0, 0); continue; }
        int x = 40 + i * 80; vg_round(x - 37, 286, 74, 30, 5, C_BUTTON); vg_text(F[i], x, 293, VG_SMALL, C_TEXT, VG_CENTER); vibe_led_button(VB_F1 + i, C_TEXT2, 0.3f);
    }
    vg_text("Play and skip in the app \xC2\xB7 1-4 sound \xC2\xB7 B filter \xC2\xB7 \xE2\x86\xA9 close", 240, 262, VG_SMALL, C_TEXT_OFF, VG_CENTER);
}
