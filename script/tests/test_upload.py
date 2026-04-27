"""Smoke tests for upload.py — exercise the field-mapping logic.

Real network tests will be added when the POST path is implemented.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from upload import build_request, parse_started_at  # noqa: E402


def test_parse_started_at_unix_int_to_rfc3339() -> None:
    assert parse_started_at(1700000000) == "2023-11-14T22:13:20Z"


def test_parse_started_at_unix_float_to_rfc3339() -> None:
    # microseconds are dropped — Squelch only requires second precision.
    assert parse_started_at(1700000000.999).startswith("2023-11-14T22:13:")


def test_parse_started_at_passthrough_string() -> None:
    assert parse_started_at("2024-01-02T03:04:05Z") == "2024-01-02T03:04:05Z"


def test_parse_started_at_rejects_unknown_type() -> None:
    with pytest.raises(ValueError):
        parse_started_at(None)  # type: ignore[arg-type]


def test_build_request_maps_tr_metadata_to_native_fields() -> None:
    req = build_request(
        server="https://squelch.example.com/",
        api_key="dummy",
        audio_path=Path("/tmp/x.m4a"),
        metadata={
            "short_name": 1,
            "talkgroup": 100,
            "start_time": 1700000000,
            "freq": 854_000_000,
            "call_length": 12.5,
            "srcList": [{"src": 12345}],
        },
    )

    assert req.url == "https://squelch.example.com/api/v1/calls"
    assert req.fields == {
        "systemId": "1",
        "talkgroupId": "100",
        "startedAt": "2023-11-14T22:13:20Z",
        "frequencyHz": "854000000",
        "durationMs": "12500",
        "unitId": "12345",
    }


def test_build_request_omits_optional_fields() -> None:
    req = build_request(
        server="https://x",
        api_key="dummy",
        audio_path=Path("/tmp/x.m4a"),
        metadata={
            "short_name": 1,
            "talkgroup": 100,
            "start_time": "2024-01-01T00:00:00Z",
        },
    )
    assert "frequencyHz" not in req.fields
    assert "durationMs" not in req.fields
    assert "unitId" not in req.fields


def test_metadata_round_trip_from_json(tmp_path: Path) -> None:
    payload = {
        "short_name": 2,
        "talkgroup": 200,
        "start_time": 1700000000,
    }
    metadata_file = tmp_path / "meta.json"
    metadata_file.write_text(json.dumps(payload), encoding="utf-8")

    metadata = json.loads(metadata_file.read_text(encoding="utf-8"))
    req = build_request("https://x", "k", tmp_path / "x.m4a", metadata)
    assert req.fields["systemId"] == "2"
    assert req.fields["talkgroupId"] == "200"
