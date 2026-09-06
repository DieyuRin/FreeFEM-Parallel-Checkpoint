# Development

## Versioning

See `VERSION` (currently `0.3.0`). Meaning:

- 0.1.x = canonical writer
- 0.2.x = N-to-M reader
- 0.3.x = stabilized repository / API (public-release hardening)

Do not call it 1.0 until several FreeFEM versions and both x86_64/ARM64 have
been exercised and the file-format compatibility policy is settled.

## Roadmap

0.3.0 is a **stabilization release**: public API, repository structure,
collective-safe error handling, strict file validation, tests, docs,
reproducible `make`/`make test`. No new core algorithms were introduced.

Future work (after the public 0.3.0 release, not before):

```text
0.4 / V4:
    benchmark + more platform/version testing
    optional mesh/FE signature design discussion
    metadata design discussion

future format (FFIOG002 only if layout changes):
    metadata
    mesh/FE signature
    optional named datasets

later:
    native complex128
    HDF5 backend
    ADIOS2 backend
    sparse/neighbor communication optimization
    MPI-4 large-count support
    optional subcommunicator API
```

## Regression safety

The core data path is frozen as the correctness baseline:

- writer: `MPI_Alltoallv` + `MPI_File_write_at_all`
- reader: `MPI_File_read_at_all` + `MPI_Alltoallv`

Never regress to `MPI_Gather` / rank-0 serial write / full-vector `MPI_Bcast`.
Memory invariant: no rank holds `O(Nglobal)` values; per-rank storage is
`O(Nglobal/P + localN + buffers)`. All tests must keep passing:
`make test` (`tests/run_all.sh`).

## Style notes

- Output prefix `[FreeFemCheckpoint]`, aggregate errors printed by rank 0 only.
- Named constants for error codes (`kErr*`) and the duplicate tolerance
  (`kDuplicateTolerance`); no raw negative returns.
- Central communicator `checkpointComm()`; no hard-coded `MPI_COMM_WORLD` in
  the algorithm bodies.
- Application-level rank-local errors are reduced unconditionally with
  `checkpointFail()` before control flow can diverge.
- Format assumptions are guarded with `static_assert` and explicit includes
  (`<cstring>`, `<array>`, `<limits>`).

## License

The project is licensed under `LGPL-3.0-or-later`.

The root `LICENSE` file contains the full license text.

Do not change the project license without an explicit maintainer decision.
