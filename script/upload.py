"""
Trunk-Recorder ``uploadScript`` adapter for Squelch.

Status: scaffolding. The CLI argument shape and multipart field set are pinned
to Squelch's native ``/api/v1/calls`` contract. The actual POST is not yet
implemented — :func:`build_request` returns the prepared call without sending.

The eventual flow is:

1. Trunk-Recorder calls this script per finished call with arguments matching
   its ``uploadScript`` contract: positional args ``<wav> <metadata.json>``.
2. The script reads the metadata JSON, maps TR's field names to Squelch's
   native field set (``systemId``, ``talkgroupId``, ``startedAt`` as RFC 3339,
   ``frequencyHz``, ``durationMs``, ``unitId``), and POSTs to
   ``${SQUELCH_URL}/api/v1/calls`` with ``Authorization: Bearer ${SQUELCH_API_KEY}``.
3. Exit code 0 on success, non-zero on failure (TR retries based on this).
"""

from __future__ import annotations

import argparse
import json
import logging
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

logger = logging.getLogger("squelch-tr-upload")


@dataclass
class UploadRequest:
    """A prepared multipart upload, ready to be sent to ``/api/v1/calls``."""

    url: str
    fields: dict[str, str]
    audio_path: Path


def parse_started_at(raw: Any) -> str:
    """Coerce a TR ``start_time`` value to RFC 3339 (UTC, ``Z`` suffix).

    Trunk-Recorder writes unix epoch seconds; Squelch's native upload requires
    RFC 3339 and rejects unix timestamps with ``validation_failed``.
    """
    if isinstance(raw, str):
        # Trust the caller; let Squelch reject malformed values.
        return raw
    if isinstance(raw, (int, float)):
        return (
            datetime.fromtimestamp(float(raw), tz=timezone.utc)
            .replace(microsecond=0)
            .isoformat()
            .replace("+00:00", "Z")
        )
    raise ValueError(f"unrecognised start_time type: {type(raw).__name__}")


def build_request(
    server: str,
    api_key: str,
    audio_path: Path,
    metadata: dict[str, Any],
) -> UploadRequest:
    """Map a Trunk-Recorder metadata blob to Squelch's native field set.

    Does **not** send the request. Returning the prepared object lets the
    caller (and tests) inspect the multipart shape without network I/O.
    """
    del api_key  # unused until the real POST lands; kept for signature stability.

    fields: dict[str, str] = {
        "systemId": str(metadata["short_name"] or metadata.get("system", "")),
        "talkgroupId": str(metadata["talkgroup"]),
        "startedAt": parse_started_at(metadata["start_time"]),
    }
    if "freq" in metadata:
        fields["frequencyHz"] = str(int(metadata["freq"]))
    if "call_length" in metadata:
        fields["durationMs"] = str(int(float(metadata["call_length"]) * 1000))
    if "srcList" in metadata and metadata["srcList"]:
        first_src = metadata["srcList"][0]
        if isinstance(first_src, dict) and "src" in first_src:
            fields["unitId"] = str(first_src["src"])

    url = f"{server.rstrip('/')}/api/v1/calls"
    return UploadRequest(url=url, fields=fields, audio_path=audio_path)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Trunk-Recorder uploadScript adapter for Squelch",
    )
    parser.add_argument("audio", type=Path, help="Path to the WAV/M4A file")
    parser.add_argument("metadata", type=Path, help="Path to the TR metadata JSON")
    parser.add_argument("--server", required=True, help="Squelch base URL")
    parser.add_argument("--api-key", required=True, help="Squelch API key")
    parser.add_argument("--dry-run", action="store_true", help="Print and exit without POSTing")
    args = parser.parse_args(argv)

    logging.basicConfig(level=logging.INFO, format="[%(name)s] %(message)s")

    metadata = json.loads(args.metadata.read_text(encoding="utf-8"))
    request = build_request(args.server, args.api_key, args.audio, metadata)

    if args.dry_run:
        logger.info("would POST to %s with fields=%s", request.url, request.fields)
        return 0

    logger.error("upload not yet implemented (scaffolding); use --dry-run for now")
    return 2


if __name__ == "__main__":
    sys.exit(main())
