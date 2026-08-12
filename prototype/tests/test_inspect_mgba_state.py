import importlib.util
import json
import struct
import subprocess
import sys
import zlib
from pathlib import Path

import pytest


TOOL = Path(__file__).parents[1] / "tools" / "inspect_mgba_state.py"
spec = importlib.util.spec_from_file_location("inspect_mgba_state", TOOL)
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)


def png_chunk(chunk_type: bytes, payload: bytes, *, valid_crc: bool = True) -> bytes:
    crc = zlib.crc32(chunk_type + payload) & 0xFFFFFFFF
    if not valid_crc:
        crc ^= 1
    return struct.pack(">I", len(payload)) + chunk_type + payload + struct.pack(">I", crc)


def synthetic_png(*chunks: bytes, trailing: bytes = b"") -> bytes:
    return module.PNG_SIGNATURE + b"".join(chunks) + trailing


def test_lists_chunks_and_reports_zlib_state_and_gbax(tmp_path):
    state = b"serialized GBA state\x00\x01"
    extra = b"battery save data"
    path = tmp_path / "state.ss1"
    path.write_bytes(
        synthetic_png(
            png_chunk(b"IHDR", struct.pack(">IIBBBBB", 240, 160, 8, 2, 0, 0, 0)),
            png_chunk(b"gbAs", zlib.compress(state)),
            png_chunk(b"gbAx", struct.pack("<II", 7, len(extra)) + zlib.compress(extra)),
            png_chunk(b"IEND", b""),
        )
    )

    inspection = module.inspect_png(path)
    report = module.inspection_dict(inspection)

    assert [chunk.type for chunk in inspection.chunks] == ["IHDR", "gbAs", "gbAx", "IEND"]
    assert inspection.image_width == 240
    assert inspection.image_height == 160
    assert report["state_chunks"][0]["compression"] == {
        "kind": "zlib (deflate)",
        "decompressed_size": len(state),
    }
    assert report["extra_data"][0]["tag"] == 7
    assert report["extra_data"][0]["declared_size"] == len(extra)
    assert report["extra_data"][0]["compression"]["decompressed_size"] == len(extra)
    assert all(chunk.crc_valid for chunk in inspection.chunks)


def test_extracts_raw_gbas_payload_without_decompressing(tmp_path):
    raw_payload = zlib.compress(b"state bytes")
    source = tmp_path / "state.ss1"
    destination = tmp_path / "payload.bin"
    source.write_bytes(
        synthetic_png(png_chunk(b"gbAs", raw_payload), png_chunk(b"IEND", b""))
    )

    assert module.extract_state_payload(source, destination) == raw_payload
    assert destination.read_bytes() == raw_payload
    with pytest.raises(FileExistsError):
        module.extract_state_payload(source, destination)


def test_reports_crc_mismatch_and_trailing_data(tmp_path):
    path = tmp_path / "state.ss1"
    path.write_bytes(
        synthetic_png(
            png_chunk(b"gbAs", b"not compressed", valid_crc=False),
            png_chunk(b"IEND", b""),
            trailing=b"after-iend",
        )
    )

    inspection = module.inspect_png(path)
    report = module.inspection_dict(inspection)
    assert report["crc_errors"] == [0]
    assert report["state_chunks"][0]["compression"]["kind"] == "unknown or uncompressed"
    assert inspection.trailing_bytes == len(b"after-iend")


def test_rejects_truncated_png_and_missing_state(tmp_path):
    truncated = tmp_path / "truncated.png"
    truncated.write_bytes(module.PNG_SIGNATURE + struct.pack(">I", 10) + b"gbAs")
    with pytest.raises(module.PNGFormatError, match="extends past end"):
        module.inspect_png(truncated)

    no_state = tmp_path / "ordinary.png"
    no_state.write_bytes(synthetic_png(png_chunk(b"IEND", b"")))
    with pytest.raises(module.PNGFormatError, match="no gbAs"):
        module.extract_state_payload(no_state)


def test_cli_json_and_explicit_extraction(tmp_path):
    payload = zlib.compress(b"cli state")
    source = tmp_path / "state.ss1"
    destination = tmp_path / "raw.bin"
    source.write_bytes(synthetic_png(png_chunk(b"gbAs", payload), png_chunk(b"IEND", b"")))

    result = subprocess.run(
        [sys.executable, str(TOOL), str(source), "--json", "--extract-state", str(destination)],
        check=True,
        capture_output=True,
        text=True,
    )
    report = json.loads(result.stdout)
    assert report["extracted_state"]["bytes"] == len(payload)
    assert destination.read_bytes() == payload
