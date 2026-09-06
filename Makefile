PLUGIN   := FreeFemCheckpoint
SRC      := src/FreeFemCheckpoint.cpp
FF_CXX   ?= ff-c++
FF_MPIRUN ?= ff-mpirun
export FF_MPIRUN

.PHONY: all clean test test-scalar test-mixed test-negative

all: $(PLUGIN).so

$(PLUGIN).so: $(SRC)
	$(FF_CXX) -mpi $(SRC) -o $(PLUGIN)

test: all
	bash tests/run_all.sh

test-scalar: all
	bash tests/run_scalar_matrix.sh

test-mixed: all
	bash tests/run_mixed_matrix.sh

test-negative: all
	bash tests/test_invalid_header.sh
	bash tests/test_wrong_version.sh
	bash tests/test_wrong_width.sh
	bash tests/test_zero_ndof.sh
	bash tests/test_short_header.sh
	bash tests/test_truncated_file.sh
	bash tests/test_trailing_bytes.sh
	bash tests/test_negative_gid.sh
	bash tests/test_read_badgid.sh
	bash tests/test_missing_dof.sh
	bash tests/test_size_mismatch.sh
	bash tests/test_conflicting_duplicate.sh
	bash tests/test_dup_tolerance.sh
	bash tests/test_nonfinite.sh

clean:
	rm -f $(PLUGIN).so
	rm -f $(PLUGIN).o
	rm -f src/*.o
	rm -rf tests/out
	rm -f tests/*.log
