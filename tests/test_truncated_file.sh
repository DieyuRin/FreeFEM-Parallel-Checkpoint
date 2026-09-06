#!/usr/bin/env bash
# test_truncated_file.sh -- reader must reject a truncated checkpoint.
set -u
cd "$(dirname "$0")/.." || exit 2
OUT="tests/out"; mkdir -p "$OUT"

ff-mpirun -np 4 examples/p1_write.edp -out "$OUT/good.ffio" -v 0 2>/dev/null \
    | grep -q "P1 WRITE OK" || { echo "FAIL truncated-file: cannot write good file"; exit 1; }

cp "$OUT/good.ffio" "$OUT/trunc.ffio"
truncate -s 100 "$OUT/trunc.ffio"

if ff-mpirun -np 2 examples/p1_read.edp -chk "$OUT/trunc.ffio" -v 0 2>/dev/null \
     | grep -q "P1 READ TEST OK"; then
    echo "FAIL truncated-file: truncated checkpoint was accepted"; exit 1
fi
echo "truncated file: rejected as expected"
