# Squelch v1 Upload Wire Contract

Source: https://raw.githubusercontent.com/revtex/OpenScanner/dev/backend/internal/handler/calls/upload_v1.go (verified 2026-04-27)

## Route
`POST /api/v1/calls`

## Auth
`Authorization: Bearer <api-key>` ONLY. v1 rejects: `X-API-Key`, `?key=`, form `key=`, JWT-shaped tokens.

## Required fields (multipart form)
- `startedAt` — RFC 3339 / RFC 3339 nano (UTC `Z`). Unix timestamp REJECTED with `validation_failed`.
- `systemId` — int64 string
- `talkgroupId` — int64 string
- `audio` — file part

## Optional integer fields (all int64 string, parse failures silently dropped)
- `frequencyHz`, `durationMs`, `unitId`, `errorCount`, `spikeCount`

## Optional JSON-string fields (passed through as strings)
- `sources`, `frequencies`, `patches`

## Optional string fields
- `talkgroupLabel`, `talkgroupTag`, `talkgroupGroup`, `talkgroupName`, `talkerAlias`
- `systemLabel` (used only on auto-create)
- `site`, `channel`, `decoder`

## Audio part
- Field name: `audio`
- 50 MiB limit (server-side `MaxBodySize(50<<20)`)
- Recognized Content-Types: audio/mpeg, audio/mp3, audio/wav, audio/x-wav, audio/ogg, audio/aac, audio/m4a, audio/mp4, audio/x-m4a, audio/opus. Unknown → application/octet-stream (still accepted).

## Error envelope (all non-2xx)
```json
{ "error": { "code": "...", "message": "...", "details": { ... } } }
```
Codes: `validation_failed` (400), `invalid_credentials` (401), `system_not_found` / `talkgroup_not_found` (422), `duplicate_call` (409), `rate_limited` (429).

## Success (200)
`{"id": <int>, "message": "Call imported successfully."}`

## Connectivity check
`POST /api/v1/calls/test` — 204 on valid Bearer, 401 otherwise.
