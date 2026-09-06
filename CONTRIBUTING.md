# Contributing to FreeFemCheckpoint

## Build

```bash
make            # builds FreeFemCheckpoint.so
```

## Run tests

```bash
make test       # full regression suite (scalar/mixed matrices, payload
                # invariance, corruption and duplicate-policy tests)
```

Run `make test` before submitting a change. There is no CI yet; tests are run
locally.

## Coding rules

- C++14; the plugin must build with the bundled `ff-c++` wrapper.
- Console output uses the `[FreeFemCheckpoint]` prefix; only rank 0 prints
  aggregate summaries.
- Use the named constants (`kErr*`, `kDuplicateTolerance`) — never scatter raw
  negative returns or magic numbers.
- Application-level rank-local errors must be **reduced unconditionally**
  (`checkpointFail`) before collective control flow can diverge. A rank may
  never return from a collective operation while other ranks continue.
- Do not introduce a global gather, rank-0 serial payload write, or
  full-vector broadcast.
- Keep per-rank memory `O(Nglobal/P + localN + buffers)`.
- Do not silently change the `FFIOG001` file layout. Format changes require a
  new magic and an update to `docs/FILE_FORMAT.md`.

## How to add tests

- Positive: extend the np matrix lists in `tests/run_scalar_matrix.sh` /
  `tests/run_mixed_matrix.sh`, or add a new `tests/run_*.sh`.
- Negative: add a fixture under `tests/edp/` that prints an explicit
  EXPECT-FAIL / EXPECT-SUCCESS marker, plus a `tests/test_*.sh` that greps
  for it, and wire it into `tests/run_all.sh`.
- A failure test passes only if **all ranks terminate** with the expected
  marker (no timeout/deadlock).

## License of contributions

By submitting a contribution to this repository, you agree that your
contribution may be distributed under the project's `LGPL-3.0-or-later`
license.

## New features and platforms

- A new feature requires a test and documentation.
- A newly tested platform must record its exact FreeFEM / MPI / architecture
  version in `docs/BUILD.md` and the README table before it is listed as
  tested.
