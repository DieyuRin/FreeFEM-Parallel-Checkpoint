#!/usr/bin/env bash
# test_short_header.sh -- reader must reject a file shorter than 64 bytes
# (truncated header).
set -u
cd "$(dirname "$0")/.." || exit 2
OUT="tests/out"; mkdir -p "$OUT"

ff-mpirun -np 4 examples/p1_write.edp -out "$OUT/good.ffio" -v 0 2>/dev/null \
    | grep -q "P1 WRITE OK" || { echo "FAIL short-header: cannot write good file"; exit 1; }

cp "$OUT/good.ffio" "$OUT/shorthdr.ffio"
truncate -s 32 "$OUT/shorthdr.ffio"

if ff-mpirun -np 2 examples/p1_read.edp -chk "$OUT/shorthdr.ffio" -v 0 2>/dev/null \
     | grep -q "P1 READ TEST OK"; then
    echo "FAIL short-header: 32-byte file was accepted"; exit 1
fi
echo "short header: rejected as expected"
