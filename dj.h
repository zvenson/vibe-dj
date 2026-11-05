/*
 * PLAYER – DJ (28.10.2026): two decks from the same list, mixed. Included at the end of main.c (one unit).
 * 30.10. (1.5, SDK 13 media): a track may come from a stick or the card – a deck loads it when its file is here (it plays
 * on until then); a stick pulled while it plays: what is read plays out, nothing breaks.
 *
 *   K6 = DJ on / off (the PLAYER as before stays as it was) · DATA (or ENTER) = the list, which stays until CLOSE (F5) or
 *   RETURN; in it F3 LOAD A · F4 LOAD B (where SYNC is on the decks) · F1 / F6 PLAY as always · F2 UP (a folder up) ·
 *   ENTER = open a folder, or a track into the deck that is not playing ·
 *   K1 / K2 = in the list LOAD A / B, on the decks PLAY A / B (5.11.2026, Sven) · K3 = AUTO VOL on / off (every track equally loud: its loudness measured with the
 *   waveform, -16 dB, +12 / -12 dB at most; in player.cfg)
 *   F1 PLAY A · F2 PHONES A · F3 SYNC A · F4 SYNC B · F5 PHONES B · F6 PLAY B  (the same on pads 1-4 = A, 5-8 = B:
 *   PLAY, PHONES, SYNC, TO THE START) · SHIFT + F1 / F6 = A / B to the start
 *   pads 9-12 (A) / 13-16 (B): CUE 1, CUE 2 (empty: set; set: jump there; SHIFT or held 1 s: clear), LOOP 4, LOOP 8
 *   beats (from the next beat; again: out of it at its end) – a loop running: SHIFT + LOOP 4 halves it, SHIFT + LOOP 8
 *   doubles it (1/8 beat .. 8 s); none: SHIFT + LOOP 4 / LOOP 8 held = ROLL 1/2 / 1/4 beat (the track runs on underneath,
 *   let go: it plays where it would be) · OCT - / + (A), ◀ / ▶ (B) held: pitch bend -4 / +4 % ·
 *   SHIFT + encoder A / B: A / B MID (to a kill)
 *   encoders (29.10., Sven: the same for each deck): 1 = A volume · 2 = A nudge · 3 = B volume · 4 = B nudge (1 % of a
 *   beat a click, ~5 ms; with SYNC it moves SYNC's beat, both push the deck while the knob turns – up to ±8 %, back to the pitch 0.2 s after the hand stops; synced, SYNC keeps where it ends; no jump) ·
 *   SHIFT + 1 / 3 = A / B FILTER (left low pass, right high pass, the middle off) · SHIFT + 2 / 4 = A / B pitch (0.1 % a
 *   click) · HIGH only through FOCUS (DATA) ·
 *   a track loaded: its deck's pitch, filter and EQ back to 0 (29.10., Sven; the volume stays) ·
 *   A = crossfader · B = headphones: PHONES only .. the master
 *
 * Pitch changes the speed and the pitch together (as a turntable; no key lock – not wanted). SYNC: the deck follows the
 * other deck's tempo and slides into its beat (a small, gentle speed nudge, no jump), as long as SYNC is lit.
 * BPM: from the file's ID3 tag (TBPM) when it has one, else measured from its first 40 seconds (onsets, autocorrelation,
 * then the beat grid's phase) – in the draw, a little each frame, never in the audio.
 *
 * The headphones (SDK 9, VIBE FM Protokoll 8.1): the decks with PHONES lit, mixed with the master by encoder B, on the
 * host's own headphone output (the VIBE's PHONES jack, the plugin's "Cue" output). A host without one (the prototype's
 * interface, the browser): SPLIT – OUT L the master in mono, OUT R the cue in mono (a Y cable into the headphones), as
 * long as a deck has PHONES on – only when GLOBAL's HEADPHONES SPLIT is on (2.5, 5.11.2026: Sven heard a deck turn mono
 * in the browser after PHONES; off, the master stays stereo; never in the Jukebox).
 */

#define DRB 16384
#define DAB 65536
#define NENV 8192
#define WFR 100                                  /* the waveform: 100 columns a second (10 ms), three bands */
#define WFN (WFR * 60 * 15)                      /* up to 15 minutes */
#define SEEKN 8192                               /* MP3 seek points (every 0.2 s: 27 minutes) – a hot cue jumps exactly */
#define LBN (48000 * 8)                          /* a loop: up to 8 s of the file (8 beats down to 60 BPM) */
#define XF 240                                   /* a loop's seam, out of it, a ROLL let go: 5 ms from the old way on into the new (no click) */
#define XFR 512                                  /* (read past a loop's end for it: XF at up to twice the speed, and some) */
typedef struct {
    int t;                                       /* the track (TR index), -1 = empty */
    int kind, w_fmt, w_bits, w_ch, w_align, kbps;
    float frate, dur;
    int fstart, fend, fpos, ch, ct, dec_end;     /* the file, and the bytes in cb[ch, ct) */
    uint8_t *cb; mp3dec_t mp3;
    float RL[DRB], RR[DRB]; uint32_t rw; double rp; /* decoded at the file's rate; rp counts frames since the start */
    int play; float fade;
    float vol, pitch, low, high, mid;            /* pitch -0.25 .. +0.25 (shown in %); low / mid / high -30 (kill) .. +6 dB */
    bq_c eq[3]; bq_s eqs[2][3]; int eq_dirty;
    float nav;                                   /* FOCUS on the overview: the position being turned to (seconds) */
    float soff;                                  /* SYNC's offset in beats: where the DJ put this deck against the other (bend while synced) */
    float flt, fs, f1[2], f2[2];                 /* the DJ filter (SHIFT + encoder 1 / 4): -1 low pass .. 0 open .. +1 high pass; smoothed, two SVF states */
    float ng, gn; float l_hp, l_px, l_acc, l_sum; int l_n, l_cnt;   /* AUTO GAIN: the target from the loudness, the gain now; the measurement */
    float bend;                                  /* pitch bend while OCT -/+ (A) or ◀ ▶ (B) is held: -0.04 / +0.04 */
    double hc[2]; int hc_set[2];                 /* hot cues 1, 2: a frame of the file (on a beat when the tempo is known) */
    int lp, lp_exit, lp_beats; double lp_in, lp_out;   /* loop: 0 off, 1 waiting for its end (recording), 2 looping */
    int xf_n; double xf_q, xf_len;                /* the seam's crossfade: frames left, where the old way goes on in the loop's frames (xf_len: around in it) */
    int roll; double lp_cyc, lp_max, gp;          /* ROLL (held) · where it turns now (doubled: the old end until the new part is read) · how far it is read · the track's place underneath */
    float LBL[LBN], LBR[LBN];                    /* the loop's frames, as they were decoded */
    struct { uint32_t smp; int byte; } sk[SEEKN]; int skn; uint32_t sk_next;   /* MP3: where each 0.2 s begins */
    int cue, sync;
    float bpm, beat0;                            /* the file's tempo, its first beat (seconds) – 0 = not known (yet) */
    float tag;                                   /* the tag's BPM (TBPM): only to choose between half and double – 28.10.: tags lie */
    float vtag;                                  /* (5.11.2026) "VIBE DJ BPM" (TXXX): measured by us – trusted, only the beat's phase is searched */
    float nudge;                                 /* SYNC's phase correction, a fraction of the speed */
    float jog;                                   /* NUDGE: seconds of the file still to push (+ ahead) – only while the knob turns (29.10.: JOG_T, JOG_MAX) */
    int jog_keep;                                /* NUDGE while synced: once pushed, the offset as it is becomes SYNC's */
    int scr, scr_y, scr_mv; double scr_f; uint32_t scr_ms;   /* (2.3) the hand on the record: held in the waveform, the frame under it, moved px, since */
    /* the BPM measurement (draw only): its own decoder over the first 40 s */
    int an;                                      /* 0 not yet, 1 running, 2 done, 3 failed */
    mp3dec_t amp; uint8_t *ab; int ach, act, apos; float a_lp, a_acc, a_prev, a_lp2, a_acc2, a_prev2; int a_n, a_hop; uint32_t a_frames;
    float env[NENV]; int nenv;
    int a_ph, a_L, a_bl; float a_ac[512], a_best, a_bpm, a_b, a_hi, a_bt, a_bo;   /* the search after the decoding, a slice a frame (28.10.: in one go it took a whole frame – clicks) */
    /* the waveform of the whole track (Pioneer-like: low / mid / high, each the peak of its 10 ms), filled by the same pass */
    uint8_t wl[WFN], wm[WFN], wh[WFN]; int wfn, wf_done, w_n, w_hop; float w_lp1, w_lp2, w_pl, w_pm, w_ph;
    uint8_t ov[480]; int ov_n, ov_w;             /* the overview's columns, kept: made again only when the waveform or the width changes */
    uint32_t an_key; int an_fsize, an_saved;     /* the analysis kept on the card (djan/): its key, the file's size, written (or read) */
    int gone;                                    /* its file went while reading (a stick pulled): what is read plays out, nothing is kept */
} deck_t;
static deck_t D[2];
static uint8_t DCB[2][CB], DAB_[2][DAB];
static float xfade = 0.5f, cue_mix = 0.0f;       /* crossfader 0 = A .. 1 = B; headphones 0 = PHONES only .. 1 = master */
static int dj_split;                             /* (shown) the headphones go out as SPLIT */
static float dj_pk_l, dj_pk_r, dj_pk_d[2], dj_lv[2], dj_hold[2]; static uint32_t dj_hold_t[2];
static int dj_hot = -1; static uint32_t dj_hot_t;
static int browse;                               /* the list is open (only the player opens and closes it – nothing by itself) */
static int focus_play = -1;                      /* a track loaded into this deck: back on the decks, FOCUS is on its PLAY – one press (29.10., Sven) */
static char dj_msg[40];

static float dj_pos(const deck_t *d) { return d->t >= 0 && d->frate > 0 ? (float)(d->rp / d->frate) : 0; }
static float dj_eff_bpm(const deck_t *d) { return d->bpm * (1.0f + d->pitch); }

/* ---- loading (events / draw) */
static void dj_gone(deck_t *d)                   /* its file went (SDK 13: a stick pulled) – never a crash: what is read plays out */
{
    d->fpos = d->fend; d->apos = d->fend; d->an_saved = 1;   /* (a measurement cut short is not kept) */
    if (d->gone) return;
    d->gone = 1;
    if (d >= D && d < D + 2) say(d == D ? "A: ITS FILE IS GONE (STICK PULLED?)" : "B: ITS FILE IS GONE (STICK PULLED?)");
}
static int dj_refill(deck_t *d, int want)        /* 1 = read some; 0 = nothing now (not here yet, the end, gone) */
{
    if (d->t < 0 || d->fpos >= d->fend) return 0;
    if (d->ch > 0 && CB - d->ct < want) { __builtin_memmove(d->cb, d->cb + d->ch, (size_t)(d->ct - d->ch)); d->ct -= d->ch; d->ch = 0; }
    int n = want < CB - d->ct ? want : CB - d->ct;
    if (n > d->fend - d->fpos) n = d->fend - d->fpos;
    if (n < 4) { d->fpos = d->fend; return 0; }
    int got = f_read(TR[d->t].name, d->fpos, d->cb + d->ct, n);
    if (got > 0) { d->ct += got; d->fpos += got; return 1; }
    if (got == VIBE_MEDIA_PENDING) return 0;
    if (got < 0) dj_gone(d); else d->fpos = d->fend;
    return 0;
}
static float id3_bpm(const uint8_t *b, int n)    /* TBPM (ID3v2.3 / 2.4) or TBP (2.2) in the tag at the file's start */
{
    if (n < 10 || b[0] != 'I' || b[1] != 'D' || b[2] != '3') return 0;
    int ver = b[3], end = 10 + (int)syncsafe(b + 6), p = 10, hdr = ver == 2 ? 6 : 10;
    if (end > n) end = n;
    while (p + hdr < end) {
        int sz = ver == 2 ? (int)(b[p + 3] << 16 | b[p + 4] << 8 | b[p + 5]) : ver == 4 ? (int)syncsafe(b + p + 4) : (int)be32(b + p + 4);
        if (sz <= 0 || p + hdr + sz > end) break;
        if ((ver == 2 && b[p] == 'T' && b[p + 1] == 'B' && b[p + 2] == 'P') || (ver != 2 && eq4(b + p, "TBPM"))) {
            float v = 0, f = 0; int dot = 0;
            for (int i = p + hdr + 1; i < p + hdr + sz; i++) {
                int c = b[i];
                if (c >= '0' && c <= '9') { if (dot) { f *= 0.1f; v += (float)(c - '0') * f; } else v = v * 10 + (float)(c - '0'); }
                else if ((c == '.' || c == ',') && !dot) { dot = 1; f = 1; }
                else if (v > 0) break;
            }
            return v >= 40 && v <= 250 ? v : 0;
        }
        p += hdr + sz;
    }
    return 0;
}
static float id3_vbpm(const uint8_t *b, int n)   /* (5.11.2026, Sven: "die Songs von mir schon analysiert") TXXX "VIBE DJ BPM" –
                                                    written by us (tools: the demo songs' mastering), so trusted: 0 = none */
{
    if (n < 10 || b[0] != 'I' || b[1] != 'D' || b[2] != '3' || (b[3] != 3 && b[3] != 4)) return 0;
    int ver = b[3], end = 10 + (int)syncsafe(b + 6), p = 10;
    if (end > n) end = n;
    static const char K[] = "VIBE DJ BPM";
    while (p + 10 < end) {
        int sz = ver == 4 ? (int)syncsafe(b + p + 4) : (int)be32(b + p + 4), e = p + 10 + sz;
        if (sz <= 0 || e > end) break;
        if (eq4(b + p, "TXXX") && (b[p + 10] == 0 || b[p + 10] == 3)) {   /* (Latin-1 / UTF-8 only) */
            int q = p + 11, k = 0;
            while (K[k] && q < e && b[q] == (uint8_t)K[k]) { q++; k++; }
            if (!K[k] && q < e && b[q] == 0) {
                float v = 0, f = 0; int dot = 0;
                for (q++; q < e; q++) {
                    int c = b[q];
                    if (c >= '0' && c <= '9') { if (dot) { f *= 0.1f; v += (float)(c - '0') * f; } else v = v * 10 + (float)(c - '0'); }
                    else if (c == '.' && !dot) { dot = 1; f = 1; }
                    else break;
                }
                return v >= 40 && v <= 250 ? v : 0;
            }
        }
        p = e;
    }
    return 0;
}
/* the cue points, kept per track (28.10., Sven: "1 cue point per channel, set and saved"): djcues.bin = up to 512
 * entries { FNV-1a of the file's name, cue 1, cue 2 (frames of the file, 0xFFFFFFFF = none) } – read once, written whole
 * when one is set or cleared (never from the audio) */
#define CUEN 512
typedef struct { uint32_t h, c[2]; } cue_t;
static cue_t CUES[CUEN]; static int ncue = -1;
static uint32_t name_hash(const char *s) { uint32_t h = 2166136261u; for (; *s; s++) { h ^= (uint8_t)*s; h *= 16777619u; } return h ? h : 1; }
static void cues_read(void) { if (ncue >= 0) return; int n = vibe_store_read("djcues.bin", CUES, (int)sizeof CUES); ncue = n > 0 ? n / (int)sizeof(cue_t) : 0; }
static void cues_keep(const deck_t *d)
{
    if (d->t < 0) return;
    cues_read();
    uint32_t h = name_hash(TR[d->t].name); int i = 0; while (i < ncue && CUES[i].h != h) i++;
    if (i == ncue) { if (ncue < CUEN) ncue++; else { __builtin_memmove(CUES, CUES + 1, sizeof(cue_t) * (CUEN - 1)); i = CUEN - 1; } }   /* (full: the oldest goes) */
    CUES[i].h = h;
    for (int n = 0; n < 2; n++) CUES[i].c[n] = d->hc_set[n] ? (uint32_t)d->hc[n] : 0xFFFFFFFFu;
    vibe_store_write("djcues.bin", CUES, ncue * (int)sizeof(cue_t));
}
static void cues_load(deck_t *d)
{
    cues_read();
    uint32_t h = name_hash(TR[d->t].name);
    for (int i = 0; i < ncue; i++) if (CUES[i].h == h) { for (int n = 0; n < 2; n++) if (CUES[i].c[n] != 0xFFFFFFFFu) { d->hc[n] = CUES[i].c[n]; d->hc_set[n] = 1; } return; }
}
/* the analysis, kept per track (29.10., Sven: "save it, so it need not be analysed again"): djan/<key>.bin = the tempo, the
 * first beat, the loudness, the waveform and the MP3 seek points; the key = the name's hash with the file's size (a file
 * replaced under the same name is measured again) – the name of a media file is ":<source>/<path>" (30.10.: the source's
 * key, the path, the size; the same stick tomorrow has the same key). djan/list.bin = { key, BPM } per track, the last used last – over ANN
 * the oldest goes; the lists show the BPM from it (Sven: "see it in the browser"). Written once when the measurement is
 * done (the draw; the VIBE's host writes in its own flow), read in dj_load. */
#define ANN 1024                                 /* (29.10.: 200 – a library scan measures them all) */
#define AN_MAGIC 0x324E4156u                     /* "VAN2" (5.11.: the tag ratios) – a new number when the measurement changes: everything again */
                                                 /* (and the index a new name – list2.bin: the lists showed the old BPM from it, 5.11., Sven) */
typedef struct { uint32_t magic, key; int32_t fsize, an, wfn, skn; float bpm, beat0, ng; } anhdr_t;
typedef struct { uint32_t key; float bpm; } anidx_t;
static anidx_t ANI[ANN]; static int nani = -1;
static uint8_t ANB[sizeof(anhdr_t) + 3 * WFN + 8 * SEEKN];
static void an_name(uint32_t h, char *s)
{
    const char *p = "djan/"; int k = 0; for (; p[k]; k++) s[k] = p[k];
    for (int i = 0; i < 8; i++) s[k++] = "0123456789abcdef"[(h >> (28 - 4 * i)) & 15];
    p = ".bin"; for (int i = 0; p[i]; i++) s[k++] = p[i]; s[k] = 0;
}
static uint32_t an_key(const char *name, int fsize) { uint32_t h = (name_hash(name) ^ (uint32_t)fsize) * 16777619u; return h ? h : 1; }
static void an_index(void)
{
    if (nani >= 0) return;
    int n = vibe_store_read("djan/list2.bin", ANI, (int)sizeof ANI); nani = n > 0 ? n / (int)sizeof(anidx_t) : 0;
    if (n >= 0) return;
    static uint32_t old[ANN]; int m = vibe_store_read("djan/index.bin", old, (int)sizeof old);   /* (29.10.'s first index: the keys only) */
    if (m <= 0) return;
    for (int i = 0; i < m / 4; i++) { char nm[24]; anhdr_t a; an_name(old[i], nm);
        if (vibe_store_read_at(nm, 0, &a, (int)sizeof a) == (int)sizeof a && a.magic == AN_MAGIC) ANI[nani++] = (anidx_t){ old[i], a.an == 2 ? a.bpm : 0 }; }
    vibe_store_write("djan/list2.bin", ANI, nani * (int)sizeof(anidx_t)); vibe_store_delete("djan/index.bin");
}
static void an_touch(uint32_t h, float bpm)      /* the key to the end of the index (the oldest's file goes when full) */
{
    an_index();
    int i = 0; while (i < nani && ANI[i].key != h) i++;
    if (i < nani) { __builtin_memmove(ANI + i, ANI + i + 1, sizeof(anidx_t) * (unsigned)(nani - i - 1)); nani--; }
    else if (nani == ANN) { char o[24]; an_name(ANI[0].key, o); vibe_store_delete(o); __builtin_memmove(ANI, ANI + 1, sizeof(anidx_t) * (ANN - 1)); nani--; }
    ANI[nani++] = (anidx_t){ h, bpm };
    vibe_store_write("djan/list2.bin", ANI, nani * (int)sizeof(anidx_t));
}
static float an_bpm(const track_t *t)            /* the lists: the BPM measured before, 0 = not (yet) */
{
    an_index(); if (!nani) return 0;
    uint32_t h = an_key(t->name, t->size);
    for (int i = nani - 1; i >= 0; i--) if (ANI[i].key == h) return ANI[i].bpm;
    return 0;
}
static void an_load(deck_t *d, int fsize)        /* kept: the deck has it all at once (nothing measured) */
{
    uint32_t h = an_key(TR[d->t].name, fsize);
    d->an_key = h; d->an_fsize = fsize; d->an_saved = 0;
    char nm[24]; an_name(h, nm); anhdr_t a;
    if (vibe_store_read_at(nm, 0, &a, (int)sizeof a) != (int)sizeof a) return;
    if (a.magic != AN_MAGIC || a.key != h || a.fsize != fsize || (a.an != 2 && a.an != 3) || a.wfn < 0 || a.wfn > WFN || a.skn < 0 || a.skn > SEEKN) return;
    int o = (int)sizeof a, sk = a.skn * (int)sizeof d->sk[0];
    if (vibe_store_size(nm) != o + 3 * a.wfn + sk) return;
    if (vibe_store_read_at(nm, o, d->wl, a.wfn) != a.wfn || vibe_store_read_at(nm, o + a.wfn, d->wm, a.wfn) != a.wfn ||
        vibe_store_read_at(nm, o + 2 * a.wfn, d->wh, a.wfn) != a.wfn || (sk && vibe_store_read_at(nm, o + 3 * a.wfn, d->sk, sk) != sk)) { d->wfn = 0; d->skn = 0; return; }
    d->wfn = a.wfn; d->wf_done = 1; d->skn = a.skn; d->bpm = a.bpm; d->beat0 = a.beat0; d->ng = a.ng;
    d->an = a.an; d->a_ph = 0; d->an_saved = 1;
    TR[d->t].bpm = d->an == 2 ? d->bpm : 0;
    an_touch(h, TR[d->t].bpm);
}
static void an_save(deck_t *d)
{
    d->an_saved = 1;
    anhdr_t a = { AN_MAGIC, d->an_key, d->an_fsize, d->an, d->wfn, d->skn, d->bpm, d->beat0, d->ng };
    int o = (int)sizeof a, sk = d->skn * (int)sizeof d->sk[0];
    __builtin_memcpy(ANB, &a, sizeof a);
    __builtin_memcpy(ANB + o, d->wl, (size_t)d->wfn); __builtin_memcpy(ANB + o + d->wfn, d->wm, (size_t)d->wfn);
    __builtin_memcpy(ANB + o + 2 * d->wfn, d->wh, (size_t)d->wfn); __builtin_memcpy(ANB + o + 3 * d->wfn, d->sk, (size_t)sk);
    char nm[24]; an_name(d->an_key, nm);
    TR[d->t].bpm = d->an == 2 ? d->bpm : 0;      /* (the lists show it now) */
    if (vibe_store_write(nm, ANB, o + 3 * d->wfn + sk) == 0) an_touch(d->an_key, TR[d->t].bpm);
}
static int dj_quiet;                             /* (the library scan opens files too: no messages from it) */
static void dj_err(const char *m) { if (!dj_quiet) say(m); }
static int dj_open(deck_t *d, int ti, uint8_t *cb, uint8_t *ab)   /* the file into d, its analysis begun (or read back);
                                                    VIBE_MEDIA_PENDING: not here yet – d untouched, ask again */
{
    const char *n = TR[ti].name;
    int pr = probe(n, TR[ti].size), size = TR[ti].size, got = pr > 0 ? p_head : -1;
    if (pr == VIBE_MEDIA_PENDING) return pr;
    d->cb = cb; d->ab = ab;
    if (got < 16) { dj_err("CANNOT READ FILE"); return 0; }
    __builtin_memcpy(d->cb, PH, (size_t)got);
    d->t = ti; d->gone = 0; d->kind = 0; d->fstart = 0; d->fend = size; d->frate = 0; d->kbps = 0; d->dur = 0;
    d->tag = id3_bpm(d->cb, got); d->vtag = id3_vbpm(d->cb, got); d->bpm = 0; d->beat0 = 0; d->an = 0; d->nudge = 0;
    if (eq4(d->cb, "RIFF") && eq4(d->cb + 8, "WAVE")) {
        int p = 12, ok = 0; d->w_fmt = 0;
        while (p + 8 <= got) {
            uint32_t len = le32(d->cb + p + 4);
            if (eq4(d->cb + p, "fmt ") && p + 24 <= got) {
                d->w_fmt = (int)le16(d->cb + p + 8); d->w_ch = (int)le16(d->cb + p + 10); d->frate = (float)le32(d->cb + p + 12);
                d->w_align = (int)le16(d->cb + p + 20); d->w_bits = (int)le16(d->cb + p + 22);
                if (d->w_fmt == 0xFFFE && p + 34 <= got) d->w_fmt = (int)le16(d->cb + p + 32);
            }
            if (eq4(d->cb + p, "data")) { d->fstart = p + 8; d->fend = len > (uint32_t)(size - d->fstart) || !len ? size : d->fstart + (int)len; ok = 1; break; }
            if (len > (uint32_t)got) break;
            p += 8 + (int)len + (int)(len & 1);
        }
        if (!ok || !d->w_align || !d->frate || !(d->w_fmt == 1 || d->w_fmt == 3) || !(d->w_bits == 16 || d->w_bits == 24 || d->w_bits == 32)) { d->t = -1; dj_err("WAV FORMAT NOT SUPPORTED"); return 0; }
        d->kind = 2; d->dur = (float)((d->fend - d->fstart) / d->w_align) / d->frate;
    } else {
        if (d->cb[0] == 'I' && d->cb[1] == 'D' && d->cb[2] == '3') d->fstart = 10 + (int)syncsafe(d->cb + 6) + ((d->cb[5] & 0x10) ? 10 : 0);
        if (p_tag) d->fend = size - 128;
        int pn = p_frame; __builtin_memcpy(d->cb, PF, (size_t)pn);
        mp3dec_frame_info_t fi = {0}; mp3dec_init(&d->mp3);
        if (pn > 0) mp3dec_decode_frame(&d->mp3, d->cb, pn, 0, &fi);
        if (!fi.frame_bytes || !fi.hz) { d->t = -1; dj_err("NOT AN MP3 / WAV"); return 0; }
        d->kind = 1; d->frate = (float)fi.hz; d->kbps = fi.bitrate_kbps;
        d->dur = d->kbps ? (float)(d->fend - d->fstart) * 8.0f / (d->kbps * 1000.0f) : 0;
        uint32_t frames = 0;
        for (int i = 0; i + 16 < pn && i < 512; i++) {
            if ((eq4(d->cb + i, "Xing") || eq4(d->cb + i, "Info")) && (be32(d->cb + i + 4) & 1u)) { frames = be32(d->cb + i + 8); break; }
            if (eq4(d->cb + i, "VBRI") && i + 18 <= pn) { frames = be32(d->cb + i + 14); break; }
        }
        if (frames) d->dur = (float)frames * (fi.hz >= 32000 ? 1152.0f : 576.0f) / (float)fi.hz;
    }
    mp3dec_init(&d->mp3);
    d->fpos = d->fstart; d->ch = d->ct = 0; dj_refill(d, 16384);   /* (small: the SD read is in the one flow – dj_service reads on) */
    d->rw = 0; d->rp = 0; d->dec_end = 0; d->fade = 0;
    __builtin_memset(d->RL, 0, sizeof d->RL); __builtin_memset(d->RR, 0, sizeof d->RR);
    /* the measurement: the tempo when the tag has none, the beat grid's phase always (dj_analyse, in the draw) */
    d->an = 1; d->a_ph = 0; mp3dec_init(&d->amp); d->apos = d->fstart; d->ach = d->act = 0; d->a_lp = d->a_acc = d->a_prev = d->a_lp2 = d->a_acc2 = d->a_prev2 = 0; d->a_n = 0; d->nenv = 0; d->a_frames = 0;
    d->a_hop = (int)(d->frate / 200.0f); if (d->a_hop < 1) d->a_hop = 1;
    d->skn = 0; d->sk_next = 0; d->hc_set[0] = d->hc_set[1] = 0; d->lp = 0; d->lp_exit = 0; d->roll = 0; d->xf_n = 0; d->bend = 0; d->jog = 0; d->jog_keep = 0; d->scr = 0;
    d->pitch = 0; d->low = d->mid = d->high = 0; d->eq_dirty = 1; d->flt = d->fs = 0;   /* (29.10., Sven: a new track starts flat) */
    __builtin_memset(d->eqs, 0, sizeof d->eqs); __builtin_memset(d->f1, 0, sizeof d->f1); __builtin_memset(d->f2, 0, sizeof d->f2);
    d->ov_n = -1; d->wfn = 0; d->wf_done = 0; d->w_n = 0; d->w_lp1 = d->w_lp2 = d->w_pl = d->w_pm = d->w_ph = 0;
    d->w_hop = (int)(d->frate / (float)WFR); if (d->w_hop < 1) d->w_hop = 1;
    d->ng = 1; d->l_hp = d->l_px = d->l_acc = d->l_sum = 0; d->l_n = d->l_cnt = 0;   /* (gn glides from where it is) */
    an_load(d, size);                            /* (measured before: all of it at once) */
    return 1;
}
static int dj_pend[2] = { -1, -1 };              /* a track for deck A / B whose file is not here yet (dj_io loads it when it has come) */
static int dj_load(int k, int ti)                /* 1 = loaded (or loading: its file comes), 0 = cannot */
{
    deck_t *d = &D[k];
    dj_pend[k] = -1;
    if (ti < 0 || ti >= ntr) return 0;
    int was_playing = d->play;                   /* (28.10., Sven: loading into a playing deck – as on a DJ deck: it stops, the new one waits) */
    int r = dj_open(d, ti, DCB[k], DAB_[k]);      /* (not here yet: the deck plays on until it is) */
    if (r == VIBE_MEDIA_PENDING) { dj_pend[k] = ti; return 1; }
    d->play = 0;
    if (!r) return 0;
    cues_load(d);                                /* (its saved cue points) */
    /* SYNC (28.10., Sven): loaded into the follower – it stays the follower: once the new track's tempo is known it runs at
       the other deck's tempo (and starts on its beat); loaded into the leader – the follower keeps its speed, SYNC off */
    { int was = d->sync; d->sync = was; d->nudge = 0; d->soff = 0;
      if (was) say(k ? "B LOADED - STAYS SYNCED" : "A LOADED - STAYS SYNCED");
      else if (was_playing) say(k ? "B LOADED - PRESS PLAY" : "A LOADED - PRESS PLAY"); }
    if (D[1 - k].sync) { D[1 - k].sync = 0; D[1 - k].nudge = 0; say(k ? "B LOADED - SYNC OFF" : "A LOADED - SYNC OFF"); }   /* (28.10.: the other deck followed this one and kept its last correction – up to 8 % off) */
    return 1;
}

/* ---- the BPM and the beat grid (draw) */
static void an_sample(deck_t *d, float x)
{
    /* two bands: the lows (~120 Hz: the kick, also the bass) and the highs (over ~5 kHz: hats, snares' crack). Their onsets
     * together find the beat – the lows alone put it on a syncopated bass line (28.10.: GROOVE's demo, a third off) */
    float k = 120.0f / d->frate * 6.2832f, k2 = 5000.0f / d->frate * 6.2832f; if (k > 1) k = 1; if (k2 > 1) k2 = 1;
    d->a_lp += (x - d->a_lp) * k; d->a_lp2 += (x - d->a_lp2) * k2;
    float hp = x - d->a_lp2;
    d->a_acc += d->a_lp * d->a_lp; d->a_acc2 += hp * hp;
    if (++d->a_n >= d->a_hop) {
        float e = vd_sqrt(d->a_acc / (float)d->a_n), e2 = vd_sqrt(d->a_acc2 / (float)d->a_n), on = e - d->a_prev, on2 = e2 - d->a_prev2;
        d->a_prev = e; d->a_prev2 = e2; d->a_acc = d->a_acc2 = 0; d->a_n = 0;
        if (d->nenv < NENV) d->env[d->nenv++] = (on > 0 ? on : 0) + 2.0f * (on2 > 0 ? on2 : 0);
    }
}
static void wf_sample(deck_t *d, float x)          /* the waveform: three bands by two one-pole splits (200 Hz, 2.5 kHz) */
{
    float k1 = 200.0f / d->frate * 6.2832f, k2 = 2500.0f / d->frate * 6.2832f; if (k2 > 1) k2 = 1;
    d->w_lp1 += (x - d->w_lp1) * k1; d->w_lp2 += (x - d->w_lp2) * k2;
    float lo = vd_absf(d->w_lp1), mi = vd_absf(d->w_lp2 - d->w_lp1), hi = vd_absf(x - d->w_lp2);
    if (lo > d->w_pl) d->w_pl = lo; if (mi > d->w_pm) d->w_pm = mi; if (hi > d->w_ph) d->w_ph = hi;
    {   /* AUTO GAIN: the loudness of the whole track – the lows below ~100 Hz count less (as the ear does), 0.4 s blocks,
         * silence (under -50 dB) left out; every deck is brought to the same level (-16 dB: mastered club music stays as it is), +12 / -12 dB at most */
        d->l_hp = 0.987f * (d->l_hp + x - d->l_px); d->l_px = x; d->l_acc += d->l_hp * d->l_hp;
        if (++d->l_n >= (int)(d->frate * 0.4f)) {
            float ms = d->l_acc / (float)d->l_n; d->l_acc = 0; d->l_n = 0;
            if (ms > 1e-5f) { d->l_sum += ms; d->l_cnt++; }
            if (d->l_cnt >= 12) d->ng = vd_clamp(0.16f / vd_sqrt(d->l_sum / (float)d->l_cnt), 0.25f, 4.0f);
        }
    }
    if (++d->w_n >= d->w_hop) {
        if (d->wfn < WFN) {
            float a = d->w_pl * 260.0f, b = d->w_pm * 240.0f, c = d->w_ph * 300.0f;
            d->wl[d->wfn] = (uint8_t)(a > 255 ? 255 : a); d->wm[d->wfn] = (uint8_t)(b > 255 ? 255 : b); d->wh[d->wfn] = (uint8_t)(c > 255 ? 255 : c);
            d->wfn++;
        }
        d->w_n = 0; d->w_pl = d->w_pm = d->w_ph = 0;
    }
}
static void an_finish(deck_t *d)                  /* the decoding is done: the search starts (an_work, a slice a frame) */
{
    float R = d->frate / (float)d->a_hop;        /* the envelope's rate (~200 Hz) */
    if (d->nenv < (int)(R * 8)) { d->an = 3; return; }   /* (under 8 s: no tempo) */
    d->a_ph = 1; d->a_L = (int)(60.0f * R / 180.0f); d->a_bl = d->a_L; d->a_best = 0;
}
static void an_work(deck_t *d)
{
    float R = d->frate / (float)d->a_hop; int n = d->nenv;
    if (d->a_ph == 1 && d->vtag > 0) {            /* (5.11.) our own tag: the tempo is known – the fine search +-0.5 % and the phase only */
        float bpm = d->vtag;
        d->a_bpm = bpm; d->a_b = bpm * 0.995f; d->a_hi = bpm * 1.005f; d->a_bt = bpm; d->a_bo = 0; d->a_best = -1; d->a_ph = 2;
        return;
    }
    if (d->a_ph == 1) {                           /* autocorrelation over 70 .. 180 BPM – 12 lags a frame */
        int l0 = (int)(60.0f * R / 180.0f), l1 = (int)(60.0f * R / 70.0f) + 1; if (l1 > 511) l1 = 511;
        for (int c = 0; c < 12 && d->a_L <= l1; c++, d->a_L++) {
            int L = d->a_L; float s = 0; for (int i = 0; i + L < n; i++) s += d->env[i] * d->env[i + L];
            d->a_ac[L] = s; if (s > d->a_best) { d->a_best = s; d->a_bl = L; }
        }
        if (d->a_L <= l1) return;
        int bl = d->a_bl; float lag = (float)bl, bpm;
        if (bl > l0 && bl < l1) { float a = d->a_ac[bl - 1], b = d->a_ac[bl], c = d->a_ac[bl + 1], den = a - 2 * b + c; if (den < 0) lag += 0.5f * (a - c) / den; }
        bpm = 60.0f * R / lag;
        if (d->tag > 0) { while (bpm < d->tag * 0.7f) bpm *= 2.0f; while (bpm > d->tag * 1.4f) bpm *= 0.5f; }   /* (the tag: which octave) */
        else { while (bpm < 85.0f) bpm *= 2.0f; while (bpm > 175.0f) bpm *= 0.5f; }   /* (DJ tempos: 85 .. 175) */
        /* (5.11.2026, Sven: Hummingbird showed 148, it is 117) a measure that stands to the tag as a syncopation does –
           5:4, 4:3, 3:2 or the other way round, a quiet intro's off-beat pattern – is that, not the tempo: the beat grid is
           sought around the tag then. Any other difference stays the measure's (28.10.: tags lie – a wrong tag wins only
           where the measure itself looks like a known mistake) */
        if (d->tag > 0) {
            static const float RQ[6] = { 1.25f, 0.8f, 1.3333f, 0.75f, 1.5f, 0.6667f };
            float r = bpm / d->tag;
            for (int q = 0; q < 6; q++) if (r > RQ[q] * 0.96f && r < RQ[q] * 1.04f) { bpm = d->tag; break; }   /* (+-4 %: the coarse lag is that rough – Hummingbird 1.28) */
        }
        d->a_bpm = bpm; d->a_b = bpm * 0.98f; d->a_hi = bpm * 1.02f; d->a_bt = bpm; d->a_bo = 0; d->a_best = -1; d->a_ph = 2;
        return;
    }
    /* the fine tempo and the phase: a comb of beats over the whole envelope, its tempo +-2 % (0.05 BPM steps, 4 a frame)
       and its offset searched */
    for (int c = 0; c < 4 && d->a_b <= d->a_hi + 1e-4f; c++, d->a_b += 0.05f) {
        float b = d->a_b, per = 60.0f * R / b;
        for (float off = 0; off < per; off += 0.5f) {
            float s = 0; for (float p = off; p < (float)(n - 1); p += per) { int i = (int)p; float f = p - (float)i; s += d->env[i] + (d->env[i + 1] - d->env[i]) * f; }
            if (s > d->a_best) { d->a_best = s; d->a_bt = b; d->a_bo = off; }
        }
    }
    if (d->a_b <= d->a_hi + 1e-4f) return;
    d->bpm = (float)(int)(d->a_bt * 100.0f + 0.5f) / 100.0f;
    d->beat0 = d->a_bo / R;
    d->a_ph = 0; d->an = 2;
}
static void dj_analyse(deck_t *d)
{
    if (d->t < 0) return;
    if (d->an != 1 && d->wf_done) { if (!d->an_saved) an_save(d); return; }   /* (done: kept on the card, once) */
    if (d->an == 1 && d->a_ph) { an_work(d); return; }   /* (the search: its slice is this frame's work) */
    /* the tempo: 40 s; the waveform: all – 0.2 s a frame (28.10.: 1.5 s took ~20 ms a frame on a PC, many times that on
       the VIBE, where the draw and the sound share one flow: the sound broke up while a track loaded) */
    /* (28.10., Sven: the waveforms took long – while no deck plays, more a frame; while one plays, little: the VIBE draws and
       plays in one flow, and the output buffer is 8 ms) */
    int quiet = !D[0].play && !D[1].play && D[0].fade < 0.0005f && D[1].fade < 0.0005f && st != ST_PLAY;
    uint32_t want = (uint32_t)(d->frate * 40.0f), budget = (uint32_t)(d->frate * (quiet ? 0.5f : 0.1f)), done = 0;   /* (28.10.: 1.5 s took 26-47 ms a frame on the VIBE) */
    while (done < budget) {
        if (d->act - d->ach < 16384 && d->apos < d->fend) {   /* read on (short reads too: 16 KB at least, whole MP3 frames) */
            if (d->ach > 0) { __builtin_memmove(d->ab, d->ab + d->ach, (size_t)(d->act - d->ach)); d->act -= d->ach; d->ach = 0; }
            int wait = 0;
            while (d->act < 16384 && d->apos < d->fend) {
                int m = DAB - d->act; if (m > d->fend - d->apos) m = d->fend - d->apos;
                if (!quiet && m > 16384) m = 16384;  /* (a deck plays: the SD read in small pieces too) */
                int got = m > 0 ? f_read(TR[d->t].name, d->apos, d->ab + d->act, m) : 0;
                if (got > 0) { d->act += got; d->apos += got; }
                else if (got == VIBE_MEDIA_PENDING) { wait = 1; break; }
                else if (got < 0) dj_gone(d);
                else d->apos = d->fend;
            }
            if (wait && d->act < 16384) break;      /* (not here yet: the next time – READY or the next frame) */
        }
        int avail = d->act - d->ach; if (avail <= 0) break;
        if (d->kind == 2) {
            int fr = avail / d->w_align; if (fr <= 0) break; if (fr > 4096) fr = 4096;
            const uint8_t *s0 = d->ab + d->ach; int bps = d->w_bits / 8;
            for (int i = 0; i < fr; i++) {
                const uint8_t *s = s0 + i * d->w_align; float a, b;
                if (d->w_bits == 16) { a = (int16_t)le16(s) * (1.0f / 32768.0f); b = d->w_ch > 1 ? (int16_t)le16(s + bps) * (1.0f / 32768.0f) : a; }
                else if (d->w_bits == 24) { a = (float)((int32_t)((uint32_t)s[0] << 8 | (uint32_t)s[1] << 16 | (uint32_t)s[2] << 24) >> 8) * (1.0f / 8388608.0f); b = a; }
                else { union { uint32_t u; float f; } v; v.u = le32(s); a = d->w_fmt == 3 ? v.f : (int32_t)v.u * (1.0f / 2147483648.0f); b = a; }
                if (d->a_frames + (uint32_t)i < want) an_sample(d, 0.5f * (a + b));
                wf_sample(d, 0.5f * (a + b));
            }
            d->ach += fr * d->w_align; d->a_frames += (uint32_t)fr; done += (uint32_t)fr;
        } else {
            mp3dec_frame_info_t fi; static float pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
            if (d->a_frames >= d->sk_next && d->skn < SEEKN) {   /* a seek point: this frame's byte, the frames before it */
                d->sk[d->skn].smp = d->a_frames; d->sk[d->skn].byte = d->apos - (d->act - d->ach); d->skn++;
                d->sk_next = d->a_frames + (uint32_t)(d->frate * 0.2f);
            }
            int s = mp3dec_decode_frame(&d->amp, d->ab + d->ach, avail, pcm, &fi);
            if (!fi.frame_bytes) { if (d->apos >= d->fend) break; d->ach = d->act; continue; }
            d->ach += fi.frame_bytes;
            int nf = s > 0 ? s : (fi.hz >= 32000 ? 1152 : 576);   /* (a frame without output – the start's reservoir – counts as silence, as in the deck) */
            for (int i = 0; i < nf; i++) { float x = i < s ? (fi.channels > 1 ? 0.5f * (pcm[2 * i] + pcm[2 * i + 1]) : pcm[i]) : 0.0f; if (d->a_frames + (uint32_t)i < want) an_sample(d, x); wf_sample(d, x); }
            d->a_frames += (uint32_t)nf; done += (uint32_t)nf;
        }
    }
    int end = d->apos >= d->fend && d->act - d->ach <= 0;
    if (d->an == 1 && (d->a_frames >= want || end)) an_finish(d);
    if (end || d->wfn >= WFN) d->wf_done = 1;
}

/* ---- the audio */
static void xf_go(deck_t *d, double q, double around) { d->xf_q = q; d->xf_len = around; d->xf_n = XF; }   /* a jump: from now the old way (frame q of the loop) fades out */
static inline void xf_old(const deck_t *d, float *a, float *b)
{
    double q = d->xf_q; if (d->xf_len > 0) while (q >= d->xf_len) q -= d->xf_len;
    uint32_t i = (uint32_t)q, j = i + 1; float f = (float)(q - i);
    if (d->xf_len > 0 && j >= (uint32_t)d->xf_len) j = 0;
    if (i >= LBN - 1) i = j = LBN - 1; else if (j >= LBN) j = LBN - 1;
    *a = d->LBL[i] + (d->LBL[j] - d->LBL[i]) * f; *b = d->LBR[i] + (d->LBR[j] - d->LBR[i]) * f;
}
static inline void ring_put(deck_t *d, float a, float b)
{
    d->RL[d->rw & (DRB - 1)] = a; d->RR[d->rw & (DRB - 1)] = b;
    if (d->lp && (double)d->rw >= d->lp_in && (double)d->rw < d->lp_max + XFR) { uint32_t i = d->rw - (uint32_t)d->lp_in; if (i < LBN) { d->LBL[i] = a; d->LBR[i] = b; } }
    d->rw++;
}
static int dj_decode(deck_t *d)                  /* frames added, 0 = nothing (wait / end) */
{
    double ref = d->lp == 2 ? d->gp : d->rp;    /* (looping: the ring runs ahead of the track's place underneath, not of the loop) */
    if (DRB - 8 - (int)(d->rw - (uint32_t)ref) < MINIMP3_MAX_SAMPLES_PER_FRAME) return 0;
    int avail = d->ct - d->ch;
    if (d->kind == 2) {
        int fr = avail / d->w_align;
        if (fr <= 0) { if (d->fpos >= d->fend) d->dec_end = 1; return 0; }
        if (fr > 1024) fr = 1024;
        int bps = d->w_bits / 8; int keep = w_bits, keep2 = w_fmt; w_bits = d->w_bits; w_fmt = d->w_fmt;   /* (wav_sample reads the globals) */
        for (int i = 0; i < fr; i++) {
            const uint8_t *s = d->cb + d->ch + i * d->w_align;
            float a = wav_sample(s), b = d->w_ch > 1 ? wav_sample(s + bps) : a;
            ring_put(d, a, b);
        }
        w_bits = keep; w_fmt = keep2;
        d->ch += fr * d->w_align;
        return fr;
    }
    if (avail <= 0 || (avail < 16384 && d->fpos < d->fend)) { if (avail <= 0 && d->fpos >= d->fend) d->dec_end = 1; return 0; }
    mp3dec_frame_info_t fi;
    int s = mp3dec_decode_frame(&d->mp3, d->cb + d->ch, avail, pcmbuf, &fi);
    if (!fi.frame_bytes) { if (d->fpos >= d->fend) d->dec_end = 1; else d->ch = d->ct; return 0; }
    d->ch += fi.frame_bytes;
    if (s <= 0) { int n = fi.hz >= 32000 ? 1152 : 576; for (int i = 0; i < n; i++) ring_put(d, 0, 0); return n; }   /* (as the analysis counts it: the frames stay in step) */
    for (int i = 0; i < s; i++) { float a = pcmbuf[i * fi.channels], b = fi.channels > 1 ? pcmbuf[i * fi.channels + 1] : a; ring_put(d, a, b); }
    return s;
}
static void dj_eq(deck_t *d)
{
    eq_shelf(&d->eq[0], 250, d->low, 0); eq_shelf(&d->eq[1], 4000, d->high, 1); eq_peak(&d->eq[2], 1000, d->mid, 0.7f);
    d->eq_dirty = 0;
}
static inline float herm(const float *x, uint32_t i, float f)   /* 4-point Hermite at i + f (i - 1 .. i + 2) */
{
    float a = x[(i - 1) & (DRB - 1)], b = x[i & (DRB - 1)], c = x[(i + 1) & (DRB - 1)], e = x[(i + 2) & (DRB - 1)];
    float c1 = 0.5f * (c - a), c2 = a - 2.5f * b + 2.0f * c - 0.5f * e, c3 = 0.5f * (e - a) + 1.5f * (b - c);
    return ((c3 * f + c2) * f + c1) * f + b;
}
#define JOG_T 0.06f                               /* NUDGE: the push follows the knob with this time (s) */
#define JOG_V 0.08f                               /* … at most ±8 % */
#define JOG_MAX 0.016f                            /* … and never more owed than that does in 0.2 s: it stops with the hand (29.10., Sven:
                                                     a whole turn of the MPK's knob owed 250 ms – seconds faster, as the pitch) */
static void dj_sync_keep(int k);
static void jog_push(deck_t *x, float sec) { x->jog = vd_clamp(x->jog + sec, -JOG_MAX, JOG_MAX); }
static void dj_sync_tick(int k)                  /* SYNC: the tempo of the other deck, and its beat, gently */
{
    deck_t *x = &D[k], *m = &D[1 - k];
    x->nudge = 0;
    if (!x->sync || m->t < 0 || x->bpm <= 0 || m->bpm <= 0 || x->an != 2 || m->an != 2) { x->nudge = 0; return; }   /* (no tempo to follow: no correction left over) */
    float r = dj_eff_bpm(m) / x->bpm, f = 1.0f;
    if (r > 1.5f) { r *= 0.5f; f = 0.5f; } else if (r < 0.75f) { r *= 2.0f; f = 2.0f; }
    x->pitch = vd_clamp(r - 1.0f, -0.25f, 0.25f);
    if (!x->play || !m->play) return;
    float bm = (dj_pos(m) - m->beat0) * m->bpm / 60.0f, bx = (dj_pos(x) - x->beat0) * x->bpm / 60.0f;
    if (x->bend != 0 || x->jog != 0 || x->scr) return;     /* (the DJ is pushing it: no correction against the hand) */
    if (x->jog_keep) { x->jog_keep = 0; dj_sync_keep(k); return; }   /* (pushed: where it is now is SYNC's) */
    float e = bm * (1.0f / f) - bx - x->soff; e -= (float)(int)e; if (e > 0.5f) e -= 1.0f; if (e < -0.5f) e += 1.0f;   /* (beats behind: +) */
    x->nudge = vd_clamp(e * 0.5f, -0.08f, 0.08f);   /* (what is left after the snap: held on the beat – and on the DJ's offset) */
}
static void dj_sync_nudge(int k, float det)     /* SYNC on: the knob pushes the deck as without SYNC; let go, SYNC keeps it where it is */
{
    deck_t *x = &D[k]; if (det == 0) return;
    if (x->t < 0 || !x->play) { say(k ? "B: NUDGE WHILE PLAYING" : "A: NUDGE WHILE PLAYING"); return; }
    jog_push(x, det * (x->bpm > 0 ? 0.6f / x->bpm : 0.005f)); x->jog_keep = 1;   /* (29.10., Sven: a short push, not SYNC slowly pulling it) */
    say(det > 0 ? (k ? "B NUDGE >>" : "A NUDGE >>") : (k ? "B NUDGE <<" : "A NUDGE <<"));
}
static void dj_jog(int k, float det)             /* no SYNC: the knob pushes the deck – 1 % of a beat a detent (5 ms without a tempo), while it turns */
{
    deck_t *x = &D[k];
    if (x->t < 0 || !x->play) { say(k ? "B: NUDGE WHILE PLAYING" : "A: NUDGE WHILE PLAYING"); return; }
    jog_push(x, det * (x->an == 2 && x->bpm > 0 ? 0.6f / x->bpm : 0.005f));   /* (seconds of the file: 1 % of its beat a detent) */
    say(det > 0 ? (k ? "B NUDGE >>" : "A NUDGE >>") : (k ? "B NUDGE <<" : "A NUDGE <<"));
}
static void dj_sync_keep(int k)                  /* the bend let go while synced: the offset as it is now is SYNC's from here on */
{
    deck_t *x = &D[k], *m = &D[1 - k];
    if (!x->sync || x->bpm <= 0 || m->bpm <= 0 || x->an != 2 || m->an != 2) return;
    float r = dj_eff_bpm(m) / x->bpm, f = 1.0f;
    if (r > 1.5f) f = 0.5f; else if (r < 0.75f) f = 2.0f;
    float bm = (dj_pos(m) - m->beat0) * m->bpm / 60.0f, bx = (dj_pos(x) - x->beat0) * x->bpm / 60.0f;
    float e = bm * (1.0f / f) - bx; e -= (float)(int)e; if (e > 0.5f) e -= 1.0f; if (e < -0.5f) e += 1.0f;
    x->soff = e; x->nudge = 0;
    static char msg[32]; int ms = (int)(e * 60000.0f / dj_eff_bpm(m) + (e < 0 ? -0.5f : 0.5f)); char n[12]; vg_itoa(ms, n);
    scopy(msg, k ? "B SYNC KEPT " : "A SYNC KEPT ", 32); int q = slen(msg); if (ms > 0) msg[q++] = '+'; for (int i = 0; n[i] && q < 28; i++) msg[q++] = n[i]; msg[q++] = ' '; msg[q++] = 'm'; msg[q++] = 's'; msg[q] = 0; say(msg);
}
static void dj_phase_snap(int k)                 /* SYNC pressed / the synced deck started: onto the other's beat at once */
{
    deck_t *x = &D[k], *m = &D[1 - k];
    if (!x->sync || !x->play || !m->play || x->bpm <= 0 || m->bpm <= 0 || x->an != 2 || m->an != 2) return;
    float r = dj_eff_bpm(m) / x->bpm, f = 1.0f;
    if (r > 1.5f) f = 0.5f; else if (r < 0.75f) f = 2.0f;
    float bm = (dj_pos(m) - m->beat0) * m->bpm / 60.0f, bx = (dj_pos(x) - x->beat0) * x->bpm / 60.0f;
    float e = bm * (1.0f / f) - bx - x->soff; e -= (float)(int)e; if (e > 0.5f) e -= 1.0f; if (e < -0.5f) e += 1.0f;
    double fr = (double)e * 60.0 / x->bpm * x->frate;   /* frames of the file: + ahead, - back */
    double back = (double)(DRB - 2048);              /* (back only as far as the ring still holds) */
    if (fr < 0 && -fr > back) fr += 60.0 / x->bpm * x->frate;   /* (too far back: a beat ahead instead) */
    if (x->rp + fr < 4) return;
    x->rp += fr; x->fade = 0;                        /* (a 4 ms fade in: no click) */
}
static void dj_audio(float *l, float *r, int n)
{
    float kf = vd_coef(0.004f, SR), kg = vd_coef(0.02f, SR);
    float fg[2], fk = 1.0f, fa1[2], fa2[2], fa3[2]; int fhp[2];
    for (int k = 0; k < 2; k++) {
        deck_t *d = &D[k]; if (d->eq_dirty) dj_eq(d); dj_sync_tick(k);
        d->fs += (d->flt - d->fs) * 0.35f; if (vd_absf(d->fs - d->flt) < 1e-4f) d->fs = d->flt;   /* (the filter glides: no steps) */
        /* -1 .. 0: low pass 20 kHz down to ~80 Hz; 0 .. +1: high pass 20 Hz up to ~10 kHz (Simper's SVF, Q 1) */
        float fc = d->fs < 0 ? 20000.0f * vd_exp2(d->fs * 8.0f) : 20.0f * vd_exp2(d->fs * 9.0f), ph = vd_minf(fc, SR * 0.45f) / SR * 0.5f;
        fg[k] = vd_sin(ph) / vd_sin(ph + 0.25f); fhp[k] = d->fs >= 0;
        fa1[k] = 1.0f / (1.0f + fg[k] * (fg[k] + fk)); fa2[k] = fg[k] * fa1[k]; fa3[k] = fg[k] * fa2[k];
    }
    float kn = vd_coef(0.5f, SR);                  /* (AUTO GAIN glides in half a second) */
    float ga = xfade < 0.5f ? 1.0f : 2.0f * (1.0f - xfade), gb = xfade > 0.5f ? 1.0f : 2.0f * xfade;   /* (both full in the middle) */
    float tv = C.vol * C.vol;
    int host = vibe_cue_host(), anycue = (D[0].cue && D[0].t >= 0) || (D[1].cue && D[1].t >= 0);
    float *cl = host ? vibe_cue(0) : 0, *cr = host ? vibe_cue(1) : 0;
    dj_split = !host && anycue && C.split && !fixed_set;
    lim_rel = 1.0f - vd_coef(0.2f, SR);
    for (int i = 0; i < n; i++) {
        float ml = 0, mr = 0, ql = 0, qr = 0;
        for (int k = 0; k < 2; k++) {
            deck_t *d = &D[k];
            float a = 0, b = 0, tg = d->play || d->scr ? 1.0f : 0.0f;   /* (2.3: a stopped record moved by hand sounds too) */
            d->fade = tg + (d->fade - tg) * kf;
            if (d->t >= 0 && d->kind && (d->play || d->fade > 0.0005f)) {
                float jv = 0;                     /* (NUDGE: pushed or braked while the knob turns, done ~0.1 s after – then the pitch alone) */
                if (d->jog != 0 && d->play) {
                    jv = vd_clamp(d->jog * (1.0f / JOG_T), -JOG_V, JOG_V);
                    if (jv > -0.01f && jv < 0.01f) jv = d->jog > 0 ? 0.01f : -0.01f;   /* (the last bit not as a long tail) */
                    d->jog -= jv / SR; if ((jv > 0) != (d->jog > 0)) d->jog = 0;
                }
                double ratio = (double)d->frate / SR * (1.0 + d->pitch + d->nudge + d->bend + jv);
                if (d->scr && d->lp != 2) {       /* (2.3) the hand on the record: it follows the frame under the finger in ~20 ms, up to 4x either way */
                    double v = (d->scr_f - d->rp) / (SR * 0.02), vm = 4.0 * d->frate / SR;
                    ratio = v < -vm ? -vm : v > vm ? vm : v;
                }
                if (d->lp == 1 && d->rp >= d->lp_out) {   /* (the loop is recorded: from now on it repeats – read a little past its end for the seam) */
                    while (d->rw < (uint32_t)d->lp_out + XFR) { if (!dj_decode(d)) break; }
                    d->lp = 2;
                }
                if (d->lp == 2 && d->lp_cyc < d->lp_out && d->rw >= (uint32_t)d->lp_out) d->lp_cyc = d->lp_out;   /* (doubled: the new part is read) */
                if (d->lp == 2 && d->rp >= d->lp_cyc) {
                    if (d->lp_exit && d->lp_cyc >= d->lp_out) {   /* (halved: on from where the stream waits – a whole number of loops on, in time) */
                        if (d->lp_max > d->lp_cyc) xf_go(d, d->rp - d->lp_in, 0);
                        d->lp = 0; d->lp_exit = 0; d->rp += d->lp_max - d->lp_cyc;
                    } else { xf_go(d, d->rp - d->lp_in, 0); d->rp -= d->lp_cyc - d->lp_in; }
                }
                if (d->lp == 2) {                                         /* from the loop's frames (the stream waits at its end) */
                    if (d->roll || d->rw < (uint32_t)d->lp_out + 4u) {   /* (ROLL: the track runs on underneath · doubled: its new part read, at the playing speed) */
                        while (d->rw < (uint32_t)d->gp + 4u) { if (!dj_decode(d)) break; }
                        d->gp += d->play ? ratio : ratio * d->fade;
                    }
                    double q = d->rp - d->lp_in; uint32_t i = (uint32_t)q; float f = (float)(q - i);   /* (q < the loop's length: it turned above) */
                    uint32_t i1 = i < LBN - 3 ? i : LBN - 3, i0 = i1 ? i1 - 1 : 0, i2 = i1 + 1, i3 = i1 + 2;   /* (past the end: the frames read past it, not the loop's start – that was the click; the seam itself is a crossfade) */
                    float c1 = 0.5f * (d->LBL[i2] - d->LBL[i0]), c2 = d->LBL[i0] - 2.5f * d->LBL[i1] + 2.0f * d->LBL[i2] - 0.5f * d->LBL[i3], c3 = 0.5f * (d->LBL[i3] - d->LBL[i0]) + 1.5f * (d->LBL[i1] - d->LBL[i2]);
                    a = ((c3 * f + c2) * f + c1) * f + d->LBL[i1];
                    c1 = 0.5f * (d->LBR[i2] - d->LBR[i0]); c2 = d->LBR[i0] - 2.5f * d->LBR[i1] + 2.0f * d->LBR[i2] - 0.5f * d->LBR[i3]; c3 = 0.5f * (d->LBR[i3] - d->LBR[i0]) + 1.5f * (d->LBR[i1] - d->LBR[i2]);
                    b = ((c3 * f + c2) * f + c1) * f + d->LBR[i1];
                    if (d->play) d->rp += ratio; else d->rp += ratio * d->fade;
                } else {
                if (d->scr) { double lo = (double)d->rw - DRB + 8; if (d->rp < lo) d->rp = lo; if (d->rp < 1) d->rp = 1; }   /* (backwards: only what the ring still holds) */
                while (d->rw < (uint32_t)d->rp + 4u) { if (!dj_decode(d)) break; }
                if (d->rw >= (uint32_t)d->rp + 4u) {
                    uint32_t ip = (uint32_t)d->rp; float fr = (float)(d->rp - ip);
                    a = herm(d->RL, ip, fr); b = herm(d->RR, ip, fr);
                    if (d->play) d->rp += ratio; else d->rp += ratio * d->fade;   /* (stopping: it slows into the fade) */
                } else if (d->dec_end && d->play) { d->play = 0; }   /* (the end: the deck stops) */
                d->gp = d->rp;
                }
                if (d->xf_n > 0) {                        /* the seam: the old way fades out under the new */
                    float w = (float)d->xf_n / XF, oa, ob; xf_old(d, &oa, &ob);
                    a = a * (1.0f - w) + oa * w; b = b * (1.0f - w) + ob * w;
                    d->xf_q += d->play ? ratio : ratio * d->fade; d->xf_n--;
                }
                if (d->low < -0.05f || d->low > 0.05f) { a = bq(&d->eq[0], &d->eqs[0][0], a); b = bq(&d->eq[0], &d->eqs[1][0], b); }
                if (d->high < -0.05f || d->high > 0.05f) { a = bq(&d->eq[1], &d->eqs[0][1], a); b = bq(&d->eq[1], &d->eqs[1][1], b); }
                if (d->mid < -0.05f || d->mid > 0.05f) { a = bq(&d->eq[2], &d->eqs[0][2], a); b = bq(&d->eq[2], &d->eqs[1][2], b); }
                for (int c = 0; c < 2; c++) {             /* the DJ filter, always running (at 0: a high pass at 20 Hz, inaudible – no click on and off) */
                    float x = c ? b : a, v3 = x - d->f2[c], v1 = fa1[k] * d->f1[c] + fa2[k] * v3, v2 = d->f2[c] + fa2[k] * d->f1[c] + fa3[k] * v3;
                    d->f1[c] = 2.0f * v1 - d->f1[c]; d->f2[c] = 2.0f * v2 - d->f2[c];
                    float y = fhp[k] ? x - fk * v1 - v2 : v2; if (c) b = y; else a = y;
                }
                { float tg = C.no_auto ? 1.0f : d->ng; d->gn = tg + (d->gn - tg) * kn; }   /* (AUTO VOL off: 0 dB, gliding there) */
                a *= d->fade * d->gn; b *= d->fade * d->gn;
            }
            float pk = vd_maxf(vd_absf(a), vd_absf(b)); if (pk > dj_pk_d[k]) dj_pk_d[k] = pk;
            if (d->cue) { ql += a; qr += b; }
            float g = d->vol * d->vol * (k ? gb : ga);
            ml += a * g; mr += b * g;
        }
        gv = tv + (gv - tv) * kg;
        ml *= gv; mr *= gv; limit(&ml, &mr);
        ANA[aw++ & 2047] = (ml + mr) * 0.5f;
        if (vd_absf(ml) > dj_pk_l) dj_pk_l = vd_absf(ml);
        if (vd_absf(mr) > dj_pk_r) dj_pk_r = vd_absf(mr);
        if (anycue) { float m = cue_mix; ql = ql * (1.0f - m) * 0.8f + ml * m; qr = qr * (1.0f - m) * 0.8f + mr * m; }
        else { ql = ml; qr = mr; }
        if (cl) { cl[i] = vd_clamp(ql, -1, 1); cr[i] = vd_clamp(qr, -1, 1); }
        if (dj_split) { l[i] = 0.5f * (ml + mr); r[i] = vd_clamp(0.5f * (ql + qr), -1, 1); }   /* SPLIT: master mono L, cue mono R */
        else { l[i] = ml; r[i] = mr; }
    }
}

/* ---- the controls */
static void dj_say_pitch(int k) { deck_t *d = &D[k]; char v[12]; vg_ftoa(d->pitch * 100.0f, 1, v); int q = 0; dj_msg[q++] = k ? 'B' : 'A'; dj_msg[q++] = ' '; const char *p = "PITCH "; for (int i = 0; p[i]; i++) dj_msg[q++] = p[i]; if (d->pitch >= 0) dj_msg[q++] = '+'; for (int i = 0; v[i] && q < 34; i++) dj_msg[q++] = v[i]; dj_msg[q++] = ' '; dj_msg[q++] = '%'; dj_msg[q] = 0; say(dj_msg); }
static void dj_phase_snap(int k);
static double beat_frames(const deck_t *d) { return d->bpm > 0 ? 60.0 / d->bpm * d->frate : 0; }
static double snap_beat(const deck_t *d, double f, int up)   /* the nearest beat (up: the next one at or after f) */
{
    double bf = beat_frames(d), b0 = (double)d->beat0 * d->frate;
    if (bf <= 0 || d->an != 2) return f;
    double n = (f - b0) / bf; n = up ? (double)(long long)(n + 0.999) : (double)(long long)(n + (n >= 0 ? 0.5 : -0.5));
    double r = b0 + n * bf; return r < 0 ? 0 : r;
}
static void dj_seek(int k, double f)             /* to frame f of the file: WAV exact, MP3 from the seek point before it (decoded up to f) */
{
    deck_t *d = &D[k];
    if (d->t < 0) return;
    d->lp = 0; d->lp_exit = 0; d->roll = 0; d->xf_n = 0;
    if (f < 0) f = 0;
    if (d->kind == 2) {
        uint32_t fi = (uint32_t)f;
        d->fpos = d->fstart + (int)fi * d->w_align; d->ch = d->ct = 0; dj_refill(d, 65536);
        d->rw = fi;
    } else {
        int i = d->skn - 1; while (i >= 0 && (double)d->sk[i].smp > f) i--;
        if (i > 0) i--;                          /* (one earlier: the bit reservoir is filled by the time we get there) */
        uint32_t smp = i >= 0 ? d->sk[i].smp : 0; int byte = i >= 0 ? d->sk[i].byte : d->fstart;
        mp3dec_init(&d->mp3);
        d->fpos = byte; d->ch = d->ct = 0; dj_refill(d, 65536);
        d->rw = smp;
    }
    d->rp = f; d->dec_end = 0; d->fade = 0;      /* (a 4 ms fade in: no click) */
    if (d->play && d->sync) dj_phase_snap(k);
}
static void dj_jump(int k, int dir)              /* ◀◀ / ▶▶: a bar (4 beats) back / on – in time; without a tempo 5 s (28.10., Sven) */
{
    deck_t *d = &D[k];
    if (d->t < 0) return;
    double bf = beat_frames(d), step = bf > 0 && d->an == 2 ? bf * 4 : d->frate * 5.0, f = d->rp + dir * step;
    if (d->kind != 2 && d->skn > 0 && f > (double)d->sk[d->skn - 1].smp) { say("NOT READ THAT FAR YET"); return; }   /* (MP3: only where the seek points reach) */
    dj_seek(k, f < 0 ? 0 : f);
}
static uint32_t cue_held[2][2];                  /* a CUE pad held: since when (a controller without SHIFT – an MPK – clears it so) */
static void dj_hotcue(int k, int n)              /* empty: set it (on the nearest beat); set: jump there; SHIFT: clear it */
{
    deck_t *d = &D[k]; static char m[32];
    if (d->t < 0) return;
    if (shift_held) { d->hc_set[n] = 0; cues_keep(d); m[0] = k ? 'B' : 'A'; scopy(m + 1, n ? " CUE 2 CLEARED" : " CUE 1 CLEARED", 20); say(m); return; }
    if (!d->hc_set[n]) { d->hc[n] = d->rp; d->hc_set[n] = 1;   /* (exactly where it was pressed – Sven 28.10.) */ cues_keep(d); m[0] = k ? 'B' : 'A'; scopy(m + 1, n ? " CUE 2 SET - SAVED" : " CUE 1 SET - SAVED", 24); say(m); return; }
    dj_seek(k, d->hc[n]);
}
static void dj_loop_say(const deck_t *d)
{
    double bf = beat_frames(d); int e = bf > 0 ? (int)((d->lp_out - d->lp_in) / bf * 8.0 + 0.5) : 0;   /* (eighths of a beat) */
    static const char *const F[4] = { "LOOP 1/8 BEAT", "LOOP 1/4 BEAT", "", "LOOP 1/2 BEAT" };
    if (e > 0 && e < 8 && F[e - 1][0]) { say(F[e - 1]); return; }
    char n[8]; vg_itoa(e / 8, n); scopy(dj_msg, "LOOP ", 40); scopy(dj_msg + 5, n, 8); scopy(dj_msg + slen(dj_msg), e == 8 ? " BEAT" : " BEATS", 8); say(dj_msg);
}
static void dj_loop_start(deck_t *d, double in, double len)
{
    d->lp_in = in; d->lp_out = d->lp_cyc = d->lp_max = in + len; d->gp = d->rp;
    for (uint32_t f = (uint32_t)d->lp_in; f < d->rw && f < (uint32_t)d->lp_out; f++) { uint32_t i = f - (uint32_t)d->lp_in; d->LBL[i] = d->RL[f & (DRB - 1)]; d->LBR[i] = d->RR[f & (DRB - 1)]; }   /* (what is decoded already) */
    d->lp = 1; d->lp_exit = 0;
}
static void dj_loop(int k, int beats)            /* a loop of 4 / 8 beats from the next beat; again: out of it at its end */
{
    deck_t *d = &D[k];
    if (d->roll) return;
    if (d->lp) { if (d->lp == 1) { d->lp = 0; say("LOOP OFF"); } else { d->lp_exit = 1; say("LOOP: OUT AT ITS END"); } return; }
    if (d->t < 0 || !d->play) { say("PLAY FIRST, THEN LOOP"); return; }
    double bf = beat_frames(d);
    if (bf <= 0 || d->an != 2) { say("NO BPM YET"); return; }
    if (bf * beats + XFR >= LBN) { say("LOOP TOO LONG"); return; }
    dj_loop_start(d, snap_beat(d, d->rp, 1), bf * beats); d->lp_beats = beats;
    say(beats == 4 ? "LOOP 4 BEATS" : "LOOP 8 BEATS");
}
static void dj_loop_size(int k, int up)          /* SHIFT + LOOP 4 / 8, a loop running: half / double (from its start, in time) */
{
    deck_t *d = &D[k]; double bf = beat_frames(d), len = d->lp_out - d->lp_in;
    if (!d->lp || d->roll || d->lp_exit || d->lp_cyc < d->lp_out) return;   /* (doubling still reads: one at a time) */
    if (!up) {
        if (len < bf / 8.0 * 1.5) { say("LOOP: 1/8 BEAT IS THE SHORTEST"); return; }
        d->lp_out = d->lp_cyc = d->lp_in + len / 2;
        if (d->lp == 1) d->lp_max = d->lp_out;   /* (still recording: no further) */
        else if (d->rp >= d->lp_out) { xf_go(d, d->rp - d->lp_in, 0); d->rp -= len / 2; }   /* (in the second half: the same place in the first) */
    } else {
        if (len * 2 + XFR >= LBN) { say("LOOP TOO LONG"); return; }
        double o = d->lp_in + len * 2;
        if (d->lp == 1 || o <= d->lp_max) { d->lp_out = d->lp_cyc = o; if (o > d->lp_max) d->lp_max = o; }   /* (recording, or read before: at once) */
        else { d->lp_out = d->lp_max = o; d->gp = d->rw; }   /* (the new part is read from the stream, as it would play; it turns at the old end until then) */
    }
    dj_loop_say(d);
}
static void dj_roll(int k, int on, int div)      /* SHIFT + LOOP 4 / 8 held, no loop: ROLL 1/2 / 1/4 beat from the next one; let go: where the track would be */
{
    deck_t *d = &D[k];
    if (!on) { if (d->roll) { if (d->lp == 2) xf_go(d, d->rp - d->lp_in, d->lp_cyc - d->lp_in); d->lp = 0; d->roll = 0; d->rp = d->gp; } return; }
    if (d->lp || d->t < 0 || !d->play) return;
    double bf = beat_frames(d);
    if (bf <= 0 || d->an != 2) { say("NO BPM YET"); return; }
    double len = bf / div, b0 = (double)d->beat0 * d->frate, n = (d->rp - b0) / len; n = (double)(long long)(n + 0.999);
    dj_loop_start(d, b0 + n * len, len); d->roll = 1; d->lp_beats = div == 2 ? 4 : 8;
    say(div == 2 ? "ROLL 1/2 BEAT" : "ROLL 1/4 BEAT");
}
static void dj_rewind(int k)                     /* back to the start – the waveform and the tempo stay (nothing measured again) */
{
    deck_t *d = &D[k];
    if (d->t < 0) return;
    mp3dec_init(&d->mp3);
    d->fpos = d->fstart; d->ch = d->ct = 0; dj_refill(d, 16384);   /* (small: the SD read is in the one flow – dj_service reads on) */
    d->rw = 0; d->rp = 0; d->dec_end = 0; d->fade = 0; d->lp = 0; d->lp_exit = 0; d->roll = 0; d->xf_n = 0;
    __builtin_memset(d->RL, 0, sizeof d->RL); __builtin_memset(d->RR, 0, sizeof d->RR);
    say(k ? "B TO THE START" : "A TO THE START");
}
static int dj_playing(int k) { return D[k].t >= 0 && D[k].play; }
static void dj_play(int k)
{
    deck_t *d = &D[k];
    if (d->t < 0) { say(k ? "B: LOAD A TRACK FIRST" : "A: LOAD A TRACK FIRST"); return; }
    if (!d->play && d->dec_end && d->rw <= (uint32_t)d->rp + 4u) dj_rewind(k);   /* (at its end: from the top again) */
    d->play = !d->play;
    if (d->play && d->sync) { dj_sync_tick(k); dj_phase_snap(k); }   /* (a synced deck starts on the beat) */
}
static void dj_load_sel(int k)                   /* the selected row: a track into deck k, a folder opens */
{
    if (sel >= nvw) return;
    if (VW[sel].kind == 3) { say("SPOTIFY: IN THE LIST, NOT ON A DECK"); return; }
    if (VW[sel].kind != 0) { activate(sel); return; }
    if (dj_load(k, VW[sel].idx)) { say(k ? "LOADED INTO B" : "LOADED INTO A"); focus_play = k; }   /* (the list stays: the next one for the other deck) */
}
static void dj_sync(int k)
{
    deck_t *d = &D[k];
    if (d->sync) { d->sync = 0; d->nudge = 0; say(k ? "B SYNC OFF" : "A SYNC OFF"); return; }
    if (d->an != 2 || D[1 - k].an != 2) { say(d->an == 1 || D[1 - k].an == 1 ? "BPM IS BEING MEASURED" : "NO BPM"); return; }
    d->sync = 1; D[1 - k].sync = 0; d->soff = 0; d->jog = 0;  /* (one follows the other; on the beat, no offset yet) */
    dj_sync_tick(k); dj_phase_snap(k);             /* the tempo now, the beat now */
    say(k ? "B FOLLOWS A" : "A FOLLOWS B");
}
static void dj_deck_key(int k, int what)         /* 0 PLAY, 1 PHONES, 2 SYNC, 3 TO THE START */
{
    if (what == 3) { dj_rewind(k); if (D[k].play && D[k].sync) dj_phase_snap(k); return; }
    if (what == 0) dj_play(k);
    else if (what == 1) { D[k].cue = !D[k].cue; say(D[k].cue ? (k ? "B IN THE HEADPHONES" : "A IN THE HEADPHONES") : (k ? "B OUT OF THE HEADPHONES" : "A OUT OF THE HEADPHONES")); }
    else if (what == 2) dj_sync(k);
    else dj_load_sel(k);
}
static int dj_free_deck(void) { return !D[0].play && D[1].play ? 0 : D[0].play && !D[1].play ? 1 : D[0].t < 0 ? 0 : D[1].t < 0 ? 1 : (D[0].play ? -1 : 0); }
/* FOCUS ids (SDK 10) – see deck_panel */
enum { FO_LOAD, FO_PLAY, FO_PHONES, FO_SYNC, FO_PITCH, FO_VOL, FO_FLT, FO_MID, FO_HIGH, FO_LOW, FO_START, FO_BACK, FO_FWD, FO_CUEPT, FO_POS, FO_BPM, FO_XFADE = 40, FO_CUEMIX, FO_AUTO };
static int browse_to = -1;                       /* the list opened from a deck's LOAD (FOCUS): ENTER loads into it */
static int dj_list_close(void) { if (!browse) return 0; browse = 0; browse_to = -1; return 1; }
static void dj_focus_press(int id)
{
    if (id == FO_AUTO) { C.no_auto ^= 1; cfg_touch(); say(C.no_auto ? "AUTO VOL OFF" : "AUTO VOL ON - EVERY TRACK EQUALLY LOUD"); return; }
    int k = id >= 16, f = id & 15;
    if (f == FO_LOAD) { browse = 1; browse_to = k; say(k ? "CHOOSE - ENTER LOADS INTO B" : "CHOOSE - ENTER LOADS INTO A"); }
    else if (f == FO_PLAY) dj_play(k);
    else if (f == FO_PHONES) dj_deck_key(k, 1);
    else if (f == FO_SYNC) dj_sync(k);
    else if (f == FO_START) dj_deck_key(k, 3);
    else if (f == FO_BACK) dj_jump(k, -1);
    else if (f == FO_FWD) dj_jump(k, 1);
    else if (f == FO_CUEPT) dj_hotcue(k, 0);
}
static int dj_bpm_editing(void) { int f = vf_focus(); return vf_editing() && f >= 0 && f < 32 && (f & 15) == FO_BPM ? f >= 16 : -1; }   /* the deck whose BPM is being set, -1 */
static void dj_bpm_set(int k)                    /* (5.11.2026, Sven) the BPM set by hand: kept for this track (the analysis file, the
                                                    lists), the beat grid keeps its first beat */
{
    deck_t *d = &D[k];
    if (d->t < 0 || d->an != 2) return;
    d->bpm = (float)(int)(d->bpm * 100.0f + 0.5f) / 100.0f;
    d->an_saved = 0; TR[d->t].bpm = d->bpm;      /* (dj_analyse keeps it – an_save – once the waveform is done) */
    char b[24] = "BPM "; vg_ftoa(d->bpm, 1, b + 4); scopy(b + slen(b), " - KEPT", 24 - slen(b)); say(b);
}
static void dj_focus_changed(int id)             /* (the value is written already: what follows from it) */
{
    if (id < 32 && (id & 15) == FO_POS) {        /* the overview turned: to that position (MP3: as far as it is read) */
        deck_t *d = &D[id >= 16]; double f = (double)d->nav * d->frate;
        if (d->kind != 2 && d->skn > 0 && f > (double)d->sk[d->skn - 1].smp) { f = (double)d->sk[d->skn - 1].smp; d->nav = (float)(f / d->frate); }
        dj_seek(id >= 16, f); return;
    }
    if (id < 32 && (id & 15) == FO_BPM) { dj_bpm_set(id >= 16); return; }
    if (id == FO_XFADE) { dj_hot = 2; dj_hot_t = ms_now; return; }
    if (id == FO_CUEMIX) { dj_hot = 3; dj_hot_t = ms_now; return; }
    int k = id >= 16, f = id & 15; deck_t *d = &D[k];
    if (f == FO_PITCH) {
        if (d->sync) { float was = d->pitch; dj_sync_tick(k); float det = (was - d->pitch) / 0.001f; dj_sync_nudge(k, det); }   /* (synced: the turn nudges the beat, the tempo stays the other's) */
        else dj_say_pitch(k);
    }
    else if (f == FO_VOL) dj_hot = k;
    else if (f == FO_FLT) { if (vd_absf(d->flt) < 0.011f) d->flt = 0; dj_hot = 4 + k; }
    else if (f == FO_MID) { d->eq_dirty = 1; dj_hot = 8 + k; }
    else if (f == FO_HIGH) { d->eq_dirty = 1; dj_hot = 6 + k; }
    else if (f == FO_LOW) { d->eq_dirty = 1; dj_hot = 10 + k; }
    dj_hot_t = ms_now;
}
#define WY0 34
#define WH 218                                    /* the waveforms' height (34 .. 252) */
#define WW 75                                     /* their width – 28.10.: side by side in the middle, the decks' panels outside; 29.10.: the two level meters between them */
#define PXA 4                                     /* deck A's panel (148 wide), the waveforms A and B, deck B's panel */
#define WXA 156
#define WXB 251
#define MXA 233                                   /* the level meters, upright, A and B side by side – to compare them (29.10., Sven: as on a mixer) */
#define MXB 242
#define MW 7
#define PXB 328
static int dj_event(const vibe_event *e)         /* 1 = taken */
{
    vf_enable(!browse);
    if (!browse && vf_focus() < 0 && e->type == VIBE_EV_BUTTON && e->down && (e->id == VB_ENTER || e->id == VB_DATA_PUSH)) {
        vf_set_focus(FO_LOAD); say("DATA: MOVE - PRESS: USE"); return 1;   /* (28.10.: the wheel pressed first showed the list – stuck on an MPK) */
    }
    if (dj_bpm_editing() >= 0 && e->type == VIBE_EV_BUTTON && (e->id == VB_K3 || e->id == VB_K4)) {   /* (5.11.) the BPM being set: K3 halves, K4 doubles it
                                                    (SHIFT + DATA is the SDK's: the soft keys) */
        if (e->down) { deck_t *d = &D[dj_bpm_editing()]; float nb = e->id == VB_K4 ? d->bpm * 2.0f : d->bpm * 0.5f;
                       if (nb >= 40.0f && nb <= 250.0f) { d->bpm = nb; dj_bpm_set(dj_bpm_editing()); } else say("BPM: 40 .. 250"); }
        return 1;
    }
    { int id = -1, r = vf_event(e, &id);          /* FOCUS first: DATA walks, ENTER uses (once the frame is there) */
      if (r == VF_PRESS) dj_focus_press(id); else if (r == VF_CHANGED) dj_focus_changed(id);
      if (r != VF_NO) return 1; }
    if (e->type == VIBE_EV_TOUCH && !browse) {    /* (2.3, Sven 3.11.) the waveforms as records: held and dragged, the deck follows the hand
                                                     (scratch, backwards too, a stopped deck as well); let go: it plays on as before; dragged
                                                     far: it jumps there; a short tap: to that place */
        static int sk = -1;
        int x = VIBE_TOUCH_X(e), y = VIBE_TOUCH_Y(e);
        if (e->id == VIBE_TOUCH_DOWN) {
            int k = x >= WXA && x < WXA + WW ? 0 : x >= WXB && x < WXB + WW ? 1 : -1;
            if (k < 0 || y < WY0 || y >= WY0 + WH || D[k].t < 0 || D[k].lp == 2 || D[k].frate <= 0) { sk = -1; return 1; }
            deck_t *d = &D[k]; sk = k;
            d->scr = 1; d->scr_y = y; d->scr_mv = 0; d->scr_f = d->rp; d->scr_ms = ms_now; d->jog = 0; d->nudge = 0;
            return 1;
        }
        if (sk < 0) return 1;
        deck_t *d = &D[sk]; float pps = (float)(WH / 2) / (3.0f * (1.0f + d->pitch));   /* (as wave_v draws it: what comes is below) */
        double end = (double)d->dur * d->frate;
        if (e->id == VIBE_TOUCH_MOVE) {
            int dy = y - d->scr_y; d->scr_y = y; d->scr_mv += dy < 0 ? -dy : dy;
            double f = d->scr_f - (double)dy / pps * d->frate; if (f < 0) f = 0; if (end > 0 && f > end - 1) f = end - 1;
            d->scr_f = f;
            if (f < (double)d->rw - DRB + 2048 || f > d->rp + 2.0 * d->frate) { dj_seek(sk, f); d->scr = 1; d->scr_f = f; }   /* (beyond what the hand can reach: a jump) */
            return 1;
        }
        if (d->scr_mv < 4 && ms_now - d->scr_ms < 300u) {   /* a tap: to the place tapped */
            double f = d->rp + (double)(y - (WY0 + WH / 2)) / pps * d->frate; if (f < 0) f = 0; if (end > 0 && f > end - 1) f = end - 1;
            dj_seek(sk, f);
        }
        d->scr = 0; sk = -1;
        return 1;
    }
    if (e->type == VIBE_EV_TOUCH) {              /* (SDK 18, the plugin's window) the list open: a row clicked is chosen, the chosen one again = ENTER */ /* (rows from y 57, 19 high – browser()) */
        if (browse && e->id != VIBE_TOUCH_UP && VIBE_TOUCH_X(e) >= 462 && nvw > 10) {   /* the bar at the right edge: clicked or dragged, the list follows */
            int y = VIBE_TOUCH_Y(e) - 57; sel = (int)(vd_clamp(y / 190.0f, 0, 1) * (nvw - 1) + 0.5f); return 1;
        }
        if (browse && e->id == VIBE_TOUCH_DOWN) {
            int y = VIBE_TOUCH_Y(e) - 57, top = sel - 4; if (top > nvw - 10) top = nvw - 10; if (top < 0) top = 0;
            if (y >= 0 && y / 19 < 10 && top + y / 19 < nvw) {
                if (top + y / 19 != sel) sel = top + y / 19;
                else { vibe_event en = { VIBE_EV_BUTTON, VB_ENTER, 1, { 0, 0, 0 }, 0 }; dj_event(&en); }
            }
        }
        return 1;
    }
    if (e->type == VIBE_EV_ENCODER) {
        float v = e->value; deck_t *a = &D[0], *b = &D[1];
        switch (e->id) {
            case VE_1: if (shift_held) { a->flt = vd_clamp(a->flt + v * 0.02f, -1, 1); if (vd_absf(a->flt) < 0.011f) a->flt = 0; dj_hot = 4; } else { a->vol = vd_clamp(a->vol + v * 0.02f, 0, 1); dj_hot = 0; } break;
            case VE_3: if (shift_held) { b->flt = vd_clamp(b->flt + v * 0.02f, -1, 1); if (vd_absf(b->flt) < 0.011f) b->flt = 0; dj_hot = 5; } else { b->vol = vd_clamp(b->vol + v * 0.02f, 0, 1); dj_hot = 1; } break;
            case VE_2: case VE_4: { int k = e->id == VE_4; deck_t *d = &D[k];   /* (29.10., Sven: nudge always, SHIFT the pitch; HIGH through FOCUS) */
                if (!shift_held) { if (d->sync) dj_sync_nudge(k, v); else dj_jog(k, v); }
                else if (d->sync) say(k ? "B SYNC: THE TEMPO IS A'S" : "A SYNC: THE TEMPO IS B'S");
                else { d->pitch = vd_clamp(d->pitch + v * 0.001f, -0.25f, 0.25f); dj_say_pitch(k); } } break;
            case VE_A: if (browse && !shift_held) {   /* (the list open: A scrolls it, as DATA – the crossfader again once it closes) */
                    list_acc += v; while (list_acc >= 1) { list_acc -= 1; if (sel < nvw - 1) sel++; } while (list_acc <= -1) { list_acc += 1; if (sel > 0) sel--; }
                } else if (shift_held) { a->mid = vd_clamp(a->mid + v, -30, 6); a->eq_dirty = 1; dj_hot = 8; } else { xfade = vd_clamp(xfade + v * 0.02f, 0, 1); dj_hot = 2; } break;
            case VE_B: if (shift_held) { b->mid = vd_clamp(b->mid + v, -30, 6); b->eq_dirty = 1; dj_hot = 9; } else { cue_mix = vd_clamp(cue_mix + v * 0.05f, 0, 1); dj_hot = 3; } break;
            case VE_DATA: browse = 1; list_acc += v; while (list_acc >= 1) { list_acc -= 1; if (sel < nvw - 1) sel++; } while (list_acc <= -1) { list_acc += 1; if (sel > 0) sel--; } break;
        }
        dj_hot_t = ms_now;
        return 1;
    }
    if (e->type == VIBE_EV_PAD) {
        if (e->id >= VIBE_PADS) return 1;        /* (an id out of range: nothing – vibe-check sends such) */
        int k = (e->id - 8) / 4, w = (e->id - 8) % 4;
        if (!e->down && e->id >= 8 && w < 2) { cue_held[k][w] = 0; return 1; }
        if (!e->down) { if (e->id >= 8 && w >= 2 && D[k].roll && D[k].lp_beats == (w == 2 ? 4 : 8)) dj_roll(k, 0, 0); return 1; }   /* (ROLL: let go) */
        int tap = shift_tap && ms_now - shift_tap < 1500u; shift_tap = 0;   /* (SHIFT tapped just before: as if held) */
        if (tap) shift_held = 1;
        if (e->id < 8) dj_deck_key(e->id / 4, e->id % 4);
        else if (w < 2) { dj_hotcue(k, w); cue_held[k][w] = !shift_held && D[k].hc_set[w] ? ms_now | 1u : 0; }   /* 9-12 A, 13-16 B: CUE 1, CUE 2, LOOP 4, LOOP 8 (a CUE held 1 s: cleared) */
        else if (!shift_held) dj_loop(k, w == 2 ? 4 : 8);
        else if (D[k].lp) dj_loop_size(k, w == 3);   /* (SHIFT, a loop: half / double) */
        else dj_roll(k, 1, w == 2 ? 2 : 4);      /* (SHIFT, none: ROLL while held) */
        if (tap) shift_held = 0;
        return 1;
    }
    if (e->type == VIBE_EV_BUTTON && (e->id == VB_OCT_DOWN || e->id == VB_OCT_UP || e->id == VB_LEFT || e->id == VB_RIGHT)) {   /* pitch bend while held */
        deck_t *d = &D[e->id == VB_OCT_DOWN || e->id == VB_OCT_UP ? 0 : 1];
        d->bend = !e->down ? 0.0f : (e->id == VB_OCT_DOWN || e->id == VB_LEFT) ? -0.04f : 0.04f;
        if (!e->down && d->sync) dj_sync_keep(d == &D[1]);   /* (synced: where the bend left it is kept) */
        return 1;
    }
    if (e->type != VIBE_EV_BUTTON || !e->down) return 0;
    if (browse) switch (e->id) {                /* the list open: the soft keys load, open, go back, close */
        case VB_F3: case VB_K3: dj_load_sel(0); return 1;   /* (where SYNC A / SYNC B are on the decks: LOAD A / LOAD B; K3 / K4 – 5.11.) */
        case VB_F4: case VB_K4: dj_load_sel(1); return 1;
        case VB_K1: dj_play(0); return 1;           /* (5.11., Sven: K1 / K2 = PLAY A / B everywhere – from the list too) */
        case VB_K2: dj_play(1); return 1;
        case VB_F1: dj_play(0); return 1;           /* (PLAY stays where it is: start while choosing) */
        case VB_F6: dj_play(1); return 1;
        case VB_F2: if (cwd[0]) go_up(); return 1;
        case VB_F5: case VB_RETURN: browse = 0; browse_to = -1; return 1;
        case VB_ENTER: case VB_DATA_PUSH: {
            if (sel < nvw && VW[sel].kind != 0) { activate(sel); return 1; }
            if (browse_to >= 0) { dj_load_sel(browse_to); browse = 0; browse_to = -1; return 1; }   /* (from LOAD: into that deck, and back) */
            int k = dj_free_deck(); if (k < 0) { say("BOTH DECKS PLAY"); return 1; }
            dj_load_sel(k); return 1; }
    }
    if (shift_held && (e->id == VB_F1 || e->id == VB_F6)) { dj_deck_key(e->id == VB_F1 ? 0 : 1, 3); return 1; }   /* SHIFT + PLAY: to the start */
    switch (e->id) {
        case VB_F1: dj_play(0); return 1;
        case VB_F2: dj_deck_key(0, 1); return 1;
        case VB_F3: dj_sync(0); return 1;
        case VB_F4: dj_sync(1); return 1;
        case VB_F5: dj_deck_key(1, 1); return 1;
        case VB_F6: dj_play(1); return 1;
        case VB_K1: dj_play(0); return 1;          /* (5.11.2026, Sven: on the decks K1 / K2 = PLAY A / B, in the list LOAD A / B) */
        case VB_K2: dj_play(1); return 1;
        case VB_K3: C.no_auto ^= 1; cfg_touch(); say(C.no_auto ? "AUTO VOL OFF" : "AUTO VOL ON - EVERY TRACK EQUALLY LOUD"); return 1;
        case VB_ENTER: case VB_DATA_PUSH: browse = 1; return 1;   /* (the list) */
        case VB_RETURN: dj_toggle(); return 1;   /* ↩: back to the PLAYER's list, where the tracks are chosen (the decks play on) */
    }
    return 0;
}

/* ---- the display (28.10., Sven): the waveforms run vertically at the sides – deck A on the left, deck B on the right, both
 * running upward (what comes is below); the playhead across the middle, the beat
 * grid with the bars marked, in the deck's colour: low the colour, mid lighter, high ivory (29.10.: theme.h), what has played dimmer. Between them the two decks:
 * title, BPM, pitch, time left, the whole track small, PLAY / PHONES / SYNC. Under all: the crossfader and the cue mix.
 * The list opens over the middle when DATA turns or ENTER is pressed, and stays until CLOSE (F5) or RETURN – nothing
 * jumps by itself. In it F3 / F4 load: LOAD A, LOAD B. */
static void wave_v(int k, int x0)
{
    deck_t *d = &D[k]; uint32_t col = k ? C_DECK_B : C_DECK_A; int up = 1;   /* (both run upward: what comes is below – Sven 28.10.) */
    vg_rect(x0, WY0, WW, WH, C_BG);
    int mid = WY0 + WH / 2, cx = x0 + WW / 2, half = WW / 2 - 3;
    vg_round(x0 + 3, WY0 + 3, 20, 18, 4, col); vg_text(k ? "B" : "A", x0 + 13, WY0 + 4, VG_BOLD, C_ON, VG_CENTER);
    if (d->t < 0) { vg_rect(cx, WY0, 1, WH, C_BUTTON); vg_rect(x0, mid, WW, 2, C_BUTTON); return; }
    float pos = dj_pos(d), span = 3.0f * (1.0f + d->pitch);   /* (seconds of the file above and below: both decks show the same real time) */
    float pps = (float)(WH / 2) / span;
    if (d->an == 2 && d->bpm > 0) {               /* the beat grid, under the wave */
        float beat = 60.0f / d->bpm, b0 = d->beat0;
        for (int n = (int)((pos - span - b0) / beat) - 1; ; n++) {
            float t = b0 + n * beat; if (t > pos + span + beat) break; if (t < 0) continue;
            int y = mid + (int)((t - pos) * pps) * (up ? 1 : -1);
            if (y < WY0 || y >= WY0 + WH) continue;
            int bar = ((n % 4) + 4) % 4 == 0;
            vg_rect(x0, y, WW, 1, bar ? C_BORDER : C_GRID);
            if (bar) vg_rect(up ? x0 + WW - 5 : x0, y - 1, 5, 3, C_TEXT2);
        }
    }
    const uint32_t WF_LO = col, WF_MI = vg_mix(col, C_TEXT, 0.5f), WF_HI = C_TEXT;
    const uint32_t pl = vg_mix(C_BG, WF_LO, 0.4f), pm = vg_mix(C_BG, WF_MI, 0.4f), ph = vg_mix(C_BG, WF_HI, 0.35f);
    for (int y = WY0; y < WY0 + WH; y++) {        /* per pixel row the peak of its 10 ms columns, the three bands over each other */
        float ta = pos + (float)(y - mid) / pps * (up ? 1 : -1), tb = pos + (float)(y + 1 - mid) / pps * (up ? 1 : -1);
        float t0 = ta < tb ? ta : tb, t1 = ta < tb ? tb : ta;
        int i0 = (int)(t0 * WFR), i1 = (int)(t1 * WFR); if (i1 <= i0) i1 = i0 + 1;
        if (i0 < 0 || i0 >= d->wfn) continue;
        int lo = 0, mi = 0, hi = 0;
        for (int i = i0; i < i1 && i < d->wfn; i++) { if (d->wl[i] > lo) lo = d->wl[i]; if (d->wm[i] > mi) mi = d->wm[i]; if (d->wh[i] > hi) hi = d->wh[i]; }
        int past = t0 < pos, hl = lo * half / 255, hm = mi * half / 255, hh = hi * half / 255;
        if (hl) vg_rect(cx - hl, y, 2 * hl, 1, past ? pl : WF_LO);
        if (hm) vg_rect(cx - hm, y, 2 * hm, 1, past ? pm : WF_MI);
        if (hh) vg_rect(cx - hh, y, 2 * hh, 1, past ? ph : WF_HI);
    }
    {   /* the loop (green band) and the hot cues (orange marks with their number) */
        float fr = d->frate > 0 ? d->frate : 1;
        if (d->lp) { int ya = mid + (int)(((float)(d->lp_in / fr) - pos) * pps), yb = mid + (int)(((float)(d->lp_out / fr) - pos) * pps);
                     if (ya < WY0) ya = WY0; if (yb > WY0 + WH) yb = WY0 + WH;
                     if (yb > ya) { vg_rect_a(x0, ya, WW, yb - ya, C_ACTIVE, d->lp == 2 ? 0.22f : 0.12f); vg_rect(x0, ya - 1, WW, 4, C_BG); vg_rect(x0, ya, WW, 2, C_ACTIVE); if (yb < WY0 + WH) { vg_rect(x0, yb - 3, WW, 4, C_BG); vg_rect(x0, yb - 2, WW, 2, C_ACTIVE); } } }
        for (int n = 0; n < 2; n++) if (d->hc_set[n]) {
            int y = mid + (int)(((float)(d->hc[n] / fr) - pos) * pps);
            if (y >= WY0 && y < WY0 + WH) { vg_rect(x0, y - 1, WW, 4, C_BG); vg_rect(x0, y, WW, 2, C_WARN); vg_round(x0 + WW - 16, y - 7, 14, 14, 3, C_WARN); vg_text(n ? "2" : "1", x0 + WW - 9, y - 6, VG_SMALL, C_ON, VG_CENTER); }
        }
    }
    vg_rect(x0, mid - 2, WW, 4, C_BG); vg_rect(x0, mid - 1, WW, 2, d->play ? C_TEXT : C_WARN);   /* the playhead (ivory; stopped: the warning colour) */
    if (d->bend != 0) vg_text(d->bend > 0 ? "BEND +" : "BEND -", x0 + WW / 2, mid + 4, VG_SMALL, C_WARN, VG_CENTER);
    if (d->cue) vg_round(x0 + WW - 23, WY0 + 3, 20, 18, 4, C_WARN);
}
static void overview(deck_t *d, int x0, int y, int w, uint32_t col)   /* the whole track, small, and where it is */
{
    vg_rect(x0, y, w, 12, C_BG);
    if (d->t < 0 || d->dur <= 0) return;
    if (w > (int)sizeof d->ov) w = (int)sizeof d->ov;
    if (d->ov_n != d->wfn || d->ov_w != w) {     /* (29.10.: it went through the whole track every frame – up to 90 000 columns a deck) */
        int total = (int)(d->dur * WFR);
        for (int x = 0; x < w; x++) {
            int i0 = x * total / w, i1 = (x + 1) * total / w, m = 0; if (i1 <= i0) i1 = i0 + 1;
            for (int i = i0; i < i1 && i < d->wfn; i++) { int v = d->wl[i] > d->wm[i] ? d->wl[i] : d->wm[i]; if (v > m) m = v; }
            d->ov[x] = (uint8_t)(m * 6 / 255);
        }
        d->ov_n = d->wfn; d->ov_w = w;
    }
    uint32_t oc = vg_mix(C_BG, col, 0.55f);
    for (int x = 0; x < w; x++) { int hh = d->ov[x]; if (hh) vg_rect(x0 + x, y + 6 - hh, 1, 2 * hh, oc); }
    int px = x0 + (int)(w * vd_clamp(dj_pos(d) / d->dur, 0, 1));
    vg_rect(x0, y + 11, px - x0, 1, col); vg_rect(px, y - 1, 2, 14, C_TEXT);
}
/* FOCUS (SDK 10, Sven 28.10.: every element reachable with one knob and one button – an MPK mini's K5 and its knob
 * pressed): the ids – deck k: k * 16 + LOAD 0, PLAY 1, PHONES 2, SYNC 3, PITCH 4, VOL 5, FILTER 6, MID 7, HIGH 8; the
 * mixer: CROSSFADER 40, CUE MIX 41, AUTO VOL 42. DATA walks deck A top to bottom, the mixer, deck B. */
static void bipolar(int x, int y, int w, float v, float lo, float hi, uint32_t col)   /* a bar from its zero */
{
    vg_rect(x, y, w, 5, C_BUTTON);
    int z = x + (int)(w * (0 - lo) / (hi - lo)), p = x + (int)(w * (v - lo) / (hi - lo)), a = z < p ? z : p, b = z < p ? p : z;
    if (b - a >= 1) vg_rect(a, y, b - a, 5, col);
    vg_rect(z, y - 2, 1, 9, C_TEXT_OFF);
}
static void meters(void)                         /* A and B upright between the waveforms: 24 segments in the deck's colour (the top: warning, overload), the peak held a second */
{
    for (int k = 0; k < 2; k++) {
        float lv = vd_minf(1, vd_sqrt(dj_pk_d[k])); dj_pk_d[k] *= 0.7f; dj_lv[k] = lv;
        if (lv >= dj_hold[k]) { dj_hold[k] = lv; dj_hold_t[k] = ms_now; } else if (ms_now - dj_hold_t[k] > 1000u) dj_hold[k] = vd_maxf(0, dj_hold[k] - 0.02f);
        int x = k ? MXB : MXA, segs = (int)(lv * 24 + 0.5f), hold = (int)(dj_hold[k] * 24 + 0.5f) - 1;
        for (int i = 0; i < 24; i++) {
            uint32_t c = i < 19 ? C_DECK(k) : i < 22 ? C_WARN : C_CLIP; int y = WY0 + WH - 9 - i * 9;
            vg_rect(x, y, MW, 8, i < segs || i == hold ? c : vg_mix(C_BG, c, 0.16f));
        }
    }
}
static void deck_panel(int k, int x0)             /* 148 wide, outside the waveforms */
{
    deck_t *d = &D[k]; char b[96], t[96]; uint32_t col = k ? C_DECK_B : C_DECK_A; int w = 148, F0 = k * 16;
    vg_round(x0, WY0, w, WH, 6, C_PANEL);
    vf_button(F0 + FO_LOAD, x0 + 4, WY0 + 4, w - 8, 30);
    if (d->t < 0) {
        vg_text(k ? "DECK B" : "DECK A", x0 + 8, WY0 + 6, VG_BOLD, col, VG_LEFT);
        vg_text("empty", x0 + 8, WY0 + 40, VG_MED, C_TEXT_OFF, VG_LEFT);
        vg_text("DATA to LOAD, ENTER:", x0 + 8, WY0 + 70, VG_SMALL, C_TEXT2, VG_LEFT);
        vg_text("the list, then LOAD", x0 + 8, WY0 + 86, VG_SMALL, C_TEXT2, VG_LEFT);
        return;
    }
    fit(TR[d->t].title, b, 96, VG_BOLD, w - 14); vg_text(b, x0 + 7, WY0 + 5, VG_BOLD, C_TEXT, VG_LEFT);
    fit(TR[d->t].artist, t, 96, VG_SMALL, w - 14); vg_text(t, x0 + 7, WY0 + 20, VG_SMALL, C_TEXT2, VG_LEFT);
    if (d->an == 2) { vg_ftoa(dj_eff_bpm(d), 1, b); vg_text(b, x0 + 7, WY0 + 38, VG_BIG, d->sync ? col : C_TEXT, VG_LEFT);
                      vf_value(F0 + FO_BPM, x0 + 4, WY0 + 36, 72, 26, &d->bpm, 40.0f, 250.0f, 0.1f); }   /* (5.11.: set by hand – ENTER, DATA 0.1, K3 /2 · K4 x2) */
    else vg_text(d->an == 1 ? "BPM ..." : "BPM ?", x0 + 7, WY0 + 42, VG_MED, C_TEXT_OFF, VG_LEFT);
    { char m2[16]; float rest = d->dur > dj_pos(d) ? d->dur - dj_pos(d) : 0; mmss(rest, m2); t[0] = '-'; scopy(t + 1, m2, 16);
      vg_text(t, x0 + w - 7, WY0 + 44, VG_MED, rest < 20 && d->play && ((ms_now / 300u) & 1u) ? C_CLIP : C_TEXT, VG_RIGHT); }
    { char p[16]; vg_ftoa(d->pitch * 100.0f, 1, p); int q = 0; if (d->pitch >= 0) b[q++] = '+'; for (int i = 0; p[i]; i++) b[q++] = p[i]; b[q++] = ' '; b[q++] = '%'; b[q] = 0;
      vg_text(b, x0 + 7, WY0 + 65, VG_MED, d->pitch > 0.0005f || d->pitch < -0.0005f ? C_WARN : C_TEXT_OFF, VG_LEFT);
      vf_value(F0 + FO_PITCH, x0 + 4, WY0 + 63, 64, 20, &d->pitch, -0.25f, 0.25f, 0.001f); }
    {   /* CUE (the headphones) and SYNC, small, beside the pitch */
        int bx = x0 + w - 7 - 70, by = WY0 + 64;
        vg_round(bx, by, 32, 18, 4, d->cue ? C_WARN : C_BUTTON);
        {   /* headphones: the band, the two cups */
            uint32_t ic = d->cue ? C_ON : C_TEXT2; float cx = (float)bx + 16, cy = (float)by + 10;
            vg_line(cx - 6, cy + 1, cx - 6, cy - 2, 1.6f, ic); vg_line(cx - 6, cy - 2, cx - 3, cy - 6, 1.6f, ic); vg_line(cx - 3, cy - 6, cx + 3, cy - 6, 1.6f, ic);
            vg_line(cx + 3, cy - 6, cx + 6, cy - 2, 1.6f, ic); vg_line(cx + 6, cy - 2, cx + 6, cy + 1, 1.6f, ic);
            vg_round((int)cx - 8, (int)cy, 4, 6, 1, ic); vg_round((int)cx + 4, (int)cy, 4, 6, 1, ic);
        }
        vg_round(bx + 36, by, 34, 18, 4, d->sync ? col : C_BUTTON);
        {   /* sync: two arrows, one each way */
            uint32_t ic = d->sync ? C_ON : C_TEXT2; float cx = (float)bx + 53, cy = (float)by + 9;
            vg_line(cx - 7, cy - 3, cx + 5, cy - 3, 1.6f, ic); vg_tri(cx + 4, cy - 6, cx + 4, cy, cx + 8, cy - 3, ic);
            vg_line(cx + 7, cy + 3, cx - 5, cy + 3, 1.6f, ic); vg_tri(cx - 4, cy, cx - 4, cy + 6, cx - 8, cy + 3, ic);
        }
        vf_button(F0 + FO_PHONES, bx, by, 32, 18); vf_button(F0 + FO_SYNC, bx + 36, by, 34, 18);
    }
    overview(d, x0 + 7, WY0 + 88, w - 14, col);
    {   /* FOCUS: the overview is the position – while it is being turned, DATA moves a beat a detent (no tempo: 0.5 s) */
        float beat = d->bpm > 0 && d->an == 2 ? 60.0f / d->bpm : 0.5f;   /* (a beat of the file) */
        if (!(vf_editing() && vf_focus() == F0 + FO_POS)) d->nav = dj_pos(d);
        vf_value(F0 + FO_POS, x0 + 5, WY0 + 86, w - 10, 16, &d->nav, 0, d->dur > 0 ? d->dur : 0, beat);
    }
    {   /* the transport: |◀ to the start · ◀◀ a bar back · ▶ / ❚❚ play / stop · ▶▶ a bar on (icons, 28.10.) */
        int cy = WY0 + 106, bw = (w - 14 - 12) / 5;
        for (int i = 0; i < 5; i++) {
            int bx = x0 + 7 + i * (bw + 3), cx = bx + bw / 2, my = cy + 10, on = i == 2 && d->play;
            uint32_t bg = on ? col : C_BUTTON, ic = on ? C_ON : C_TEXT;
            vg_round(bx, cy, bw, 20, 4, bg);
            if (i == 0) { vg_rect(cx - 6, my - 5, 2, 10, ic); vg_tri((float)cx + 5, (float)my - 5, (float)cx + 5, (float)my + 5, (float)cx - 3, (float)my, ic); }
            else if (i == 1) { vg_tri((float)cx, (float)my - 5, (float)cx, (float)my + 5, (float)cx - 7, (float)my, ic); vg_tri((float)cx + 7, (float)my - 5, (float)cx + 7, (float)my + 5, (float)cx, (float)my, ic); }
            else if (i == 2) { if (d->play) { vg_rect(cx - 5, my - 5, 3, 10, ic); vg_rect(cx + 2, my - 5, 3, 10, ic); } else vg_tri((float)cx - 4, (float)my - 6, (float)cx - 4, (float)my + 6, (float)cx + 6, (float)my, ic); }
            else if (i == 3) { vg_tri((float)cx - 7, (float)my - 5, (float)cx - 7, (float)my + 5, (float)cx, (float)my, ic); vg_tri((float)cx, (float)my - 5, (float)cx, (float)my + 5, (float)cx + 7, (float)my, ic); }
            else {                                   /* ◆ the cue point: orange when set (press: set / jump · SHIFT: clear) */
                if (d->hc_set[0]) vg_round(bx, cy, bw, 20, 4, C_WARN);
                uint32_t c = d->hc_set[0] ? C_ON : C_WARN;
                vg_tri((float)cx, (float)my - 6, (float)cx + 6, (float)my, (float)cx, (float)my + 6, c); vg_tri((float)cx, (float)my - 6, (float)cx - 6, (float)my, (float)cx, (float)my + 6, c);
            }
            vf_button(F0 + (i == 0 ? FO_START : i == 1 ? FO_BACK : i == 2 ? FO_PLAY : i == 3 ? FO_FWD : FO_CUEPT), bx, cy, bw, 20);
        }
    }
    /* the channel, clearly: its volume (encoder 1 / 3) as a fader with the number (its level: upright in the middle – meters()) */
    { int vy = WY0 + 132, hot = (dj_hot == k) && ms_now - dj_hot_t < 1500u; char v[12];
      vg_text("VOL", x0 + 7, vy, VG_BOLD, hot ? col : C_TEXT2, VG_LEFT);
      { float db = 20.0f * vd_log2(d->ng) * 0.30103f; char g[16], n[12]; scopy(g, "AUTO ", 16); int q = 5; if (db >= 0.05f) g[q++] = '+';   /* (AUTO VOL: what it adds or takes) */
        vg_ftoa(db, 1, n); for (int i = 0; n[i] && q < 12; i++) g[q++] = n[i]; g[q] = 0; if (C.no_auto) scopy(g, "AUTO off", 16);
        vg_text(g, x0 + 42, vy + 1, VG_SMALL, C.no_auto ? C_TEXT2 : C_TEXT_OFF, VG_LEFT); }
      vg_itoa((int)(d->vol * 100 + 0.5f), v); int q = 0; while (v[q]) q++; v[q++] = '%'; v[q] = 0;
      vg_text(v, x0 + w - 7, vy, VG_BOLD, hot ? col : C_TEXT, VG_RIGHT);
      vg_round(x0 + 7, vy + 16, w - 14, 9, 4, C_BUTTON);
      vg_round(x0 + 7, vy + 16, (int)((w - 14) * d->vol) < 8 ? 8 : (int)((w - 14) * d->vol), 9, 4, hot ? C_TEXT : col);
      vf_value(F0 + FO_VOL, x0 + 4, vy - 2, w - 8, 39, &d->vol, 0, 1, 0.02f); }
    {   /* FILTER (SHIFT + 1 / 3) · LOW (FOCUS) · MID (SHIFT + A / B) · HIGH (FOCUS): four cells, value and a bar from zero */
        int fy = WY0 + 176, gw = (w - 14 - 9) / 4; char e[12];
        static const char *const NM[4] = { "FLT", "LOW", "MID", "HI" };
        for (int c = 0; c < 4; c++) {
            int x = x0 + 7 + c * (gw + 3), hot = ms_now - dj_hot_t < 1500u && dj_hot == (c == 0 ? 4 + k : c == 1 ? 10 + k : c == 2 ? 8 + k : 6 + k);
            float v = c == 0 ? d->flt : c == 1 ? d->low : c == 2 ? d->mid : d->high; int on = c == 0 ? v != 0 : (v < -0.05f || v > 0.05f);
            vg_round(x, fy, gw, 36, 4, C_BUTTON);
            vg_text(NM[c], x + gw / 2, fy + 3, VG_SMALL, hot ? col : C_TEXT2, VG_CENTER);
            if (c == 0) { if (!on) scopy(e, "off", 12); else { scopy(e, v < 0 ? "LP" : "HP", 12); vg_itoa((int)(vd_absf(v) * 100 + 0.5f), e + 2); } }
            else db_text(v, e);
            vg_text(e, x + gw / 2, fy + 16, VG_SMALL, on ? (hot ? col : C_TEXT) : C_TEXT_OFF, VG_CENTER);
            if (c == 0) bipolar(x + 3, fy + 30, gw - 6, v, -1, 1, hot ? C_TEXT : col); else bipolar(x + 3, fy + 30, gw - 6, v, -30, 6, hot ? C_TEXT : col);
            if (c == 0) vf_value(F0 + FO_FLT, x, fy, gw, 36, &d->flt, -1, 1, 0.02f);
            else vf_value(F0 + (c == 1 ? FO_LOW : c == 2 ? FO_MID : FO_HIGH), x, fy, gw, 36, c == 1 ? &d->low : c == 2 ? &d->mid : &d->high, -30, 6, 1);
        }
    }
}
static void browser(void)                         /* the list, over the decks and the waveforms (while choosing) */
{
    int x0 = PXA, w = PXB + 148 - PXA;
    vg_round(x0, WY0, w, WH, 6, C_PANEL);
    const char *here = cwd[0] == 1 ? "FAVORITES" : "ALL MUSIC"; for (int f = 0; f < nfd; f++) if (cwd[0] && same(FD[f].path, cwd)) here = FD[f].title;
    char t[96]; fit(here, t, 96, VG_BOLD, w - 16); vg_text(t, x0 + 8, WY0 + 5, VG_BOLD, C_TEXT, VG_LEFT);
    int rows = 10, top = sel - 4; if (top > nvw - rows) top = nvw - rows; if (top < 0) top = 0;
    for (int k = 0; k < rows && top + k < nvw; k++) {
        int i = top + k, y = WY0 + 24 + k * 19, on = i == sel; const vitem_t *it = &VW[i];
        if (on) { vg_round(x0 + 3, y - 1, w - 6, 19, 4, C_BUTTON); vg_rect(x0 + 3, y - 1, 3, 19, browse_to >= 0 ? C_DECK(browse_to) : C_ACTIVE); }
        if (it->kind == 2) vg_text("\xE2\x86\xA9 BACK", x0 + 10, y + 1, VG_BOLD, on ? C_TEXT : C_TEXT2, VG_LEFT);
        else if (it->kind == 3) vg_text("SPOTIFY  (in the list, not on a deck)", x0 + 10, y + 1, VG_BOLD, on ? C_TEXT : C_TEXT_OFF, VG_LEFT);
        else if (it->kind == 4) vg_text("GLOBAL  (scan the BPM, AUTO VOL)", x0 + 10, y + 1, VG_BOLD, on ? C_TEXT : C_TEXT_OFF, VG_LEFT);
        else if (it->kind == 6) vg_text("+ ADD FOLDER  (disk, USB stick)", x0 + 10, y + 1, VG_BOLD, on ? C_TEXT : C_TEXT_OFF, VG_LEFT);
        else if (it->kind == 5) { star(x0 + 16, y + 8, 6, C_WARN); vg_text("FAVORITES", x0 + 28, y + 1, VG_BOLD, on ? C_TEXT : C_TEXT2, VG_LEFT); }
        else if (it->kind == 1) { fit(FD[it->idx].title, t, 96, VG_BOLD, w - 30); vg_text(t, x0 + 10, y + 1, VG_BOLD, on ? C_TEXT : C_TEXT2, VG_LEFT); }
        else { int ti = it->idx, bw = 0; uint32_t c = ti == D[0].t ? C_DECK_A : ti == D[1].t ? C_DECK_B : on ? C_TEXT : C_TEXT2;
               if (TR[ti].bpm > 0) { char b[12]; vg_ftoa(TR[ti].bpm, 1, b); bw = vg_text(b, x0 + w - 26, y + 2, VG_SMALL, C_TEXT_OFF, VG_RIGHT) + 8; }   /* (measured before) */
               fit(TR[ti].title, t, 96, VG_BOLD, w - 40 - bw); vg_text(t, x0 + 10, y + 1, VG_BOLD, c, VG_LEFT);
               if (ti == D[0].t || ti == D[1].t) vg_text(ti == D[0].t ? "A" : "B", x0 + w - 10, y + 1, VG_BOLD, c, VG_RIGHT); }
    }
    if (nvw > rows) { int h = 190 * rows / nvw; if (h < 16) h = 16; int y0 = 57 + (190 - h) * top / (nvw - rows);   /* (the bar: the plugin's mouse drags it) */
                      vg_round(x0 + w - 7, 57, 4, 190, 2, C_BUTTON); vg_round(x0 + w - 7, y0, 4, h, 2, C_TEXT_OFF); }
    if (!nvw) vg_text(lib_reading() ? "reading the music ..." : "no music – a USB stick, /root/vibe-media, or Media in the browser", x0 + w / 2, WY0 + 90, VG_SMALL, C_TEXT2, VG_CENTER);
}
static void dj_io(void)                          /* read ahead (the draw and READY) – also from the PLAYER's list */
{
    for (int k = 0; k < 2; k++) {
        deck_t *d = &D[k];
        if (dj_pend[k] >= 0) dj_load(k, dj_pend[k]);   /* (its file has come?) */
        int lim = d->t >= 0 && is_media(TR[d->t].name) ? 32 : 2;   /* (media: the host has it read; own storage: small reads, as io_work) */
        for (int j = 0; j < lim && d->t >= 0 && d->kind && d->ct - d->ch < CB / 2 && d->fpos < d->fend; j++) if (!dj_refill(d, 16384)) break;
    }
}
static void dj_service(void)
{
    for (int k = 0; k < 2; k++) dj_analyse(&D[k]);   /* measure the tempo, draw the waveform */
    for (int k = 0; k < 2; k++) for (int n = 0; n < 2; n++) if (cue_held[k][n] && ms_now - cue_held[k][n] >= 1000u) {
        cue_held[k][n] = 0; int sh = shift_held; shift_held = 1; dj_hotcue(k, n); shift_held = sh;   /* (as SHIFT + CUE) */
    }
}   /* measure the tempo, draw the waveform */
static int dj_deck_has(void) { return D[0].t >= 0 || D[1].t >= 0; }
static int dj_active(void) { return D[0].play || D[1].play || D[0].fade > 0.0005f || D[1].fade > 0.0005f; }
static void dj_strip(int y)                      /* the PLAYER's list: what the decks hold and play, one line */
{
    vg_rect(0, y, VIBE_SCREEN_W, 30, C_PANEL);
    for (int k = 0; k < 2; k++) {
        deck_t *d = &D[k]; int x0 = 4 + k * 238; char t[96], m[16];
        vg_round(x0, y + 6, 18, 18, 4, k ? C_DECK_B : C_DECK_A); vg_text(k ? "B" : "A", x0 + 9, y + 7, VG_BOLD, C_ON, VG_CENTER);
        if (d->t < 0) { vg_text("empty", x0 + 24, y + 7, VG_SMALL, C_TEXT_OFF, VG_LEFT); continue; }
        float rest = d->dur > dj_pos(d) ? d->dur - dj_pos(d) : 0; mmss(rest, m); t[0] = '-'; scopy(t + 1, m, 15);
        int tw = vg_text(t, x0 + 230, y + 7, VG_SMALL, d->play ? C_DECK(k) : C_TEXT2, VG_RIGHT);
        char n[96]; n[0] = 0; scopy(n, d->play ? "\xE2\x96\xB6 " : "", 8); scopy(n + slen(n), TR[d->t].title, 80);
        fit(n, t, 96, VG_SMALL, 200 - tw); vg_text(t, x0 + 24, y + 7, VG_SMALL, d->play ? C_TEXT : C_TEXT2, VG_LEFT);
    }
}
static void dj_levels(void) { for (int k = 0; k < 2; k++) { dj_lv[k] = vd_minf(1, vd_sqrt(dj_pk_d[k])); dj_pk_d[k] *= 0.7f; } }   /* (as meters() does) */
static void dj_head(int x, int w)                /* (5.11.2026, Sven) the PLAYER's head line: what plays – the decks that play (else
                                                    the player's track, else the loaded decks), title, time, a thin bar */
{
    dj_levels();
    int it[2], n = 0;                             /* 0 / 1 a deck, 2 the player */
    if (dj_deck_has()) { it[0] = 0; it[1] = 1; n = 2; }   /* (5.11., Sven: both decks always, an empty one says so) */
    else if (cur >= 0) it[n++] = 2;
    if (!n) { vg_text("nothing plays", x, 8, VG_MED, C_TEXT_OFF, VG_LEFT); return; }
    int cw = (w - (n - 1) * 10) / n;
    for (int i = 0; i < n; i++) {
        int x0 = x + i * (cw + 10), k = it[i]; char tm[16], t[96], s[96]; float fr = 0; int on;
        const char *ti; uint32_t col;
        if (k < 2 && D[k].t < 0) {               /* (an empty deck) */
            vg_round(x0, 4, 16, 16, 4, vg_mix(C_BG, C_DECK(k), 0.5f)); vg_text(k ? "B" : "A", x0 + 8, 4, VG_BOLD, C_ON, VG_CENTER);
            vg_text("empty", x0 + 22, 6, VG_SMALL, C_TEXT_OFF, VG_LEFT); continue;
        }
        if (k < 2) {
            deck_t *d = &D[k]; float p = dj_pos(d), rest = d->dur > p ? d->dur - p : 0; char m2[16]; mmss(rest, m2); tm[0] = '-'; scopy(tm + 1, m2, 15);
            fr = d->dur > 0 ? p / d->dur : 0; on = d->play; ti = TR[d->t].title; col = C_DECK(k);
            vg_round(x0, 4, 16, 16, 4, col); vg_text(k ? "B" : "A", x0 + 8, 4, VG_BOLD, C_ON, VG_CENTER);
        } else {
            mmss(position(), tm); on = st == ST_PLAY; ti = TR[cur].title; col = C_ACTIVE;
            vg_text(on ? "\xE2\x96\xB6" : "\xE2\x96\xA0", x0 + 8, 3, VG_BOLD, on ? col : C_TEXT2, VG_CENTER);
        }
        int tw = vg_text(tm, x0 + cw, 6, VG_SMALL, on ? col : C_TEXT2, VG_RIGHT);
        scopy(s, ti, 96); fit(s, t, 96, VG_BOLD, cw - 26 - tw); vg_text(t, x0 + 22, 4, VG_BOLD, on ? C_TEXT : C_TEXT2, VG_LEFT);
        if (k < 2) {                              /* the channel's level (5.11., Sven: "Aussteuerung" – as on the decks), the position thin under it */
            int sw = (cw - 15) / 16, segs = (int)(dj_lv[k] * 16 + 0.5f);
            for (int s2 = 0; s2 < 16; s2++) { uint32_t c = s2 < 12 ? col : s2 < 14 ? C_WARN : C_CLIP; vg_rect(x0 + s2 * (sw + 1), 20, sw, 5, s2 < segs ? c : vg_mix(C_BG, c, 0.18f)); }
            vg_rect(x0, 27, cw, 2, C_BUTTON); vg_rect(x0, 27, (int)(cw * vd_clamp(fr, 0, 1)), 2, on ? col : C_TEXT_OFF);
        }
    }
}
static void dj_draw(void)
{
    { int top = sel - 4 < 0 ? 0 : sel - 4;        /* one title a frame for the list rows */
      for (int k = 0; k < 10; k++) { int v = top + k; if (v >= nvw) break; if (VW[v].kind == 0 && !TR[VW[v].idx].meta) { read_meta(&TR[VW[v].idx]); break; } if (VW[v].kind == 1 && !FD[VW[v].idx].meta) { folder_meta(&FD[VW[v].idx]); break; } } }
    for (int k = 0; k < 2; k++) if (D[k].t >= 0 && !TR[D[k].t].meta) read_meta(&TR[D[k].t]);
    vg_clear(C_BG);
    vf_begin(); vf_enable(!browse);              /* (FOCUS: the list uses DATA and ENTER itself) */
    if (!browse && focus_play >= 0) { vf_set_focus(focus_play * 16 + FO_PLAY); focus_play = -1; }   /* (loaded: PLAY is one press away) */
    int hx;                                       /* (where the head line is free after the name) */
    { int nw = vg_text("VIBE DJ", 10, 8, VG_MED, C_TEXT, VG_LEFT); hx = 30 + nw + vg_text("DECKS", 20 + nw, 8, VG_MED, C_TEXT2, VG_LEFT); }   /* (29.10., Sven: PLAYER is now VIBE DJ) */
    if (browse) {                                 /* the list over the decks: their levels in the head line – which one plays (29.10., Sven) */
        int bw = (306 - hx - 2 * 14 - 8) / 2, sw = (bw - 15) / 16; bw = sw * 16 + 15;
        for (int k = 0; k < 2; k++) {
            int x = hx + k * (bw + 22); uint32_t col = k ? C_DECK_B : C_DECK_A;
            vg_text(k ? "B" : "A", x, 9, VG_BOLD, D[k].play ? col : C_TEXT2, VG_LEFT);
            float lvl = dj_lv[k];
            int segs = (int)(lvl * 16 + 0.5f);
            for (int i = 0; i < 16; i++) { uint32_t c = i < 12 ? col : i < 14 ? C_WARN : C_CLIP; vg_rect(x + 14 + i * (sw + 1), 11, sw, 8, i < segs ? c : vg_mix(C_BG, c, 0.18f)); }
        }
    } else if (dj_split) vg_text("SPLIT: L master, R cue", 112, 11, VG_SMALL, C_WARN, VG_LEFT);
    else if (!vibe_cue_host() && ((D[0].cue && D[0].t >= 0) || (D[1].cue && D[1].t >= 0))) vg_text(fixed_set ? "PHONES: no headphone out here" : "PHONES: GLOBAL - HEADPHONES SPLIT", 112, 11, VG_SMALL, C_TEXT2, VG_LEFT);
    vg_round(316, 6, 76, 20, 4, C.no_auto ? C_BUTTON : C_ACTIVE);   /* AUTO VOL (K3) */
    vg_text("AUTO VOL", 354, 10, VG_SMALL, C.no_auto ? C_TEXT2 : C_ON, VG_CENTER);
    float ml = vd_minf(1, vd_sqrt(dj_pk_l)), mr = vd_minf(1, vd_sqrt(dj_pk_r));
    vg_rect(402, 9, 66, 5, C_BUTTON); vg_rect(402, 9, (int)(66 * ml), 5, dj_pk_l > 0.95f ? C_CLIP : C_ACTIVE);
    vg_rect(402, 17, 66, 5, C_BUTTON); vg_rect(402, 17, (int)(66 * mr), 5, dj_pk_r > 0.95f ? C_CLIP : C_ACTIVE);
    dj_pk_l *= 0.7f; dj_pk_r *= 0.7f;
    wave_v(0, WXA); wave_v(1, WXB); meters();
    if (browse) browser(); else deck_panel(0, PXA);
    vf_value(FO_XFADE, 20, 256, 200, 20, &xfade, 0, 1, 0.02f); vf_button(FO_AUTO, 316, 6, 76, 20); vf_value(FO_CUEMIX, 284, 256, 142, 20, &cue_mix, 0, 1, 0.05f);
    if (!browse) deck_panel(1, PXB);
    { int y = 258, hot = dj_hot == 2 && ms_now - dj_hot_t < 1500u, h2 = dj_hot == 3 && ms_now - dj_hot_t < 1500u;   /* crossfader (A), cue mix (B) */
      vg_text("A", 8, y, VG_BOLD, C_DECK_A, VG_LEFT); vg_text("B", 230, y, VG_BOLD, C_DECK_B, VG_RIGHT);
      vg_round(22, y + 6, 196, 6, 3, C_BUTTON); vg_rect(120, y + 2, 1, 14, C_TEXT_OFF);
      vg_round(22 + (int)(xfade * 182), y + 1, 14, 16, 3, hot ? C_ACTIVE : C_TEXT);
      vg_text("CUE", 250, y, VG_BOLD, C_WARN, VG_LEFT); vg_round(286, y + 6, 136, 6, 3, C_BUTTON);
      vg_round(286 + (int)(cue_mix * 124), y + 1, 12, 16, 3, h2 ? C_TEXT : C_WARN); vg_text("MST", 472, y, VG_BOLD, C_TEXT2, VG_RIGHT); }
    { int f = vf_focus(); vf_end(f >= 0 && f < 16 ? C_DECK_A : f >= 16 && f < 32 ? C_DECK_B : C_TEXT); }   /* (FOCUS: the deck's colour, the mixer ivory) */
    if (toast && ms_now - toast_t < 1200u) {     /* (28.10., Sven: up in the head line, small – not over the decks) */
        int w = vg_text_width(toast, VG_BOLD) + 20; if (w > 200) w = 200;
        vg_round(212 - w / 2, 5, w, 22, 5, C_BUTTON); vg_text(toast, 212, 9, VG_BOLD, C_TEXT, VG_CENTER);
    }
    /* soft keys: in the list LOAD A / OPEN / BACK / – / CLOSE / LOAD B; else the decks' keys, mirrored */
    const char *F[6]; uint32_t on[6] = { 0 };
    if (browse) {
        F[0] = D[0].play ? "STOP A" : "PLAY A"; F[1] = cwd[0] ? "UP" : ""; F[2] = "LOAD A"; F[3] = "LOAD B"; F[4] = "CLOSE"; F[5] = D[1].play ? "STOP B" : "PLAY B";
        on[0] = D[0].play ? C_DECK_A : 0; on[2] = C_DECK_A; on[3] = C_DECK_B; on[5] = D[1].play ? C_DECK_B : 0;
    }
    else {
        F[0] = D[0].play ? "STOP A" : "PLAY A"; F[1] = "PHONES A"; F[2] = "SYNC A"; F[3] = "SYNC B"; F[4] = "PHONES B"; F[5] = D[1].play ? "STOP B" : "PLAY B";
        on[0] = D[0].play ? C_DECK_A : 0; on[1] = D[0].cue ? C_WARN : 0; on[2] = D[0].sync ? C_DECK_A : 0; on[3] = D[1].sync ? C_DECK_B : 0; on[4] = D[1].cue ? C_WARN : 0; on[5] = D[1].play ? C_DECK_B : 0;
        if (shift_held) { F[0] = "|\xE2\x97\x80 A"; F[5] = "|\xE2\x97\x80 B"; on[0] = C_DECK_A; on[5] = C_DECK_B; }   /* (SHIFT: to the start) */
    }
    vg_rect(0, 282, VIBE_SCREEN_W, 38, C_BG);
    for (int i = 0; i < 6; i++) {
        int x = 40 + i * 80;
        if (!F[i][0]) { vibe_label(VB_F1 + i, ""); vibe_led_button(VB_F1 + i, 0, 0); continue; }
        vg_round(x - 37, 286, 74, 30, 5, on[i] ? on[i] : C_BUTTON);
        vg_text(F[i], x, 293, VG_SMALL, on[i] ? C_ON : C_TEXT, VG_CENTER);
        vibe_label(VB_F1 + i, F[i]); vibe_led_button(VB_F1 + i, on[i] ? on[i] : C_TEXT2, on[i] ? 0.9f : 0.1f);
    }
    const char *K[6] = { D[0].play ? "STOP A" : "PLAY A", D[1].play ? "STOP B" : "PLAY B", "AUTOVOL", "", fixed_set ? "" : "GLOBAL", "LIST" };   /* (5.11.: K1 / K2 PLAY always) */
    if (browse) { K[2] = "LOAD A"; K[3] = "LOAD B"; }   /* (the list: K3 / K4 load) */
    if (!browse && dj_bpm_editing() >= 0) { K[2] = "BPM /2"; K[3] = "BPM x2"; }   /* (5.11.: while the BPM is being set) */
    for (int i = 0; i < 6; i++) { vibe_label(VB_K1 + i, K[i]); vibe_led_button(VB_K1 + i, i == 5 ? C_DECK_B : i == 2 && !browse ? C_ACTIVE : i == 4 ? C_TEXT : i == 1 || i == 3 ? C_DECK_B : C_DECK_A, !K[i][0] ? 0 : i == 2 && !browse ? (C.no_auto ? 0.06f : 0.9f) : i < 2 && D[i].play ? 0.9f : 0.4f); }
    for (int p = 0; p < VIBE_PADS; p++) {         /* pads 1-4 deck A, 5-8 deck B: PLAY, PHONES, SYNC, TO THE START */
        if (p >= 8) {                             /* 9-12 A, 13-16 B: CUE 1, CUE 2 (orange when set), LOOP 4, LOOP 8 (green, blinking while it records) */
            deck_t *d = &D[(p - 8) / 4]; int w = (p - 8) % 4;
            if (w < 2) vibe_led_pad(p, C_WARN, d->hc_set[w] ? 0.9f : 0.1f);
            else { int on = d->lp && d->lp_beats == (w == 2 ? 4 : 8); vibe_led_pad(p, C_ACTIVE, on ? (d->lp == 1 && ((ms_now / 150u) & 1u) ? 0.3f : 1.0f) : 0.1f); }
            continue;
        }
        deck_t *d = &D[p / 4]; int w = p % 4; uint32_t c = p < 4 ? C_DECK_A : C_DECK_B;
        int lit = w == 0 ? d->play : w == 1 ? d->cue : w == 2 ? d->sync : 0;
        vibe_led_pad(p, w == 0 ? C_ACTIVE : w == 1 ? C_WARN : c, lit ? 1.0f : 0.15f);
    }
    vibe_led_button(VB_PLAY, C_ACTIVE, D[0].play || D[1].play ? 1.0f : 0.08f);
}

static void dj_init(void) { for (int k = 0; k < 2; k++) { D[k].t = -1; D[k].vol = 0.8f; D[k].eq_dirty = 1; D[k].ng = D[k].gn = 1; } }
static int dj_deck_of(int ti) { return ti >= 0 && ti == D[0].t ? 0 : ti >= 0 && ti == D[1].t ? 1 : -1; }
static int dj_load_list(int k)                   /* the PLAYER's list: its selected track into deck k (a folder: opens it) */
{
    if (sel >= nvw) return 0;
    if (VW[sel].kind == 3) { say("SPOTIFY: ENTER - NOT ON A DECK"); return 0; }
    if (VW[sel].kind != 0) { activate(sel); return 0; }
    if (!dj_load(k, VW[sel].idx)) return 0;
    focus_play = k;
    say(k ? "IN DECK B  -  F6 DECKS" : "IN DECK A  -  F6 DECKS");
    return 1;
}
static void dj_toggle(void)
{
    if (!dj_on) { if (st == ST_PLAY) st = ST_PAUSE; dj_on = 1; browse = 0; say("DECKS"); }
    else { dj_on = 0; say(D[0].play || D[1].play ? "THE DECKS PLAY ON - F6 DECKS" : "LIST"); }   /* (choose the next track while one plays) */
}
