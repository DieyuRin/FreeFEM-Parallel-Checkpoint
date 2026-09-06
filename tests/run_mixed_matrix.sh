#!/usr/bin/env bash
# run_mixed_matrix.sh -- mixed [P2,P2,P1] N-to-M regression matrix.
# writers {2,4,8} x readers {1,3,4,8,16} = 15 combinations.
set -u
cd "$(dirname "$0")/.." || exit 2
OUT="tests/out"; mkdir -p "$OUT"
W=(2 4 8); R=(1 3 4 8 16)
fails=0; runs=0

for w in "${W[@]}"; do
    out="$OUT/mixed_w$w.ffio"
    if ! ff-mpirun -np "$w" examples/mixed_write.edp -out "$out" -v 0 2>/dev/null \
         | grep -q "MIXED WRITE OK"; then
        echo "FAIL mixed write np=$w"; fails=$((fails + 1)); continue
    fi
    for r in "${R[@]}"; do
        runs=$((runs + 1))
        if ff-mpirun -np "$r" examples/mixed_read.edp -chk "$out" -v 0 2>/dev/null \
             | grep -q "MIXED READ TEST OK"; then
            printf '.'
        else
            echo "FAIL mixed write np=$w -> read np=$r"; fails=$((fails + 1))
        fi
    done
done
echo
echo "mixed matrix: $runs combinations, failures = $fails"
[ "$fails" -eq 0 ]
