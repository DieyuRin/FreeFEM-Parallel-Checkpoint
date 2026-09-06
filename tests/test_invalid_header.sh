#!/usr/bin/env bash
# test_invalid_header.sh -- reader must reject a checkpoint with a bad magic.
set -u
cd "$(dirname "$0")/.." || exit 2
OUT="tests/out"; mkdir -p "$OUT"

ff-mpirun -np 4 examples/p1_write.edp -out "$OUT/good.ffio" -v 0 2>/dev/null \
    | grep -q "P1 WRITE OK" || { echo "FAIL invalid-header: cannot write good file"; exit 1; }

python3 - "$OUT/good.ffio" "$OUT/badmagic.ffio" <<'PYEOF'
import sys
data = bytearray(open(sys.argv[1], "rb").read())
data[0:8] = b"BADMAGIC"
open(sys.argv[2], "wb").write(bytes(data))
PYEOF

if ff-mpirun -np 2 examples/p1_read.edp -chk "$OUT/badmagic.ffio" -v 0 2>/dev/null \
     | grep -q "P1 READ TEST OK"; then
    echo "FAIL invalid-header: bad magic was accepted"; exit 1
fi
echo "invalid header: rejected as expected"
