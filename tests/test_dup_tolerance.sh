#!/usr/bin/env bash
# test_dup_tolerance.sh -- duplicate-value policy:
#   * within-tolerance disagreement (1e-15) must be accepted,
#   * outside-tolerance disagreement must fail (see test_conflicting_duplicate.sh).
set -u
cd "$(dirname "$0")/.." || exit 2

if ff-mpirun -np 4 tests/edp/neg_tol_ok.edp -v 0 2>/dev/null \
     | grep -q "NEG-TOL EXPECT-SUCCESS OK"; then
    echo "duplicates within tolerance: accepted as expected"
else
    echo "FAIL dup-tolerance: within-tolerance duplicates were rejected"; exit 1
fi
rm -f neg_tol_ok.ffio
