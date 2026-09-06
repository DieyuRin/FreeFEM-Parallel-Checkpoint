#!/usr/bin/env bash
# test_nonfinite.sh -- writer must reject NaN and +Inf checkpoint values
# (kErrNonFinite).
set -u
cd "$(dirname "$0")/.." || exit 2

if ff-mpirun -np 4 tests/edp/neg_nan.edp -v 0 2>/dev/null \
     | grep -q "NEG-NAN EXPECT-FAIL OK"; then
    echo "NaN value: rejected as expected"
else
    echo "FAIL nonfinite: NaN was accepted"; exit 1
fi

if ff-mpirun -np 4 tests/edp/neg_inf.edp -v 0 2>/dev/null \
     | grep -q "NEG-INF EXPECT-FAIL OK"; then
    echo "+Inf value: rejected as expected"
else
    echo "FAIL nonfinite: +Inf was accepted"; exit 1
fi
