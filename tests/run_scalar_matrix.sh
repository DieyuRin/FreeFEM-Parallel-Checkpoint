#!/usr/bin/env bash
# run_scalar_matrix.sh -- scalar P1 N-to-M regression matrix.
# writers {1,2,3,4,8,16} x readers {1,2,3,4,8,16} = 36 combinations.
set -u
cd "$(dirname "$0")/.." || exit 2
FF_MPIRUN="${FF_MPIRUN:-ff-mpirun}"
TIMEOUT="${FFCP_TEST_TIMEOUT:-120}"
FF_RUN=(timeout "$TIMEOUT" "$FF_MPIRUN")

OUT="tests/out"; mkdir -p "$OUT"
W=(1 2 3 4 8 16); R=(1 2 3 4 8 16)
fails=0; runs=0

for w in "${W[@]}"; do
    out="$OUT/p1_w$w.ffio"
    if ! "${FF_RUN[@]}" -np "$w" examples/p1_write.edp -out "$out" -v 0 2>/dev/null \
         | grep -q "P1 WRITE OK"; then
        echo "FAIL scalar write np=$w"; fails=$((fails + 1)); continue
    fi
    for r in "${R[@]}"; do
        runs=$((runs + 1))
        if "${FF_RUN[@]}" -np "$r" examples/p1_read.edp -chk "$out" -v 0 2>/dev/null \
             | grep -q "P1 READ TEST OK"; then
            printf '.'
        else
            echo "FAIL scalar write np=$w -> read np=$r"; fails=$((fails + 1))
        fi
    done
done
echo
echo "scalar matrix: $runs combinations, failures = $fails"
[ "$fails" -eq 0 ]
