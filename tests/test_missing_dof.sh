#!/usr/bin/env bash
# test_missing_dof.sh -- writer must collectively fail when a canonical gid
# is not covered by any rank (here: shifting all gids by +1 drops gid 0).
set -u
cd "$(dirname "$0")/.." || exit 2

if ff-mpirun -np 4 tests/edp/neg_badgid.edp -v 0 2>/dev/null \
     | grep -q "NEG-BADGID EXPECT-FAIL OK"; then
    echo "missing canonical DOF: collective failure as expected"
else
    echo "FAIL missing-dof: expected collective failure not observed"; exit 1
fi
