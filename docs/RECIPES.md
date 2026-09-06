# Recipes

Tested user-level patterns that need no plugin/format changes.

## One checkpoint state = one .ffio file

Time-series / DNS checkpoints use one file per state; naming and retention
policy belong to the FreeFEM script:

```text
run/
  checkpoint_000000.ffio
  checkpoint_001000.ffio
  checkpoint_002000.ffio
```

In the FreeFEM script, build the filename from the step counter, e.g.:

```cpp
int step = 1000;
ffcpWrite("checkpoint_" + step + ".ffio", gid, u[]);
```

(FreeFEM string concatenation of `string + int` produces `checkpoint_1000.ffio`
when the int is the last operand; keep exactly this form.) Rotate/retain
previous checkpoints in the shell script; `ffcpWrite` overwrites its target,
and a crash during the overwrite can leave that file incomplete.

## Complex-field workaround

The plugin stores `real[int]` (double) only. Save a complex field as two
checkpoints:

```text
save real part as one canonical checkpoint
save imaginary part as another canonical checkpoint
```

The exact FreeFEM complex-to-real extraction syntax is left to the user's
FreeFEM version; do not rely on untested syntax.

## Multiple independent fields

For several independent FE vectors in one simulation, use **one file per
vector/state**, each with its own `gid[]`:

```cpp
ffcpWrite("u.ffio", gidU, u[]);
ffcpWrite("p.ffio", gidP, p[]);
ffcpWrite("T.ffio", gidT, T[]);
```

A multi-dataset file format is planned for a future format version
(`FFIOG002`), not for 0.3.0.
