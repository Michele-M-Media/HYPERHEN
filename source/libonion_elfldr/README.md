# libonion_elfldr

Single implementation of ptrace helpers (`pt_*`) and inject-path ELF loading
(`elfldr_load`, `elfldr_payload_args`, `elfldr_spawn`,
`elfldr_raise_privileges`).

## Authid policy

**Do not** flip ucred authid around every `ptrace` syscall.

Elevate **once** for the inject / attach window with:

```c
set_ucred_to_ptrace();  // → PTRACE_AUTHID 0x4800000000010003
```

That is Sony's SceTracer-style id. **`DEBUG_AUTHID` (`…0006`) is not accepted
for PT_*** — using it causes attach/`waitpid` failures (e.g. errno ECHILD).

Restore the previous authid after the inject window (see `inject_elf`).

## Consumers

| Target | Uses |
|--------|------|
| libNineS | `pt_*`, `elfldr_load`, `elfldr_payload_args` |
| bootstrapper | `elfldr_raise_privileges` |
| hyperhen_elfldr_server | `elfldr_spawn`, `elfldr_read` |
| util | `pt_attach` / `pt_mmap` after `set_ucred_to_ptrace()` |
| daemon | links via NineS |

Headers: `<onion/pt.h>`, `<onion/elfldr.h>`. Compatibility shims remain under
`libNineS/include/{pt,elfldr}.h` and `bootstrapper/include/`.
