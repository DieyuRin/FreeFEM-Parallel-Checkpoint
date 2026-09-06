#!/usr/bin/env bash
# test_conflicting_duplicate.sh -- writer must collectively fail when two
# copies of the same canonical DOF disagree.
set -u
cd "$(dirname "$0")/.." || exit 2
FF_MPIRUN="${FF_MPIRUN:-ff-mpirun}"
TIMEOUT="${FFCP_TEST_TIMEOUT:-120}"
FF_RUN=(timeout "$TIMEOUT" "$FF_MPIRUN")


if "${FF_RUN[@]}" -np 4 tests/edp/neg_conflict.edp -v 0 2>/dev/null \
     | grep -q "NEG-CONFLICT EXPECT-FAIL OK"; then
    echo "conflicting duplicate: collective failure as expected"
else
    echo "FAIL conflicting-duplicate: expected collective failure not observed"; exit 1
fi
