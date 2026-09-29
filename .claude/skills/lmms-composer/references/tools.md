# Tool reference

`python scripts/lmmsctl.py tools --schema` prints the **authoritative** JSON schema straight from the
running LMMS. This file is the readable companion: 36 tools, what each is for, its arguments, what it
returns, and the mistake that bites.

Every result is an object with `ok`. `Lmms.call()` hands it back as-is; `Lmms.ok()` raises
`LmmsToolError` when `ok` is `false`. Times are **ticks**, keys are **MIDI numbers**, volumes are
0–200 with 100 = unity. Track indices come from `get_project_summary` and shift after `remove_track`.

Groups: [Meta](#meta) · [Project](#project) · [Convenience](#convenience) ·
[Discovery](#discovery) · [Actions](#actions)

## Meta

### ping

Liveness check — the first call after connecting.

- **Args:** none.
- **Returns:** `{ok, version}` — the LMMS version string.

### list_tools

Every tool with its JSON schema, in OpenAI function format. `lmmsctl.py tools` wraps this.

- **Args:** none.
- **Returns:** `{ok, tools: [{type, function: {name, description, parameters}}]}`.
- **Gotcha:** the count is the ground truth for whether the build has all 36 tools.

### checkpoint

Snapshot the whole project in memory so `revert` can restore it. Take one before a batch of edits.

- **Args:** none.
- **Returns:** `{ok}`.
- **Gotcha:** replaces any earlier checkpoint, and undo history is paused until `revert` or `commit`.

### revert

Reload the project from the last checkpoint and drop it, discarding every edit made since.

- **Args:** none.
- **Returns:** `{ok}`, or an error when no checkpoint is held.
- **Gotcha:** not an undo step — it throws away *everything* since the checkpoint.

### commit

Keep the current project state and drop the checkpoint; undo history resumes.

- **Args:** none.
- **Returns:** `{ok}`, or an error when no checkpoint is held.
- **Gotcha:** it does not save to disk. Use `save` for that.

## Project

### get_project_summary

Compact state of the open project: bpm, time signature, `ticksPerBar`, `lengthBars`, and every track
(index, type, instrument, volume, muted, clips with pos/len/noteCount). First call of every turn and
last call to verify; far cheaper than `get_track_xml`.

- **Args:** none.
- **Returns:** the summary object.
- **Gotcha:** indices shift after `remove_track` — re-read it.

### get_head

Tempo, time signature, master volume and master pitch only. Use it when you need `masterVol` or
`masterPitch`, which the summary omits.

- **Args:** none.
- **Returns:** `{bpm, timesigNum, timesigDen, masterVol, masterPitch}`.

### set_head

Set tempo, time signature, master volume/pitch. Call once at the start of a song, before writing
notes — `ticksPerBar` depends on the time signature.

- **Args:** all optional — `bpm` (integer, 10..999), `timesigNum` (integer, beats per bar),
  `timesigDen` (integer, beat unit), `masterVol` (integer, 0..200), `masterPitch` (integer, −12..12
  semitones).
- **Returns:** the new head.
- **Gotcha:** changing the time signature later does not move existing clips.

### get_track_xml

The full `<track>` element of one track in `.mmp` format: instrument settings, effect chain, clips
and notes. Use it for something the convenience tools do not expose, or as a template.

- **Args:** `index` (integer, **required**) — track index.
- **Returns:** `{index, xml}`.
- **Gotcha:** fails over 64 KB; fall back to `get_project_summary`.

### add_track

Append a track from `<track>` XML — from `get_track_xml`, or `<track type="0" name="…">` wrapping
`get_preset_xml` output. For preset sounds, clones, and anything the convenience tools lack.

- **Args:** `xml` (string, **required**) — `<track type="0|1|2|5" name="…">…</track>`.
- **Returns:** `{index}`.
- **Gotcha:** current `.mmp` format only; `local:` plugin paths are rejected.

### replace_track

Swap the track at `index` for new `<track>` XML, keeping its position — for changing a track's
instrument or effects wholesale after editing `get_track_xml` output.

- **Args:** `index` (integer, **required**), `xml` (string, **required**).
- **Returns:** `{index}`.
- **Gotcha:** automation clips on other tracks that targeted the old track's parameters are
  disconnected.

### remove_track

Delete a track and all its clips.

- **Args:** `index` (integer, **required**).
- **Returns:** `{trackCount}`.
- **Gotcha:** every track after it moves down one index; re-read `get_project_summary`.

### get_mixer_xml

The whole mixer as `<mixer>` XML: channels, names, volumes, sends, effect chains. Edit it and pass it
to `set_mixer_xml` when you need buses or sends.

- **Args:** none.
- **Returns:** `{xml}`.
- **Gotcha:** for plain channel effects `add_effect` with `mixerChannel` is enough.

### set_mixer_xml

Replace the entire mixer from `<mixer>` XML.

- **Args:** `xml` (string, **required**) — `<mixer>…</mixer>`.
- **Returns:** `{channels}`.
- **Gotcha:** it replaces everything; tracks keep their channel numbers, so keep the channels they
  point at.

## Convenience

### add_instrument_track

Append an instrument track loading a plugin by name (see `list_instruments`). One call per part:
drums, bass, chords, lead. For a preset sound use `get_preset_xml` + `add_track`; for a realistic
instrument use `add_sf2_track`.

- **Args:** `name` (string) — track name; `instrument` (string) — plugin name, e.g.
  `tripleoscillator`, `kicker`, `sf2player`; `mixerChannel` (integer, 0 = master).
- **Returns:** `{index}` for `add_clips` / `add_notes` / `set_track`.
- **Gotcha:** omitting `instrument` gives a silent empty track.

### add_sf2_track

Append an instrument track playing one SoundFont preset through `sf2player` — the default palette for
realistic instruments: piano, guitars, bass, drum kits, strings, brass.

- **Args:** `file` (string, **required**) — absolute path to the `.sf2`; `name` (string) — track
  name; `bank` (integer, 0..128, default 0; **128 = drum kits**); `patch` (integer, 0..127 GM
  program, default 0); `mixerChannel` (integer, 0 = master).
- **Returns:** `{index}`.
- **Gotcha:** the file must be an absolute path inside the allowed roots — the LMMS working
  directory's `samples/soundfonts/` is one, which is where `fetch_soundfont.py` puts it. Bank 0
  melodic: 0 piano, 27 clean guitar, 29 overdriven, 30 distortion, 33 finger bass, 48 strings. Bank
  128 drums: patch 0 standard; GM map 36 kick, 38 snare, 42 closed hat, 46 open hat, 49 crash,
  51 ride. Full tables in `theory.md`.

### add_notes

Write notes into one MIDI clip on an instrument track, creating the clip at `clipPos` if it is
missing. For a single clip, or editing one; several clips per track → `add_clips`.

- **Args:** `track` (integer, **required**); `clipPos` (integer, **required**) — clip start in
  absolute ticks, bar N = `(N-1)*ticksPerBar`; `notes` (array, **required**) — each
  `{pos, len, key, vol?, pan?}` where `pos` (ticks from clip start) and `len` (> 0) and `key`
  (0..127, 60 = C4) are required, `vol` 0..200 default 100, `pan` −100..100 default 0;
  `len` (integer) — clip length in ticks, must cover the notes; `name` (string) — clip name in the
  song editor; `clear` (boolean) — replace the existing clip's notes instead of adding to them.
- **Returns:** `{track, clipPos, len, noteCount}`.
- **Gotcha:** without `len` the clip auto-grows to whole bars covering its notes. Note `pos` is
  relative to the clip, `clipPos` is absolute.

### add_clips

Batch `add_notes`: several clips on one instrument track in one call — the way to lay out a song
section by section, one call per track.

- **Args:** `track` (integer, **required**); `clips` (array, **required**) — each
  `{clipPos, notes, len?, name?, clear?}` exactly as `add_notes`.
- **Returns:** `{track, clipCount, noteCount, clips}`.
- **Gotcha:** all clips are validated before any is written — one bad note creates nothing.
  `clipPos` must equal the start of a clip you want to reuse.

### remove_clip

Delete the clip that starts at `clipPos` on a track (MIDI, sample or automation).

- **Args:** `track` (integer, **required**); `clipPos` (integer, **required**) — clip start in
  absolute ticks, as listed by `get_project_summary`.
- **Returns:** `{track, clipCount}`.
- **Gotcha:** `clipPos` must match the clip's start exactly. To rewrite notes in place use
  `add_notes` with `clear:true`.

### set_track

Quick mix of one track: name, volume, pan, mute, solo, mixer channel. Faster than
`describe_model_tree` + `set_params`.

- **Args:** `index` (integer, **required**); `name` (string); `volume` (number, 0..200, 100 = unity);
  `pan` (number, −100 left .. 100 right); `muted` (boolean); `solo` (boolean); `mixerChannel`
  (integer, 0 = master).
- **Returns:** the resulting values.
- **Gotcha:** volume/pan/mixerChannel need an instrument or sample track; for changes over time use
  `add_automation`.

### add_effect

Append an effect plugin to a track's chain or a mixer channel's chain, optionally with initial
parameters.

- **Args:** `effect` (string, **required**) — plugin name from `list_effects`, e.g. `reverbsc`,
  `delay`, `compressor`, `eq`, `bassbooster`, `amplifier`; `track` (integer) **or** `mixerChannel`
  (integer); `params` (object) — `{name: value}` as listed by `describe_model_tree`.
- **Returns:** `{effectIndex, params}` — `effectIndex` is the `effect:N` target for `set_params`.
- **Gotcha:** give `track` **or** `mixerChannel`, not both; parameter names come from
  `describe_model_tree`.

### set_params

Set named parameters on a track, its instrument, or an effect — sound design work. Names are
case-insensitive; `Parent>Name` paths disambiguate duplicates.

- **Args:** `target` (string, **required**) — `track` | `instrument` | `effect:N`; `params` (object,
  **required**) — `{name: value}`; `track` (integer) or `mixerChannel` (integer, then `target` must
  be `effect:N`).
- **Returns:** `{applied}` with clamped values.
- **Gotcha:** one unknown name rejects the whole call — check `describe_model_tree` first. For
  level/pan/mute use `set_track`.

### describe_model_tree

Every automatable parameter (name, path, value, min, max, automated) of a track, its instrument and
each effect — or of a mixer channel's effects. Call it before `set_params` or `add_automation` on
anything but track Volume/Panning.

- **Args:** `track` (integer) **or** `mixerChannel` (integer, 0 = master).
- **Returns:** `{track, instrument, effects}`.
- **Gotcha:** large for complex instruments — call once and remember the names.

### add_automation

New automation track with one clip driving a parameter through points over time: filter sweeps,
volume builds, fades. Static values belong in `set_params` / `set_track`.

- **Args:** `target` (string, **required**) — `track` | `instrument` | `effect:N`; `model` (string,
  **required**) — parameter name from `describe_model_tree`, e.g. `Volume`, `Cutoff frequency`;
  `points` (array, **required**) — each `{pos, value}` with `pos` in absolute ticks and `value` in
  the parameter's own range; `track` (integer) or `mixerChannel` (integer, then `target` must be
  `effect:N`); `progression` (string) — `discrete` | `linear` (default) | `cubic`.
- **Returns:** `{automationTrack, model, points}`.
- **Gotcha:** each call adds one automation track — put all points for one parameter in one call.

### add_sample_clip

Place an audio file as a clip on a sample track, creating a track named after the file unless `track`
is given. For one-shots, loops and vocals.

- **Args:** `file` (string, **required**) — path to a wav/ogg/flac/mp3/aiff file; `pos` (integer,
  **required**) — clip start in absolute ticks; `track` (integer) — existing sample track index.
- **Returns:** `{track, pos, len}` — `len` in ticks at the current tempo.
- **Gotcha:** the clip plays at natural speed; tempo changes never stretch it.

## Discovery

### list_instruments

Every instrument plugin installed, with `name` (the id `add_instrument_track` takes), `displayName`
and a one-line description.

- **Args:** none.
- **Returns:** `{instruments}`.
- **Gotcha:** call it when you need the descriptions or are unsure a name exists — do not invent
  plugin names.

### list_effects

Every effect plugin installed, with `name` (the id `add_effect` takes), `displayName` and
description.

- **Args:** none.
- **Returns:** `{effects}`.
- **Gotcha:** LADSPA/VST/LV2 host plugins are listed but cannot be added by name.

### list_presets

Instrument preset files (`.xpf`) in the factory and user preset folders, filtered by a
case-insensitive substring of the path (the folder is the plugin name). Use it when you want a
ready-made sound instead of raw plugin defaults, then feed a path to `get_preset_xml`.

- **Args:** `query` (string) — substring filter, e.g. `bass`, `drum`, `TripleOscillator`; empty for
  all; `limit` (integer) — max results, default 50, max 500.
- **Returns:** `{presets, truncated}`.
- **Gotcha:** query `drum` or `kick` for drum sounds.

### list_samples

Audio files (`.wav`/`.ogg`/`.flac`/`.mp3`/`.aiff`) in the factory and user sample folders, filtered
by a case-insensitive path substring — for `add_sample_clip`.

- **Args:** `query` (string) — substring filter, e.g. `kick`; empty for all; `limit` (integer) — max
  results, default 50, max 500.
- **Returns:** `{samples, truncated}`.
- **Gotcha:** paths are absolute; pass them back unchanged.

### get_preset_xml

Read a `.xpf` preset and return its `<instrumenttrack>` element, upgraded to the current format.

- **Args:** `path` (string, **required**) — absolute path of a `.xpf` file from `list_presets`.
- **Returns:** `{xml}`.
- **Gotcha:** wrap the result as `<track type="0" name="…">…</track>` before `add_track`, then write
  notes on the returned index. `.xiz` files are not presets.

## Actions

### play

Start song playback, optionally from a 1-based bar. For auditioning; verifying edits is
`get_project_summary`'s job, not playback's.

- **Args:** `fromBar` (integer) — 1-based bar to start from; default the current position.
- **Returns:** `{playing: true}`.
- **Gotcha:** refused while rendering; call `stop` before `render`.

### stop

Stop playback.

- **Args:** none.
- **Returns:** `{playing: false}`.

### render

Export the whole song to an audio file; blocks until done.

- **Args:** `path` (string, **required**) — output path, without extension or with the format's
  extension; `format` (string) — `wav` (default), `flac`, `ogg` or `mp3`, inferred from the path's
  extension when omitted.
- **Returns:** `{path, bytes}`.
- **Gotcha:** playback must be stopped and **the output folder must already exist**. Follow it with
  `check_render.py <wav> --bpm <bpm>`.

### save

Save the project as `.mmp`; omit `path` to overwrite the current project file.

- **Args:** `path` (string) — output path; `.mmp` is appended when missing.
- **Returns:** `{path}`.
- **Gotcha:** an untitled project needs a path, and the folder must exist. Only save when the user
  asks.

### export_midi

Write the song as a Standard MIDI File (format 1): one MIDI track per instrument track, notes and
tempo only. For GarageBand, Logic, or any DAW that should get editable notes.

- **Args:** `path` (string, **required**) — output path; `.mid` is appended when missing.
- **Returns:** `{path, bytes}`.
- **Gotcha:** instrument sounds, effects and sample tracks are not carried — the receiving app
  assigns its own; pair it with `render` for a reference mix. LMMS's exporter appends one empty
  track from the pattern store ("Kicker"); the importer can delete it. Drum tracks use GM drum keys,
  so put them on a Drums track in the target app.

### new_project

Discard the current project and load the default template (one TripleOscillator, sample, pattern and
automation track).

- **Args:** `discardChanges` (boolean) — true to throw away unsaved changes.
- **Returns:** `{tracks}`.
- **Gotcha:** refused while there are unsaved changes unless `discardChanges:true`; the default
  tracks are empty, so remove or reuse them.
