# Design

FreeFemCheckpoint turns a distributed, overlapping FreeFEM FE field into a
partition-independent checkpoint and back.

## Canonical numbering

Every MPI rank builds the identical *global* mesh (`ThGlobal`, e.g.
`square(32,32)`). `DmeshCreate(Th)` then turns `Th` into that rank's local
overlapping subdomain mesh. Two FreeFEM facilities provide the local-to-global
map:

1. `macro ThN2O()n2o//` makes `DmeshCreate` record `n2o` = local element →
   global element;
2. `int[int] gid = restrict(Vh, VhGlobal, n2o)` maps every local DOF to its
   canonical global DOF id — an index in the *global* FE space `VhGlobal`
   (for vectorial/mixed FE spaces this spans all components, one unique id
   per (component, DOF)).

The canonical numbering is a function of the global mesh and the FE space
only, hence identical for every writer/reader MPI size.

```
FreeFEM local overlapping FE vector
        ↓  restrict() canonical gid[]
(gid, value) pairs
        ↓  MPI_Alltoall / MPI_Alltoallv
contiguous canonical blocks   (block ownership = f(Nglobal, np) only)
        ↓  MPI_File_write_at_all
one checkpoint file
```

## Writer

1. infer `Nglobal = max(gid) + 1` (all-reduce);
2. validate collectively: gid/values sizes, gid range `[0, Nglobal)`,
   finite values;
3. block ownership `ownerOfGlobalDof(g, Nglobal, np)` decides which rank will
   store canonical dof `g`;
4. `Alltoallv` routes every `(gid, value)` pair to its block owner (counts and
   displacements validated with 64-bit accumulation before use);
5. owner deduplicates: the first value for a gid is kept; later copies must
   satisfy `valuesConsistent` (finite, relative tolerance 1e-12), otherwise
   the write fails collectively; any canonical gid never seen is a missing
   DOF and fails the write collectively;
6. each rank writes its contiguous block with `MPI_File_write_at_all`
   (one 64-byte header buffer written once at offset 0, payload at
   `64 + ioBegin*sizeof(double)`; `MPI_File_set_size(0)` gives
   overwrite/truncate semantics).

## Reader

```
canonical file blocks
        ↓  MPI_File_read_at_all      (each rank reads only its own block)
distributed gid lookup               (payload[gid] for every local DOF)
        ↓  MPI_Alltoallv             (requests out, answers back)
current local overlapping FE vector
```

1. collective open; `MPI_File_get_size` **before** decoding the header;
   require `>= 64` bytes;
2. read exactly 64 header bytes (`MPI_Get_count == 64`), decode magic /
   version / scalar width / Nglobal with overflow-safe arithmetic;
3. strict exact-size check: `fileSize == 64 + Nglobal*width` (rejects
   truncated payloads and trailing bytes);
4. validate local gids against `[0, Nglobal)` collectively;
5. each rank reads only its contiguous canonical block (`ioBegin, ioN`);
   payload values must be finite;
6. every local DOF requests `payload[gid[i]]` from the block owner
   (overlapping copies may issue duplicate requests);
7. owners answer from their in-memory block; answers return along the
   transposed `Alltoallv` pattern;
8. values are unpacked in the original local DOF order.

## Collective-error invariant

All application-level conditions that may differ by rank (size mismatch,
`INT_MAX` limits, gid range, non-finite values, count/displacement overflow,
canonicalization statistics) are converted to a local error **code** and
reduced unconditionally with `checkpointFail()` (an `MPI_Allreduce`) before
any rank returns. Every rank therefore executes the same collective sequence
and returns the same code; no rank can strand others in a collective.

MPI-I/O and communication return codes are checked and reported consistently.
Recovery from arbitrary MPI-level faults (e.g. a broken communicator) is
**not** claimed.

## Memory invariant

- No rank-0 global gather and no full-vector broadcast is performed.
- Each rank stores its canonical I/O block plus its local FE vector and the
  redistribution buffers: `O(Nglobal / P + localN + communication buffers)`.
  Memory is not claimed to be strictly `Nglobal/P` in every phase.
- `MPI_Alltoallv` buffers are bounded by the local overlap; counts and
  displacements use MPI `int` (see Scaling considerations).

## Scaling considerations

- canonical I/O storage is distributed across the reader/writer ranks;
- `MPI_Alltoallv` is the main communication-scaling point; the classic MPI
  `int` count/displacement limits apply (no single exchange buffer may exceed
  `INT_MAX` entries on a rank; enforced and rejected with `kErrOverflow`);
- aggregate I/O performance depends on the underlying shared/parallel
  filesystem;
- large-scale benchmark results are not yet claimed.
