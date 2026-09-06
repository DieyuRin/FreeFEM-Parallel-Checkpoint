#!/usr/bin/env bash
# test_size_mismatch.sh -- writer must collectively fail when
# gid[].n != values[].n.
set -u
cd "$(dirname "$0")/.." || exit 2
FF_MPIRUN="${FF_MPIRUN:-ff-mpirun}"
TIMEOUT="${FFCP_TEST_TIMEOUT:-120}"
FF_RUN=(timeout "$TIMEOUT" "$FF_MPIRUN")


if "${FF_RUN[@]}" -np 4 tests/edp/neg_size_mismatch.edp -v 0 2>/dev/null \
     | grep -q "NEG-SIZEMISMATCH EXPECT-FAIL OK"; then
    echo "gid/value size mismatch: collective failure as expected"
else
    echo "FAIL size-mismatch: expected collective failure not observed"; exit 1
fi
