#!/usr/bin/env bash
# test_read_badgid.sh -- reader must collectively fail when a local gid lies
# outside the checkpoint's canonical range [0, Nglobal).
set -u
cd "$(dirname "$0")/.." || exit 2
FF_MPIRUN="${FF_MPIRUN:-ff-mpirun}"
TIMEOUT="${FFCP_TEST_TIMEOUT:-120}"
FF_RUN=(timeout "$TIMEOUT" "$FF_MPIRUN")

OUT="tests/out"; mkdir -p "$OUT"

"${FF_RUN[@]}" -np 4 examples/p1_write.edp -out "$OUT/good.ffio" -v 0 2>/dev/null \
    | grep -q "P1 WRITE OK" || { echo "FAIL read-badgid: cannot write good file"; exit 1; }

if "${FF_RUN[@]}" -np 3 tests/edp/neg_read_badgid.edp -chk "$OUT/good.ffio" -v 0 2>/dev/null \
     | grep -q "NEG-READ-BADGID EXPECT-FAIL OK"; then
    echo "read gid out of range: collective failure as expected"
else
    echo "FAIL read-badgid: expected collective failure not observed"; exit 1
fi
