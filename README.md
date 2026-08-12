# fxpipe

**Parallel BLAKE3 under FsCap** for [fx](https://github.com/ledocorp/fxlang) projects.

fxpipe hashes many paths with a structured worker pool (host nursery). Output is ordered `hex  path`. Not GNU parallel; not language spawn-closures. Dual-path: emit-C and IR.

| | |
|--|--|
| **Requires** | [fx](https://github.com/ledocorp/fxlang) **0.9.6+** (with `--cli`) |
| **Platforms** | Windows + Linux **x86_64** |
| **License** | Apache-2.0 (tool) · Apache-2.0 / CC0 (BLAKE3) |
| **Org** | [LedoCorp](http://www.ledocorp.org) |

## Install (release binaries)

1. Install [fx 0.9.6+](https://github.com/ledocorp/fxlang/releases/tag/v0.9.6).  
2. Download the asset for your OS from [Releases](https://github.com/ledocorp/fxpipe/releases).  
3. Put `bin/windows/fxpipe.exe` or `bin/linux/fxpipe` on your `PATH`.

```text
# Windows (PowerShell)
Invoke-WebRequest -Uri https://github.com/ledocorp/fxpipe/releases/download/v0.1.0/fxpipe-0.1.0-windows-x86_64.zip -OutFile fxpipe.zip
Expand-Archive fxpipe.zip -DestinationPath .
.\bin\windows\fxpipe.exe --help

# Linux
curl -LO https://github.com/ledocorp/fxpipe/releases/download/v0.1.0/fxpipe-0.1.0-linux-x86_64.tar.gz
tar xzf fxpipe-0.1.0-linux-x86_64.tar.gz
./bin/linux/fxpipe --help
```

Optional: `fxpipe-ir` is the IR dual-path binary (same CLI).

## Quick start

```text
fxpipe --allow fixtures --workers 4 abc.txt empty.txt
fxpipe --allow fixtures --workers 2 --stdin < paths.txt
```

## CLI

| Flag / arg | Behavior |
|------------|----------|
| `--allow <dir>` | Required FsCap root |
| `--workers N` | Default 4; clamp 1..16 |
| `--stdin` | Paths one per line (UTF-8); empty lines skipped |
| `<path>…` | Relative to allow or already under allow; no `..` |

Exit codes: `0` all hashed · `1` usage · `2` deny · `3` one or more hash failures (success lines still printed).

## Rebuild from source

Needs fx 0.9.6+ with `--cli`, `host/cap`, `host/concur` nursery, BLAKE3 amalgamation, and this repo’s `fx_pipe_ref.c`:

```text
fx build fxpipe_lib.fx -o out --emit-c --cli \
  --link fx_pipe_ref.c \
  --link <blake3>/blake3_amalg.c \
  --link <host>/cap/fx_cap_runtime.c \
  --link <host>/concur/fx_task_nursery.c \
  --link-include <blake3> \
  --link-include <host>/cap \
  --link-include <host>/concur
```

On Linux also link pthread (`--link-lib pthread` or `-pthread` as your toolchain expects). Same with `--backend ir` for IR.

## Non-goals (v1)

Shell/`{}` templates · GNU parallel parity · Chan/spawn-closure language epic · tree mode (use fxblake3 `--tree`) · Tokio/Go identity · macOS prebuilt claim

## Docs

- [docs/FXPIPE.md](docs/FXPIPE.md) — design summary  
- [docs/releases/](docs/releases/) — release notes  
- Language: [ledocorp/fxlang](https://github.com/ledocorp/fxlang)

## License

Copyright Shawn Londono · LedoCorp · Apache-2.0 — see [LICENSE](LICENSE).
