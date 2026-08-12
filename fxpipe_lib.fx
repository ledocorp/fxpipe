// fxpipe_lib — parallel BLAKE3 under FsCap (--cli).
module fxpipe_lib;

using core;
import std/io;
import std/string;
import std/strutil;

extern "c" {
    fn fx_cli_argc() -> i32;
    fn fx_cli_arg(i: i32) -> string;
    effects { alloc } fn fx_guest_begin(root: string, arena_bytes: i64) -> i64;
    effects { alloc } fn fx_guest_end(ctx_handle: i64) -> i32;
    effects { alloc } fn fx_guest_mint_fscap(ctx_handle: i64, root: string) -> i64;
    effects { alloc, io, concur } fn fx_pipe_reset() -> i32;
    effects { alloc, io, concur } fn fx_pipe_add_path(path: string) -> i32;
    effects { alloc, io, concur } fn fx_pipe_run(fs_handle: i64, workers: i32) -> i32;
    effects { alloc, io, concur } fn fx_pipe_count() -> i32;
    effects { alloc, io, concur } fn fx_pipe_path_at(i: i32) -> string;
    effects { alloc, io, concur } fn fx_pipe_hex_at(i: i32) -> string;
    effects { alloc, io, concur } fn fx_pipe_status_at(i: i32) -> i32;
    effects { alloc, io, concur } fn fx_pipe_stdin_next_path() -> string;
}

fn eq(a: string, b: string) -> bool {
    return string.compare(a, b);
}

fn usage() -> i32 effects { io } {
    let _u = io.write_err("usage: fxpipe --allow <dir> [--workers N] [--stdin] [<path> ...]");
    return 1;
}

fn parse_workers(s: string) -> i32 {
    let nlen = str_len(s);
    let i: i32 = 0;
    let n: i32 = 0;
    while (i < nlen) {
        let c: i32 = str_byte_at(s, i);
        if (c < 48) {
            break;
        }
        if (c > 57) {
            break;
        }
        n = n * 10 + (c - 48);
        i = i + 1;
    }
    return n;
}

fn clamp_workers(n: i32) -> i32 {
    if (n < 1) {
        return 1;
    }
    if (n > 16) {
        return 16;
    }
    return n;
}

fn path_has_dotdot(s: string) -> bool {
    return strutil.contains(s, "..");
}

fn resolve_under(allow: string, rel: string) -> Result<string, core_Err> effects { alloc } {
    let al = string.len(allow);
    let dl = string.len(rel);
    if (dl > al) {
        if (strutil.starts_with(rel, allow) == true) {
            let c = string.byte_at(rel, al);
            if (c == 47) {
                return Ok(rel);
            }
            if (c == 92) {
                return Ok(rel);
            }
        }
    }
    let mid = string.concat(allow, "/")?;
    return string.concat(mid, rel);
}

fn add_one(allow: string, a: string) -> Result<i32, core_Err> effects { alloc, io, concur } {
    if (path_has_dotdot(a) == true) {
        let _m = io.write_err("fxpipe: path must not contain ..");
        return Ok(1);
    }
    let resolved = resolve_under(allow, a)?;
    let st = fx_pipe_add_path(resolved);
    if (st != 0) {
        let _m = io.write_err("fxpipe: too many paths or add failed");
        return Ok(1);
    }
    return Ok(0);
}

fn map_st(st: i32) -> i32 effects { io } {
    if (st == 0) {
        return 0;
    }
    if (st == -1) {
        let _u = io.write_err("fxpipe: bad flags / no paths");
        return 1;
    }
    if (st == -2) {
        let _d = io.write_err("fxpipe: path outside allow / denied");
        return 2;
    }
    let _f = io.write_err("fxpipe: one or more hash failures");
    return 3;
}

fn cli_main() -> Result<i32, core_Err> effects { alloc, io, concur, mut } {
    let allow = "";
    let workers: i32 = 4;
    let use_stdin: bool = false;
    let argc = fx_cli_argc();
    let i: i32 = 1;
    let path_count: i32 = 0;

    let _r = fx_pipe_reset();

    while (i < argc) {
        let a = fx_cli_arg(i);
        if (eq(a, "--help") == true) {
            return Ok(usage());
        }
        if (eq(a, "-h") == true) {
            return Ok(usage());
        }
        if (eq(a, "--allow") == true) {
            i = i + 1;
            if (i >= argc) {
                let _m = io.write_err("fxpipe: --allow requires a directory");
                return Ok(1);
            }
            allow = fx_cli_arg(i);
            i = i + 1;
        } else {
            if (eq(a, "--workers") == true) {
                i = i + 1;
                if (i >= argc) {
                    let _m = io.write_err("fxpipe: --workers requires N");
                    return Ok(1);
                }
                workers = parse_workers(fx_cli_arg(i));
                if (workers < 1) {
                    let _m = io.write_err("fxpipe: --workers must be >= 1");
                    return Ok(1);
                }
                workers = clamp_workers(workers);
                i = i + 1;
            } else {
                if (eq(a, "--stdin") == true) {
                    use_stdin = true;
                    i = i + 1;
                } else {
                    if (string.len(allow) == 0) {
                        let _m = io.write_err("fxpipe: --allow is required before paths");
                        return Ok(1);
                    }
                    let add_st = add_one(allow, a)?;
                    if (add_st != 0) {
                        return Ok(add_st);
                    }
                    path_count = path_count + 1;
                    i = i + 1;
                }
            }
        }
    }

    if (string.len(allow) == 0) {
        return Ok(usage());
    }

    if (use_stdin == true) {
        while (true) {
            let line = fx_pipe_stdin_next_path();
            if (string.len(line) == 0) {
                break;
            }
            let add_st = add_one(allow, line)?;
            if (add_st != 0) {
                return Ok(add_st);
            }
            path_count = path_count + 1;
        }
    }

    if (path_count == 0) {
        let _m = io.write_err("fxpipe: at least one path required");
        return Ok(1);
    }

    workers = clamp_workers(workers);

    let g = fx_guest_begin(allow, 65536);
    if (g == 0) {
        let _g = io.write_err("fxpipe: guest begin failed");
        return Ok(2);
    }
    let fs = fx_guest_mint_fscap(g, "");
    if (fs == 0) {
        let _e0 = fx_guest_end(g);
        let _f = io.write_err("fxpipe: mint_fs failed");
        return Ok(2);
    }

    let st = fx_pipe_run(fs, workers);
    let n = fx_pipe_count();
    let j: i32 = 0;
    while (j < n) {
        let js = fx_pipe_status_at(j);
        if (js == 0) {
            let hex = fx_pipe_hex_at(j);
            let path = fx_pipe_path_at(j);
            let line0 = string.concat(hex, "  ")?;
            let line = string.concat(line0, path)?;
            let _w = io.write_line(line);
        }
        j = j + 1;
    }
    let _en = fx_guest_end(g);
    return Ok(map_st(st));
}
