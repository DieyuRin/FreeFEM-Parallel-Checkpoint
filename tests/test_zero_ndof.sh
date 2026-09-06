#!/usr/bin/env bash
# test_zero_ndof.sh -- reader must reject Nglobal = 0 in the header.
set -u
cd "$(dirname "$0")/.." || exit 2
OUT="tests/out"; mkdir -p "$OUT"

ff-mpirun -np 4 examples/p1_write.edp -out "$OUT/good.ffio" -v 0 2>/dev/null \
    | grep -q "P1 WRITE OK" || { echo "FAIL zero-ndof: cannot write good file"; exit 1; }

python3 - "$OUT/good.ffio" "$OUT/zerondof.ffio" <<'PYEOF'
import sys, struct
data = bytearray(open(sys.argv[1], "rb").read())
data[16:24] = struct.pack("<Q", 0)     # Nglobal = 0
open(sys.argv[2], "wb").write(bytes(data))
PYEOF

if ff-mpirun -np 2 examples/p1_read.edp -chk "$OUT/zerondof.ffio" -v 0 2>/dev/null \
     | grep -q "P1 READ TEST OK"; then
    echo "FAIL zero-ndof: Nglobal=0 was accepted"; exit 1
fi
echo "zero Nglobal: rejected as expected"
