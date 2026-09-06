#!/usr/bin/env bash
# run_all.sh -- full FreeFemCheckpoint regression suite.
# Run from the repository root after `make` (plugin FreeFemCheckpoint.so).
set -uo pipefail
cd "$(dirname "$0")/.." || exit 2

[ -f FreeFemCheckpoint.so ] || { echo "run_all: plugin missing, run 'make' first"; exit 2; }

fails=0
for t in \
    tests/run_scalar_matrix.sh \
    tests/run_mixed_matrix.sh \
    tests/test_payload_invariance.sh \
    tests/test_invalid_header.sh \
    tests/test_wrong_version.sh \
    tests/test_wrong_width.sh \
    tests/test_zero_ndof.sh \
    tests/test_short_header.sh \
    tests/test_truncated_file.sh \
    tests/test_trailing_bytes.sh \
    tests/test_negative_gid.sh \
    tests/test_read_badgid.sh \
    tests/test_missing_dof.sh \
    tests/test_size_mismatch.sh \
    tests/test_conflicting_duplicate.sh \
    tests/test_dup_tolerance.sh \
    tests/test_nonfinite.sh ; do
    echo "== $t"
    if ! bash "$t"; then
        echo "== $t FAILED"
        fails=$((fails + 1))
    fi
done

echo "========================================"
if [ "$fails" -eq 0 ]; then
    echo "ALL TESTS PASSED"
else
    echo "FAILURES: $fails"
    exit 1
fi
