#!/usr/bin/env bash
# test_trailing_bytes.sh -- strict exact-size policy: reader must reject a
# checkpoint with unexpected trailing bytes.
set -u
cd "$(dirname "$0")/.." || exit 2
FF_MPIRUN="${FF_MPIRUN:-ff-mpirun}"
TIMEOUT="${FFCP_TEST_TIMEOUT:-120}"
FF_RUN=(timeout "$TIMEOUT" "$FF_MPIRUN")

OUT="tests/out"; mkdir -p "$OUT"

"${FF_RUN[@]}" -np 4 examples/p1_write.edp -out "$OUT/good.ffio" -v 0 2>/dev/null \
    | grep -q "P1 WRITE OK" || { echo "FAIL trailing-bytes: cannot write good file"; exit 1; }

python3 - "$OUT/good.ffio" "$OUT/trailing.ffio" <<'PYEOF'
import sys
data = bytearray(open(sys.argv[1], "rb").read())
data += b"\x00" * 8                  # 8 unexpected trailing bytes
open(sys.argv[2], "wb").write(bytes(data))
PYEOF

if "${FF_RUN[@]}" -np 2 examples/p1_read.edp -chk "$OUT/trailing.ffio" -v 0 2>/dev/null \
     | grep -q "P1 READ TEST OK"; then
    echo "FAIL trailing-bytes: file with trailing bytes was accepted"; exit 1
fi
echo "trailing bytes: rejected as expected"
