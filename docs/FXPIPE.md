# fxpipe — user design summary

fxpipe is a **parallel BLAKE3 CLI** under **FsCap**: N host-nursery workers hash files and print ordered `hex  path`.

## Why fxpipe

| Keep | Refuse (v1) |
|------|-------------|
| Worker pool + ordered results | Shell ambient fan-out |
| Required `--allow` | Ambient filesystem |
| Host nursery task fns | Language spawn-closure claims |
| Dual-path emit-C + IR | Optional-IR theater |

## Exit codes

| Code | Meaning |
|------|---------|
| 0 | all hashed |
| 1 | usage |
| 2 | cap deny |
| 3 | one or more hash/IO failures |

## Rebuild

See root README. Needs fx 0.9.6+ with `--cli`, `host/cap`, `host/concur`, and BLAKE3 amalgamation.

## Non-goals

GNU parallel · Chan/spawn epics · tree mode · macOS claim
