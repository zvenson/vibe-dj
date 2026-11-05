# VIBE DJ

A native firmware for the **VIBE FM** groovebox: a music player and a two-deck DJ setup in one.

- **The list:** MP3 and WAV from a USB stick, the device's own folder, or a folder on your computer in the browser. Folders,
  favourites, a queue, shuffle and repeat.
- **Two decks:** SYNC (tempo and beat), pitch ±25 %, nudge, a crossfader, a 3-band DJ EQ and a filter for each deck,
  two hot cues and beat-exact loops (4 / 8, halve, double, roll).
- **BPM and beat grid** measured once per track and kept, or set by hand. A track's own tag `TXXX:VIBE DJ BPM` is trusted.
- **Headphones (PHONES):** the next track in your ear on a host with a cue output (the VIBE FM, the VIBE FM Plugin). In a
  browser, optional SPLIT (master left, cue right) for a Y-cable.
- **AUTO VOL:** every track equally loud.

Play it in your browser: **[play.thefirmwarehub.com](https://play.thefirmwarehub.com/?fw=player)** · the FM-1 Jukebox:
**[thefirmwarehub.com/dj](https://thefirmwarehub.com/dj/)** · more about VIBE FM: **[thefirmwarehub.com/platform](https://thefirmwarehub.com/platform/)**

## Build

VIBE DJ is plain C for the VIBE FM SDK (`vibe.h`, `vibe_gfx.h`, `vibe_dsp.h`) and builds to one `.wasm` that runs on the
VIBE FM, in the VIBE FM Plugin and in the browser. The SDK is in early access – ask for it at
[thefirmwarehub.com/platform](https://thefirmwarehub.com/platform/). With it: put this folder into the SDK's `firmware/`
as `player/` and run `sh tools/build.sh player`.

`cheatsheet.html` is what the Web Player shows beside the device.

## License

VIBE DJ: **GPL-3.0** (`LICENSE`), © 2026 Sven Trogus (zvenson).
The MP3 decoder [minimp3](https://github.com/lieff/minimp3) by lieff: CC0 (`minimp3.LICENSE`).
