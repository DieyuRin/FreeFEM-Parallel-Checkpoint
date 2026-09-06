#!/usr/bin/env bash
# test_inspector.sh -- regression for tools/inspect_ffio.py: its validation
# policy must match the C++ reader (docs/FILE_FORMAT.md).
set -u
cd "$(dirname "$0")/.." || exit 2
FF_MPIRUN="${FF_MPIRUN:-ff-mpirun}"
TIMEOUT="${FFCP_TEST_TIMEOUT:-120}"
FF_RUN=(timeout "$TIMEOUT" "$FF_MPIRUN")
OUT="tests/out"; mkdir -p "$OUT"
PY=tools/inspect_ffio.py
fails=0

"${FF_RUN[@]}" -np 4 examples/p1_write.edp -out "$OUT/good.ffio" -v 0 2>/dev/null \
    | grep -q "P1 WRITE OK" || { echo "FAIL inspector: cannot write good file"; exit 1; }

# good file must validate
if ! python3 "$PY" --validate "$OUT/good.ffio" >/dev/null 2>&1; then
    echo "FAIL inspector: good file rejected"; fails=$((fails + 1))
else
    echo "inspector: good file VALID"
fi

mutate() { # src dst offset value(8 bytes as hex string)
    python3 - "$1" "$2" "$3" "$4" <<'PYEOF'
import sys, struct
data = bytearray(open(sys.argv[1], "rb").read())
data[int(sys.argv[3]):int(sys.argv[3])+8] = bytes.fromhex(sys.argv[4])
open(sys.argv[2], "wb").write(bytes(data))
PYEOF
}

# hard rejections the C++ reader also enforces
mutate "$OUT/good.ffio" "$OUT/bad_version.ffio" 8  "0200000000000000"
mutate "$OUT/good.ffio" "$OUT/bad_width.ffio"   24 "0400000000000000"
mutate "$OUT/good.ffio" "$OUT/bad_ndof.ffio"    16 "0000000000000000"
for f in bad_version bad_width bad_ndof; do
    if python3 "$PY" --validate "$OUT/$f.ffio" >/dev/null 2>&1; then
        echo "FAIL inspector: $f accepted"; fails=$((fails + 1))
    else
        echo "inspector: $f rejected"
    fi
done

# trailing bytes (strict exact-size policy)
python3 - "$OUT/good.ffio" "$OUT/trailing.ffio" <<'PYEOF'
import sys
data = bytearray(open(sys.argv[1], "rb").read())
data += b"\x00" * 8
open(sys.argv[2], "wb").write(bytes(data))
PYEOF
if python3 "$PY" --validate "$OUT/trailing.ffio" >/dev/null 2>&1; then
    echo "FAIL inspector: trailing bytes accepted"; fails=$((fails + 1))
else
    echo "inspector: trailing bytes rejected"
fi

# informational warnings on a "valid" but inconsistent header
python3 - "$OUT/good.ffio" "$OUT/warn.ffio" <<'PYEOF'
import sys, struct
data = bytearray(open(sys.argv[1], "rb").read())
data[56:64] = struct.pack("<Q", 7)     # reserved != 0
open(sys.argv[2], "wb").write(bytes(data))
PYEOF
if python3 "$PY" --validate "$OUT/warn.ffio" 2>&1 | grep -q "warning -- reserved"; then
    echo "inspector: reserved warning reported"
else
    echo "FAIL inspector: reserved warning missing"; fails=$((fails + 1))
fi

# argument handling and equality helpers
if python3 "$PY" --hash >/dev/null 2>&1; then
    echo "FAIL inspector: --hash without args accepted"; fails=$((fails + 1))
else
    echo "inspector: --hash without args rejected"
fi
python3 "$PY" --hash "$OUT/good.ffio" "$OUT/good.ffio" >/dev/null 2>&1 \
    || { echo "FAIL inspector: --hash on good file"; fails=$((fails + 1)); }
python3 "$PY" --cmp "$OUT/good.ffio" "$OUT/good.ffio" >/dev/null 2>&1 \
    || { echo "FAIL inspector: --cmp identical failed"; fails=$((fails + 1)); }

echo "inspector tests: failures = $fails"
[ "$fails" -eq 0 ]
