#!/usr/bin/env bash
# run_mixed_matrix.sh -- mixed [P2,P2,P1] N-to-M regression matrix.
# writers {2,4,8} x readers {1,3,4,8,16} = 15 combinations.
set -u
cd "$(dirname "$0")/.." || exit 2
FF_MPIRUN="${FF_MPIRUN:-ff-mpirun}"
TIMEOUT="${FFCP_TEST_TIMEOUT:-120}"
FF_RUN=(timeout "$TIMEOUT" "$FF_MPIRUN")

OUT="tests/out"; mkdir -p "$OUT"
W=(2 4 8); R=(1 3 4 8 16)
fails=0; runs=0

for w in "${W[@]}"; do
    out="$OUT/mixed_w$w.ffio"
    if ! "${FF_RUN[@]}" -np "$w" examples/mixed_write.edp -out "$out" -v 0 2>/dev/null \
         | grep -q "MIXED WRITE OK"; then
        echo "FAIL mixed write np=$w"; fails=$((fails + 1)); continue
    fi
    for r in "${R[@]}"; do
        runs=$((runs + 1))
        if "${FF_RUN[@]}" -np "$r" examples/mixed_read.edp -chk "$out" -v 0 2>/dev/null \
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
