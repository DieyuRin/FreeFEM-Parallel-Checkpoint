# Changelog

## [Unreleased]

- Selected `LGPL-3.0-or-later` as the project license.
- Added root `LICENSE`.
- Added SPDX license identifiers and contribution-license notice.

## [0.3.0] - 2026-09-07

Public-release hardening / stabilization:

- Renamed project to FreeFemCheckpoint
- Added stable ffcpWrite / ffcpRead API (collective on MPI_COMM_WORLD)
- Standardized return semantics: both functions return Nglobal on success,
  rank-invariant
- Made all application-level validation failures collective-safe (no
  rank-local return can strand other ranks in a collective)
- Replaced raw negative returns with named error codes (`kErr*`, see
  docs/API.md)
- Hardened reader header/file validation: size check before decoding,
  `MPI_Get_count == 64`, strict exact-size policy (rejects truncated payloads
  and trailing bytes), overflow-safe expected-size arithmetic
- Added MPI_Alltoallv int count/displacement overflow checks
  (`kErrOverflow`)
- Defined non-finite policy: NaN/Inf checkpoint values rejected
  (`kErrNonFinite`); duplicate consistency via centralized relative tolerance
- Added compile-time format guards (`static_assert`) and explicit includes
- Removed the V0 rank-concatenated `ffioWrite` helper from the public plugin
  (see docs/legacy-v0.md)
- Added repository structure and build system (Makefile, `make test`)
- Expanded test suite: scalar (36) and mixed (15) N-to-M matrices, payload
  invariance, corruption tests (magic/version/width/Nglobal/short header/
  truncation/trailing bytes), duplicate-policy tests (within tolerance,
  conflict, NaN, +Inf), gid-range/size-mismatch/missing-DOF negative tests
- Added formal documentation (API, DESIGN, FILE_FORMAT, BUILD, DEVELOPMENT,
  RECIPES, legacy-v0)

## [0.2.0]

- Added N-to-M parallel reader/restart
- Added mixed FE support validation

## [0.1.0]

- Added canonical global-DOF parallel writer
