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

Usage:
    python3 tools/inspect_ffio.py FILE ...          # describe files
    python3 tools/inspect_ffio.py --validate FILE   # strict structural check
    python3 tools/inspect_ffio.py --cmp A B [A B]   # payload byte compare
    python3 tools/inspect_ffio.py --hash FILE ...   # payload sha1
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
MAGIC_ERR = None


class FormatError(Exception):
    pass


def parse(path):
    with open(path, "rb") as f:
        raw = f.read()
    if len(raw) < 64:
        raise FormatError(f"{path}: too small ({len(raw)} < 64 header bytes)")
    if raw[0:8] != MAGIC:
        raise FormatError(f"{path}: bad magic {raw[0:8]!r} (expected {MAGIC!r})")
    hdr = struct.unpack(HEADER, raw[8:64])
    h = dict(zip([k for k, _ in FIELDS], hdr))
    expected = 64 + h["ndof"] * h["width"]
    if len(raw) != expected:
        raise FormatError(
            f"{path}: payload mismatch: size {len(raw)} != 64 + ndof*width "
            f"({expected})"
        )
    return h, raw


def describe(path):
    h, raw = parse(path)
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
    print(f"payload_size   {len(raw) - 64}")
    if h["width"] == 8 and h["ndof"]:
        values = struct.unpack(f"<{h['ndof']}d", raw[64:])
        print(f"payload        min={min(values):.17g} max={max(values):.17g} "
              f"mean={sum(values) / len(values):.17g}")
    print(f"payload_sha1   {hashlib.sha1(raw[64:]).hexdigest()}")
    return h


def payload(path):
    h, raw = parse(path)
    return raw[64:]


def validate(path):
    parse(path)  # raises FormatError on any problem
    print(f"{path}: VALID (FFIOG001, structurally consistent)")
    return True


def main(argv):
    if not argv:
        print(__doc__)
        return 0

    if argv[0] == "--validate":
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
                same = payload(a) == payload(b)
            except FormatError as e:
                print(f"{a} vs {b}: INVALID -- {e}")
                ok = False
                continue
            ok &= same
            print(f"{a} vs {b}: payload {'IDENTICAL' if same else 'DIFFERS'}"
                  f" ({len(payload(a))} bytes)")
        return 0 if ok else 1

    if argv[0] == "--hash":
        for path in argv[1:]:
            try:
                print(f"{path}  {hashlib.sha1(payload(path)).hexdigest()}")
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
