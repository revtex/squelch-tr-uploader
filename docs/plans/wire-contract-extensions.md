# Squelch wire-contract additions for richer TR call metadata

This is a wishlist of Trunk-Recorder `Call_Data_t` fields the plugin already
has access to but cannot send today, because the Squelch
`POST /api/v1/calls` contract does not accept them.

The intent is to add these to Squelch's request schema (and to
[squelch-wire-contract.md](./squelch-wire-contract.md) once they land), then
follow up with a small plugin change that emits the new multipart parts.

All fields below are present on the `Call_Data_t` struct passed to TR's
`Plugin_Api::call_end` hook, so adding them on the plugin side after the
contract is extended is a handful of `add_text_part(...)` lines.

## Field-by-field proposal

### Tier 1 — high user value, trivial schema addition

| Wire field          | Type                | Source on `Call_Data_t`                | Why                                                                                                    |
|---------------------|---------------------|----------------------------------------|--------------------------------------------------------------------------------------------------------|
| `emergency`         | `bool`              | `emergency`                            | Top-level emergency flag for the call. Today only available per-source inside the `sources[]` array.   |
| `signalDb`          | `number` (float)    | `signal`                               | Signal level at the receiver. Coverage maps, quality alerts.                                           |
| `noiseDb`           | `number` (float)    | `noise`                                | Noise floor. Combined with `signalDb` gives SNR.                                                       |
| `endedAt`           | RFC 3339 UTC string | `stop_time` (epoch sec)                | Native call-end timestamp instead of inferring from `startedAt + durationMs`. Simplifies range queries. |
| `priority`          | `int` (0–15)        | `priority`                             | Talkgroup priority for routing/alerting.                                                               |

### Tier 2 — per-transmission timeline (`transmissions[]`)

Add a top-level array, one element per radio keyup, in chronological
order. This is what drives a "who's talking right now" indicator in the
UI: the existing `sources[]` only lists each unit's *first* appearance,
so once Unit A → Unit B → Unit A happens the UI loses track of A's
second turn.

Source: `Call_Data_t::transmission_list` — TR already builds this
internally; we just don't surface it.

| Field            | Type             | Source on `Transmission`     | Notes                                                                                                                                                |
|------------------|------------------|------------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------|
| `unitId`         | `int64`          | `source`                     | Radio unit that keyed up.                                                                                                                            |
| `startMs`        | `int64`          | derived: `start_time - call.start_time`, *milliseconds* | Offset within the **merged audio** file. Use audio-time, not wall-clock — the UI binds to `<audio>.currentTime` and that starts at 0. |
| `endMs`          | `int64`          | derived: `stop_time - call.start_time`, *milliseconds*  | Same basis as `startMs`. `endMs - startMs` should equal the transmission's audible duration in the merged file.                       |
| `frequencyHz`    | `int64`          | `freq`                       | The frequency this transmission used. Tells the UI when a freq change happened mid-call.                                                             |
| `errorCount`     | `int64`          | `error_count`                | Voice-codec errors for this keyup only. Lets the UI flag the *one* bad transmission inside an otherwise-clean call.                                  |
| `spikeCount`     | `int64`          | `spike_count`                | Audio spikes for this keyup only.                                                                                                                    |
| `sampleCount`    | `int64`          | `sample_count`               | Number of audio samples from this transmission. Useful sanity check (`sampleCount / sampleRate ≈ (endMs-startMs)/1000`).                              |

Wire format: a single multipart text part named `transmissions`, value
is a JSON array, same pattern as the existing `sources` / `frequencies`
/ `patches` parts.

Notes for the Squelch side:

- Time origin matters. **Compute `startMs`/`endMs` plugin-side, relative
  to the call's overall `start_time`**, so the UI can render a timeline
  without doing date math. If TR's transmission `start_time` is in
  seconds (it is), multiply the delta by 1000 — sub-second granularity
  comes from `Transmission::sample_count` / sample rate if you want it,
  but ms-resolution is plenty for "now talking" display.
- Long, busy calls can produce dozens of entries — store as a single
  JSONB column or a child table; either works.
- Older plugin versions won't send this, so the field must be optional.

### Tier 3 — diagnostic / advanced

| Wire field                  | Type                | Source on `Call_Data_t`        | Why                                                                       |
|-----------------------------|---------------------|--------------------------------|---------------------------------------------------------------------------|
| `frequencyErrorHz`          | `int`               | `freq_error`                   | RX frequency offset for diagnostics.                                      |
| `audioBitrate`              | `string` ("32k", …) | `audio_bitrate`                | Encoding bitrate reference (matters when `compress_wav` is on).            |
| `filteredTransmissionCount` | `int`               | `min_transmissions_removed`    | Transparency on how many short transmissions TR discarded.                |

### Tier 4 — `sources[]` array enrichment

Add an optional element to each existing `sources[]` entry:

| Field           | Type     | Source on `Call_Source` | Why                                                                                |
|-----------------|----------|-------------------------|------------------------------------------------------------------------------------|
| `unitNameOta`   | `string` | `tag_ota`               | Over-the-air self-announced unit alias, separate from the CSV-resolved `tag`.      |

Today we collapse `tag_ota` into the call-level `talkerAlias`; surfacing
it per-source preserves both names.

## Suggested rollout

1. **Squelch side:** add Tier 1 fields to the request schema, validators,
   DB columns / JSONB column, and indexers. Document them in
   [squelch-wire-contract.md](./squelch-wire-contract.md) under the
   field map.
2. **Tier 2 (`transmissions[]`)** is the highest-leverage follow-up:
   it's what unlocks the "who's talking right now" UI. Land it as a
   second pass once Tier 1 is in.
3. **Plugin side:** in `enqueue_from_call_info`, emit `add_text_part`
   calls guarded by sentinels (`if (call_info.signal != 0.0)`, etc.) so
   older Squelch versions silently ignore unknown parts.
4. Tier 3/4 land as separate, smaller PRs.

## Plugin-side notes for whoever picks this up

- All Tier 1, 3, and 4 fields live on the same `Call_Data_t` struct
  already used by the plugin's `enqueue_from_call_info`. Tier 2 reads
  `Call_Data_t::transmission_list` from the same struct. No new TR
  hooks required.
- Boolean fields go on the wire as the strings `"true"` / `"false"`
  (matches the existing convention for `compress_wav` etc.).
- Float fields: format with the existing `fmt_fixed(value, 2)` helper;
  3 decimal places for `signalDb`/`noiseDb` is plenty.
- RFC 3339 fields: reuse the existing `to_rfc3339_utc(epoch_s)` helper.
- Add a sentinel-skip rule per field — never send `0` / empty / default
  values; that lets older Squelch deployments stay silent and keeps the
  payload small.

## Hooks we are deliberately not adopting

For reference — these `Plugin_Api` hooks exist but do not fit an
upload-on-completion plugin and are out of scope for this contract work:

- Real-time / per-tick: `poll_one`, `audio_stream`, `voice_codec_data`,
  `calls_active`, `trunk_message`, `system_rates`.
- Unit affiliation events: `unit_registration`, `unit_deregistration`,
  `unit_acknowledge_response`, `unit_group_affiliation`, `unit_data_grant`,
  `unit_answer_request`, `unit_location` — only relevant if Squelch grows a
  "live unit presence" feature.
- Setup-time hooks: `setup_recorder`, `setup_system`, `setup_systems`,
  `setup_sources`, `setup_config`, `init` — only relevant if Squelch wants
  TR to push a system inventory at boot.
