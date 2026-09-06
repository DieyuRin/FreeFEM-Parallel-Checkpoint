# Build

## Requirements

Required:

- a FreeFEM installation with an MPI-capable `ff-c++`
- `FreeFem++-mpi` / `ff-mpirun` or an equivalent MPI launch setup
- an MPI-IO capable MPI implementation
- C++14
- Python 3 for the test/inspection utilities (`tools/inspect_ffio.py`,
  corruption fixtures)

The plugin source does **not** call PETSc. The author's tested installation
may provide MPI under a bundled PETSc toolchain, but that deployment detail is
not a universal requirement.

Tested environment:

- FreeFEM 4.17 (little-endian x86_64, MPI from its bundled PETSc toolchain)

Do not claim support for untested FreeFEM versions or architectures.

## Build

```bash
make
```

which is equivalent to:

```bash
ff-c++ -mpi src/FreeFemCheckpoint.cpp -o FreeFemCheckpoint
```

and produces `FreeFemCheckpoint.so` in the repository root. The `ff-c++`
wrapper can be overridden, e.g. `make FF_CXX=/path/to/ff-c++`.

Note: the message

```
eval: top_srcdir: not found
```

printed by the bundled `ff-c++` wrapper is a harmless packaging artifact (an
unexpanded `-I$(top_srcdir)/...` flag); it does not affect the build.

## Tests

```bash
make test
```

runs the full regression suite (`tests/run_all.sh`): scalar and mixed N-to-M
matrices, payload invariance, and the corruption/negative tests. Each script
can also be run alone, e.g.:

```bash
bash tests/run_scalar_matrix.sh
bash tests/test_truncated_file.sh
```

Test runs write their checkpoints under `tests/out/`; nothing under that
directory is tracked (see `.gitignore`).

## Cleaning

```bash
make clean     # removes FreeFemCheckpoint.so, *.o, tests/out, *.log
```

## Installing system-wide

Not implemented yet. Examples load the plugin relatively
(`load "./FreeFemCheckpoint"`), so no root access or FreeFEM plugin-directory
guesswork is needed. `make install` belongs to the packaging/upstream phase.
