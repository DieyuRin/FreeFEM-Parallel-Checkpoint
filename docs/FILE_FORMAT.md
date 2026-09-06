# File format (FFIOG001)

Format name: `FFIOG001` (magic bytes). Unchanged by the V3 project rename; a
real layout change requires a new magic (e.g. `FFIOG002`).

## Layout

Fixed 64-byte header followed by the payload:

```
offset  size  field                      meaning
------  ----  -------------------------  --------------------------------
0       8     magic  "FFIOG001"          format identification
8       8     version (uint64)           = 1
16      8     global DOF count           Nglobal
24      8     scalar byte width          (8 for IEEE-754 double)
32      8     writer MPI size            informational only
40      8     overlapping input count    informational (write-time)
48      8     duplicates removed         informational (write-time)
56      8     reserved                   (0)
64      N*8   payload                    payload[i] = value of canonical
                                         global FE DOF i
```

## Valid file size (strict)

A valid FFIOG001 file has **exactly**

```text
file size == 64 + Nglobal * width
```

bytes. Readers reject files that are smaller (truncated) or larger
(unexpected trailing bytes). The writer truncates the target before writing
(`MPI_File_set_size(0)`), so files it produces satisfy the equality.

## Conventions

- **Integer representation:** uint64 fields written one field at a time at
  fixed byte offsets (no raw C++ structs, no compiler padding). 64-bit double
  and uint64 are asserted at compile time (`static_assert`).
- **Byte order:** fields are written in native host order. The V1 format
  targets little-endian hosts; tested on little-endian x86_64. ARM64 (also
  little-endian in practice) is expected to work but is not yet tested.
  Cross-endian conversion is not implemented and not claimed.
- **Scalar representation:** IEEE-754 double (64-bit). FreeFEM `real`.
  `complex` and `float32` are not supported; a reader rejects any scalar
  width other than 8.
- **Payload ordering:** `payload[i]` is the value of canonical global FE DOF
  `i`. The ordering is defined by the *global* FE space used at write time
  (via FreeFEM's `restrict()`), never by the writer's MPI partition.

## Duplicate-value semantics

The writer receives possibly duplicated (overlapping) local copies of a
canonical DOF. Copies must be finite and agree within the relative tolerance
`1e-12 * (1 + max(|a|,|b|))`; the first value seen for a gid is stored.
Disagreement beyond the tolerance or any NaN/Inf input fails the write. The
stored payload contains exactly one value per canonical DOF.

## Partition-independence semantics

The canonical **layout** is writer-partition independent:

- For a fixed canonical field whose duplicate overlap copies are identical,
  the payload is byte-identical across the tested writer MPI sizes
  ({1,2,3,4,8,16}; verified by `tests/test_payload_invariance.sh`).
- If duplicate copies differ numerically but remain within the accepted
  tolerance, the checkpoint is considered consistent, but byte-for-byte
  payload identity across different decompositions is not guaranteed.

Header fields "writer MPI size", "overlapping input count", and "duplicates
removed" are informational and may differ between runs.

## Reader rejection rules

A reader rejects, collectively on all ranks:

| condition                       | error                |
|---------------------------------|----------------------|
| file smaller than 64 bytes      | `kErrHeader`         |
| header read shorter than 64 B   | `kErrHeader`         |
| magic != `FFIOG001`             | `kErrMagic`          |
| version != 1                    | `kErrVersion`        |
| scalar width != 8               | `kErrScalarWidth`    |
| Nglobal == 0 or malformed       | `kErrEmpty`/`kErrHeader` |
| size overflow in 64 + N*width   | `kErrHeader`         |
| file < 64 + Nglobal*width       | `kErrTruncated`      |
| file > 64 + Nglobal*width       | `kErrTrailingBytes`  |
| local gid outside [0, Nglobal)  | `kErrGidRange`       |
| NaN/Inf payload value           | `kErrNonFinite`      |

## Compatibility policy

- FFIOG001 readers read FFIOG001 only.
- A layout-incompatible future format uses a new magic (e.g. `FFIOG002`).
- The project/plugin rename does not change the file magic.
