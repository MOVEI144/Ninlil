# Coding style

## C

- C11 first-party code.
- GCC and Clang with warnings treated as errors.
- No variable-length arrays, recursion, C bitfields on wire, packed-struct wire encoding, `setjmp`/`longjmp`, or unchecked signed overflow.
- No struct-to-packet `memcpy`; encode and decode fields explicitly.
- Core hot paths do not allocate per packet.
- Public symbols use the `ninlil_` prefix.
- Public structs are opaque unless a value type is intentionally part of the ABI.
- API documentation states ownership, lifetime, blocking, durability, and state-change semantics.
- `goto` is limited to forward-only cleanup.

## Structure

- One module, one responsibility.
- A normal function should remain below roughly 80 lines; larger functions receive explicit review.
- A normal source file should remain below roughly 500 lines unless it is a table, codec, or test-vector file.
- Do not create an interface for a single implementation without a platform, test, or security boundary.

## Comments

Explain why an invariant exists, especially around durability, nonce safety, and recovery. Do not restate the next line of code in English.
