# Isolated IO/pump boundary declarations
These are deliberately named test doubles, not SDK ABI or cryptography. Tests
compile byte-identical production C while substituting these dependencies.
The canonical include/ninlil.h is used unchanged. Never install these headers.
