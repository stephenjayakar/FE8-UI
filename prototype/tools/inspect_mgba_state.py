#!/usr/bin/env python3
"""Inspect mGBA save states embedded in PNG files.

mGBA stores screenshot save states in PNG private chunks named ``gbAs`` and
``gbAx``.  This module intentionally parses PNG bytes directly; Pillow (or any
image decoder) is not required because image pixels are irrelevant here.
"""

from __future__ import annotations

import argparse
import binascii
import json
import struct
import sys
import zlib
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import BinaryIO, Iterable, Optional, Union

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
_CHUNK_HEADER = struct.Struct(">I4s")
_GBAX_HEADER = struct.Struct("<II")


class PNGFormatError(ValueError):
    """The input is not a safely parseable PNG stream."""


@dataclass(frozen=True)
class PNGChunk:
    """A PNG chunk and its location in the source file."""

    type: str
    length: int
    offset: int
    data: bytes
    crc: int
    calculated_crc: int

    @property
    def crc_valid(self) -> bool:
        return self.crc == self.calculated_crc


@dataclass(frozen=True)
class CompressionInfo:
    kind: str
    decompressed_size: Optional[int] = None
    error: Optional[str] = None


@dataclass(frozen=True)
class ExtraDataInfo:
    chunk_index: int
    tag: int
    declared_size: int
    compressed_size: int
    compression: CompressionInfo


@dataclass(frozen=True)
class Inspection:
    source: str
    file_size: int
    chunks: tuple[PNGChunk, ...]
    state_chunks: tuple[int, ...]
    extra_data: tuple[ExtraDataInfo, ...]
    image_width: Optional[int]
    image_height: Optional[int]
    trailing_bytes: int


def _compression_info(payload: bytes) -> CompressionInfo:
    """Classify a payload without changing or writing it."""

    try:
        decoded = zlib.decompress(payload)
    except zlib.error as exc:
        if len(payload) == 0:
            return CompressionInfo("empty", 0)
        return CompressionInfo("unknown or uncompressed", error=str(exc))
    return CompressionInfo("zlib (deflate)", len(decoded))


def read_png_chunks(source: Union[bytes, bytearray, memoryview, BinaryIO]) -> tuple[PNGChunk, ...]:
    """Read PNG chunks, validating bounds but preserving CRC failures for reporting.

    ``source`` may be bytes or a binary file object.  The returned chunk offsets
    point at each chunk's four-byte length field, matching the PNG file layout.
    """

    if hasattr(source, "read"):
        raw = source.read()
    else:
        raw = bytes(source)
    if not raw.startswith(PNG_SIGNATURE):
        raise PNGFormatError("missing PNG signature")

    chunks: list[PNGChunk] = []
    position = len(PNG_SIGNATURE)
    saw_iend = False
    while position < len(raw):
        chunk_offset = position
        if len(raw) - position < _CHUNK_HEADER.size:
            raise PNGFormatError(f"truncated chunk header at offset {position}")
        length, chunk_type_bytes = _CHUNK_HEADER.unpack_from(raw, position)
        position += _CHUNK_HEADER.size
        if any(byte < 32 or byte > 126 for byte in chunk_type_bytes):
            raise PNGFormatError(f"invalid chunk type at offset {chunk_offset}")
        end = position + length + 4
        if end > len(raw):
            raise PNGFormatError(
                f"chunk {chunk_type_bytes.decode('ascii')} at offset {chunk_offset} "
                f"extends past end of file"
            )
        data = raw[position : position + length]
        crc = struct.unpack_from(">I", raw, position + length)[0]
        calculated_crc = binascii.crc32(chunk_type_bytes + data) & 0xFFFFFFFF
        chunks.append(
            PNGChunk(
                type=chunk_type_bytes.decode("ascii"),
                length=length,
                offset=chunk_offset,
                data=data,
                crc=crc,
                calculated_crc=calculated_crc,
            )
        )
        position = end
        if chunk_type_bytes == b"IEND":
            saw_iend = True
            break
    if not saw_iend:
        raise PNGFormatError("PNG has no IEND chunk")
    return tuple(chunks)


def _image_dimensions(chunks: Iterable[PNGChunk]) -> tuple[Optional[int], Optional[int]]:
    for chunk in chunks:
        if chunk.type == "IHDR" and len(chunk.data) >= 8:
            return struct.unpack_from(">II", chunk.data)
    return None, None


def inspect_png(path: Union[str, Path]) -> Inspection:
    """Inspect a PNG save state without decoding its image pixels."""

    input_path = Path(path)
    raw = input_path.read_bytes()
    chunks = read_png_chunks(raw)
    state_chunks = tuple(index for index, chunk in enumerate(chunks) if chunk.type == "gbAs")
    extra_data: list[ExtraDataInfo] = []
    for index, chunk in enumerate(chunks):
        if chunk.type != "gbAx":
            continue
        if len(chunk.data) < _GBAX_HEADER.size:
            extra_data.append(
                ExtraDataInfo(
                    index,
                    -1,
                    -1,
                    len(chunk.data),
                    CompressionInfo("invalid gbAx payload", error="missing 8-byte header"),
                )
            )
            continue
        tag, declared_size = _GBAX_HEADER.unpack_from(chunk.data)
        compressed = chunk.data[_GBAX_HEADER.size :]
        compression = _compression_info(compressed)
        extra_data.append(
            ExtraDataInfo(index, tag, declared_size, len(compressed), compression)
        )
    width, height = _image_dimensions(chunks)
    consumed = len(PNG_SIGNATURE) + sum(12 + chunk.length for chunk in chunks)
    return Inspection(
        source=str(input_path),
        file_size=len(raw),
        chunks=chunks,
        state_chunks=state_chunks,
        extra_data=tuple(extra_data),
        image_width=width,
        image_height=height,
        trailing_bytes=len(raw) - consumed,
    )


def extract_state_payload(
    path: Union[str, Path],
    output_path: Optional[Union[str, Path]] = None,
    state_index: int = 0,
) -> bytes:
    """Return one gbAs payload exactly as stored, optionally writing it.

    The returned/written bytes are the chunk payload, normally zlib-compressed.
    No decompression or transformation is performed.  Existing output files are
    rejected to avoid overwriting unrelated save data.
    """

    inspection = inspect_png(path)
    if not inspection.state_chunks:
        raise PNGFormatError("PNG contains no gbAs save-state chunk")
    try:
        chunk_number = inspection.state_chunks[state_index]
    except IndexError as exc:
        raise PNGFormatError(
            f"gbAs index {state_index} is out of range (found {len(inspection.state_chunks)})"
        ) from exc
    payload = inspection.chunks[chunk_number].data
    if output_path is not None:
        destination = Path(output_path)
        with destination.open("xb") as output:
            output.write(payload)
    return payload


def _compression_dict(info: CompressionInfo) -> dict[str, object]:
    result: dict[str, object] = {"kind": info.kind}
    if info.decompressed_size is not None:
        result["decompressed_size"] = info.decompressed_size
    if info.error is not None:
        result["error"] = info.error
    return result


def inspection_dict(inspection: Inspection) -> dict[str, object]:
    """Convert inspection data into stable, JSON-friendly metadata."""

    state_details = []
    for index in inspection.state_chunks:
        chunk = inspection.chunks[index]
        state_details.append(
            {
                "chunk_index": index,
                "offset": chunk.offset,
                "length": chunk.length,
                "crc_valid": chunk.crc_valid,
                "compression": _compression_dict(_compression_info(chunk.data)),
            }
        )
    return {
        "source": inspection.source,
        "file_size": inspection.file_size,
        "image": {"width": inspection.image_width, "height": inspection.image_height},
        "chunks": [
            {
                "index": index,
                "type": chunk.type,
                "offset": chunk.offset,
                "length": chunk.length,
                "crc_valid": chunk.crc_valid,
            }
            for index, chunk in enumerate(inspection.chunks)
        ],
        "state_chunks": state_details,
        "extra_data": [
            {
                "chunk_index": item.chunk_index,
                "tag": item.tag,
                "declared_size": item.declared_size,
                "compressed_size": item.compressed_size,
                "compression": _compression_dict(item.compression),
            }
            for item in inspection.extra_data
        ],
        "crc_errors": [
            index for index, chunk in enumerate(inspection.chunks) if not chunk.crc_valid
        ],
        "trailing_bytes": inspection.trailing_bytes,
    }


def _print_human(inspection: Inspection) -> None:
    print(f"source: {inspection.source}")
    print(f"file size: {inspection.file_size} bytes")
    if inspection.image_width is not None:
        print(f"image: {inspection.image_width} x {inspection.image_height}")
    print("chunks:")
    for index, chunk in enumerate(inspection.chunks):
        crc_status = "ok" if chunk.crc_valid else "INVALID"
        print(
            f"  [{index}] {chunk.type} offset={chunk.offset} "
            f"length={chunk.length} crc={crc_status}"
        )
    print(f"gbAs state chunks: {len(inspection.state_chunks)}")
    for state_number, chunk_index in enumerate(inspection.state_chunks):
        chunk = inspection.chunks[chunk_index]
        info = _compression_info(chunk.data)
        detail = f", decompressed={info.decompressed_size} bytes" if info.decompressed_size is not None else ""
        print(
            f"  state[{state_number}]: chunk={chunk_index}, payload={chunk.length} bytes, "
            f"likely compression={info.kind}{detail}"
        )
    print(f"gbAx extra-data chunks: {len(inspection.extra_data)}")
    for item in inspection.extra_data:
        print(
            f"  chunk={item.chunk_index}, tag={item.tag}, declared={item.declared_size} bytes, "
            f"compressed={item.compressed_size} bytes, likely compression={item.compression.kind}"
        )
    if inspection.trailing_bytes:
        print(f"trailing bytes after IEND: {inspection.trailing_bytes}")


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="List and extract mGBA gbAs/gbAx chunks from a PNG save state."
    )
    parser.add_argument("input", type=Path, help="PNG-embedded mGBA save state (.ss1, etc.)")
    parser.add_argument(
        "--extract-state",
        metavar="PATH",
        type=Path,
        help="write the selected gbAs payload exactly as stored; refuses existing paths",
    )
    parser.add_argument(
        "--state-index",
        type=int,
        default=0,
        help="gbAs occurrence to extract (default: 0)",
    )
    parser.add_argument("--json", action="store_true", help="emit machine-readable metadata")
    return parser


def main(argv: Optional[list[str]] = None) -> int:
    parser = _build_parser()
    args = parser.parse_args(argv)
    try:
        inspection = inspect_png(args.input)
        if args.extract_state is not None:
            payload = extract_state_payload(args.input, args.extract_state, args.state_index)
            if not args.json:
                print(f"wrote gbAs state[{args.state_index}] payload: {len(payload)} bytes -> {args.extract_state}")
        if args.json:
            result = inspection_dict(inspection)
            if args.extract_state is not None:
                result["extracted_state"] = {
                    "index": args.state_index,
                    "path": str(args.extract_state),
                    "bytes": len(extract_state_payload(args.input, None, args.state_index)),
                }
            print(json.dumps(result, indent=2, sort_keys=True))
        elif args.extract_state is None:
            _print_human(inspection)
    except (OSError, PNGFormatError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
