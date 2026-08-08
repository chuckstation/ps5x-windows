# Security Policy

Repository: [github.com/libaerto/ps5x-windows](https://github.com/libaerto/ps5x-windows)

## Scope

PS5x is a research emulator framework. Security disclosures relevant to PS5x:

- Memory safety bugs in the Loader (ELF parser accepts untrusted input)
- Memory safety bugs in the Filesystem VFS (guest path traversal)
- Vulnerabilities in the Debugger that allow host code execution
- Vulnerabilities in CrashHandler (crash handler accepts OS signals/exceptions)
- Vulnerabilities in SaveState (deserialization of untrusted save files)
- Dependency vulnerabilities (Kyty, Catch2, Dear ImGui, SDL2)

Out of scope:
- Bugs in emulated PS5 firmware or games
- Requests to implement DRM circumvention
- Requests to add firmware decryption

## Reporting

Please open a **private** GitHub Security Advisory at
[github.com/libaerto/ps5x-windows/security](https://github.com/libaerto/ps5x-windows/security)
rather than a public issue for vulnerabilities in PS5x itself.

For Kyty upstream bugs, report them to https://github.com/InoriRus/Kyty/issues.

## Firmware

PS5x never handles real PS5 firmware encryption keys.
The `Loader::ValidateFirmware()` function only checks for path existence;
it does not decrypt, verify signatures on, or embed any cryptographic material.
