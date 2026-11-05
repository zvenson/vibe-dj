/*
 * VIBE DJ – the library (30.10.2026, SDK 13 media, Protokoll 13a): which tracks there are. Included at the end of main.c,
 * after global.h (one unit).
 *
 *   The own storage (vibe_store_list) is read at once, as before. The media sources (vibe_media_sources – the VIBE's card
 *   /root/vibe-media and its USB sticks, the browser's Media folder, the plugin's media folder) are read on from the draw
 *   and from READY, folder by folder (depth first; the host gives each folder's folders, then its files, by name), page by
 *   page, until the host says "not yet". A media track is ":<source key>/<folders>/<file>"; each source is a folder of the
 *   list, named as the host names it.
 *   The new list replaces the old one when it is complete – the first time (the list still empty) every half second too,
 *   so a big stick shows its music early. A track the player or a deck holds keeps going by its name; is it gone (the
 *   stick pulled), it stays in the list hidden (gone) while it is held, and plays out what is read.
 *   A source coming or going (VIBE_EV_MEDIA SOURCES) reads it all again.
 */

#define SRCN 16
static char SRC_K[SRCN][48], SRC_L[SRCN][48]; static int nsrc, media_ok = -1;   /* the sources: key, name · media_ok: -1 not asked, 0 this host has none */
typedef struct { char name[PATHLEN]; int size; } lib_t;
static lib_t LB[MAXT]; static int nlb;          /* the list being read */
#define LQN 4096
static char LQ[LQN][PATHLEN]; static int nlq;    /* the media folders still to read: a stack, the next one on top */
#define LSUBN 2048                               /* (30.10.: 256 cut Sven's Music folder – 490 artists – off at K) */
static char LSUB[LSUBN][PATHLEN]; static int nsub;   /* the folder being read: its folders (onto the stack when it is done) */
static int lib_run, lib_skip, lib_first; static uint32_t lib_pub_ms;
static track_t *NTR = TRB; static folder_t *NFD = FDB;   /* the next list, built in steps (pub_step), then swapped with TR, FD */
#define TRH 32768
static int TH[TRH];                              /* TR by name hash, while the next list is built: a track found again keeps its title, artist, BPM */
static int REMAP[MAXT];                          /* TR's index -> the next list's (-1: not in it) – the queue goes along */
#define PUBSTEP 192
static int pub_on, pub_i, pub_n, pub_nn, pub_nf;   /* (30.10.: in one go 16k tracks held the one flow up to 384 ms – the sound broke) */
static char lbuf[131072];

static int lib_reading(void) { return lib_run || pub_on; }
static const char *src_label(const char *key, int n)
{
    for (int i = 0; i < nsrc; i++) if (slen(SRC_K[i]) == n && !__builtin_memcmp(SRC_K[i], key, (size_t)n)) return SRC_L[i];
    return 0;
}
static void lib_sources_text(char *b, int max)   /* GLOBAL's last line */
{
    char n[12];
    if (lib_run) { scopy(b, "Reading the music ... ", max); vg_itoa(nlb, n); scopy(b + slen(b), n, max - slen(b)); scopy(b + slen(b), " tracks so far", max - slen(b)); return; }
    if (media_ok == 0) { scopy(b, "Music: the own storage only (this host has no media)", max); return; }
    if (!nsrc) { scopy(b, can_add ? "Music: no folder yet - ADD FOLDER in the list" : "Music: no sources - a USB stick, /root/vibe-media", max); return; }
    scopy(b, "Music from: ", max);
    for (int i = 0; i < nsrc; i++) { if (i) scopy(b + slen(b), " \xC2\xB7 ", max - slen(b)); scopy(b + slen(b), SRC_L[i][0] ? SRC_L[i] : SRC_K[i], max - slen(b)); }
}
static void lib_add(const char *name, int size)
{
    if (nlb >= MAXT - 8 || size <= 0) return;    /* (room left for the held tracks of a pulled stick) */
    scopy(LB[nlb].name, name, PATHLEN); LB[nlb].size = size; nlb++;
}

static void lib_publish(void)                    /* the list read so far becomes the list (the held tracks keep going) – from here, in steps */
{
    for (int k = 0; k < TRH; k++) TH[k] = -1;
    for (int j = 0; j < ntr; j++) { unsigned k = TR[j].h & (TRH - 1); while (TH[k] >= 0) k = (k + 1) & (TRH - 1); TH[k] = j; }
    for (int j = 0; j < ntr; j++) REMAP[j] = -1;
    pub_on = 1; pub_i = 0; pub_n = nlb; pub_nn = 0; pub_nf = 0;
}

static void pub_step(void)                       /* PUBSTEP tracks of the next list; done: it becomes the list */
{
    if (!pub_on) return;
    for (int e = pub_n - pub_i > PUBSTEP ? pub_i + PUBSTEP : pub_n; pub_i < e; pub_i++) {
        const lib_t *l = &LB[pub_i]; track_t *t = &NTR[pub_nn++]; uint32_t h = name_hash(l->name); int o = -1;
        for (unsigned k = h & (TRH - 1); TH[k] >= 0; k = (k + 1) & (TRH - 1)) { int j = TH[k]; if (TR[j].h == h && same(TR[j].name, l->name)) { o = j; break; } }
        if (o >= 0) REMAP[o] = pub_nn - 1;
        if (o >= 0 && TR[o].size == l->size) { *t = TR[o]; t->gone = 0; }   /* (its title, artist and BPM stay) */
        else {
            scopy(t->name, l->name, PATHLEN); t->size = l->size; t->h = h; t->gone = 0; t->meta = 0; t->bpm = 0; t->artist[0] = 0; t->fav = (uint8_t)fav_has(t->name, h);
            int d = dir_len(t->name); pretty(t->name + (d ? d + 1 : 0), t->title, 64, is_media(t->name)); fold(t->title);
        }
        add_folder(NFD, &pub_nf, t->name, dir_len(t->name));
    }
    if (pub_i < pub_n) return;
    pub_on = 0;
    char keep[PATHLEN] = ""; int kk = sel < nvw ? VW[sel].kind : -1;   /* (the selected row stays selected) */
    if (kk == 0) scopy(keep, TR[VW[sel].idx].name, PATHLEN); else if (kk == 1) scopy(keep, FD[VW[sel].idx].path, PATHLEN);
    int nn = pub_nn;
    /* the held tracks: the player's (also waiting, also after its fade), the decks' (also waiting) – as they are now */
    int *use[7] = { &cur, &op_pend, &sw_idx, &D[0].t, &D[1].t, &dj_pend[0], &dj_pend[1] }, from[7], to[7], nm = 0;
    for (int u = 0; u < 7; u++) {
        int x = *use[u]; if (x < 0 || x >= ntr) continue;
        int y = -1; for (int m = 0; m < nm; m++) if (from[m] == x) y = to[m];
        if (y < 0) {
            for (int k = 0; k < nn; k++) if (NTR[k].h == TR[x].h && same(NTR[k].name, TR[x].name)) { y = k; break; }
            if (y >= 0 && NTR[y].size == TR[x].size) { NTR[y] = TR[x]; NTR[y].gone = 0; }
        }
        if (y < 0 && nn < MAXT) { NTR[nn] = TR[x]; NTR[nn].gone = 1; y = nn++; }   /* (its source went: hidden, held) */
        from[nm] = x; to[nm] = y; nm++;
        *use[u] = y;
    }
    { int k = 0; for (int i = 0; i < nqu; i++) { int y = QU[i] < ntr ? REMAP[QU[i]] : -1; if (y >= 0) QU[k++] = y; } nqu = k; }   /* (a pulled stick's: out of the queue) */
    track_t *ts = TR; TR = NTR; NTR = ts; ntr = nn;
    nfav = 0; for (int t = 0; t < ntr; t++) nfav += TR[t].fav && !TR[t].gone;
    folder_t *fs = FD; FD = NFD; NFD = fs; nfd = pub_nf; list_gen++;
    int ok = !cwd[0] || cwd[0] == 1; for (int f = 0; f < nfd; f++) if (same(FD[f].path, cwd)) ok = 1;
    if (!ok) cwd[0] = 0;
    if (fixed_set && !fixed_opened && !cwd[0] && nfd > 0) { fixed_opened = 1; scopy(cwd, FD[0].path, PATHLEN); }   /* (the Jukebox: straight into its tracks) */
    build_view();
    if (keep[0]) for (int i = 0; i < nvw; i++) if ((VW[i].kind == 0 && same(TR[VW[i].idx].name, keep)) || (VW[i].kind == 1 && same(FD[VW[i].idx].path, keep))) { sel = i; break; }
    if (cur < 0 && st != ST_STOP) { st = ST_STOP; kind = 0; }
    scan_reset();
    lib_pub_ms = ms_now;
}

static void scan(void)                           /* read the list again: the own storage now, the media sources on from here */
{
    nlb = 0; nlq = 0; nsub = 0; lib_skip = 0; pub_on = 0;
    int n = vibe_store_list(lbuf, (int)sizeof lbuf - 1);
    for (int i = 0; i < n;) {
        int j = i; while (j < n && lbuf[j] != '\n') j++;
        lbuf[j] = 0;
        if ((has_ext(lbuf + i, ".mp3") || has_ext(lbuf + i, ".wav")) && j - i < PATHLEN) lib_add(lbuf + i, vibe_store_size(lbuf + i));
        i = j + 1;
    }
    static char sb[2048]; int r = vibe_media_sources(sb, (int)sizeof sb - 1);   /* (the first call also asks for VIBE_EV_MEDIA) */
    media_ok = r >= 0; nsrc = 0; can_add = media_ok && vibe_media_add(0) == 1; fixed_set = 0;
    for (int i = 0; i < r && nsrc < SRCN;) {     /* "<key>\t<flags>\t<label>\n" */
        int j = i; while (j < r && sb[j] != '\n') j++;
        int a = i; while (a < j && sb[a] != '\t') a++;
        int b = a < j ? a + 1 : j; while (b < j && sb[b] != '\t') b++;
        int k = 0; for (int p = i; p < a && k < 47; p++) SRC_K[nsrc][k++] = sb[p]; SRC_K[nsrc][k] = 0;
        k = 0; for (int p = b + 1; p < j && k < 47; p++) SRC_L[nsrc][k++] = sb[p]; SRC_L[nsrc][k] = 0;
        for (int p = a + 1; p < b; p++) if (sb[p] == 'k') fixed_set = 1;   /* (5.11.: 'k' = a fixed set, the Jukebox) */
        if (SRC_K[nsrc][0]) nsrc++;
        i = j + 1;
    }
    for (int i = nsrc - 1; i >= 0; i--) scopy(LQ[nlq++], SRC_K[i], PATHLEN);   /* (the first source first) */
    can_list = n >= 0 || media_ok;
    lib_first = ntr == 0; lib_run = 1; lib_pub_ms = ms_now;
    lib_work();
}

static void lib_work(void)                       /* on until the host says "not yet" (at most 4 pages a call); a step of the next list */
{
    pub_step();
    if (!lib_run) return;
    static char pg[16384];
    for (int calls = 0; calls < 4 && nlq > 0; calls++) {
        const char *f = LQ[nlq - 1];
        int r = vibe_media_list(f, lib_skip, pg, (int)sizeof pg - 1);
        if (r == VIBE_MEDIA_PENDING) { if (lib_first && nlb && !pub_on && ms_now - lib_pub_ms > 500u) lib_publish(); return; }
        if (r <= 0) {                            /* the folder is read (or gone): its folders next, the first on top */
            nlq--;
            for (int i = nsub - 1; i >= 0 && nlq < LQN; i--) scopy(LQ[nlq++], LSUB[i], PATHLEN);
            nsub = 0; lib_skip = 0; continue;
        }
        pg[r] = 0;
        int fl = slen(f), depth = 0; for (int i = 0; i < fl; i++) depth += f[i] == '/';
        for (char *p = pg; *p;) {                /* "d\t\t<name>\n" a folder, "f\t<bytes>\t<name>\n" a file */
            char *nl = p; while (*nl && *nl != '\n') nl++;
            char *a = p; while (a < nl && *a != '\t') a++;
            char *b = a < nl ? a + 1 : nl; while (b < nl && *b != '\t') b++;
            lib_skip++;
            if (b < nl) {
                const char *nm = b + 1; int len = (int)(nl - nm);
                if (p[0] == 'd' && depth < 8 && nsub < LSUBN && fl + 1 + len < PATHLEN - 1) {
                    char *s = LSUB[nsub++]; __builtin_memcpy(s, f, (size_t)fl); s[fl] = '/'; __builtin_memcpy(s + fl + 1, nm, (size_t)len); s[fl + 1 + len] = 0;
                } else if (p[0] == 'f' && 1 + fl + 1 + len < PATHLEN) {
                    char t[PATHLEN]; t[0] = ':'; __builtin_memcpy(t + 1, f, (size_t)fl); t[1 + fl] = '/'; __builtin_memcpy(t + 2 + fl, nm, (size_t)len); t[2 + fl + len] = 0;
                    uint64_t sz = 0; for (char *c = a + 1; c < b && *c >= '0' && *c <= '9'; c++) sz = sz * 10 + (uint64_t)(*c - '0');
                    if ((has_ext(t, ".mp3") || has_ext(t, ".wav")) && sz < 0x7FFF0000u) lib_add(t, (int)sz);   /* (2 GB at most: the offsets are int) */
                }
            }
            p = *nl ? nl + 1 : nl;
        }
    }
    if (nlq > 0) return;
    lib_run = 0; lib_publish(); pub_step();      /* (a small list is done at once) */
}
