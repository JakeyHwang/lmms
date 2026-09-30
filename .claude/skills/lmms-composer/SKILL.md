---
name: lmms-composer
description: Use whenever the user asks to create, edit, arrange, mix, or render music in LMMS, or to recreate a song, artist or genre — "make a beat", "write a chorus", "build Creep by Radiohead", "add a bass line", "make it less echoey". Drives the running LMMS through the agent server with scripts/lmmsctl.py. Not for changing LMMS's own source code.
---

# LMMS composer

You compose in the user's **running** LMMS by calling tools over a localhost socket. You are a
producer: you decide tempo, key, form and sounds, you state the decision in one line, and you build
the whole arrangement — not a loop.

`references/theory.md` — keys, ticks, scales, chords, progressions, GM patch and drum tables, genre
recipes. `references/tools.md` — all 37 tools, readable. `lmmsctl.py tools --schema` is the
authoritative schema.

## Connect

1. The agent server is on by default. If it has been switched off: **Settings → AI → "Allow an
   external agent to control LMMS over localhost (takes effect after restart)"**, then restart LMMS.
   Equivalent: `<ai agentserver="1"/>` in `~/.lmmsrc.xml` before launch.
2. Start LMMS under the process supervisor:
   `hub start name=lmms application=C:/git_repos/lmms/build/lmms.exe ready.log="AiAgentServer: listening"`.
   The ready line is `AiAgentServer: listening on 127.0.0.1:<port>, token file <path>`.
3. Handshake: `python .claude/skills/lmms-composer/scripts/lmmsctl.py call ping` → `{"ok": true,
   "version": "…"}`. Then `… lmmsctl.py tools` — it must list **37** tools.

The client finds LMMS through the token file `{"port", "token"}` named `.lmms-agent.json`, written
in the LMMS working directory. It is looked for in this order, first hit wins: `$LMMS_AGENT_FILE`
(a full path to the file, and nothing else is tried), the `workingdir` in `~/.lmmsrc.xml` when that
file exists, `~/Documents/lmms`, `~/OneDrive/Documents/lmms`, `~/lmms`. A development build keeps
its `.lmmsrc.xml` next to the executable in `build/` rather than in `$HOME`, so there the working
directory is the Documents one — `C:/Users/<you>/OneDrive/Documents/lmms/` when OneDrive has
redirected Documents. `lmmsctl.py` prints every path it tried when it finds none.

**No token file, or `ping` exits 2:** LMMS is not running, or the setting is off. Say exactly that —
"LMMS isn't exposing the agent server: open Settings → AI, tick *Allow an external agent to control
LMMS over localhost*, and restart LMMS" — and stop. Never guess a port, never edit `.lmmsrc.xml`
behind the user's back while LMMS is running (it rewrites the file on exit).

CLI:

```
lmmsctl.py tools [--schema]              # names + one-line descriptions, or full JSON schemas
lmmsctl.py summary                       # get_project_summary, pretty-printed
lmmsctl.py call <tool> '<json>'          # args inline
lmmsctl.py call <tool> --args-file f.json
lmmsctl.py call <tool> -                 # args on stdin
```

Exit 0 = `ok:true`, 1 = the tool returned `ok:false`, 2 = transport failure. Result JSON goes to
stdout, errors to stderr. Use the CLI for one-off calls; use the Python module for a song.

## Research first

**A named song** — the goal is a faithful recreation, so look for the real thing before writing a
note, in this order, and say what you found with sources:

1. **A MIDI transcription.** `web_search "<artist> <title> midi"` — BitMidi (`bitmidi.com/uploads/<id>.mid`,
   needs a browser `User-Agent` + `Referer` header), midiworld, freemidi, mididb demos, Nonstop2k
   (paid). Download every candidate into `<workingdir>/samples/midi/` and inspect it *before*
   importing (format, ppq, tempo, tracks with note counts / channels / programs — a 40-line stdlib
   parser does it; channel 10 = drums). Pick the one whose track list matches the record's
   instrumentation, not the biggest file. Then `import_midi` with the SoundFont path; each channel
   becomes an `sf2player` track, program changes become patches.
2. **Scores and tabs** when no usable MIDI exists, or to fix what the MIDI got wrong: Ultimate
   Guitar tabs (`read` the page — picking patterns, voicings, strum direction), drum tabs, MuseScore
   / Hooktheory (melody and rhythm), sheet-music previews. Write the parts from them with the flow
   rules below.
3. **Facts either way:** tempo (BPM), key, chord progression *per section*, form with bar counts,
   instrumentation and feel, from at least two independent sources. Never guess a named song's
   tempo or key; if sources disagree, say which you took and why.

**A genre or mood** — pick a recipe from `references/theory.md`, name it ("house, 126 BPM,
four-on-the-floor"), and build. No search needed.

After an import: `get_project_summary`, then re-voice (replace karaoke lead instruments such as pan
flute with the record's instrument via `set_params` on `patch`, or a fresh `add_sf2_track` + moved
notes), remove empty or duplicate tracks, fix levels and pans, and keep the imported tempo
automation unless it is wrong. The result is the user's private recreation; say in one line that it
is derived from a transcription.

## Making it flow (read before writing any part by hand)

Notes that are "just notes" come from short, isolated, equal-velocity events. Real players connect:

- **Let ring.** Arpeggios and picked chords sustain until the chord changes: `len` runs to the end of
  the chord, so notes overlap and ring as a chord. Piano and pad chords likewise.
- **Strums are not simultaneous.** Offset the strings of one chord 2-3 ticks apart, low to high on
  a down-stroke, reversed on an up-stroke, with the later strings a little quieter.
- **Connected rhythm parts.** Chugs, 8th-note bass, comping: `len` reaches the next note (a 2-4 tick
  gap at most). Accent on-beat notes (+12 vol), lighten off-beats. Let the last chord of a phrase
  ring across the bar line now and then.
- **Legato bass with approach notes.** Notes touch; step into the next chord's root by a semitone
  or from the fifth on the last 8th of the bar.
- **Contour, not lines.** Velocities follow the phrase (rise into the bar, fall out of it); ±3 tick
  timing humanisation on hats, arpeggios and melody; ghost notes on the snare; open hat lifting into
  the next bar.
- **Glue.** A sustained strings or pad layer at 40-55 volume under the loud sections, held across
  the chord, is what makes a section "sway".
- **Melody is legato by default.** Each note's `len` runs until the next starts (+4 ticks); breathe
  at phrase ends only.

## Build order

`checkpoint` → `get_project_summary` → `set_head` → palette → sections with `add_clips` → mix →
verify. In detail:

1. `checkpoint` — one snapshot before the batch. Undo history is paused until `commit` or `revert`.
2. `get_project_summary` — track indices, existing clips, and **`ticksPerBar`** (192 in 4/4). Every
   position below is computed from it, never assumed.
3. `set_head` — bpm and time signature first; changing the signature later does not move clips.
4. Palette — one track per role (see below). Record each returned `index`.
5. Sections — **one `add_clips` call per track** covering the whole song: one clip per section, at
   `clipPos = (bar - 1) * ticksPerBar`, `len` = section length, `name` = the section name. Reuse the
   same note list for repeated sections and vary the second chorus (octave up, extra layer, fill).
6. Mix — `set_track` for level/pan, then `add_effect` / `set_params`, then `add_automation` for
   builds and sweeps.
7. Verify — `render`, `check_render.py`, `get_project_summary`.

The default project has four empty starter tracks. Either reuse them or `remove_track` them (indices
shift down after each removal — re-read the summary).

## Palette

**SoundFont first.** Real instruments come from GeneralUser GS through `add_sf2_track`:

```
python .claude/skills/lmms-composer/scripts/fetch_soundfont.py          # once, ~30 s
python .claude/skills/lmms-composer/scripts/fetch_soundfont.py --dest D # optional target dir
```

It prints the absolute `.sf2` path (default `<workingdir>/samples/soundfonts/GeneralUser-GS.sf2`) and
is a no-op when the file is already there. Pass that path to every `add_sf2_track` call with a patch
number from the GM table in `references/theory.md`: bank 0 for melodic patches (0 piano, 27 clean
guitar, 33 finger bass, 48 strings…), **bank 128 for the drum kit** (patch 0 standard, 25 TR-808,
24 electronic). One kit track plays the whole drum part — kick 36, snare 38, closed hat 42, open hat
46, crash 49.

**LMMS synths** when the style is synth-native: `kicker` (kick), `lb302` (acid/303 bass),
`tripleoscillator` (saws, supersaw leads, sub bass), `watsyn`/`organic`/`monstro` (pads and stabs),
`freeboy`/`nes`/`sid` (chiptune). Use `add_instrument_track`, or `list_presets` → `get_preset_xml` →
wrap in `<track type="0" name="…">…</track>` → `add_track` for a ready-made sound.

One track per musical role — drums, bass, chords/pad, lead, FX — named for the role. Never one track
per drum voice when a kit track will do.

## Writing parts

For anything past a couple of calls, write a per-song build script and run it once. Positions inside
a clip are relative to the clip; `clipPos` is absolute.

```python
import sys; sys.path.insert(0, r"C:/git_repos/lmms/.claude/skills/lmms-composer/scripts")
from lmmsctl import Lmms

TPB = 192                                      # ticks per bar, from get_project_summary
SF2 = r"C:/Users/you/lmms/samples/soundfonts/GeneralUser-GS.sf2"
TRIAD = {"min": (0, 3, 7), "maj": (0, 4, 7), "m7": (0, 3, 7, 10)}
bar   = lambda n: (n - 1) * TPB                # 1-based bar  -> absolute ticks
beat  = lambda b: (b - 1) * (TPB // 4)         # 1-based beat -> ticks within a bar
chord = lambda root, kind, pos, length, vol=70: [
    {"pos": pos, "len": length, "key": root + i, "vol": vol} for i in TRIAD[kind]]

m = Lmms()
m.ok("checkpoint")
m.ok("set_head", {"bpm": 96, "timesigNum": 4, "timesigDen": 4})
kit  = m.ok("add_sf2_track", {"name": "Drums", "file": SF2, "bank": 128, "patch": 0})["index"]
bass = m.ok("add_sf2_track", {"name": "Bass",  "file": SF2, "patch": 33})["index"]
keys = m.ok("add_sf2_track", {"name": "Keys",  "file": SF2, "patch": 4})["index"]

PROG = [(45, "min"), (41, "maj"), (48, "maj"), (43, "maj")]   # i VI III VII in A minor
drums, low, pad = [], [], []
for i, (root, kind) in enumerate(PROG):                        # four bars
    at = i * TPB
    drums += [{"pos": at + beat(b), "len": 12, "key": 36, "vol": 110} for b in (1, 3)]
    drums += [{"pos": at + beat(b), "len": 12, "key": 38, "vol": 100} for b in (2, 4)]
    drums += [{"pos": at + e * 24, "len": 12, "key": 42, "vol": 70} for e in range(8)]
    low   += [{"pos": at + e * 24, "len": 22, "key": root - 12, "vol": 95} for e in range(8)]
    pad   += chord(root + 12, kind, at, TPB)

for track, notes in ((kit, drums), (bass, low), (keys, pad)):
    m.ok("add_clips", {"track": track,
        "clips": [{"clipPos": bar(1), "len": 4 * TPB, "name": "Verse", "notes": notes}]})
print(m.call("get_project_summary"))
```

`call(tool, args)` returns the result dict as-is; `ok(tool, args)` raises `LmmsToolError` when the
tool reports `ok:false` — use `ok` everywhere in a build script so it stops at the first bad call.
`add_clips` validates every note before writing any, so one bad note creates nothing.

Scale it up by making `PROG` a section map (`[("Intro", 8, …), ("Verse", 16, …)]`) and emitting one
clip per section per track in a single `add_clips` call per track.

## Mix discipline

Starting levels with `set_track` (`volume` 0–200, 100 = unity): **drums 100, bass 90, chords 70,
lead 85**. Pan hats and keys slightly (±15 to ±30); keep kick, snare and bass centred.

Reverb on **pads, keys and leads only, ≤ 25 % wet**. Drums and bass stay dry — a wet low end is the
single most common way a mix turns to mud. Call `describe_model_tree` on the track once and read the
real ReverbSC parameter names before `add_effect` / `set_params`; do not invent knob names (one
unknown name rejects the whole `set_params` call).

No effect without a stated purpose. `add_effect` with a compressor on `mixerChannel: 0` only if the
render actually clips. `add_automation` for filter sweeps, builds and fades — one call per
parameter, all points in it.

## Verify

```
lmmsctl.py call stop
lmmsctl.py call render '{"path": "C:/Users/you/lmms/renders/song.wav"}'
python .claude/skills/lmms-composer/scripts/check_render.py C:/Users/you/lmms/renders/song.wav --bpm 96
```

`render` blocks until done and needs playback stopped and **the output folder to exist** — create
`<workingdir>/renders/` first. `check_render.py` prints `channels, sample_rate, duration_s,
peak_dbfs, rms_dbfs, clipped_samples, silent_bars` and exits 1 on clipping or a near-silent file.

Act on what it says: `clipped_samples > 0` → drop the loudest track 10 and re-render, or compress
channel 0. `silent_bars` that are not meant to be silent → a clip is missing or misplaced; check its
`clipPos` against `(bar - 1) * ticksPerBar`. Finish with `get_project_summary` and confirm every
track's clips line up with the section map.

## Hand over

- Happy with it → `commit` (keeps the work, drops the checkpoint, undo resumes).
- Wrong direction → `revert` (restores the checkpoint, discards everything since) and say what you
  will do differently.
- `save` only when the user asks — with a path for an untitled project.
- **GarageBand / another DAW** → `export_midi` to `<workingdir>/renders/<name>.mid` plus a
  `render` mixdown (`.mp3` or `.wav`) as the reference. In GarageBand on iPad: Tracks view → Loop
  Browser → Files → pick the `.mid`; drop the Drums track on a Drums track, the rest on Keyboard
  tracks, then choose sounds there (patches are not carried; the exporter's trailing empty "Kicker"
  track can be deleted). Set the song section length to Automatic first or the import is cut.
- Report: the section map with bar counts, one line per track (role, sound, what it plays), and the
  render path. Offer to share the rendered file. No transcript of tool calls.
