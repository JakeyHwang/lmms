# Music theory, sounds and recipes

Everything here is in the units the tools take: **MIDI key numbers** and **ticks**.

## Numbers

**Keys are MIDI numbers.** C1 24, C2 36, C3 48, **C4 60**, C5 72, C6 84. Semitone = +1, octave =
+12. **A4 (440 Hz) = 69.** Note names to offsets from C: C 0, C# 1, D 2, D# 3, E 4, F 5, F# 6, G 7,
G# 8, A 9, A# 10, B 11 — so A2 = 36 + 9 = 45, F#3 = 48 + 6 = 54.

**The instrument base note defaults to 69** (`basenote` in `<instrumenttrack>`): a sample or
SoundFont plays at its natural pitch on key 69, and transposes by semitone either side. Only set
`basenote` when you deliberately want a sample track pitched somewhere else.

**Ticks.** `ticksPerBar` comes from `get_project_summary` — 192 in 4/4. Derived lengths:

|Value|Ticks|Value|Ticks|
|---|---|---|---|
|Whole (bar, 4/4)|192|Eighth triplet|16|
|Half|96|Dotted eighth|36|
|Quarter (beat)|48|2 bars|384|
|Eighth|24|4 bars|768|
|Sixteenth|12|8 bars|1536|
|Thirty-second|6|16 bars|3072|

Bar N starts at `(N - 1) * ticksPerBar` (absolute, for `clipPos`). Beat B inside a bar is
`(B - 1) * ticksPerBar / timesigNum`. In 3/4 and 6/8 a bar is 144 ticks. Note `pos` is **relative to
its clip**; `clipPos` is absolute from song start.

**Velocities.** `vol` 0–200, 100 = normal. 60 ghost note, 80 soft, 100 normal, 120 accent. Vary by
±10 across a hat pattern so it does not sound programmed. `pan` −100 (left) … 100 (right).

**Track levels.** `volume` 0–200, 100 = unity. Tempo 40–250 BPM is sane; outside 10–999 is refused.

## Scales

Offsets from the root key:

|Scale|Offsets|
|---|---|
|Major (ionian)|0 2 4 5 7 9 11|
|Natural minor (aeolian)|0 2 3 5 7 8 10|
|Harmonic minor|0 2 3 5 7 8 11|
|Dorian|0 2 3 5 7 9 10|
|Phrygian|0 1 3 5 7 8 10|
|Mixolydian|0 2 4 5 7 9 10|
|Major pentatonic|0 2 4 7 9|
|Minor pentatonic|0 3 5 7 10|
|Blues|0 3 5 6 7 10|

## Chords

Offsets from the chord root:

|Chord|Offsets|Chord|Offsets|
|---|---|---|---|
|Major|0 4 7|Minor 7 (m7)|0 3 7 10|
|Minor|0 3 7|Major 7 (maj7)|0 4 7 11|
|Diminished|0 3 6|sus2|0 2 7|
|Augmented|0 4 8|sus4|0 5 7|
|Dominant 7|0 4 7 10|Power chord|0 7 12|
|Add9|0 4 7 14|Minor 9|0 3 7 10 14|

Voice chords around keys 48–72; below 48 thirds turn to mud — play the root alone down there.
Inversions: move the lowest note up 12 to keep the top voice moving by small steps.

## Progressions

|Name|Degrees|In A minor / C major|
|---|---|---|
|Pop|I–V–vi–IV|C G Am F → 60 67 69 65 (roots)|
|Ballad|vi–IV–I–V|Am F C G → 57 53 60 55|
|Jazz|ii–V–I|Dm7 G7 Cmaj7 → 62 67 60|
|Minor epic|i–VI–III–VII|Am F C G → 57 53 60 55|
|Minor classic|i–iv–v|Am Dm Em → 57 62 64|
|EDM/trap|i–VI–VII or i–VII–VI–VII|Am F G → 57 53 55|
|Andalusian|i–VII–VI–V|Am G F E → 57 55 53 52|
|12-bar blues|I I I I IV IV I I V IV I V|bars of 4|

## Song form

State a section map with bar counts before building, e.g. `Intro 8 / Verse 16 / Chorus 16 / Verse 16
/ Chorus 16 / Bridge 8 / Outro 8`. Sections are 8/16/32 bars in almost every popular style. Keep one
clip per section per track, named for the section, so the song editor reads like the map.

Repeat with variation: the second verse adds a counter-line, the second chorus doubles the lead an
octave up, the last chorus drops the hats for a bar before coming back.

## SoundFont patches (`add_sf2_track`, GeneralUser GS)

Bank 0 — melodic GM programs:

|#|Patch|#|Patch|#|Patch|
|---|---|---|---|---|---|
|0|Acoustic Grand|29|Overdriven Guitar|52|Choir Aahs|
|1|Bright Piano|30|Distortion Guitar|56|Trumpet|
|4|Electric Piano 1|32|Acoustic Bass|61|Brass Section|
|5|Electric Piano 2|33|Finger Bass|65|Alto Sax|
|16|Drawbar Organ|34|Pick Bass|73|Flute|
|24|Nylon Guitar|35|Fretless|80|Square Lead|
|25|Steel Guitar|38|Synth Bass 1|81|Saw Lead|
|26|Jazz Guitar|40|Violin|88|New Age Pad|
|27|Clean Guitar|42|Cello|89|Warm Pad|
|28|Muted Guitar|48|String Ensemble|||
|||49|Slow Strings|||

Bank 128 — drum kits: **0 Standard**, 8 Room, 16 Power, 24 Electronic, 25 TR-808, 32 Jazz, 40 Brush.

## GM drum map (bank 128, any kit)

|Key|Voice|Key|Voice|
|---|---|---|---|
|35|Acoustic kick|45|Low-mid tom|
|36|Kick (use this one)|46|Open hat|
|37|Side stick|48|High tom|
|38|Snare|49|Crash|
|39|Hand clap|51|Ride|
|40|Snare 2 (electric)|53|Ride bell|
|41|Low tom|54|Tambourine|
|42|Closed hat|56|Cowbell|
|44|Pedal hat|||

One kit track plays the whole drum part. `kicker` ignores the key (use 36). An
`audiofileprocessor` sample voice plays at natural pitch on its base note (69 by default).

## Genre recipes

Each names SoundFont patches where a real instrument fits and an LMMS synth where the style is
synth-native.

**House / techno** — 120–130 BPM. Kick 36 on every beat (0, 48, 96, 144), clap 39 on 2 and 4, open
hat 46 on the offbeats (24, 72, 120, 168), closed hats 42 in sixteenths at vol 60–80. Kit 128:24
(Electronic). Bass: eighth-note roots or an offbeat pulse on `lb302` or `tripleoscillator` saw. Stabs
and pads: `watsyn` or SF2 88/89; automate a `dualfilter` cutoff for the build. Compressor on the bass
for the pumping feel.

**Hip-hop / trap** — 85–100 BPM (trap: 140 with a half-time snare on beat 3). Kit 128:25 (TR-808).
Kick 36 syncopated (0, 72, 96, 168), snare/clap on 2 and 4, hats 42 in eighths with sixteenth and
thirty-second rolls (len 6–12). 808 sub = `kicker` with a long decay, or SF2 38 Synth Bass, on keys
24–36 following the chord roots. Dark minor keys; sparse Electric Piano (SF2 4) or strings (48).
Short reverb on the snare only.

**Lo-fi** — 70–90 BPM. Kit 128:40 (Brush) or 128:32 (Jazz), soft kick, snare at vol 80, hats swung
(offbeats late by 4–8 ticks). Chords: SF2 4 Electric Piano or 26 Jazz Guitar, m7/maj7 voicings,
slight detune. Bass: SF2 32 Acoustic Bass on roots. Gentle low-pass eq on the chords, reverb on
everything but kick and bass, `bitcrush` lightly if asked for tape.

**Pop** — 100–125 BPM. I–V–vi–IV or vi–IV–I–V. Verse sparse (kick + SF2 0 piano), pre-chorus builds
the hats, chorus adds a lead in octaves and a full kit (128:0). Bass SF2 33 Finger Bass on root
eighths, pad SF2 89 Warm Pad, lead `tripleoscillator` saws or SF2 81 Saw Lead. Reverb + delay on the
lead, compressor on mixer channel 0.

**EDM drop** — 126–132 BPM. Build 16 bars: rising cutoff automation on the lead, a snare roll halving
every 4 bars (48 → 24 → 12 → 6 ticks), crash 49 on the drop. Drop 16 bars: supersaw
(`tripleoscillator`, three detuned saws) stabs, kick every beat, sine sub bass on the roots. Big
reverb on the lead only; compressor on the drums.

**Drum and bass** — 170–176 BPM. Breakbeat: kick 36 on 1 and the "and" of 2, snare 38 on 2 and 4,
sixteenth hats, ghost snares at vol 60. Reese bass = two detuned saws (`lb302` or
`tripleoscillator`) on long notes. Minor pads (SF2 88/89) with wide reverb. 32-bar sections.

**Ambient** — 60–90 BPM or no drums at all. Long pads (`watsyn`, `organic`, SF2 88 New Age Pad, 52
Choir Aahs) with slow Volume automation swelling over 8 bars, sparse pentatonic melody (SF2 73
Flute), a drone on the root, big reverb plus delay. 16–32-bar sections.

**Rock / punk** — 120–180 BPM. Kit 128:16 (Power): kick on 1 and 3 (plus the "and" of 3), snare on 2
and 4, crash at section starts. Guitars: SF2 29 Overdriven or 30 Distortion on power chords
(0 7 12); clean arpeggios on SF2 27. Bass SF2 34 Pick Bass doubling the guitar root in eighths.
I–IV–V or vi–IV–I–V.

**Chiptune** — 120–160 BPM. `freeboy` (Game Boy), `nes`, `sid` or `bitinvader`: square lead with
sixteenth-note arpeggios over the chord tones, triangle bass, noise-channel drums. No reverb, 4–8 bar
phrases, bright major or driving minor.

## Project XML cheat-sheet

For `get_track_xml` / `add_track` / `replace_track`, when the convenience tools do not cover
something. It is the full LMMS project format, so everything LMMS can save is reachable here.

```xml
<track type="0|2|5" name="…" muted="0" solo="0">   <!-- 0 instrument, 2 sample, 5 automation -->
  <instrumenttrack vol="100" pan="0" pitch="0" basenote="69" mixch="0">
    <instrument name="tripleoscillator"><tripleoscillator …/></instrument>
    <instrument name="sf2player"><sf2player src="…/GeneralUser-GS.sf2" bank="0" patch="33"/></instrument>
    <instrument name="audiofileprocessor"><audiofileprocessor src="ABSOLUTE PATH" amp="100"/></instrument>
    <fxchain enabled="1" numofeffects="1"><effect name="reverbsc"><…/></effect></fxchain>
  </instrumenttrack>
  <midiclip pos="0" len="192" name="Verse"><note key="60" vol="100" pan="0" len="48" pos="0"/></midiclip>
  <sampleclip pos="0" len="768" src="path.wav"/>
</track>
```

`basenote="69"` is the default — the sample or SoundFont sounds at its natural pitch on MIDI key 69.

`get_preset_xml` returns the `<instrumenttrack>` element on its own: wrap it as
`<track type="0" name="Name">…</track>` before `add_track`.
