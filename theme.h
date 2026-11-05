/*
 * VIBE DJ – the colours, all in one place: "VIBE DJ – Vintage Hi-Fi Dark" (29.10.2026, Sven). Dark, calm, precise; no
 * neon, no gradients, no glow. Included by main.c before anything draws (one unit).
 *
 *   LEFT = DECK A = petrol teal, RIGHT = DECK B = amber – everywhere, never swapped: the waveforms, the level meters, the
 *   letters A / B, PLAY / PAUSE when it plays, SYNC, the focused control, the crossfader's ends. The panels stay the same
 *   dark on both sides; titles and the big numbers (BPM, time) stay ivory.
 *
 *   The states:  normal   C_BUTTON, text C_TEXT          selected  C_BUTTON + a bar / frame in C_ACTIVE or the deck colour
 *                active   the colour filled, text C_ON   disabled  text C_TEXT_OFF       warning  C_WARN (C_CLIP: overload)
 */
#define C_BG        0x0D1113   /* the screen */
#define C_PANEL     0x202A2D   /* panels: the decks, the list, the strips */
#define C_BUTTON    0x263B48   /* buttons, the selected row, empty bars */
#define C_BORDER    0x35505C   /* lines, frames, the bars of the beat grid */
#define C_TEXT      0xEAE6DA   /* ivory: titles, BPM, times, labels that matter */
#define C_TEXT2     0xA8B5BC   /* secondary: artist, hints, what is not selected */
#define C_TEXT_OFF  0x66767E   /* disabled, off, the faintest hints (from C_TEXT2 towards C_BG) */
#define C_ON        0x0D1113   /* text and icons on a filled colour (C_ACTIVE, the deck colours, C_WARN) */

#define C_DECK_A    0x4DB6AC   /* deck A – left – petrol teal */
#define C_DECK_B    0xE6A83C   /* deck B – right – amber */
#define C_ACTIVE    0x52D6A4   /* on: playing (the single player), AUTO VOL, the loop, a scan running */
#define C_WARN      0xF28C38   /* cue: the headphones, the hot cues, pitch moved, the last 20 s, what was just turned */
#define C_CLIP      0xD9574A   /* overload only (the meters' top), errors */

#define C_GRID      0x1A2326   /* the beat grid's beats (between C_BG and C_PANEL) */
#define C_DECK(k)   ((k) ? C_DECK_B : C_DECK_A)
