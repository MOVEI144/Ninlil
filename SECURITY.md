# Security policy

Ninlil is pre-release software. No version is currently declared production-supported.

## Reporting a vulnerability

Do not publish exploit details in a public issue. Use GitHub private vulnerability reporting for this repository when available, or contact the MOVEI144 maintainers through a private organization channel. Include the affected commit, threat model, reproduction steps, and impact.

## Security boundaries

- The runtime transports opaque application bytes; it does not make product authorization decisions.
- A radio packet is not trusted merely because its CRC is valid.
- Secure sessions require authenticated key establishment; diagnostic M1 radio traffic is not production-secure.
- Unknown, corrupt, or ambiguous persistent state fails closed.
- Secrets and application payloads must not be written to routine logs.

Security fixes must add a regression test or a written explanation of why automation is not possible.
