/*
 * VIBE DJ (was PLAYER) – GPL-3.0 (see LICENSE), © 2026 Sven Trogus (zvenson). The MP3 decoder: minimp3 (CC0, minimp3.LICENSE).
 *
 * PLAYER – a simple music player for VIBE FM: the MP3 and WAV files in the firmware's storage as a list, play / pause,
 * stop, previous / next, shuffle, repeat; volume and a DJ-style three-band EQ (bass, mid, high); a spectrum and an
 * L/R meter. For testing the box and for the music before the gig.
 *
 * 2.2 (30.10.2026): a big USB stick (16k tracks: the list in steps, no break in the sound); the list: DATA held (0.7 s) =
 *   PLAYS NEXT (a queue – what plays goes on; SHIFT + DATA push too), SHIFT + DATA turned = favourite on (right) / off (left),
 *   FAVORITES in ALL MUSIC.
 * 2.1 (29.10.2026): a track loaded – back on the decks, FOCUS stands on that deck's PLAY (one press).
 * 2.0 (29.10.2026): NUDGE only while the knob turns (±8 %, nothing owed past 0.2 s – an MPK knob's turn owed seconds).
 * 1.9 (29.10.2026): NUDGE (encoders 2 / 4) a short push or brake while the knob turns, as a jog wheel – with SYNC too
 *   (it used to drift at 4 %, synced SYNC pulled it there over a second: it felt like the pitch).
 * 1.8 (29.10.2026): the colours "Vintage Hi-Fi Dark", all in theme.h – deck A petrol teal (left), deck B amber (right);
 *   BANK − / +: LIST → DECKS → NOW PLAYING → SPOTIFY (the decks right after the list).
 * 1.7 (29.10.2026): loops without a click (5 ms crossfade at the seam, out of it, a ROLL let go); the decks' levels
 *   upright in the middle, A and B side by side; SHIFT tapped, then a pad (an MPK); a CUE pad held 1 s clears it.
 * 1.6 (29.10.2026): GLOBAL also as a row of ALL MUSIC (under SPOTIFY – a controller without K5 reaches it); DECKS: loop
 *   ½ / ×2 and ROLL (dj.h).
 * Getting music on (1.5, 30.10.2026 – SDK 13 media): the host's media sources – VIBE: the card's /root/vibe-media and
 * every USB stick, browser: the Media button (a folder), plugin: its media folder – each a folder in the list; the files
 * keep their names (UTF-8, any folders). library.h reads them. The firmware's own storage still counts too (the
 * browser's demos, music copied there before). Titles come from the MP3's ID3 tag.
 *
 * Controls: encoders 1-4 = VOLUME, BASS, MID, HIGH · B = DJ filter (left low-pass, right high-pass) · DATA = through the list
 * (folders first, ENTER opens one, RETURN / the ↩ row goes back; PLAY on an album plays it; next / previous go through
 * the whole collection, album after album; REPEAT ALL starts over after the last, REPEAT ONE repeats the track) · DATA push / ENTER = play it ·
 * A = seek (5 s per click) · OCT − / + (keyboard ← →) = 10 s back / on, with SHIFT a minute · PLAY = play / pause · STOP ·
 * ◀ ▶ = previous / next · pads = tracks of the
 * selected bank of 16 · F1 ◀◀ F2 PLAY F3 STOP F4 ▶▶ F5 SHUFFLE F6 REPEAT · MENU = read the list again · HOME = firmwares.
 *
 * K6 = DJ: two decks, crossfader, PHONES (pre-listening) per deck, SYNC and pitch – dj.h (28.10.2026).
 *
 * Realtime: files are read ahead from the draw and from READY (vibe_media_read / vibe_store_read_at) into a 1 MB buffer; the audio
 * only decodes from memory (minimp3, CC0) into a ring, resamples (4-point Hermite), EQs and plays. Track changes fade.
 */
#include "vibe.h"
#include "vibe_gfx.h"
#include "vibe_dsp.h"
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_FLOAT_OUTPUT
#include "minimp3.h"

/* ---------------------------------------------------------------- state ----------------------------------------- */
#define MAXT 20480                               /* (30.10.2026: 2048 cut a 16k stick off at B; int16 view index – at most 32767) */
#define PATHLEN 256                              /* SDK 4: names may hold folders; SDK 13: ":<source>/<folders>/<file>" (longer ones are left out) */
typedef struct { char name[PATHLEN], title[64], artist[48]; uint8_t meta, gone, fav; float bpm; int size; uint32_t h; } track_t;
/* fav: a favourite (favs.txt) · bpm: measured before (DJ), 0 = not yet · gone: its source went (a stick pulled) – kept only while a deck or the player holds it ·
   size: the file's bytes (from the list) · h: the name's hash */
static track_t TRA[MAXT], TRB[MAXT], *TR = TRA;   /* (two: library.h builds the next list in the other one) */
static int list_gen;                             /* +1 each time the list is swapped (what the draw keeps from it is old) */
static int ntr, sel, cur = -1, can_list = 1, can_add;   /* can_add: the host opens a folder dialog (SDK 22, vibe_media_add) */
static int fixed_set, fixed_opened;   /* (5.11.) a source flagged 'k': a page with one fixed set (the Jukebox) – no SPOTIFY, GLOBAL, ADD FOLDER; opened at once */

typedef struct { uint32_t magic; float vol, bass, mid, high; int shuffle, repeat, dj; int no_auto; int split; } cfg_t;   /* repeat 0 off, 1 all, 2 one; dj -64 LP .. 0 off .. +64 HP;
                                                  no_auto: the DJ's AUTO VOL off (28.10.: appended – an older player.cfg still reads);
                                                  split: the headphones as SPLIT on a host without a cue output (5.11., 2.5: default off) */
static cfg_t C = { 0x32594C50u, 0.8f, 0, 0, 0, 0, 1, 0 };
static int cfg_dirty, eq_dirty = 1; static uint32_t cfg_t0;

enum { ST_STOP, ST_PLAY, ST_PAUSE };
static int st;
static float SR = 48000.0f;
static uint32_t ms_now, ms_acc;

/* the source: the file's bytes [fpos, fend) not yet read, the buffer cbuf[ch, ct) read but not decoded */
#define CB (1 << 20)
static uint8_t cbuf[CB];
static int ch, ct, fpos, fend, fstart, kind;     /* kind 1 MP3, 2 WAV */
static int w_fmt, w_bits, w_ch, w_align;
static float frate, dur, start_sec; static int fchan, kbps, dur_exact;
static int dec_end, want_next;
static mp3dec_t mp3;
static float pcmbuf[MINIMP3_MAX_SAMPLES_PER_FRAME];

/* decoded audio at the file's rate, read at the device's rate */
#define RB 16384
static float RL[RB], RR[RB];
static uint32_t rw; static double rp;
static float fade, gv;                           /* fade in/out of track changes, smoothed volume */
static int sw_idx = -1; static float sw_pos;     /* a track change waiting for the fade out */

/* EQ: low shelf 250 Hz, peak 1.2 kHz, high shelf 4 kHz – each -30 (kill) .. +12 dB */
typedef struct { float b0, b1, b2, a1, a2; } bq_c;
typedef struct { float z1, z2; } bq_s;
static bq_c EQ[3]; static bq_s EQS[2][3];
/* the DJ filter, as OberVave's MASTER: one knob, middle = off, left = low-pass 18 kHz .. 60 Hz, right = high-pass 20 Hz .. 6 kHz */
static vd_svf DJL, DJR; static vd_svf_c DJC; static int dj_was = 999;
static float dj_hz(int dj) { float x = (float)(dj < 0 ? -dj : dj) / 64.0f; return dj < 0 ? 18000.0f * vd_pow(60.0f / 18000.0f, x) : 20.0f * vd_pow(6000.0f / 20.0f, x); }
#define EQ_MIN -30.0f
#define EQ_MAX 12.0f

/* the analyser and the meter */
#define AN 1024
static float ANA[2048]; static uint32_t aw;
static float pk_l, pk_r;

static const char *toast; static uint32_t toast_t;
static void say(const char *s) { toast = s; toast_t = ms_now; }

/* ---------------------------------------------------------------- small helpers --------------------------------- */
static int eq4(const uint8_t *p, const char *s) { return p[0] == s[0] && p[1] == s[1] && p[2] == s[2] && p[3] == s[3]; }
static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint32_t syncsafe(const uint8_t *p) { return (uint32_t)(p[0] & 127) << 21 | (uint32_t)(p[1] & 127) << 14 | (uint32_t)(p[2] & 127) << 7 | (p[3] & 127); }
static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static int same(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void scopy(char *d, const char *s, int max) { int i = 0; for (; s[i] && i < max - 1; i++) d[i] = s[i]; d[i] = 0; }
static int has_ext(const char *n, const char *e) { int a = slen(n), b = slen(e); if (a <= b) return 0; for (int i = 0; i < b; i++) { int c = n[a - b + i]; if (c >= 'A' && c <= 'Z') c += 32; if (c != e[i]) return 0; } return 1; }   /* (".MP3" too) */
static int is_media(const char *n) { return n[0] == ':'; }   /* a media source's file (SDK 13), else the own storage's */

/* the name as a title: without the extension; the own storage's names (a-z 0-9 . _ -) with _ and - as spaces, a media file's as it is */
static void pretty(const char *name, char *out, int max, int raw)
{
    int n = slen(name), e = n; while (e > 0 && name[e - 1] != '.') e--; if (e > 0) n = e - 1;
    int k = 0; for (int i = 0; i < n && k < max - 1; i++) out[k++] = !raw && (name[i] == '_' || name[i] == '-') ? ' ' : name[i];
    out[k] = 0;
}

/* the font knows ASCII, ä ö ü Ä Ö Ü ß and a few signs: the other letters of Latin-1 (é, ñ, ø – real music, 30.10.) shown as
   their base letter, in place (never longer) */
static void fold(char *s)
{
    static const char B[65] = "AAAA\001AACEEEEIIIIDNOOOO\001\001OUUU\001YT\001aaaa\001aaceeeeiiiidnoooo\001/ouuu\001yty";   /* U+00C0..U+00FF, \001 = the font has it */
    char *o = s;
    for (const unsigned char *p = (const unsigned char *)s; *p;) {
        if (p[0] == 0xC3 && p[1] >= 0x80 && p[1] <= 0xBF && B[p[1] - 0x80] != 1) { *o++ = B[p[1] - 0x80]; p += 2; continue; }
        *o++ = (char)*p++;
    }
    *o = 0;
}

/* the files, from the draw and from events only: f_read answers as vibe_media_read – the bytes (maybe fewer than asked, also
   before the end: read on from there), 0 = the end, -1 = gone, VIBE_MEDIA_PENDING = not here yet (READY says when) */
static int f_read(const char *n, int off, void *b, int len) { return is_media(n) ? vibe_media_read(n + 1, (uint64_t)off, b, len) : vibe_store_read_at(n, off, b, len); }
static int f_read_all(const char *n, int off, uint8_t *b, int len)   /* up to len bytes, short reads read on */
{
    int got = 0;
    while (got < len) { int r = f_read(n, off + got, b + got, len - got); if (r < 0) return r; if (!r) break; got += r; }
    return got;
}
/* opening a track reads its start, its last 128 bytes (ID3v1) and its first MP3 frame – all before anything is touched: when
   one is not here yet the deck plays on as it was and the track opens when it has come (asked again from READY / the draw) */
static uint8_t PH[16384], PF[16384], PT[128]; static int p_head, p_frame, p_tag;
static int probe(const char *n, int size)        /* 1 = read, 0 = cannot be read, VIBE_MEDIA_PENDING = not yet */
{
    p_head = p_frame = p_tag = 0;
    if (size <= 0) return 0;
    int r = f_read_all(n, 0, PH, size < 16384 ? size : 16384);
    if (r < 16) return r == VIBE_MEDIA_PENDING ? r : 0;
    p_head = r;
    if (eq4(PH, "RIFF") && eq4(PH + 8, "WAVE")) return 1;
    if (size >= 256) { r = f_read_all(n, size - 128, PT, 128); if (r == VIBE_MEDIA_PENDING) return r; p_tag = r == 128 && PT[0] == 'T' && PT[1] == 'A' && PT[2] == 'G'; }
    int fs = PH[0] == 'I' && PH[1] == 'D' && PH[2] == '3' ? 10 + (int)syncsafe(PH + 6) + ((PH[5] & 0x10) ? 10 : 0) : 0;
    if (fs < size) { r = f_read_all(n, fs, PF, size - fs < 16384 ? size - fs : 16384); if (r == VIBE_MEDIA_PENDING) return r; p_frame = r > 0 ? r : 0; }
    return 1;
}

/* ---------------------------------------------------------------- the list -------------------------------------- */
/* Folders (SDK 4): the list holds paths like "grace/06-hallelujah.mp3". FD = every folder, VW = what the list shows in
 * the folder `cwd`: "↩ back" (inside a folder), its folders, then its tracks. A media source is a folder ":<key>" named
 * as the host names it; an own folder's nice name comes from its name.txt (the old copy script wrote it), else from the
 * folder name. Next / previous / shuffle go through the whole
 * collection, album after album (shuffle: all of it). */
typedef struct { char path[PATHLEN], title[64]; uint8_t meta; int16_t tracks; } folder_t;
typedef struct { int8_t kind; int16_t idx; } vitem_t;   /* kind 0 track (TR), 1 folder (FD), 2 back, 3 SPOTIFY (spotify.h), 4 GLOBAL (global.h), 5 FAVORITES, 6 + ADD FOLDER (SDK 22) */
#define MAXF 4096
#define MAXV (MAXT + MAXF + 1)
static folder_t FDA[MAXF], FDB[MAXF], *FD = FDA; static int nfd;   /* (two, as TR) */
static vitem_t VW[MAXV]; static int nvw;
static char cwd[PATHLEN];                        /* "\x01": FAVORITES */
static int nfav;                                 /* the list's favourites */
static int dir_len(const char *p) { int n = 0; for (int i = 0; p[i]; i++) if (p[i] == '/') n = i; return n; }   /* "a/b/c.mp3" -> 3 ("a/b") */
static int same_dir(const char *a, const char *b) { int n = dir_len(a); if (n != dir_len(b)) return 0; for (int i = 0; i < n; i++) if (a[i] != b[i]) return 0; return 1; }
static int in_dir(const char *p, const char *d) { int n = dir_len(p), m = slen(d); if (n != m) return 0; for (int i = 0; i < n; i++) if (p[i] != d[i]) return 0; return 1; }
static void build_view(void)
{
    nvw = 0;
    if (cwd[0]) VW[nvw++] = (vitem_t){ 2, -1 };
    else if (!fixed_set) { VW[nvw++] = (vitem_t){ 3, -1 }; VW[nvw++] = (vitem_t){ 4, -1 }; if (nfav) VW[nvw++] = (vitem_t){ 5, -1 }; }   /* (SDK 12: SPOTIFY, first in ALL MUSIC; then GLOBAL – a controller without K5 finds it here) */
    if (cwd[0] == 1) { for (int t = 0; t < ntr && nvw < MAXV; t++) if (!TR[t].gone && TR[t].fav) VW[nvw++] = (vitem_t){ 0, (int16_t)t }; }
    else {
        for (int f = 0; f < nfd && nvw < MAXV; f++) if (in_dir(FD[f].path, cwd)) VW[nvw++] = (vitem_t){ 1, (int16_t)f };
        if (!cwd[0] && can_add && nvw < MAXV) VW[nvw++] = (vitem_t){ 6, -1 };   /* (5.11.2026, Sven: "Musik direkt auswählen" – after the sources) */
        for (int t = 0; t < ntr && nvw < MAXV; t++) if (!TR[t].gone && in_dir(TR[t].name, cwd)) VW[nvw++] = (vitem_t){ 0, (int16_t)t };
    }
    if (sel >= nvw) sel = nvw ? nvw - 1 : 0;
}
static int view_of_track(int t) { for (int i = 0; i < nvw; i++) if (VW[i].kind == 0 && VW[i].idx == t) return i; return -1; }
static void add_folder(folder_t *fd, int *nf, const char *p, int n)   /* the folder p[0..n) and, before it, its parents, into fd[0..*nf) */
{
    if (n <= 0) return;
    int up = 0; for (int i = 0; i < n; i++) if (p[i] == '/') up = i;
    add_folder(fd, nf, p, up);
    static int hot[16]; static unsigned nhot;   /* (30.10.: the folders used last – a track's folder and its parents, mostly the last track's) */
    for (int h = 0; h < 16; h++) { int f = hot[h]; if (f < *nf && fd[f].path[n] == 0 && !__builtin_memcmp(fd[f].path, p, (size_t)n)) { fd[f].tracks++; return; } }
    for (int f = 0; f < *nf; f++) if (fd[f].path[n] == 0 && !__builtin_memcmp(fd[f].path, p, (size_t)n)) { fd[f].tracks++; hot[nhot++ & 15] = f; return; }
    if (*nf >= MAXF) return;
    hot[nhot++ & 15] = *nf;
    folder_t *f = &fd[(*nf)++]; int k = 0; for (; k < n && k < PATHLEN - 1; k++) f->path[k] = p[k]; f->path[k] = 0;
    if (is_media(f->path)) { scopy(f->title, f->path + (up ? up + 1 : 1), 64); fold(f->title); } else pretty(f->path + (up ? up + 1 : 0), f->title, 64, 0);
    f->meta = 0; f->tracks = 1;
}
static void scan(void);                          /* library.h: read the list again – the own storage at once, the media sources on from there */
static void lib_work(void);                      /* library.h: the media sources' folders read on (the draw, READY) */
static const char *src_label(const char *key, int n);   /* library.h: a source's name ("VIBE", the stick's) */
static int lib_reading(void);                    /* library.h: the sources are being read */
static void lib_sources_text(char *b, int max);  /* library.h: where the music comes from, one line (GLOBAL) */
static void folder_meta(folder_t *f)             /* its nice name: a source's name; name.txt in an own folder */
{
    f->meta = 1;
    if (is_media(f->path)) { if (dir_len(f->path) == 0) { const char *l = src_label(f->path + 1, slen(f->path) - 1); if (l && l[0]) { scopy(f->title, l, 64); fold(f->title); } } return; }
    char p[PATHLEN + 12], t[64]; int k = 0;
    for (int i = 0; f->path[i] && k < PATHLEN; i++) p[k++] = f->path[i];
    const char *n = "/name.txt"; for (int i = 0; n[i]; i++) p[k++] = n[i]; p[k] = 0;
    int got = vibe_store_read(p, t, 63); if (got <= 0) return;
    t[got] = 0; while (got > 0 && (t[got - 1] == '\n' || t[got - 1] == '\r' || t[got - 1] == ' ')) t[--got] = 0;
    if (t[0]) scopy(f->title, t, 64);
}

/* ID3v2 title / artist (v2.2, 2.3, 2.4; text as Latin-1, UTF-16 or UTF-8 – shown as UTF-8) */
static void text_frame(const uint8_t *d, int n, char *out, int max)
{
    int k = 0, enc = n > 0 ? d[0] : 0, i = 1, be = enc == 2;
    if (enc == 1 && n >= 3) { be = d[1] == 0xFE; i = 3; }
    while (i < n && k < max - 4) {
        uint32_t c;
        if (enc == 1 || enc == 2) { if (i + 1 >= n) break; c = be ? (uint32_t)d[i] << 8 | d[i + 1] : (uint32_t)d[i + 1] << 8 | d[i]; i += 2; }
        else c = d[i++];
        if (!c) break;
        if (enc == 3 || c < 0x80) out[k++] = (char)c;
        else if (c < 0x800) { out[k++] = (char)(0xC0 | c >> 6); out[k++] = (char)(0x80 | (c & 63)); }
        else { out[k++] = (char)(0xE0 | c >> 12); out[k++] = (char)(0x80 | ((c >> 6) & 63)); out[k++] = (char)(0x80 | (c & 63)); }
    }
    while (k > 0 && out[k - 1] == ' ') k--;
    out[k] = 0;
}
static float an_bpm(const track_t *t);          /* (dj.h: the BPM kept from an earlier analysis) */
static float id3_vbpm(const uint8_t *b, int n);  /* (dj.h: our own "VIBE DJ BPM" tag – 5.11.2026) */
static void read_tags(track_t *t);
static void read_meta(track_t *t) { read_tags(t); fold(t->title); fold(t->artist); }   /* (not here yet: asked again from the next frame) */
static void read_tags(track_t *t)
{
    static uint8_t b[8192];
    if (t->gone) { t->meta = 1; return; }
    int n = f_read_all(t->name, 0, b, t->size > 0 && t->size < (int)sizeof b ? t->size : (int)sizeof b);
    if (n == VIBE_MEDIA_PENDING) return;
    t->meta = 1; t->bpm = an_bpm(t);
    if (has_ext(t->name, ".wav")) {                /* WAV: the LIST / INFO chunk (INAM title, IART artist) */
        for (int p = 12; n > 12 && p + 8 <= n;) {
            int len = (int)le32(b + p + 4);
            if (eq4(b + p, "LIST") && p + 12 <= n && eq4(b + p + 8, "INFO")) {
                for (int q = p + 12; q + 8 <= p + 8 + len && q + 8 <= n;) {
                    int sl = (int)le32(b + q + 4), av = n - q - 8 < sl ? n - q - 8 : sl; char tmp[64]; int k = 0;
                    if (sl < 0 || sl > n) break;
                    for (; k < av && k < 63 && b[q + 8 + k]; k++) tmp[k] = (char)b[q + 8 + k]; tmp[k] = 0;
                    if (eq4(b + q, "INAM") && tmp[0]) scopy(t->title, tmp, 64);
                    if (eq4(b + q, "IART") && tmp[0]) scopy(t->artist, tmp, 48);
                    q += 8 + sl + (sl & 1);
                }
            }
            if (eq4(b + p, "data") || len < 0 || len > n) break;   /* (a stick may hold anything: no length runs off) */
            p += 8 + len + (len & 1);
        }
        return;
    }
    if (!has_ext(t->name, ".mp3")) return;
    if (n < 10 || b[0] != 'I' || b[1] != 'D' || b[2] != '3') return;
    if (t->bpm <= 0) t->bpm = id3_vbpm(b, n);    /* (5.11.: our tag – the list shows the BPM before the track was ever on a deck) */
    int ver = b[3], end = 10 + (int)syncsafe(b + 6), p = 10;
    if (end > n) end = n;
    if (b[5] & 0x40) p += ver == 4 ? (int)syncsafe(b + 10) : (int)be32(b + 10) + 4;
    if (p < 10 || p > end) return;
    while (p + (ver == 2 ? 6 : 10) <= end) {
        const uint8_t *f = b + p; int hdr = ver == 2 ? 6 : 10, sz;
        if (!f[0]) break;
        sz = ver == 2 ? (int)(f[3] << 16 | f[4] << 8 | f[5]) : ver == 4 ? (int)syncsafe(f + 4) : (int)be32(f + 4);
        if (sz <= 0) break;
        int avail = end - p - hdr < sz ? end - p - hdr : sz;
        if (avail > 0) {
            if ((ver == 2 && f[0] == 'T' && f[1] == 'T' && f[2] == '2') || (ver != 2 && eq4(f, "TIT2"))) { char tmp[64]; text_frame(f + hdr, avail, tmp, 64); if (tmp[0]) scopy(t->title, tmp, 64); }
            if ((ver == 2 && f[0] == 'T' && f[1] == 'P' && f[2] == '1') || (ver != 2 && eq4(f, "TPE1"))) text_frame(f + hdr, avail, t->artist, 48);
        }
        p += hdr + sz;
    }
}

/* ---------------------------------------------------------------- reading a file (events / draw, never the audio) */
static int refill(int want)                      /* 1 = read some; 0 = nothing now (not here yet, the end, gone) */
{
    if (cur < 0 || fpos >= fend) return 0;
    if (ch > 0 && CB - ct < want) { __builtin_memmove(cbuf, cbuf + ch, (size_t)(ct - ch)); ct -= ch; ch = 0; }
    int n = want < CB - ct ? want : CB - ct;
    if (n > fend - fpos) n = fend - fpos;
    if (n < 4) { fpos = fend; return 0; }
    int got = f_read(TR[cur].name, fpos, cbuf + ct, n);
    if (got > 0) { ct += got; fpos += got; return 1; }
    if (got == VIBE_MEDIA_PENDING) return 0;
    if (got < 0) say("THE FILE IS GONE (STICK PULLED?)");   /* (what is read plays out, then it stops) */
    fpos = fend; return 0;
}

static void sinc_prepare(void);
static int op_pend = -1; static float op_at;     /* a track whose file is not here yet: opened when it has come (io_work) */
static int open_track(int i, float at)
{
    if (i < 0 || i >= ntr) return 0;
    const char *n = TR[i].name;
    int pr = probe(n, TR[i].size), size = TR[i].size, got = pr > 0 ? p_head : -1;
    cur = i; kind = 0; st = ST_STOP; op_pend = -1;
    if (pr == VIBE_MEDIA_PENDING) { op_pend = i; op_at = at; st = ST_PLAY; return 1; }   /* (silent until it opens) */
    if (got < 16) { say("CANNOT READ FILE"); return 0; }
    __builtin_memcpy(cbuf, PH, (size_t)got);
    fstart = 0; fend = size; frate = 0; fchan = 2; kbps = 0; dur = 0; dur_exact = 0;
    if (eq4(cbuf, "RIFF") && eq4(cbuf + 8, "WAVE")) {
        int p = 12, ok = 0; w_fmt = 0;
        while (p + 8 <= got) {
            uint32_t len = le32(cbuf + p + 4);
            if (eq4(cbuf + p, "fmt ") && p + 24 <= got) {
                w_fmt = (int)le16(cbuf + p + 8); w_ch = (int)le16(cbuf + p + 10); frate = (float)le32(cbuf + p + 12);
                w_align = (int)le16(cbuf + p + 20); w_bits = (int)le16(cbuf + p + 22);
                if (w_fmt == 0xFFFE && p + 34 <= got) w_fmt = (int)le16(cbuf + p + 32);   /* WAVE_FORMAT_EXTENSIBLE: the sub format */
            }
            if (eq4(cbuf + p, "data")) { fstart = p + 8; fend = len > (uint32_t)(size - fstart) || !len ? size : fstart + (int)len; ok = 1; break; }
            if (len > (uint32_t)got) break;
            p += 8 + (int)len + (int)(len & 1);
        }
        if (!ok || !w_align || !frate || !(w_fmt == 1 || w_fmt == 3) || !(w_bits == 8 || w_bits == 16 || w_bits == 24 || w_bits == 32)) { say("WAV FORMAT NOT SUPPORTED"); return 0; }
        kind = 2; fchan = w_ch; dur = (float)((fend - fstart) / w_align) / frate; dur_exact = 1;
    } else {
        if (cbuf[0] == 'I' && cbuf[1] == 'D' && cbuf[2] == '3') fstart = 10 + (int)syncsafe(cbuf + 6) + ((cbuf[5] & 0x10) ? 10 : 0);
        if (p_tag) fend = size - 128;
        int pn = p_frame; __builtin_memcpy(cbuf, PF, (size_t)pn);
        mp3dec_frame_info_t fi = {0};
        mp3dec_init(&mp3);
        if (pn > 0) mp3dec_decode_frame(&mp3, cbuf, pn, 0, &fi);
        if (!fi.frame_bytes || !fi.hz) { say("NOT AN MP3 / WAV"); return 0; }
        kind = 1; frate = (float)fi.hz; fchan = fi.channels; kbps = fi.bitrate_kbps;
        dur = kbps ? (float)(fend - fstart) * 8.0f / (kbps * 1000.0f) : 0;
        /* a VBR file says its length in the first frame (Xing / Info from LAME, VBRI from Fraunhofer) */
        uint32_t frames = 0;
        for (int k = 0; k + 16 < pn && k < 512; k++) {
            if ((eq4(cbuf + k, "Xing") || eq4(cbuf + k, "Info")) && (be32(cbuf + k + 4) & 1u)) { frames = be32(cbuf + k + 8); break; }
            if (eq4(cbuf + k, "VBRI") && k + 18 <= pn) { frames = be32(cbuf + k + 14); break; }
        }
        if (frames) { dur = (float)frames * (fi.hz >= 32000 ? 1152.0f : 576.0f) / (float)fi.hz; kbps = (int)((float)(fend - fstart) / dur / 125.0f + 0.5f); dur_exact = 1; }
    }
    if (at < 0 || (dur > 0 && at >= dur)) at = 0;
    int pos = fstart;
    if (at > 0) pos = kind == 2 ? fstart + (int)(at * frate) * w_align : fstart + (int)(at * kbps * 125.0f);
    if (pos > fend) pos = fend;
    mp3dec_init(&mp3);
    fpos = pos; ch = ct = 0; refill(32768);
    rw = 0; rp = 0; start_sec = at; dec_end = want_next = 0; fade = 0;
    __builtin_memset(RL, 0, sizeof RL); __builtin_memset(RR, 0, sizeof RR);
    sinc_prepare();
    st = ST_PLAY;
    return 1;
}

/* ---------------------------------------------------------------- decoding (audio: memory only) ----------------- */
#define NT 64                                    /* resampler taps (see the audio) */
static int ring_free(void) { return RB - 8 - NT - (int)(rw - (uint32_t)rp); }
static void push(const float *p, int frames, int chans)
{
    for (int i = 0; i < frames; i++) {
        float a = p[i * chans], b = chans > 1 ? p[i * chans + 1] : a;
        RL[rw & (RB - 1)] = a; RR[rw & (RB - 1)] = b; rw++;
    }
}
static float wav_sample(const uint8_t *s)
{
    switch (w_bits) {
        case 8: return ((int)s[0] - 128) * (1.0f / 128.0f);
        case 16: return (int16_t)le16(s) * (1.0f / 32768.0f);
        case 24: return (float)((int32_t)((uint32_t)s[0] << 8 | (uint32_t)s[1] << 16 | (uint32_t)s[2] << 24) >> 8) * (1.0f / 8388608.0f);
        default: { union { uint32_t u; float f; } v; v.u = le32(s); return w_fmt == 3 ? v.f : (int32_t)v.u * (1.0f / 2147483648.0f); }
    }
}
static int decode_some(void)                     /* frames added; -1 = went on without sound; 0 = nothing (wait / end) */
{
    if (ring_free() < MINIMP3_MAX_SAMPLES_PER_FRAME) return 0;
    int avail = ct - ch;
    if (kind == 2) {
        int fr = avail / w_align;
        if (fr <= 0) { if (fpos >= fend) dec_end = 1; return 0; }
        if (fr > 1024) fr = 1024;
        int bps = w_bits / 8;
        for (int i = 0; i < fr; i++) {
            const uint8_t *s = cbuf + ch + i * w_align;
            float a = wav_sample(s), b = w_ch > 1 ? wav_sample(s + bps) : a;
            RL[rw & (RB - 1)] = a; RR[rw & (RB - 1)] = b; rw++;
        }
        ch += fr * w_align;
        return fr;
    }
    if (avail <= 0 || (avail < 16384 && fpos < fend)) { if (avail <= 0 && fpos >= fend) dec_end = 1; return 0; }
    mp3dec_frame_info_t fi;
    int s = mp3dec_decode_frame(&mp3, cbuf + ch, avail, pcmbuf, &fi);
    if (!fi.frame_bytes) { if (fpos >= fend) dec_end = 1; else ch = ct; return 0; }   /* (no frame in 16 KB: skip it) */
    ch += fi.frame_bytes;
    if (s <= 0) return -1;
    if (fi.hz) frate = (float)fi.hz;
    push(pcmbuf, s, fi.channels);
    return s;
}

/* ---------------------------------------------------------------- EQ ------------------------------------------------ */
static void eq_shelf(bq_c *c, float f0, float db, int high)
{
    float A = vd_exp2(db * 0.0830482f), sa = vd_sqrt(A), ph = f0 / SR;
    float sn = vd_sin(ph), cs = vd_sin(ph + 0.25f), al = sn * 0.70710678f, t = 2.0f * sa * al;
    float b0, b1, b2, a0, a1, a2;
    if (!high) { b0 = A * ((A + 1) - (A - 1) * cs + t); b1 = 2 * A * ((A - 1) - (A + 1) * cs); b2 = A * ((A + 1) - (A - 1) * cs - t);
                 a0 = (A + 1) + (A - 1) * cs + t; a1 = -2 * ((A - 1) + (A + 1) * cs); a2 = (A + 1) + (A - 1) * cs - t; }
    else       { b0 = A * ((A + 1) + (A - 1) * cs + t); b1 = -2 * A * ((A - 1) + (A + 1) * cs); b2 = A * ((A + 1) + (A - 1) * cs - t);
                 a0 = (A + 1) - (A - 1) * cs + t; a1 = 2 * ((A - 1) - (A + 1) * cs); a2 = (A + 1) - (A - 1) * cs - t; }
    c->b0 = b0 / a0; c->b1 = b1 / a0; c->b2 = b2 / a0; c->a1 = a1 / a0; c->a2 = a2 / a0;
}
static void eq_peak(bq_c *c, float f0, float db, float q)
{
    float A = vd_exp2(db * 0.0830482f), ph = f0 / SR, sn = vd_sin(ph), cs = vd_sin(ph + 0.25f), al = sn / (2.0f * q);
    float a0 = 1 + al / A;
    c->b0 = (1 + al * A) / a0; c->b1 = -2 * cs / a0; c->b2 = (1 - al * A) / a0; c->a1 = -2 * cs / a0; c->a2 = (1 - al / A) / a0;
}
static int eq_on[3];                             /* a band at 0 dB is left out: nothing touches the signal */
static void eq_update(void)
{
    float g[3] = { C.bass, C.mid, C.high };
    eq_shelf(&EQ[0], 250, C.bass, 0); eq_peak(&EQ[1], 1200, C.mid, 0.6f); eq_shelf(&EQ[2], 4000, C.high, 1);
    for (int k = 0; k < 3; k++) { int on = g[k] > 0.05f || g[k] < -0.05f; if (on && !eq_on[k]) EQS[0][k].z1 = EQS[0][k].z2 = EQS[1][k].z1 = EQS[1][k].z2 = 0; eq_on[k] = on; }
    eq_dirty = 0;
}
static inline float bq(const bq_c *c, bq_s *s, float x)
{
    float y = c->b0 * x + s->z1; s->z1 = c->b1 * x - c->a1 * y + s->z2; s->z2 = c->b2 * x - c->a2 * y; return y;
}

/* ---------------------------------------------------------------- the audio --------------------------------------- */
/* The resampler: a windowed sinc, 64 taps, Kaiser window (beta 9, about -90 dB), 256 phases interpolated linearly.
 * 44.1 -> 48 kHz: everything up to 20 kHz passes flat, the images that would fold back into the audible range (content
 * up to 20 kHz -> images from 24.1 kHz on) are pushed below -90 dB (a 4-point Hermite left them at -14 dB at 15 kHz).
 * Going down (96 -> 48 kHz) the cutoff follows the output's Nyquist. A file at the device's rate is not touched at all.
 * The table is built outside the audio (when a track opens), with double-precision sin – the SDK's fast sin would
 * limit the stopband to about -60 dB. */
#define NP 256
static float SK[NP + 1][NT]; static float sk_rate = -1;
static double dsin(double x)
{
    const double PI2 = 6.283185307179586;
    double k = (double)(long long)(x / PI2); x -= k * PI2; if (x > 3.141592653589793) x -= PI2; if (x < -3.141592653589793) x += PI2;
    double t = x, s = x, x2 = x * x; for (int i = 1; i < 14; i++) { t *= -x2 / ((2 * i) * (2 * i + 1)); s += t; } return s;
}
static double bessel_i0(double x) { double s = 1, t = 1; for (int k = 1; k < 60; k++) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; if (t < 1e-14 * s) break; } return s; }
static void sinc_prepare(void)
{
    if (frate <= 0 || frate == sk_rate) return;
    sk_rate = frate;
    double ratio = SR / frate, fc = 0.5 * (ratio < 1 ? ratio * 0.94 : 1.0), beta = 9.0, i0b = bessel_i0(beta), half = NT / 2;
    for (int p = 0; p <= NP; p++) {
        double fr = (double)p / NP, sum = 0, h[NT];
        for (int k = 0; k < NT; k++) {
            double x = (k - (half - 1)) - fr, w = x / half, win = 1 - w * w;
            double sn = x == 0 ? 2 * fc : dsin(2 * 3.141592653589793 * fc * x) / (3.141592653589793 * x);
            h[k] = win > 0 ? sn * bessel_i0(beta * __builtin_sqrt(win)) / i0b : 0; sum += h[k];
        }
        for (int k = 0; k < NT; k++) SK[p][k] = (float)(h[k] / sum);   /* (each phase sums to 1: no level wobble) */
    }
}

/* The limiter: only when the sum would go over -0.2 dBFS (EQ boost, a hot master at full volume) – it looks 1.3 ms
 * ahead and turns down before the peak, so nothing is clipped and nothing is touched below the ceiling. */
#define LA 64
static float DLL[LA], DLR[LA], LG[LA], lim_env = 1, lim_rel = 0.0001f; static uint32_t lw; static int lim_hold;
static inline void limit(float *a, float *b)
{
    const float ceil = 0.977f;
    float pk = vd_maxf(vd_absf(*a), vd_absf(*b)), tg = pk > ceil ? ceil / pk : 1.0f;
    uint32_t k = lw++ & (LA - 1);
    float oa = DLL[k], ob = DLR[k]; DLL[k] = *a; DLR[k] = *b; LG[k] = tg;
    float mn = 1.0f; for (int i = 0; i < LA; i++) if (LG[i] < mn) mn = LG[i];
    /* down fast (before the peak arrives), then hold 50 ms and come back over ~200 ms: no ripple on bass waves */
    if (mn < lim_env) { lim_env -= (lim_env - mn) * 0.15f; lim_hold = (int)(SR * 0.05f); }
    else if (lim_hold > 0) lim_hold--;
    else lim_env += (mn - lim_env) * lim_rel;
    *a = vd_clamp(oa * lim_env, -1, 1); *b = vd_clamp(ob * lim_env, -1, 1);
}

/* DJ (dj.h, at the end): two decks instead of the player, K6 switches */
static int dj_on;
static int sp_on;                                /* SPOTIFY (spotify.h, at the end): its own view and sound */
static void spot_audio(float *l, float *r, int n);
static int spot_event(const vibe_event *e);
static void spot_draw(void);
static void spot_open(void);
static int dj_list_close(void);                  /* the DJ's list was open: closed (1) */
static void dj_audio(float *l, float *r, int n);
static int dj_event(const vibe_event *e);
static void dj_draw(void);
static void dj_toggle(void);
static void dj_init(void);
static int dj_load_list(int k);                  /* the list's selected track into deck k (F1 / F2 on the list) */
static int dj_deck_of(int ti);                  /* 0 / 1: the track is in deck A / B, -1: not */
static int dj_active(void);                      /* a deck plays (or fades): the audio is the DJ's, also from the list */
static int dj_deck_has(void);                    /* a deck holds a track */
static void dj_service(void);
static int glob_on;                              /* GLOBAL (global.h, K5): the player's settings and the library scan */
static int glob_event(const vibe_event *e);
static void glob_toggle(void); static int scan_running(void);
static void glob_draw(void);
static void scan_service(void);
static void dj_strip(int y);
static void dj_head(int x, int w);               /* (dj.h, 5.11.: the head line – what plays) */
static void dj_play(int k);                      /* (dj.h: PLAY / STOP deck k) */
static int dj_playing(int k);                    /* (dj.h: deck k plays) */
static int dj_free_deck(void);

void vibe_on_audio(const float *in, float *l, float *r, int n)
{
    (void)in;
    if (dj_on || dj_active()) { dj_audio(l, r, n); for (ms_acc += (uint32_t)n * 1000u; ms_acc >= (uint32_t)SR; ms_acc -= (uint32_t)SR) ms_now++; return; }
    if (sp_on) { spot_audio(l, r, n); for (ms_acc += (uint32_t)n * 1000u; ms_acc >= (uint32_t)SR; ms_acc -= (uint32_t)SR) ms_now++; return; }
    if (eq_dirty) eq_update();
    int djm = C.dj <= -2 ? 1 : C.dj >= 2 ? 2 : 0;
    if (C.dj != dj_was) { if (djm) vd_svf_set(&DJC, dj_hz(C.dj), 1.1f, SR); dj_was = C.dj; }
    float kf = vd_coef(0.004f, SR), kg = vd_coef(0.02f, SR);
    double ratio = frate > 0 ? (double)frate / SR : 1.0;
    int same = ratio > 0.999999 && ratio < 1.000001, look = same ? 1 : NT / 2 + 1;
    float tv = C.vol * C.vol;
    lim_rel = 1.0f - vd_coef(0.2f, SR);
    for (int i = 0; i < n; i++) {
        float tg = (st == ST_PLAY && sw_idx < 0) ? 1.0f : 0.0f, a = 0, b = 0;
        fade = tg + (fade - tg) * kf; gv = tv + (gv - tv) * kg;
        if (kind && cur >= 0 && (st == ST_PLAY || fade > 0.0005f)) {
            while (rw < (uint32_t)rp + (uint32_t)look) { int k = decode_some(); if (!k) break; }
            if (rw >= (uint32_t)rp + (uint32_t)look) {
                uint32_t ip = (uint32_t)rp;
                if (same) { a = RL[ip & (RB - 1)]; b = RR[ip & (RB - 1)]; rp += 1.0; }
                else {
                    double fr = rp - ip; float ph = (float)(fr * NP); int p0 = (int)ph; float pf = ph - p0;
                    const float *c0 = SK[p0], *c1 = SK[p0 + 1];
                    uint32_t base = ip - (NT / 2 - 1);
                    for (int k = 0; k < NT; k++) { float c = c0[k] + (c1[k] - c0[k]) * pf; uint32_t j = (base + (uint32_t)k) & (RB - 1); a += RL[j] * c; b += RR[j] * c; }
                    rp += ratio;
                }
            } else if (dec_end && st == ST_PLAY && !want_next) want_next = 1;
        }
        if (eq_on[0]) { a = bq(&EQ[0], &EQS[0][0], a); b = bq(&EQ[0], &EQS[1][0], b); }
        if (eq_on[1]) { a = bq(&EQ[1], &EQS[0][1], a); b = bq(&EQ[1], &EQS[1][1], b); }
        if (eq_on[2]) { a = bq(&EQ[2], &EQS[0][2], a); b = bq(&EQ[2], &EQS[1][2], b); }
        if (djm) { vd_svf_out ol = vd_svf_tick(&DJL, &DJC, a), orr = vd_svf_tick(&DJR, &DJC, b); a = djm == 1 ? ol.lp : ol.hp; b = djm == 1 ? orr.lp : orr.hp; }
        ANA[aw++ & 2047] = (a + b) * 0.5f * fade;
        float g = gv * fade;
        a *= g; b *= g; limit(&a, &b);
        l[i] = a; r[i] = b;
        if (vd_absf(a) > pk_l) pk_l = vd_absf(a);
        if (vd_absf(b) > pk_r) pk_r = vd_absf(b);
    }
    for (ms_acc += (uint32_t)n * 1000u; ms_acc >= (uint32_t)SR; ms_acc -= (uint32_t)SR) ms_now++;
}

/* ---------------------------------------------------------------- transport ---------------------------------------- */
static void mmss_fwd(float s, char *b);
static float position(void) { return cur >= 0 && frate > 0 ? start_sec + (float)(rp / frate) : 0; }
static void request(int idx, float at)           /* change track / position: now if silent, else after the fade */
{
    if (idx < 0 || idx >= ntr) return;
    if (st != ST_PLAY || fade < 0.001f) open_track(idx, at);
    else { sw_idx = idx; sw_pos = at; }
}
static int next_of(int from, int dir, int wrap)   /* through the whole collection in list order: album after album */
{
    if (!ntr || from < 0) return -1;
    if (C.shuffle && ntr > 1) { int k, n = 0; do k = (int)(vd_rand() % (uint32_t)ntr); while ((k == from || TR[k].gone) && ++n < 64); return k; }
    for (int k = from + dir, n = 0; n < ntr; k += dir, n++) {   /* (a pulled stick's track is skipped) */
        if (k < 0 || k >= ntr) { if (!wrap) return dir < 0 ? from : -1; k = dir > 0 ? 0 : ntr - 1; }
        if (!TR[k].gone) return k;
    }
    return -1;
}
static int first_in(const char *folder)          /* the first track in a folder (or below it) */
{
    int n = slen(folder);
    for (int t = 0; t < ntr; t++) if (!__builtin_memcmp(TR[t].name, folder, (size_t)n) && TR[t].name[n] == '/') return t;
    return -1;
}
static int view = 1;                             /* 0 NOW PLAYING (big), 1 LIST (big) – F5 switches */
static int sel_track(void) { return sel < nvw && VW[sel].kind == 0 ? VW[sel].idx : -1; }
static void follow(int t) { int v = view_of_track(t); if (v >= 0) sel = v; }   /* the list shows the track that plays */
static void go_up(void)
{
    if (!cwd[0]) return;
    char was[PATHLEN]; scopy(was, cwd, PATHLEN); int d = dir_len(cwd); cwd[d] = 0;
    build_view(); sel = 0; for (int i = 0; i < nvw; i++) if ((VW[i].kind == 1 && same(FD[VW[i].idx].path, was)) || (VW[i].kind == 5 && was[0] == 1)) sel = i;
}
static void activate(int v)                      /* ENTER / a pad on a list row: play the track, open the folder, go back */
{
    if (v < 0 || v >= nvw) return;
    sel = v;
    if (VW[v].kind == 2) { go_up(); return; }
    if (VW[v].kind == 3) { spot_open(); return; }
    if (VW[v].kind == 4) { glob_toggle(); return; }
    if (VW[v].kind == 6) { if (vibe_media_add(1) == 1) say("CHOOSE A FOLDER ON THE COMPUTER"); return; }   /* (it comes as a new source: the list reads it) */
    if (VW[v].kind == 5) { cwd[0] = 1; cwd[1] = 0; build_view(); sel = 1; if (sel >= nvw) sel = 0; return; }
    if (VW[v].kind == 1) { scopy(cwd, FD[VW[v].idx].path, PATHLEN); build_view(); sel = cwd[0] ? 1 : 0; if (sel >= nvw) sel = 0; return; }
    int t = VW[v].idx; if (st == ST_STOP) open_track(t, 0); else request(t, 0);
    view = 0;                                    /* a track started: show it big */
}
/* the queue (2.2): SHIFT + DATA push on a track – it plays next, what plays goes on; ▶▶ and a track's end take it first */
#define QN 64
static int QU[QN], nqu;
static int queue_pos(int t) { for (int i = 0; i < nqu; i++) if (QU[i] == t) return i; return -1; }
static int queue_pop(void) { if (!nqu) return -1; int t = QU[0]; nqu--; for (int i = 0; i < nqu; i++) QU[i] = QU[i + 1]; return t; }
static void queue_add(int t)
{
    static char m[24];
    if (t < 0) return;
    if (cur < 0 || st == ST_STOP) { open_track(t, 0); return; }   /* (nothing plays: it plays now) */
    if (t == cur || queue_pos(t) >= 0) { say("ALREADY QUEUED"); return; }
    if (nqu >= QN) { say("THE QUEUE IS FULL"); return; }
    QU[nqu++] = t;
    if (nqu == 1) say("PLAYS NEXT"); else { scopy(m, "QUEUED AS ", 24); vg_itoa(nqu, m + slen(m)); say(m); }
}
/* favourites (2.2): SHIFT + DATA turned on a track – right on, left off; kept by name in favs.txt (the own storage) */
#define FVN 1024
static char FV[FVN][PATHLEN]; static uint32_t FVH[FVN]; static int nfv, fav_dirty; static uint32_t fav_t0;
static uint32_t name_hash(const char *s);
static int fav_has(const char *name, uint32_t h) { for (int i = 0; i < nfv; i++) if (FVH[i] == h && same(FV[i], name)) return 1; return 0; }
static void fav_read(void)
{
    static char b[FVN * 96]; int n = vibe_store_read("favs.txt", b, (int)sizeof b - 1); nfv = 0;
    for (int i = 0; i < n && nfv < FVN;) {
        int j = i; while (j < n && b[j] != '\n') j++;
        if (j > i && j - i < PATHLEN) { __builtin_memcpy(FV[nfv], b + i, (size_t)(j - i)); FV[nfv][j - i] = 0; FVH[nfv] = name_hash(FV[nfv]); nfv++; }
        i = j + 1;
    }
}
static void fav_write(void)
{
    static char b[FVN * 96]; int n = 0;
    for (int i = 0; i < nfv; i++) { int l = slen(FV[i]); if (n + l + 1 > (int)sizeof b) break; __builtin_memcpy(b + n, FV[i], (size_t)l); n += l; b[n++] = '\n'; }
    vibe_store_write("favs.txt", b, n);
}
static void fav_set(int t, int on)
{
    if (t < 0 || t >= ntr || TR[t].fav == on) return;
    if (on) { if (nfv >= FVN) { say("TOO MANY FAVORITES"); return; } scopy(FV[nfv], TR[t].name, PATHLEN); FVH[nfv] = TR[t].h; nfv++; }
    else for (int i = 0; i < nfv; i++) if (FVH[i] == TR[t].h && same(FV[i], TR[t].name)) { nfv--; scopy(FV[i], FV[nfv], PATHLEN); FVH[i] = FVH[nfv]; break; }
    TR[t].fav = (uint8_t)on; nfav += on ? 1 : -1; fav_dirty = 1; fav_t0 = ms_now;
    say(on ? "FAVORITE" : "NO LONGER A FAVORITE");
    if (!cwd[0] && (nfav == 0 || nfav == 1)) { int k = VW[sel].kind, x = VW[sel].idx; build_view(); for (int i = 0; i < nvw; i++) if (VW[i].kind == k && VW[i].idx == x) sel = i; }   /* (the FAVORITES row came / went) */
}
static void star(float cx, float cy, float r, uint32_t c)   /* (the font has no ★) */
{
    vg_tri(cx, cy - r, cx - r * 0.87f, cy + r * 0.5f, cx + r * 0.87f, cy + r * 0.5f, c);
    vg_tri(cx, cy + r, cx - r * 0.87f, cy - r * 0.5f, cx + r * 0.87f, cy - r * 0.5f, c);
}
static uint32_t ent_t; static int ent_sel, ent_done;   /* DATA / ENTER pushed on a track of the list: short plays, held queues */
static void enter_hold(void)                     /* (the draw) held long enough: plays next */
{
    if (!ent_t || ent_done || ms_now - ent_t < 700u) return;
    ent_done = 1;
    if (view == 1 && ent_sel == sel && sel_track() >= 0) queue_add(sel_track());
}
static void play_pause(void)
{
    int pick = sel_track();
    int other = (pick >= 0 && pick != cur) || (sel < nvw && VW[sel].kind == 1 && (cur < 0 || !in_dir(TR[cur].name, FD[VW[sel].idx].path)));
    if (st == ST_PLAY && !other) { st = ST_PAUSE; return; }
    /* PLAY on something else than what plays starts it: a track, or a whole folder (album) */
    int t = sel_track();
    if (sel < nvw && VW[sel].kind == 1) { int f = first_in(FD[VW[sel].idx].path); if (f >= 0 && (cur < 0 || !in_dir(TR[cur].name, FD[VW[sel].idx].path))) { request(f, 0); view = 0; say("PLAYING THE ALBUM"); return; } }
    if (t >= 0 && t != cur) { request(t, 0); return; }
    if (st == ST_PAUSE && cur >= 0) { st = ST_PLAY; return; }
    if (cur >= 0) { request(cur, 0); return; }
    for (int i = 0; i < nvw; i++) if (VW[i].kind == 0) { request(VW[i].idx, 0); return; }
}
static int shift_held, shift_used; static uint32_t shift_tap; static char seek_msg[24];
static void seek_by(float d)                     /* jumps add up: from the target still waiting, else from where it plays */
{
    if (cur < 0 || st == ST_STOP || !kind) return;
    float base = sw_idx == cur ? sw_pos : position(), p = base + d;
    if (p < 0) p = 0;
    if (dur > 1 && p > dur - 1) p = dur - 1;
    char t[12]; mmss_fwd(p, t);
    int k = 0; const char *a = d < 0 ? "\xE2\x97\x80\xE2\x97\x80 " : "\xE2\x96\xB6\xE2\x96\xB6 ";
    for (int i = 0; a[i]; i++) seek_msg[k++] = a[i]; for (int i = 0; t[i] && k < 23; i++) seek_msg[k++] = t[i]; seek_msg[k] = 0;
    say(seek_msg);
    request(cur, p);
}
static void stop(void) { op_pend = -1; if (st != ST_STOP) { st = ST_STOP; say("STOP"); } }
static void skip(int dir)
{
    if (cur < 0) { play_pause(); return; }
    if (dir < 0 && position() > 3.0f) { request(cur, 0); return; }
    int k = dir > 0 && nqu ? queue_pop() : next_of(cur, dir, 1); if (k >= 0) { follow(k); if (st == ST_STOP) open_track(k, 0); else request(k, 0); }
}

/* ---------------------------------------------------------------- events ------------------------------------------- */
static float list_acc; static int hot_k = -1; static uint32_t hot_t;
static void cfg_touch(void) { cfg_dirty = 1; cfg_t0 = ms_now; }
/* SDK 12: the PLAYER's areas for a controller's BANK − / + (an MPK mini): LIST → DECKS → NOW PLAYING → SPOTIFY, around
 * (29.10., Sven: the decks right after the list); TAP TEMPO: one step back (↩) */
static void player_area(int dir)
{
    static const char *const N[4] = { "LIST", "DECKS", "NOW PLAYING", "SPOTIFY" };
    int now = sp_on ? 3 : dj_on ? 1 : view == 0 ? 2 : 0, to = (now + dir + 4) % 4;
    if (dir == 0) {                              /* TAP TEMPO (the same area again): one step back, as ↩ */
        if (sp_on) { sp_on = 0; vibe_request(VIBE_REQ_STREAM_OFF); say("SPOTIFY OFF"); }
        else if (dj_on && dj_list_close()) say("LIST CLOSED");   /* (the DJ's list first, then the DJ) */
        else if (dj_on) dj_toggle();
        else if (view == 0) view = 1;
        else if (cwd[0]) go_up();
        return;
    }
    if (to == 2 && cur < 0) to = (to + dir + 4) % 4;   /* (nothing plays: NOW PLAYING is skipped) */
    if (to == now) return;
    if (sp_on) { sp_on = 0; vibe_request(VIBE_REQ_STREAM_OFF); }
    if (to != 1 && dj_on) dj_toggle();
    if (to == 0) view = 1;
    else if (to == 2) view = 0;
    else if (to == 1) { if (!dj_on) dj_toggle(); }
    else spot_open();
    say(N[to]);
}
static void io_work(void);
static void bg_work(void);
static uint32_t drawn_ms;                        /* when the host drew last (a host that does not draw: READY carries the work) */
static void list_touch(int x, int y)             /* (SDK 18, the plugin's window) the list: a row clicked is chosen, the chosen one again plays;
                                                    NOW PLAYING: the bar seeks, above it back to the list */
{
    if (view == 0) {
        if (y >= 152 && y < 178 && cur >= 0 && dur > 0) seek_by(vd_clamp((x - 10) / 460.0f, 0, 1) * dur - position());
        else if (y >= 30 && y < 152) view = 1;
        return;
    }
    if (x >= 462 && nvw > 5) { if (y >= 56 && y < 224) sel = (int)(vd_clamp((y - 60) / 160.0f, 0, 1) * (nvw - 1) + 0.5f); return; }   /* the bar at the right edge */
    int top = sel - 2; if (top > nvw - 5) top = nvw - 5; if (top < 0) top = 0;
    if (y < 60 || (y - 60) / 32 >= 5 || top + (y - 60) / 32 >= nvw) return;
    int i = top + (y - 60) / 32;
    if (i != sel) { sel = i; return; }
    if (dj_active()) { int k = dj_free_deck(); if (k >= 0) dj_load_list(k); else say("BOTH DECKS PLAY"); }   /* (as ENTER let go) */
    else activate(sel);
}
static int help;                                 /* HOME: the controls (vg_controls) – the list's or the decks' */
static const char *const HELP_LIST[] = {
    "DATA", "choose \xC2\xB7 held: plays next \xC2\xB7 SHIFT + turn: favourite", "ENTER", "play / open the folder",
    "\xE2\x86\xA9", "back, a folder up", "F1 \xC2\xB7 F2", "load onto deck A \xC2\xB7 B", "F6 \xC2\xB7 K6", "the decks (SHIFT + F6: repeat)",
    "PLAY \xC2\xB7 STOP \xC2\xB7 \xE2\x97\x80 \xE2\x96\xB6", "play / pause \xC2\xB7 stop \xC2\xB7 previous / next",
    "OCT - / +", "10 s back / on (SHIFT: a minute)", "ENCODER 1 - 4", "volume \xC2\xB7 bass \xC2\xB7 mid \xC2\xB7 high",
    "B", "DJ filter (left low-pass, right high-pass)", "K5", "GLOBAL: scan library, auto vol" };
static const char *const HELP_DJ[] = {
    "F1 \xC2\xB7 F6", "play A \xC2\xB7 B (SHIFT: to the start)", "F2 \xC2\xB7 F5", "headphones A \xC2\xB7 B",
    "F3 \xC2\xB7 F4", "sync A \xC2\xB7 B", "ENCODER 1 \xC2\xB7 3", "volume A \xC2\xB7 B (SHIFT: filter)",
    "ENCODER 2 \xC2\xB7 4", "nudge A \xC2\xB7 B (SHIFT: pitch +/- 25 %)", "A \xC2\xB7 B", "crossfader \xC2\xB7 cue / master",
    "PADS 1-8", "A: play, phones, sync, start \xC2\xB7 B the same", "PADS 9-16", "cue 1, cue 2, loop 4, loop 8 \xC2\xB7 A, then B",
    "OCT \xC2\xB7 \xE2\x97\x80 \xE2\x96\xB6 HELD", "bend A \xC2\xB7 bend B (+/- 4 %)", "WAVEFORM", "drag: scratch \xC2\xB7 tap: jump",
    "DATA \xC2\xB7 ENTER \xC2\xB7 \xE2\x86\xA9", "FOCUS: every control \xC2\xB7 \xE2\x86\xA9 the list (decks play on)" };
void vibe_on_event(const vibe_event *e)
{
    if (e->type == VIBE_EV_MEDIA) {              /* SDK 13: a source came or went – the list again; READY – read on */
        if (e->id == VIBE_MEDIA_EV_SOURCES) { scan(); say(e->down ? "MUSIC CAME - READING IT" : "A SOURCE WENT"); }
        else if (e->id == VIBE_MEDIA_EV_READY) { io_work(); if (ms_now - drawn_ms > 100u) bg_work(); }
        return;
    }
    if (e->type == VIBE_EV_AREA) { player_area((int)e->value); return; }
    if (e->type == VIBE_EV_BUTTON && (e->id == VB_HOME || (help && e->id == VB_RETURN))) {   /* HOME = what every control does (Protokoll 3.2, 3.11.2026) */
        if (e->down) help = e->id == VB_HOME ? !help : 0;
        return;
    }
    if (e->type == VIBE_EV_BUTTON && e->id == VB_K5) { if (e->down && !fixed_set) glob_toggle(); return; }   /* GLOBAL, from everywhere */
    if (glob_event(e)) return;
    if (sp_on) {                                 /* SPOTIFY: ↩ / F6 close it, the encoders are the sound as always */
        if (spot_event(e)) return;
        if (e->type == VIBE_EV_ENCODER && (e->id == VE_A || e->id == VE_DATA)) return;   /* (no seeking, no list) */
        if (e->type == VIBE_EV_PAD || e->type == VIBE_EV_KEY) return;
    }
    if (e->type == VIBE_EV_BUTTON && e->id == VB_K6) { if (e->down) dj_toggle(); return; }
    if (dj_on) {                                 /* DJ: its own controls; SHIFT, HOME and MENU as always */
        if (e->type == VIBE_EV_BUTTON && e->id == VB_SHIFT) {   /* (SHIFT tapped alone: the next pad within 1.5 s has it – an MPK's pads are silent while its SHIFT is held) */
            if (e->down) shift_used = 0; else { shift_tap = shift_used ? 0 : ms_now | 1u; if (shift_tap) say("SHIFT - NOW THE PAD"); }
            shift_held = e->down; return;
        }
        if (shift_held) shift_used = 1;
        if (dj_event(e)) return;
        if (e->type == VIBE_EV_BUTTON && e->down && e->id == VB_MENU) { scan(); say("LIST READ AGAIN"); }
        return;
    }
    if (e->type == VIBE_EV_TOUCH) {              /* (a click; dragged only the list's bar) */
        if (!sp_on && (e->id == VIBE_TOUCH_DOWN || (e->id == VIBE_TOUCH_MOVE && view == 1 && VIBE_TOUCH_X(e) >= 462))) list_touch(VIBE_TOUCH_X(e), VIBE_TOUCH_Y(e));
        return;
    }
    if (e->type == VIBE_EV_ENCODER) {
        float v = e->value;
        switch (e->id) {
            case VE_1: C.vol = vd_clamp(C.vol + v * 0.02f, 0, 1); cfg_touch(); hot_k = 0; hot_t = ms_now; break;
            case VE_2: C.bass = vd_clamp(C.bass + v, EQ_MIN, EQ_MAX); eq_dirty = 1; cfg_touch(); hot_k = 1; hot_t = ms_now; break;
            case VE_3: C.mid = vd_clamp(C.mid + v, EQ_MIN, EQ_MAX); eq_dirty = 1; cfg_touch(); hot_k = 2; hot_t = ms_now; break;
            case VE_4: C.high = vd_clamp(C.high + v, EQ_MIN, EQ_MAX); eq_dirty = 1; cfg_touch(); hot_k = 3; hot_t = ms_now; break;
            case VE_A:                           /* (28.10.: in the list, A scrolls it as DATA does – an MPK's K7; on NOW PLAYING: seek) */
                if (view == 1) { list_acc += v; while (list_acc >= 1) { list_acc -= 1; if (sel < nvw - 1) sel++; } while (list_acc <= -1) { list_acc += 1; if (sel > 0) sel--; } }
                else seek_by(v * 5.0f);
                break;
            case VE_B: {
                static char m[32]; int n = C.dj + (int)(v * 2.0f + (v < 0 ? -0.5f : 0.5f)); C.dj = n < -64 ? -64 : n > 64 ? 64 : n; cfg_touch(); hot_k = 4; hot_t = ms_now;
                if (C.dj > -2 && C.dj < 2) say("FILTER OFF");
                else { float hz = dj_hz(C.dj); char f[16]; int k = 0; const char *a = C.dj < 0 ? "LOW-PASS " : "HIGH-PASS "; for (int i = 0; a[i]; i++) m[k++] = a[i];
                       if (hz >= 1000) { vg_ftoa(hz / 1000.0f, hz < 10000 ? 1 : 0, f); } else vg_itoa((int)hz, f); for (int i = 0; f[i]; i++) m[k++] = f[i];
                       a = hz >= 1000 ? " kHz" : " Hz"; for (int i = 0; a[i]; i++) m[k++] = a[i]; m[k] = 0; say(m); }
                break; }
            case VE_DATA:
                if (view == 0) { view = 1; list_acc = 0; break; }   /* turning DATA on NOW PLAYING: to the list */
                if (shift_held) { shift_used = 1; if (v != 0) fav_set(sel_track(), v > 0); break; }   /* (2.2) SHIFT + DATA: favourite on / off */
                list_acc += v;
                while (list_acc >= 1) { list_acc -= 1; if (sel < nvw - 1) sel++; }
                while (list_acc <= -1) { list_acc += 1; if (sel > 0) sel--; }
                break;
        }
        return;
    }
    if (e->type == VIBE_EV_PAD && e->down) {
        activate((sel / 16) * 16 + e->id);
        return;
    }
    if (e->type == VIBE_EV_BUTTON && e->id == VB_SHIFT) {   /* (2.2: tapped alone, the next DATA push within 1.5 s has it – as the DJ's pads) */
        if (e->down) shift_used = 0; else shift_tap = shift_used || view != 1 ? 0 : ms_now | 1u;
        shift_held = e->down; return;
    }
    if (shift_held) shift_used = 1;
    if (e->type == VIBE_EV_BUTTON && !e->down && (e->id == VB_ENTER || e->id == VB_DATA_PUSH) && ent_t) {   /* (2.2) let go: short = plays */
        int was = !ent_done && ent_sel == sel; ent_t = 0;
        if (!was) return;
        if (dj_active()) { int k = dj_free_deck(); if (k >= 0) dj_load_list(k); else say("BOTH DECKS PLAY"); }   /* (the decks play: into the free one) */
        else activate(sel);
        return;
    }
    if (e->type != VIBE_EV_BUTTON || !e->down) return;
    if ((e->id == VB_ENTER || e->id == VB_DATA_PUSH) && view == 1 && (shift_held || (shift_tap && ms_now - shift_tap < 1500u)) && sel_track() >= 0) {
        shift_tap = 0; queue_add(sel_track()); return;   /* (2.2) SHIFT + DATA push: plays next */
    }
    shift_tap = 0;
    if (view == 1 && (e->id == VB_F1 || e->id == VB_F2 || e->id == VB_K3 || e->id == VB_K4)) { dj_load_list(e->id == VB_F1 || e->id == VB_K3 ? 0 : 1); return; }   /* (K3 / K4 too, 5.11.) */
    if (e->id == VB_K1 || e->id == VB_K2) { dj_play(e->id == VB_K2); return; }   /* (5.11., Sven: K1 / K2 = PLAY A / B, from the list – the same keys as on the decks) */   /* the list: LOAD A / LOAD B */
    switch (e->id) {
        case VB_PLAY: case VB_F2: play_pause(); break;
        case VB_STOP: case VB_F3: stop(); break;
        case VB_ENTER: case VB_DATA_PUSH:
            if (view == 0) view = 1;
            else if (sel_track() >= 0) { ent_t = ms_now | 1u; ent_sel = sel; ent_done = 0; }   /* (a track: decided when let go, or held) */
            else if (dj_active() && sel < nvw && VW[sel].kind == 0) { int k = dj_free_deck(); if (k >= 0) dj_load_list(k); else say("BOTH DECKS PLAY"); }   /* (the decks play: into the free one) */
            else activate(sel);
            break;
        case VB_RETURN:                          /* ↩ back, one step: NOW PLAYING -> the list -> its folder up (28.10.: it did nothing) */
            if (view == 0) view = 1; else if (cwd[0]) go_up();
            break;
        case VB_RIGHT: case VB_F4: skip(1); break;
        case VB_LEFT: case VB_F1: skip(-1); break;
        case VB_OCT_UP: seek_by(shift_held ? 60.0f : 10.0f); break;     /* (keyboard → ; SHIFT: a minute) */
        case VB_OCT_DOWN: seek_by(shift_held ? -60.0f : -10.0f); break;
        case VB_F5:
            if (shift_held) { C.shuffle ^= 1; say(C.shuffle ? "SHUFFLE ON" : "SHUFFLE OFF"); cfg_touch(); }
            else view = !view;
            break;
        case VB_F6:
            if (shift_held) { C.repeat = (C.repeat + 1) % 3; say(C.repeat == 0 ? "REPEAT OFF" : C.repeat == 1 ? "REPEAT ALL" : "REPEAT ONE"); cfg_touch(); }
            else dj_toggle();                    /* DJ: the two decks */
            break;
        case VB_MENU: scan(); say("LIST READ AGAIN"); break;
    }
}

/* ---------------------------------------------------------------- the spectrum (in the draw) ---------------------- */
static float win[AN], tw_c[AN / 2], tw_s[AN / 2], fre[AN], fim[AN]; static uint16_t rev[AN];
#define NB 24
static float band_lo[NB + 1], lvl[NB], peak[NB]; static uint8_t peak_hold[NB];
static void fft_init(void)
{
    for (int i = 0; i < AN; i++) { win[i] = 0.5f - 0.5f * vd_sin((float)i / AN + 0.25f); int r = 0; for (int b = 0; b < 10; b++) r |= ((i >> b) & 1) << (9 - b); rev[i] = (uint16_t)r; }
    for (int i = 0; i < AN / 2; i++) { tw_c[i] = vd_sin((float)i / AN + 0.25f); tw_s[i] = -vd_sin((float)i / AN); }
    for (int k = 0; k <= NB; k++) band_lo[k] = 40.0f * vd_pow(400.0f, (float)k / NB);   /* 40 Hz .. 16 kHz */
}
static void spectrum(void)
{
    for (int i = 0; i < AN; i++) { int j = rev[i]; fre[j] = ANA[(aw - AN + (uint32_t)i) & 2047] * win[i]; fim[j] = 0; }
    for (int len = 2; len <= AN; len <<= 1) {
        int half = len >> 1, step = AN / len;
        for (int i = 0; i < AN; i += len) for (int k = 0; k < half; k++) {
            float wr = tw_c[k * step], wi = tw_s[k * step], xr = fre[i + k + half], xi = fim[i + k + half];
            float tr = xr * wr - xi * wi, ti = xr * wi + xi * wr;
            fre[i + k + half] = fre[i + k] - tr; fim[i + k + half] = fim[i + k] - ti; fre[i + k] += tr; fim[i + k] += ti;
        }
    }
    float hz = SR / AN;
    for (int k = 0; k < NB; k++) {
        int a = (int)(band_lo[k] / hz), b = (int)(band_lo[k + 1] / hz); if (b <= a) b = a + 1; if (b > AN / 2) b = AN / 2;
        float pw = 0; for (int i = a; i < b; i++) pw += fre[i] * fre[i] + fim[i] * fim[i];   /* the band's energy: pink noise is flat */
        float db = 10.0f * 0.30103f * vd_log2(pw / (1.5f * 256.0f * 256.0f) + 1e-12f);   /* 0 dB = a full-scale sine (Hann: 1.5 x the peak bin) */
        float v = vd_clamp((db + 66.0f) / 66.0f, 0, 1);
        lvl[k] = v > lvl[k] ? v : lvl[k] * 0.86f;
        if (v >= peak[k]) { peak[k] = v; peak_hold[k] = 20; } else if (peak_hold[k]) peak_hold[k]--; else peak[k] = vd_maxf(0, peak[k] - 0.02f);
    }
}

/* ---------------------------------------------------------------- init -------------------------------------------- */
void vibe_on_init(float sr)
{
    SR = sr;
    vibe_areas(4);                               /* (SDK 12: BANK − / + = LIST, NOW PLAYING, DJ, SPOTIFY) */
    vf_clicks(1);                                /* (SDK 18: the decks' controls take clicks in the plugin's window – LOAD, PLAY, SYNC, the faders) */
    cfg_t c; int cn = vibe_store_read("player.cfg", &c, (int)sizeof c);
    if ((cn == (int)sizeof c || cn == (int)sizeof c - (int)sizeof(int) || cn == (int)sizeof c - 2 * (int)sizeof(int)) && c.magic == C.magic) {
        if (cn < (int)sizeof c) c.split = 0;
        if (cn < (int)sizeof c - (int)sizeof(int)) c.no_auto = 0;
        C = c;
    }
    gv = C.vol * C.vol;
    fav_read(); fft_init(); eq_update(); scan();
    vibe_outputs(VIBE_OUT_CUE);                  /* (SDK 9) the DJ's PHONES: the headphones get their own signal */
    dj_init();
    vibe_log("VIBE DJ 2.2");
}

/* ---------------------------------------------------------------- the display ------------------------------------- */
#include "theme.h"                               /* (all colours: theme.h – "Vintage Hi-Fi Dark", 29.10.) */

static void fit(const char *s, char *out, int max, int font, int w)   /* cut with … to w pixels */
{
    scopy(out, s, max);
    if (vg_text_width(out, font) <= w) return;
    int n = slen(out);
    while (n > 0) {
        do n--; while (n > 0 && (out[n] & 0xC0) == 0x80);
        out[n] = 0; char t[96]; scopy(t, out, 90); int k = slen(t); t[k] = (char)0xE2; t[k + 1] = (char)0x80; t[k + 2] = (char)0xA6; t[k + 3] = 0;
        if (vg_text_width(t, font) <= w) { scopy(out, t, max); return; }
    }
}
static void mmss(float s, char *b)
{
    int t = (int)(s + 0.5f); if (t < 0) t = 0;
    char m[12]; vg_itoa(t / 60, m); int k = 0; for (int i = 0; m[i]; i++) b[k++] = m[i];
    b[k++] = ':'; b[k++] = (char)('0' + (t % 60) / 10); b[k++] = (char)('0' + t % 10); b[k] = 0;
}
static void mmss_fwd(float s, char *b) { mmss(s, b); }
static void db_text(float v, char *b) { if (v <= EQ_MIN + 0.01f) { scopy(b, "KILL", 8); return; } int d = (int)(v + (v < 0 ? -0.5f : 0.5f)); if (d > 0) { b[0] = '+'; vg_itoa(d, b + 1); } else vg_itoa(d, b); }

static void eq_slider(int cx, const char *label, float v, float lo, float hi, int bip, uint32_t col, const char *val)
{
    vg_text(label, cx, 102, VG_SMALL, C_TEXT2, VG_CENTER);
    int y0 = 118, h = 60; vg_round(cx - 4, y0, 8, h, 3, C_BUTTON);
    float f = (v - lo) / (hi - lo), z = bip ? (0 - lo) / (hi - lo) : 0;
    int yv = y0 + h - (int)(f * h), yz = y0 + h - (int)(z * h);
    int a = yv < yz ? yv : yz, b = yv < yz ? yz : yv; if (b - a < 2) b = a + 2;
    vg_round(cx - 4, a, 8, b - a, 3, col);
    if (bip) vg_rect(cx - 8, yz, 16, 1, C_TEXT_OFF);
    vg_round(cx - 9, yv - 3, 18, 6, 2, C_TEXT);
    vg_text(val, cx, 184, VG_SMALL, C_TEXT, VG_CENTER);
}

/* the work that touches the files – from the draw, and from READY (SDK 13: the plugin with its window closed, a browser tab
   in the background do not draw; READY comes at least every 33 ms then). Reads on until the host says "not yet". */
static void dj_io(void);
static void io_work(void)
{
    if (op_pend >= 0 && st != ST_STOP) open_track(op_pend, op_at);   /* (its file has come?) */
    if (sw_idx >= 0 && (st != ST_PLAY || fade < 0.001f)) { int k = sw_idx; sw_idx = -1; open_track(k, sw_pos); }
    if (want_next) {
        want_next = 0;
        int k = nqu ? queue_pop() : C.repeat == 2 ? cur : next_of(cur, 1, C.repeat == 1);
        if (k >= 0) { follow(k); open_track(k, 0); } else st = ST_STOP;
    }
    /* own storage: 64 KB a frame (1.9 MB/s; the SD is read in the one flow, small reads never stall the audio); media: the
       host has read it in its own thread – on until half the buffer or "not yet" */
    int lim = cur >= 0 && is_media(TR[cur].name) ? 32 : 2;
    for (int k = 0; k < lim && kind && ct - ch < CB / 2 && fpos < fend; k++) if (!refill(32768)) break;
    dj_io();
    lib_work();
}
static void bg_work(void)                        /* measuring and keeping: the draw (or READY while nothing draws) */
{
    enter_hold();
    sinc_prepare();
    dj_service();                                /* (the decks measure while the list shows) */
    scan_service();                              /* (the library scan, also with GLOBAL closed) */
    if (cfg_dirty && ms_now - cfg_t0 > 1500u) { cfg_dirty = 0; vibe_store_write("player.cfg", &C, (int)sizeof C); }
    if (fav_dirty && ms_now - fav_t0 > 1500u) { fav_dirty = 0; fav_write(); }
}

void vibe_on_draw(uint16_t *fb)
{
    (void)fb;
    drawn_ms = ms_now;
    io_work(); bg_work();
    if (help) { if (dj_on) vg_controls("VIBE DJ \xC2\xB7 DECKS", HELP_DJ, sizeof HELP_DJ / sizeof HELP_DJ[0] / 2); else vg_controls("VIBE DJ \xC2\xB7 LIST", HELP_LIST, sizeof HELP_LIST / sizeof HELP_LIST[0] / 2); return; }
    if (glob_on) { glob_draw(); return; }
    if (dj_on) { dj_draw(); return; }
    if (sp_on) { spot_draw(); return; }
    { int top = sel - 1 < 0 ? 0 : sel - 1;
      for (int k = 0; k < 5; k++) {                /* one title per frame: the visible rows, then the track that plays */
          int v = top + k, done = 0;
          if (k == 4) { if (cur >= 0 && !TR[cur].meta) { read_meta(&TR[cur]); } break; }
          if (v >= nvw) continue;
          if (VW[v].kind == 0 && !TR[VW[v].idx].meta) { read_meta(&TR[VW[v].idx]); done = 1; }
          else if (VW[v].kind == 1 && !FD[VW[v].idx].meta) { folder_meta(&FD[VW[v].idx]); done = 1; }
          if (done) break;
      } }
    spectrum();
    char b[96], t[96];

    vg_clear(C_BG);
    /* header: PLAYER, the view, shuffle / repeat, the L/R meter */
    vg_text("VIBE DJ", 10, 8, VG_MED, C_TEXT, VG_LEFT);   /* (29.10., Sven: PLAYER is now VIBE DJ) */
    int lr = 1;                                   /* the L/R meter (the master): not while the decks' own levels are in the head line */
    if (!(hot_k >= 0 && ms_now - hot_t < 1500u)) { lr = !dj_deck_has(); dj_head(92, lr ? 300 : 376); }   /* (5.11.2026, Sven: "oben was gerade läuft" – the sound only while a knob turns) */
    else {   /* the sound, while a knob turns: VOL LOW MID HI FLT – value and a bar (EQ and filter from the middle) */
        const char *L[5] = { "VOL", "LOW", "MID", "HI", "FLT" };
        float f[5] = { C.vol, (C.bass - EQ_MIN) / (EQ_MAX - EQ_MIN), (C.mid - EQ_MIN) / (EQ_MAX - EQ_MIN), (C.high - EQ_MIN) / (EQ_MAX - EQ_MIN), (C.dj + 64) / 128.0f };
        float z[5] = { 0, -EQ_MIN / (EQ_MAX - EQ_MIN), -EQ_MIN / (EQ_MAX - EQ_MIN), -EQ_MIN / (EQ_MAX - EQ_MIN), 0.5f };
        uint32_t col[5] = { C_ACTIVE, C_TEXT2, C_TEXT2, C_TEXT2, C_TEXT2 };   /* (the volume on, the sound neutral) */
        for (int k = 0; k < 5; k++) {
            int x = 92 + k * 61, w = 55, hot = hot_k == k && ms_now - hot_t < 1500u;
            char v[12];
            if (k == 0) vg_itoa((int)(C.vol * 100 + 0.5f), v);
            else if (k < 4) db_text(k == 1 ? C.bass : k == 2 ? C.mid : C.high, v);
            else scopy(v, C.dj <= -2 ? "LP" : C.dj >= 2 ? "HP" : "off", 12);
            vg_text(L[k], x, 2, VG_SMALL, hot ? C_WARN : C_TEXT_OFF, VG_LEFT);
            vg_text(v, x + w, 2, VG_SMALL, hot ? C_WARN : C_TEXT, VG_RIGHT);
            vg_rect(x, 19, w, 5, C_BUTTON);
            int a = x + (int)(w * z[k]), b = x + (int)(w * vd_clamp(f[k], 0, 1)); if (b < a) { int t2 = a; a = b; b = t2; }
            int off = (k == 4 && C.dj > -2 && C.dj < 2) || (k > 0 && k < 4 && b - a < 1);
            if (!off) vg_rect(a, 19, b - a < 2 ? 2 : b - a, 5, hot ? C_WARN : col[k]);
            if (k > 0) vg_rect(x + (int)(w * z[k]), 17, 1, 9, C_TEXT_OFF);
        }
    }
    float ml = vd_minf(1, vd_sqrt(pk_l)), mr = vd_minf(1, vd_sqrt(pk_r));
    if (lr) { vg_rect(402, 9, 66, 5, C_BUTTON); vg_rect(402, 9, (int)(66 * ml), 5, pk_l > 0.95f ? C_CLIP : C_ACTIVE);
              vg_rect(402, 17, 66, 5, C_BUTTON); vg_rect(402, 17, (int)(66 * mr), 5, pk_r > 0.95f ? C_CLIP : C_ACTIVE); }
    pk_l *= 0.7f; pk_r *= 0.7f;
    vg_rect(0, 31, VIBE_SCREEN_W, 1, C_BORDER);
    if (view == 0 && cur < 0) view = 1;           /* nothing plays: the list */

    if (!can_list) { vg_text("THIS HOST CANNOT LIST FILES", 240, 120, VG_BIG, C_CLIP, VG_CENTER); vg_text("VIBE DJ needs VIBE FM Protokoll 4", 240, 152, VG_MED, C_TEXT2, VG_CENTER); }
    else if (!ntr) {
        vg_text(lib_reading() ? "READING THE MUSIC ..." : "NO MUSIC YET", 240, 92, VG_BIG, C_TEXT, VG_CENTER);
        vg_text("VIBE: a USB stick, or the card's /root/vibe-media", 240, 134, VG_MED, C_TEXT2, VG_CENTER);
        vg_text("Browser: Media, or drop a folder onto the device", 240, 158, VG_MED, C_TEXT2, VG_CENTER);
        vg_text("F7 / MENU = read the list again", 240, 190, VG_MED, C_TEXT_OFF, VG_CENTER);
    } else if (view == 0) {
        /* NOW PLAYING: title, artist · album, the time big, progress, a wide spectrum, one line for the sound */
        const track_t *c = &TR[cur];
        fit(c->title, b, 96, VG_BIG, 460); vg_text(b, 10, 40, VG_BIG, C_TEXT, VG_LEFT);
        static int np_cur = -1, np_gen = -1, np_album, np_pos, np_all;   /* (30.10.: worked out each frame over 16k tracks, the sound broke) */
        if (np_cur != cur || np_gen != list_gen) {
            np_cur = cur; np_gen = list_gen; np_album = -1; np_pos = np_all = 0;
            for (int f = 0; f < nfd; f++) if (in_dir(c->name, FD[f].path)) np_album = f;
            int n = dir_len(c->name);           /* (the cheap tests first: one track change, 16k names) */
            for (int i = 0; i < ntr; i++) if ((!n || TR[i].name[n] == '/') && !__builtin_memcmp(TR[i].name, c->name, (size_t)n) && dir_len(TR[i].name) == n) { np_all++; if (i <= cur) np_pos++; }
        }
        const char *album = np_album >= 0 ? FD[np_album].title : 0;
        { char line[128]; int q = 0; for (int z = 0; c->artist[z] && q < 60; z++) line[q++] = c->artist[z];
          if (album) { if (q) { line[q++] = ' '; line[q++] = (char)0xC2; line[q++] = (char)0xB7; line[q++] = ' '; } for (int z = 0; album[z] && q < 120; z++) line[q++] = album[z]; }
          line[q] = 0; fit(line, t, 96, VG_MED, 460); vg_text(t, 10, 68, VG_MED, C_TEXT2, VG_LEFT); }
        char tm[16], d[16]; mmss(position(), tm);
        int tw = vg_text(tm, 10, 92, VG_HUGE, st == ST_PLAY ? C_TEXT : C_TEXT2, VG_LEFT);
        if (dur > 0) { char x[24] = "/ "; if (!dur_exact) x[2] = '~', x[3] = 0; mmss(dur, d); scopy(x + slen(x), d, 16); vg_text(x, 24 + tw, 124, VG_BIG, C_TEXT2, VG_LEFT); }
        { int pos = np_pos, all = np_all;
          char a1[12], a2[12], x[40] = "TRACK "; vg_itoa(pos, a1); vg_itoa(all, a2); int k = slen(x); for (int i = 0; a1[i]; i++) x[k++] = a1[i]; x[k++] = ' '; x[k++] = '/'; x[k++] = ' '; for (int i = 0; a2[i]; i++) x[k++] = a2[i]; x[k] = 0;
          vg_text(x, 470, 98, VG_MED, C_TEXT_OFF, VG_RIGHT);
          vg_text(st == ST_PLAY ? "\xE2\x96\xB6 PLAYING" : st == ST_PAUSE ? "PAUSED" : "STOPPED", 470, 124, VG_MED, st == ST_PLAY ? C_ACTIVE : C_TEXT2, VG_RIGHT);
          int bx = 470; if (C.repeat) bx -= vg_text(C.repeat == 2 ? "REPEAT 1" : "REPEAT", bx, 144, VG_SMALL, C_WARN, VG_RIGHT) + 10;
          if (C.shuffle) vg_text("SHUFFLE", bx, 144, VG_SMALL, C_WARN, VG_RIGHT); }
        vg_round(10, 160, 460, 8, 4, C_BUTTON);
        if (dur > 0) vg_round(10, 160, (int)(460 * vd_clamp(position() / dur, 0, 1)), 8, 4, C_ACTIVE);
        for (int k = 0; k < NB; k++) {             /* the spectrum, full width: 24 bands, 15 segments */
            int x = 10 + k * 460 / NB, segs = (int)(lvl[k] * 15 + 0.5f), pk = (int)(peak[k] * 15);
            for (int j = 0; j < 15; j++) {
                uint32_t col = j < 8 ? C_ACTIVE : j < 12 ? C_WARN : j < 14 ? C_WARN : C_CLIP;
                vg_rect(x, 274 - j * 6 - 5, 460 / NB - 3, 5, j < segs ? col : C_PANEL);
                if (j == pk && pk > 0 && pk >= segs) vg_rect(x, 274 - j * 6 - 5, 460 / NB - 3, 5, vg_mix(C_BG, col, 0.7f));
            }
        }
    } else {
        /* LIST: where you are, six big rows, what plays, the keys */
        { const char *here = cwd[0] == 1 ? "FAVORITES" : "ALL MUSIC"; for (int f = 0; f < nfd; f++) if (cwd[0] && same(FD[f].path, cwd)) here = FD[f].title;
          fit(here, t, 96, VG_MED, 400); vg_text(t, 10, 37, VG_MED, cwd[0] ? C_TEXT : C_TEXT2, VG_LEFT);
          int bx = 470; if (C.repeat) bx -= vg_text(C.repeat == 2 ? "REPEAT 1" : "REPEAT", bx, 39, VG_SMALL, C_WARN, VG_RIGHT) + 10;
          if (C.shuffle) vg_text("SHUFFLE", bx, 39, VG_SMALL, C_WARN, VG_RIGHT); }
        int rows = 5, top = sel - 2; if (top > nvw - rows) top = nvw - rows; if (top < 0) top = 0;
        int tno = 0; for (int i = 0; i < top; i++) if (VW[i].kind == 0) tno++;
        for (int k = 0; k < rows && top + k < nvw; k++) {
            int i = top + k, y = 60 + k * 32; const vitem_t *it = &VW[i]; int on = i == sel;
            if (on) { vg_round(4, y, 472, 28, 6, C_BUTTON); vg_rect(4, y, 4, 28, C_ACTIVE); }   /* (selected: a bar) */
            if (it->kind == 2) {
                vg_text("\xE2\x86\xA9  BACK", 14, y + 3, VG_BIG, on ? C_TEXT : C_TEXT2, VG_LEFT);
            } else if (it->kind == 3) {             /* SPOTIFY (SDK 12) */
                vg_circle(25, y + 14, 10, C_ACTIVE); vg_line(19, y + 11, 31, y + 11, 2, C_ON); vg_line(20, y + 15, 30, y + 15, 2, C_ON); vg_line(21, y + 19, 29, y + 19, 2, C_ON);
                vg_text("SPOTIFY", 46, y + 3, VG_BIG, on ? C_TEXT : C_ACTIVE, VG_LEFT);
                vg_text(sp_on ? "on" : "Connect", 468, y + 6, VG_MED, C_TEXT_OFF, VG_RIGHT);
            } else if (it->kind == 4) {             /* GLOBAL: SCAN LIBRARY, AUTO VOL, the list again (global.h) */
                for (int r = 0; r < 3; r++) vg_rect(16, y + 7 + r * 6, 18, 2, on ? C_TEXT : C_TEXT_OFF);
                vg_text("GLOBAL", 46, y + 3, VG_BIG, on ? C_TEXT : C_TEXT2, VG_LEFT);
                vg_text(lib_reading() ? "reading ..." : scan_running() ? "scanning ..." : "Scan BPM", 468, y + 6, VG_MED, C_TEXT_OFF, VG_RIGHT);
            } else if (it->kind == 6) {             /* + ADD FOLDER (SDK 22): the computer's folder dialog */
                vg_round(14, y + 8, 22, 15, 3, on ? C_TEXT : C_TEXT_OFF); vg_rect(14, y + 5, 10, 4, on ? C_TEXT : C_TEXT_OFF);
                vg_line(25, y + 11, 25, y + 20, 2, C_PANEL); vg_line(21, y + 15, 29, y + 15, 2, C_PANEL);
                vg_text("ADD FOLDER", 46, y + 3, VG_BIG, on ? C_TEXT : C_TEXT2, VG_LEFT);
                vg_text("disk, USB stick", 468, y + 6, VG_MED, C_TEXT_OFF, VG_RIGHT);
            } else if (it->kind == 5) {             /* FAVORITES (2.2) */
                star(25, y + 14, 10, C_WARN);
                vg_text("FAVORITES", 46, y + 3, VG_BIG, on ? C_TEXT : C_TEXT2, VG_LEFT);
                char cnt[12]; vg_itoa(nfav, cnt); vg_text(cnt, 468, y + 6, VG_MED, C_TEXT_OFF, VG_RIGHT);
            } else if (it->kind == 1) {
                const folder_t *f = &FD[it->idx];
                vg_round(14, y + 8, 22, 15, 3, on ? C_TEXT : C_TEXT2); vg_rect(14, y + 5, 10, 4, on ? C_TEXT : C_TEXT2);
                char cnt[12]; vg_itoa(f->tracks, cnt); int cw = vg_text(cnt, 468, y + 6, VG_MED, C_TEXT_OFF, VG_RIGHT);
                fit(f->title, t, 96, VG_BIG, 420 - cw); vg_text(t, 46, y + 3, VG_BIG, on ? C_TEXT : C_TEXT2, VG_LEFT);
            } else {
                int ti = it->idx; tno++; const track_t *tr = &TR[ti];
                char num[8]; vg_itoa(tno, num); vg_text(num, 36, y + 6, VG_MED, ti == cur ? C_ACTIVE : C_TEXT_OFF, VG_RIGHT);
                int bx = ti == cur || dj_deck_of(ti) >= 0 ? 438 : 468, bw = 0;   /* (the BPM, left of ▶ / the deck's letter) */
                if (tr->bpm > 0) { char b[12]; vg_ftoa(tr->bpm, 1, b); bw = vg_text(b, bx, y + 6, VG_MED, C_TEXT_OFF, VG_RIGHT) + 10; }
                { int q = queue_pos(ti); if (q >= 0) { char b[12] = "NEXT"; if (q) { b[0] = '+'; vg_itoa(q + 1, b + 1); } bw += vg_text(b, bx - bw, y + 6, VG_MED, C_WARN, VG_RIGHT) + 10; } }   /* (2.2) */
                if (tr->fav) { star(bx - bw - 8, y + 14, 7, C_WARN); bw += 20; }
                fit(tr->title, t, 96, VG_BIG, bw ? bx - bw - 46 : 400); vg_text(t, 46, y + 3, VG_BIG, ti == cur ? C_ACTIVE : on ? C_TEXT : C_TEXT2, VG_LEFT);
                if (ti == cur) vg_text(st == ST_PLAY ? "\xE2\x96\xB6" : "\xE2\x96\xA0", 466, y + 3, VG_BIG, st == ST_PLAY ? C_ACTIVE : C_TEXT2, VG_RIGHT);
                else if (dj_deck_of(ti) >= 0) { int dk = dj_deck_of(ti); vg_round(446, y + 4, 22, 20, 4, dk ? C_DECK_B : C_DECK_A); vg_text(dk ? "B" : "A", 457, y + 5, VG_BOLD, C_ON, VG_CENTER); }
            }
        }
        if (nvw > rows) { int h = 160 * rows / nvw; if (h < 16) h = 16; int y0 = 60 + (160 - h) * top / (nvw - rows); vg_round(474, 60, 4, 160, 2, C_BUTTON); vg_round(474, y0, 4, h, 2, C_TEXT_OFF); }   /* (the bar: the plugin's mouse drags it) */
        if (dj_deck_has()) dj_strip(222);           /* the decks */
        else { vg_rect(0, 222, VIBE_SCREEN_W, 30, C_PANEL);   /* what plays */
        if (cur >= 0) {
            char tm[16]; mmss(position(), tm); int tw = vg_text(tm, 470, 229, VG_MED, C_TEXT, VG_RIGHT);
            char x[96] = ""; scopy(x, st == ST_PLAY ? "\xE2\x96\xB6 " : "\xE2\x96\xA0 ", 8); scopy(x + slen(x), TR[cur].title, 80);
            fit(x, t, 96, VG_MED, 440 - tw); vg_text(t, 10, 229, VG_MED, st == ST_PLAY ? C_ACTIVE : C_TEXT2, VG_LEFT);
        } else vg_text("nothing plays", 10, 229, VG_MED, C_TEXT_OFF, VG_LEFT); }
        vg_text("DATA push: play \xC2\xB7 hold: next \xC2\xB7 SHIFT+DATA turn: fav \xC2\xB7 F1/F2: deck A/B", 240, 260, VG_SMALL, C_TEXT_OFF, VG_CENTER);
    }

    /* toast */
    if (toast && ms_now - toast_t < 1200u) {
        int big = toast == seek_msg, f = big ? VG_BIG : VG_MED, h = big ? 40 : 30, w = vg_text_width(toast, f) + 32;
        vg_round(240 - w / 2, 126, w, h, 6, C_BUTTON); vg_text(toast, 240, big ? 134 : 133, f, C_TEXT, VG_CENTER);
    }

    /* soft keys */
    /* the list: F1 / F2 load the selected track into the DJ's decks A / B, F6 goes to them; NOW PLAYING: the player's keys */
    const char *F[6] = { "\xE2\x97\x80\xE2\x97\x80", st == ST_PLAY ? "PAUSE" : "PLAY", "STOP", "\xE2\x96\xB6\xE2\x96\xB6", view == 0 ? "LIST" : "NOW", "DECKS" };
    if (view == 1) { F[0] = "LOAD A"; F[1] = "LOAD B"; }
    uint32_t on = view == 1 ? 0u : (st == ST_PLAY ? 2u : 0);
    vg_rect(0, 282, VIBE_SCREEN_W, 38, C_BG);
    for (int i = 0; i < 6; i++) {
        int x = 40 + i * 80, o = (on >> i) & 1u;
        uint32_t bg = o ? C_ACTIVE : view == 1 && i == 0 ? C_DECK_A : view == 1 && i == 1 ? C_DECK_B : i == 5 ? C_BUTTON : C_BUTTON;   /* (LOAD A / B in the decks' colours) */
        vg_round(x - 37, 286, 74, 30, 5, bg);
        vg_text(F[i], x, 293, VG_MED, o || (view == 1 && i < 2) ? C_ON : C_TEXT, VG_CENTER);
        vibe_label(VB_F1 + i, F[i]);
    }
    for (int i = 0; i < 6; i++) {               /* (5.11.2026, Sven: K1 / K2 in the list = LOAD A / B) */
        const char *k = i == 5 ? "DECKS" : i == 4 ? (fixed_set ? "" : "GLOBAL") : i == 0 ? (dj_playing(0) ? "STOP A" : "PLAY A") : i == 1 ? (dj_playing(1) ? "STOP B" : "PLAY B")
                      : view == 1 && i == 2 ? "LOAD A" : view == 1 && i == 3 ? "LOAD B" : "";   /* (5.11.: K1 / K2 PLAY, K3 / K4 LOAD) */
        vibe_led_button(VB_K1 + i, i == 5 || (k[0] && (i == 1 || i == 3)) ? C_DECK_B : i == 4 ? C_TEXT : k[0] ? C_DECK_A : 0, !k[0] ? 0 : i < 2 && dj_playing(i) ? 0.9f : 0.4f);
        vibe_label(VB_K1 + i, k);
    }

    /* lights: the pads = the tracks of the selected bank of 16, PLAY / STOP, SHUFFLE / REPEAT */
    int bank = (sel / 16) * 16;                  /* the pads = the list rows of the selected bank of 16 */
    for (int p = 0; p < VIBE_PADS; p++) {
        int i = bank + p;
        if (i >= nvw) vibe_led_pad(p, 0, 0);
        else if (VW[i].kind == 0 && VW[i].idx == cur) vibe_led_pad(p, C_ACTIVE, st == ST_PLAY ? 1.0f : 0.5f);
        else if (VW[i].kind == 1) vibe_led_pad(p, C_TEXT2, i == sel ? 0.7f : 0.3f);
        else if (VW[i].kind == 2) vibe_led_pad(p, C_TEXT, i == sel ? 0.45f : 0.08f);
        else vibe_led_pad(p, i == sel ? C_TEXT : C_TEXT2, i == sel ? 0.45f : 0.1f);
    }
    vibe_led_button(VB_PLAY, C_ACTIVE, st == ST_PLAY ? 1.0f : st == ST_PAUSE ? ((ms_now / 400u) & 1u ? 0.8f : 0.1f) : 0.08f);
    vibe_led_button(VB_STOP, C_CLIP, st == ST_STOP ? 0.6f : 0.12f);
    vibe_led_button(VB_F5, C_DECK_A, 0.6f); vibe_led_button(VB_F6, C_DECK_B, 0.6f);
    if (view == 1) { vibe_led_button(VB_F1, C_DECK_A, 0.6f); vibe_led_button(VB_F2, C_DECK_B, 0.6f); }
}

#include "dj.h"
#include "global.h"
#include "library.h"
#include "spotify.h"
