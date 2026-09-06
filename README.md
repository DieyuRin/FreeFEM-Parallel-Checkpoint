# FreeFemCheckpoint

> FreeFemCheckpoint is an independent third-party project and is not part of
> the official FreeFEM distribution.

FreeFemCheckpoint is an independent third-party FreeFEM plugin for
partition-independent parallel checkpoint/restart of distributed
finite-element coefficient vectors. It uses canonical global DOF ids supplied
by FreeFEM (`restrict()`) and collective MPI-IO for the checkpoint payload.
Writer and reader MPI sizes may differ (N-to-M restart).

## Features

- collective parallel write **and** read of a single checkpoint file
- canonical global DOF layout (independent of the writer MPI partition)
- overlap/ghost duplicate removal with conflict checking (finite values,
  relative tolerance 1e-12; NaN/Inf rejected)
- N-to-M restart
- scalar (`P1`) and mixed (`[P2,P2,P1]`) FE spaces, one call per field
- strict reader validation of header, exact file size, and gid range
- no rank-0 global gather; each rank stores its canonical I/O block plus its
  local FE vector and redistribution buffers
- file format `FFIOG001`: 64-byte header + IEEE-754 double payload

## Requirements

- a FreeFEM installation with an MPI-capable `ff-c++` (tested: **FreeFEM 4.17**)
- `FreeFem++-mpi` / `ff-mpirun` or an equivalent MPI launch setup
- an MPI-IO capable MPI implementation
- C++14
- Python 3 (only for `tools/inspect_ffio.py` and the test suite)

FreeFemCheckpoint does **not** call PETSc APIs and does not use PETSc objects
or viewers. It uses the MPI toolchain selected by the FreeFEM installation;
the tested FreeFEM 4.17 installation happens to provide MPI through its
bundled PETSc toolchain.

## Build

```bash
make              # -> FreeFemCheckpoint.so
make test         # full regression suite
```

Equivalent manual build:

```bash
ff-c++ -mpi src/FreeFemCheckpoint.cpp -o FreeFemCheckpoint
```

(The `eval: top_srcdir: not found` message printed by the bundled `ff-c++`
wrapper is a harmless packaging artifact.)

## Quick start

```bash
make
ff-mpirun -np 4 examples/p1_write.edp -v 0
ff-mpirun -np 3 examples/p1_read.edp -chk checkpoint_p1.ffio -v 0
```

## Scalar example (P1)

The canonical numbering setup is identical for write and read: every rank
builds the same global mesh, keeps `ThGlobal` **before** `DmeshCreate`,
captures `n2o` via `macro ThN2O`, and derives `gid = restrict(...)`.

```cpp
load "./FreeFemCheckpoint"

macro dimension()2//
include "macro_ddm.idp"

int[int] n2o;
macro ThN2O()n2o//

mesh Th = square(32,32,[x,y]);
mesh ThGlobal = Th;               // keep the global mesh before DmeshCreate

DmeshCreate(Th);                  // Th becomes the local overlapping submesh

fespace Vh(Th,P1);
fespace VhGlobal(ThGlobal,P1);

Vh u = cos(x - 0.5)*cos(y - 0.5);

int[int] gid = restrict(Vh,VhGlobal,n2o);
assert(gid.n == u[].n);

ffcpWrite("checkpoint.ffio", gid, u[]);
```

Restart (rebuild `ThGlobal`/`n2o`/`gid` exactly as above; writer and reader MPI sizes may differ):

```cpp
Vh u = 0.0;
ffcpRead("checkpoint.ffio", gid, u[]);
```

Runnable versions: `examples/p1_write.edp`, `examples/p1_read.edp`.

## Mixed FE example

One `ffcpWrite` call stores the whole mixed field. In the tested FreeFEM
setup, `u1[]` is the entire mixed-space coefficient vector
(`u1[].n == Wh.ndof`), and `restrict()` returns one unique canonical id per
(component, DOF):

```cpp
func Pk = [P2,P2,P1];
fespace Wh(Th,Pk);
fespace WhGlobal(ThGlobal,Pk);

Wh [u1,u2,p] = [cos(x), sin(y), x+y];   // component-list assignment only

int[int] gid = restrict(Wh,WhGlobal,n2o);
assert(gid.n == u1[].n);

ffcpWrite("mixed.ffio", gid, u1[]);
```

Restart:

```cpp
Wh [u1,u2,p] = [0.,0.,0.];
ffcpRead("mixed.ffio", gid, u1[]);
```

Runnable versions: `examples/mixed_write.edp`, `examples/mixed_read.edp`.

## N-to-M restart

- Writer and reader MPI sizes may differ.
- The writer MPI size is stored in the header for information only; it does
  not affect the payload layout.
- The regression matrix covers exactly these MPI sizes:
  writer np ∈ {1,2,3,4,8,16} × reader np ∈ {1,2,3,4,8,16} (36 combinations
  for the scalar field, 15 for the mixed field).

## API

```cpp
int ffcpWrite(string filename, int[int] gid, real[int] values);
int ffcpRead (string filename, int[int] gid, real[int] values);
```

- `gid.n == values.n` must hold on every rank.
- Return value is **rank-invariant**: the number of canonical global DOFs
  (`Nglobal`) on success, a negative error code on failure.
- `ffioWriteGlobal` / `ffioReadGlobal` are legacy aliases of the same
  implementations. The old V0 `ffioWrite` helper is no longer registered
  (see `docs/legacy-v0.md`).

Full reference (exact error-code table, overwrite semantics, communicator):
`docs/API.md`.

## Collective-call contract

`ffcpWrite` and `ffcpRead` are **collective** operations on
`checkpointComm()`, which is currently `MPI_COMM_WORLD` (subcommunicators are
not a public feature):

- all ranks must call the same operation in the same order;
- all ranks must pass the same checkpoint filename, visible from every rank;
- `gid[]` and `values[]` are local and may differ by rank;
- the global mesh / global FE-space definition must yield the same canonical
  numbering on every rank.

> **Never call `ffcpWrite` or `ffcpRead` on only a subset of ranks in
> `MPI_COMM_WORLD`** — this can hang the job.

Application-level validation failures are collectively agreed before any rank
returns (no rank can strand others in a collective). MPI-I/O return codes are
checked and reported consistently; recovery from arbitrary MPI faults is not
claimed.

## Tested configurations

| Item | Status |
|---|---|
| FreeFEM 4.17 | tested |
| P1 scalar | tested |
| `[P2,P2,P1]` mixed | tested |
| N-to-M restart (rank matrix above) | tested |
| canonical payload layout independent of writer partition | yes |
| byte-identical payload for regression fields | tested |
| x86_64 | tested |
| ARM64 | not tested yet |
| cross-architecture read | not tested yet |
| complex payload | not native; two-real-file workaround |
| HDF5 | not supported |
| ADIOS2 | not supported |
| mesh serialization | not supported |
| PETSc API dependency | none |
| subcommunicator API | not supported / not tested |

Supported scalar type: FreeFEM `real` (IEEE-754 double). The I/O layer is
FE-type agnostic once a valid canonical `gid[]` is supplied; the DOF mapping
itself is provided by FreeFEM's `restrict()` — FreeFemCheckpoint does not own
or build the FE mapping.

## File format

`FFIOG001`: 64-byte header + canonical payload. Readers validate magic,
version, scalar width, `Nglobal`, and the **exact** file size
`64 + Nglobal * 8` (truncated and trailing-byte files are rejected). Full
specification and compatibility policy: `docs/FILE_FORMAT.md`.

> `ffcpWrite` overwrites/truncates the target checkpoint. A job failure
> during an overwrite may leave that checkpoint incomplete; for production
> runs, prefer rotating/unique checkpoint filenames and retain at least one
> previous known-good checkpoint. Atomic commit is not claimed.

## Limitations

- double (`real`) payloads only; `complex` not supported (save real and
  imaginary parts as two checkpoints — see `docs/RECIPES.md`)
- restart requires the same global mesh / FE definition as the write (writer and reader MPI sizes may differ);
  the file format does not detect a *different* mesh/FE space with a
  coincidentally compatible gid range
- non-Lagrange FE (edge/face DOFs) and periodic conditions are not tested
- writer coverage guarantee: complete contiguous coverage of the inferred `[0, max(gid)]` is required and interior gaps are rejected; a missing canonical suffix beyond `max(gid)` is not detectable because the plugin does not know the expected global FE-space dimension (a FreeFEM-side responsibility)
- one checkpoint state = one file (no multi-dataset format)
- `MPI_Alltoallv` uses `int` counts/displacements: no single exchange buffer
  may exceed `INT_MAX` entries on a rank
- large-scale scalability is not benchmarked; aggregate I/O performance
  depends on the underlying parallel filesystem

## Practical recipes

`docs/RECIPES.md` — time-series checkpoints, complex-field workaround,
multiple independent fields.

## Development status

V3 stabilization release, version **0.3.0**. Roadmap and coding rules:
`docs/DEVELOPMENT.md`, `CONTRIBUTING.md`.

## License

FreeFemCheckpoint is licensed under the GNU Lesser General Public License
v3.0 or later (`LGPL-3.0-or-later`).

See [LICENSE](LICENSE) for the full license text.
