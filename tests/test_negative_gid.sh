#!/usr/bin/env bash
# test_negative_gid.sh -- writer must collectively fail on a negative gid.
set -u
cd "$(dirname "$0")/.." || exit 2
FF_MPIRUN="${FF_MPIRUN:-ff-mpirun}"
TIMEOUT="${FFCP_TEST_TIMEOUT:-120}"
FF_RUN=(timeout "$TIMEOUT" "$FF_MPIRUN")


if "${FF_RUN[@]}" -np 4 tests/edp/neg_negativegid.edp -v 0 2>/dev/null \
     | grep -q "NEG-NEGATIVEGID EXPECT-FAIL OK"; then
    echo "negative gid: collective failure as expected"
else
    echo "FAIL negative-gid: expected collective failure not observed"; exit 1
fi
