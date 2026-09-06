#!/usr/bin/env bash
# test_wrong_version.sh -- reader must reject an unsupported format version.
set -u
cd "$(dirname "$0")/.." || exit 2
FF_MPIRUN="${FF_MPIRUN:-ff-mpirun}"
TIMEOUT="${FFCP_TEST_TIMEOUT:-120}"
FF_RUN=(timeout "$TIMEOUT" "$FF_MPIRUN")

OUT="tests/out"; mkdir -p "$OUT"

"${FF_RUN[@]}" -np 4 examples/p1_write.edp -out "$OUT/good.ffio" -v 0 2>/dev/null \
    | grep -q "P1 WRITE OK" || { echo "FAIL wrong-version: cannot write good file"; exit 1; }

python3 - "$OUT/good.ffio" "$OUT/badver.ffio" <<'PYEOF'
import sys, struct
data = bytearray(open(sys.argv[1], "rb").read())
data[8:16] = struct.pack("<Q", 2)      # version = 2
open(sys.argv[2], "wb").write(bytes(data))
PYEOF

if "${FF_RUN[@]}" -np 2 examples/p1_read.edp -chk "$OUT/badver.ffio" -v 0 2>/dev/null \
     | grep -q "P1 READ TEST OK"; then
    echo "FAIL wrong-version: version 2 was accepted"; exit 1
fi
echo "wrong version: rejected as expected"
