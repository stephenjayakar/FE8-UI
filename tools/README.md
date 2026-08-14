# Prototype tools

## `inspect_mgba_state.py`

`inspect_mgba_state.py` reads an mGBA screenshot save state directly as PNG
bytes. It does not use Pillow or decode image pixels. The tool lists every PNG
chunk, identifies the mGBA private `gbAs` (serialized emulator state) and
`gbAx` (extra data) chunks, checks CRCs, reports image dimensions, and attempts
to classify each payload as zlib/deflate or unknown/uncompressed.

mGBA's PNG format stores the `gbAs` payload as a zlib-compressed serialized
state. A `gbAx` payload starts with two little-endian 32-bit values: an extra
data tag and its uncompressed size, followed by zlib-compressed bytes. The
reported `gbAs` extraction is the raw chunk payload: it is not decompressed or
otherwise transformed.

### Inspect

```sh
python3 tools/inspect_mgba_state.py path/to/state.ss1
```

Use `--json` for machine-readable output:

```sh
python3 tools/inspect_mgba_state.py state.ss1 --json
```

### Extract

Pass an explicit destination path to write the raw `gbAs` payload. The tool
refuses to overwrite an existing path (`open(..., "xb")`) and does not touch
the input, ROM, or save files:

```sh
python3 tools/inspect_mgba_state.py state.ss1 \
  --extract-state extracted-state.zlib
```

If a PNG contains multiple `gbAs` chunks, select one with `--state-index N`.
The default is the first (`0`). Extraction requires at least one `gbAs` chunk.

Malformed PNG structure, missing `IEND`, truncated chunks, and invalid chunk
types are reported as errors. CRC mismatches are retained in the report so the
file can be investigated rather than silently changed or rejected. Bytes after
`IEND` are reported as trailing data.
