#!/usr/bin/env python3
"""inspect_ffio.py -- parse, inspect and validate FreeFemCheckpoint .ffio files.

File layout (format FFIOG001; see docs/FILE_FORMAT.md):

    offset  size  field
    ------  ----  -----------------------------
    0       8     magic      = b"FFIOG001"
    8       8     version    (uint64, = 1)
    16      8     global DOF count          Nglobal
    24      8     scalar byte width         (8 for double)
    32      8     writer MPI size           (informational)
    40      8     overlapping input count   (informational)
    48      8     duplicates removed        (informational)
    56      8     reserved
    64      ...   payload: Nglobal doubles, payload[i] = value of global DOF i

Validation policy mirrors the C++ reader (docs/FILE_FORMAT.md):
hard errors for magic/version/width/Nglobal/size; informational fields are
reported as warnings.

Usage:
    python3 tools/inspect_ffio.py FILE ...          # describe files
    python3 tools/inspect_ffio.py --validate FILE   # strict structural check
    python3 tools/inspect_ffio.py --cmp A B [A B]   # payload byte compare
    python3 tools/inspect_ffio.py --hash FILE ...   # streamed payload sha256
"""

import hashlib
import struct
import sys

MAGIC = b"FFIOG001"
HEADER = "<7Q"                      # 7 x uint64 (little-endian host order)
FIELDS = (
    ("version", "format version"),
    ("ndof", "global dofs"),
    ("width", "scalar bytes"),
    ("writer_ranks", "writer ranks"),
    ("input", "input values"),
    ("duplicates", "duplicates"),
    ("reserved", "reserved"),
)
PAYLOAD_OFFSET = 64
CHUNK = 1 << 20


class FormatError(Exception):
    pass


def parse(path):
    with open(path, "rb") as f:
        raw = f.read()
    if len(raw) < PAYLOAD_OFFSET:
        raise FormatError(
            f"{path}: too small ({len(raw)} < 64 header bytes)")
    if raw[0:8] != MAGIC:
        raise FormatError(f"{path}: bad magic {raw[0:8]!r} (expected {MAGIC!r})")
    hdr = struct.unpack(HEADER, raw[8:PAYLOAD_OFFSET])
    h = dict(zip([k for k, _ in FIELDS], hdr))
    # hard validation (must match the C++ reader)
    if h["version"] != 1:
        raise FormatError(f"{path}: unsupported format version {h['version']} "
                          f"(expected 1)")
    if h["width"] != 8:
        raise FormatError(f"{path}: unsupported scalar width {h['width']} "
                          f"(expected 8 = double)")
    if h["ndof"] == 0:
        raise FormatError(f"{path}: empty checkpoint (Nglobal = 0)")
    expected = PAYLOAD_OFFSET + h["ndof"] * h["width"]
    if len(raw) != expected:
        raise FormatError(
            f"{path}: size mismatch: got {len(raw)} bytes, expected "
            f"64 + ndof*width = {expected} "
            f"({'truncated' if len(raw) < expected else 'trailing bytes'})")
    return h, raw


def warnings(h, path):
    out = []
    if h["reserved"] != 0:
        out.append(f"reserved field is {h['reserved']} (expected 0)")
    if h["writer_ranks"] <= 0:
        out.append(f"writer_ranks = {h['writer_ranks']} (expected > 0)")
    if h["input"] < h["ndof"]:
        out.append(f"input values {h['input']} < Nglobal {h['ndof']}")
    if h["duplicates"] > h["input"]:
        out.append(f"duplicates {h['duplicates']} > input {h['input']}")
    return out


def stream_payload_sha256(path, payload_len):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        f.seek(PAYLOAD_OFFSET)
        remaining = payload_len
        while remaining > 0:
            block = f.read(min(CHUNK, remaining))
            if not block:
                raise FormatError(f"{path}: unexpected end of payload")
            h.update(block)
            remaining -= len(block)
    return h.hexdigest()


def describe(path):
    h, raw = parse(path)
    payload_len = len(raw) - PAYLOAD_OFFSET
    print(f"file           {path}")
    print(f"magic          {raw[0:8].decode('ascii')}")
    print(f"version        {h['version']}")
    print(f"global_dofs    {h['ndof']}")
    print(f"scalar_width   {h['width']}")
    print(f"writer_ranks   {h['writer_ranks']}")
    print(f"overlap_entries {h['input']}")
    print(f"duplicates_removed {h['duplicates']}")
    print(f"reserved       {h['reserved']}")
    print(f"file_size      {len(raw)}")
    print(f"payload_size   {payload_len}")
    for w in warnings(h, path):
        print(f"warning        {w}")
    if h["width"] == 8 and h["ndof"]:
        values = struct.unpack(f"<{h['ndof']}d", raw[PAYLOAD_OFFSET:])
        print(f"payload        min={min(values):.17g} max={max(values):.17g} "
              f"mean={sum(values) / len(values):.17g}")
    print(f"payload_sha256 {stream_payload_sha256(path, payload_len)}")
    return h


def payload(path):
    h, raw = parse(path)
    return raw[PAYLOAD_OFFSET:]


def validate(path):
    h, _ = parse(path)  # raises FormatError on any hard problem
    print(f"{path}: VALID (FFIOG001, structurally consistent)")
    for w in warnings(h, path):
        print(f"{path}: warning -- {w}")
    return True


def main(argv):
    if not argv:
        print(__doc__)
        return 0

    if argv[0] == "--validate":
        if len(argv) < 2:
            print("--validate requires at least one file", file=sys.stderr)
            return 2
        ok = True
        for path in argv[1:]:
            try:
                validate(path)
            except FormatError as e:
                print(f"{path}: INVALID -- {e}")
                ok = False
        return 0 if ok else 1

    if argv[0] == "--cmp":
        files = [a for a in argv[1:] if a != "--cmp"]
        if len(files) < 2 or len(files) % 2:
            print("--cmp requires an even number >= 2 of file names",
                  file=sys.stderr)
            return 2
        ok = True
        for i in range(0, len(files), 2):
            a, b = files[i], files[i + 1]
            try:
                pa, pb = payload(a), payload(b)
            except FormatError as e:
                print(f"{a} vs {b}: INVALID -- {e}")
                ok = False
                continue
            same = pa == pb
            ok &= same
            print(f"{a} vs {b}: payload {'IDENTICAL' if same else 'DIFFERS'}"
                  f" ({len(pa)} bytes)")
        return 0 if ok else 1

    if argv[0] == "--hash":
        if len(argv) < 2:
            print("--hash requires at least one file", file=sys.stderr)
            return 2
        for path in argv[1:]:
            try:
                with open(path, "rb") as f:
                    head = f.read(PAYLOAD_OFFSET)
                if len(head) < PAYLOAD_OFFSET:
                    raise FormatError(f"{path}: too small")
                hdr = struct.unpack(HEADER, head[8:PAYLOAD_OFFSET])
                h = dict(zip([k for k, _ in FIELDS], hdr))
                if h["version"] != 1 or h["width"] != 8 or h["ndof"] == 0:
                    raise FormatError(f"{path}: invalid header "
                                      "(version/width/ndof)")
                payload_len = h["ndof"] * h["width"]
                print(f"{path}  "
                      f"{stream_payload_sha256(path, payload_len)}")
            except FormatError as e:
                print(f"{path}: INVALID -- {e}")
                return 1
        return 0

    try:
        for path in argv:
            describe(path)
    except FormatError as e:
        print(f"INVALID -- {e}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
