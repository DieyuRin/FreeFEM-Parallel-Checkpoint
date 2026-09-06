# legacy-v0.md — the historical V0 `ffioWrite` helper

The original V0 smoke-test function

```cpp
ffioWrite(filename, values)
```

appended each rank's local FE coefficient vector to one file
(`MPI_File_write_at_all`):

```
rank 0 values ─┐
rank 1 values ─┤── one file (rank-concatenated)
...
```

The resulting payload contained overlap/ghost duplicates, depended on the
writer's domain decomposition, and could not be restarted with a different
number of ranks. It only proved the collective MPI-IO plumbing.

**Status:** `ffioWrite` is **no longer registered** by the 0.3.0 plugin
(hardening guide, "legacy V0 decision", option A). The historical
implementation lives in the development snapshots (archived v0/v1 folders of
the project history), not in `src/FreeFemCheckpoint.cpp`.

Use `ffcpWrite` / `ffcpRead` for all checkpointing. The compatibility aliases
`ffioWriteGlobal` / `ffioReadGlobal` still exist and use the canonical
implementation.
