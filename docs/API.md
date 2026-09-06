# API

Public FreeFEM interface of the `FreeFemCheckpoint` plugin
(`load "./FreeFemCheckpoint"`).

## Functions

```cpp
int ffcpWrite(string filename, int[int] gid, real[int] values);
int ffcpRead (string filename, int[int] gid, real[int] values);
```

### ffcpWrite

Collectively write `values` as the canonical checkpoint `filename`.

- `gid[i]` = canonical global FE DOF id of `values[i]`. Multiple ranks may
  hold the same gid (overlap/ghost copies); duplicates are removed after a
  consistency check.
- Requirements: `gid.n == values.n` on every rank; all gids in
  `[0, max(gid)+1)`; finite values only (NaN/Inf rejected).
- The writer MPI size is recorded in the header for information only; it does
  not affect the payload layout.
- `ffcpWrite` **overwrites/truncates** the target checkpoint.

### ffcpRead

Collectively read the canonical checkpoint `filename` and fill the *current*
local (overlapping) vector: `values[i] = payload[gid[i]]`.

- The same canonical numbering recipe must be used as at write time (same
  global mesh / FE space definition; any MPI size).
- The reader validates: magic, version, scalar width, `Nglobal`, exact file
  size `64 + Nglobal * 8`, and the local gid range `[0, Nglobal)`.

## Return value

Rank-invariant on every rank:

| range  | meaning                                                |
|--------|--------------------------------------------------------|
| > 0    | number of canonical global DOFs `Nglobal` (write and read) |
| < 0    | error code (table below)                                |

## Error codes (exact)

These are the only codes returned by the canonical writer/reader; the source
uses the same named constants (`src/FreeFemCheckpoint.cpp`).

| code | name                  | meaning                                            |
|------|-----------------------|----------------------------------------------------|
| -1   | `kErrInvalidArgument` | null filename / gid / values argument              |
| -2   | `kErrSizeMismatch`    | `gid.n != values.n` on some rank                   |
| -3   | `kErrLocalTooLarge`   | a local array exceeds `INT_MAX` entries            |
| -4   | `kErrOpen`            | cannot open the checkpoint file                    |
| -5   | `kErrHeader`          | truncated/malformed 64-byte header (incl. short read, header-size overflow) |
| -6   | `kErrMagic`           | file is not `FFIOG001`                             |
| -7   | `kErrVersion`         | unsupported format version                         |
| -8   | `kErrScalarWidth`     | scalar width != 8 (double only)                    |
| -9   | `kErrEmpty`           | `Nglobal == 0`                                     |
| -10  | `kErrGidRange`        | local gid outside `[0, Nglobal)`                   |
| -11  | `kErrBlockTooLarge`   | local canonical I/O block exceeds `INT_MAX`        |
| -12  | `kErrTruncated`       | file smaller than `64 + Nglobal * 8`               |
| -13  | `kErrTrailingBytes`   | file larger than `64 + Nglobal * 8`                |
| -14  | `kErrRead`            | MPI-IO read / close failure                        |
| -15  | `kErrWrite`           | MPI-IO write / set_size failure                    |
| -16  | `kErrInvalidGid`      | negative (invalid) global DOF ids on write         |
| -17  | `kErrCanonicalization`| missing canonical DOFs or conflicting duplicate values |
| -18  | `kErrOverflow`        | `MPI_Alltoallv` `int` count/displacement overflow  |
| -19  | `kErrNonFinite`       | NaN/Inf value in the checkpoint field              |
| -20  | `kErrComm`            | MPI communication (Alltoall/Alltoallv) failure     |

Every error is detected locally, collectively agreed (`MPI_Allreduce`), and
returned by **all** ranks; diagnostics are printed by rank 0 with the prefix
`[FreeFemCheckpoint]`. MPI-level failures are checked and reported;
recovery from arbitrary MPI faults (e.g. a broken communicator) is not
claimed.

## Collective-call contract

- `ffcpWrite` / `ffcpRead` are collective on `checkpointComm()`, which is
  currently `MPI_COMM_WORLD` (subcommunicators are not a public feature).
- All ranks must call the same operation in the same order, with the same
  checkpoint filename; the path must be visible from all ranks.
- `gid[]` and `values[]` are local arrays and may differ by rank.
- The global mesh / global FE-space definition must define the same canonical
  numbering on every rank.

> **Never call `ffcpWrite` or `ffcpRead` on only a subset of ranks in
> `MPI_COMM_WORLD`** — this can hang the job.

## Duplicate-value policy

Overlapping copies of the same canonical DOF are compared with a relative
tolerance of `1e-12 * (1 + max(|a|,|b|))`. Finite duplicates within tolerance
are accepted (the first value seen for a gid is stored); values outside the
tolerance fail the write (`kErrCanonicalization`). NaN/Inf values are rejected
outright (`kErrNonFinite`) on both write and read.

## Legacy aliases

Same implementations as the public API, kept for backward compatibility:

```cpp
int ffioWriteGlobal(string, int[int], real[int]);   // == ffcpWrite
int ffioReadGlobal (string, int[int], real[int]);   // == ffcpRead
```

The historical V0 rank-concatenated helper `ffioWrite` is **no longer
registered** in 0.3.0 (it was not a canonical checkpoint format); see
`docs/legacy-v0.md`.

## Abstraction boundary

`gid[]` is built in FreeFEM via `restrict()` (see `docs/DESIGN.md`).
FreeFemCheckpoint does **not** own or build the FE mapping:

```text
FreeFEM = FE semantics / canonical mapping   (restrict, ThGlobal, n2o)
plugin  = distributed checkpoint I/O         (dedup, redistribution, MPI-IO)
```
