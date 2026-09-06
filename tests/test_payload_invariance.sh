#!/usr/bin/env bash
# test_payload_invariance.sh -- writer np must not change the canonical payload.
set -u
cd "$(dirname "$0")/.." || exit 2
OUT="tests/out"; mkdir -p "$OUT"
W=(1 2 4 8 16)
fails=0

files=()
for w in "${W[@]}"; do
    f="$OUT/p1_w$w.ffio"
    if ff-mpirun -np "$w" examples/p1_write.edp -out "$f" -v 0 2>/dev/null \
         | grep -q "P1 WRITE OK"; then
        files+=("$f")
    else
        echo "FAIL payload-invariance write np=$w"; fails=$((fails + 1))
    fi
done

if [ "${#files[@]}" -gt 0 ]; then
    for f in "${files[@]}"; do
        size=$(stat -c '%s' "$f")
        if [ "$size" != "8776" ]; then
            echo "FAIL $f size $size != 8776"; fails=$((fails + 1))
        fi
    done
    if python3 tools/inspect_ffio.py --hash "${files[@]}" \
         | awk '{print $2}' | sort -u | wc -l | grep -qx "1"; then
        :  # all hashes identical
    else
        echo "FAIL payload hashes differ across writer np"; fails=$((fails + 1))
    fi
fi

echo "payload invariance: files = ${#files[@]}, failures = $fails"
[ "$fails" -eq 0 ]
