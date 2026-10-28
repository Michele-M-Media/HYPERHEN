# HyperLinux 13.60 binary lock

HyperHEN keeps the supplied `1360linux.elf` as a dedicated, experimental
firmware 13.60 loader. It is embedded independently and never replaces the
source-built HyperLinux loader used by the established firmware profiles.

- SHA-256: `e0bdd37ae0367b2864fd2486bcd77e2e930f7d89f2ef96fd235bb15d22db90c7`
- Size: `248768` bytes
- Format: little-endian x86-64 ELF
- Provenance: binary supplied for HyperHEN integration; matching source and a
  standalone license file were not included.

The loader retains its own fail-closed firmware checks. HyperHEN only selects
and hands off the byte-exact image; it does not patch those checks. Confirm the
upstream license and complete the loader's 13.60 profile before redistribution
or claiming a successful Linux boot.
