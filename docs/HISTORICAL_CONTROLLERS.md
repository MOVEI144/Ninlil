# Superseded host-owned controllers

Historical RF evidence remains tied to its original source. The autonomous
owner and reference firmware replace the USB-owned orchestration and model
controller below. Original sources remain recoverable from the local Git
objects; no firmware data, original results or review witnesses are removed.

Each destination is `git show f5b5b7eb84f7ad830cb2bbba2a9a7820820d53d4:<path>`.
Before removal, the Git blob was read and verified against this SHA-256.
Historical evidence is measured separately from the active secure profile,
and remains included in the project-wide first-party ceiling.

| Original path | SHA-256 (Git bytes) |
|---|---|
| `embedded/esp32s3/main/bench_relay.c` | `06f91ecf495043cc2023f67851cf918b2743485a75cbf213d151dcfd2e21bb46` |
| `embedded/esp32s3/main/bench_relay.h` | `352c42aac6f3b8d63969fc724752e3e2ce70f4c61d615843b9b1d0e6df37201b` |
| `embedded/esp32s3/main/bench_sessions.c` | `debc04f196a761bc9e7ebdffba42da42ada172bc56d991aae8bb33f6626f4a29` |
| `embedded/esp32s3/main/secure_bench.c` | `48eeac67698621e2cd6d8a964b87f77ff20aa8febdad8d437ee1a9d2a25704e6` |
| `embedded/esp32s3/main/secure_bench.h` | `a8b2f266e05ee28b79ca3b00cd7f5e43391420f40d1f4e503b14a3751a31d560` |
| `embedded/esp32s3/main/secure_bench_crypto.c` | `0c8c25d6046c22764c6ffb872aeefcf7f8c81ea167ee0d1e43b9b4f06b64649a` |
| `examples/secure_network/lab.h` | `c8e7b5b83de520c245c0e4a13f75008b29a006e2b71c7447ef23533d09f21f75` |
| `examples/secure_network/lab_crypto.c` | `8c237f6e6f3135465062c0d3ebda4037fab967ed2d1908432c76dadee1057185` |
| `examples/secure_network/lab_crypto.h` | `e36ead96e89a036f593f93031789b3ffeab94dfe12f2e5d895121189dd71bcb7` |
| `examples/secure_network/lab_run.c` | `13fbd5813d6b9ee3454c90193090f7b47cb39a56e81cf588bf84dbfe666679b6` |
| `examples/secure_network/lab_setup.c` | `a3bba93c0a8a1955fdaf4a6241f9232ccdc22a71b9a1af0ed6ae51df5b45c8be` |
| `examples/secure_network/main.c` | `d27264a3882f21dba4fd8548b215b12b4dd70da2ac6b36f0785e8ed86dde5e2a` |
| `tools/secure_bench/analyze.py` | `ba3b1e44779a5f33eaf6d1ede9ec770cbaf808f70ec3140bdd442b2e0e95bb4e` |
| `tools/secure_bench/build.sh` | `9d7a8f3a3af47e18c8ff7c354d60167c292cf9f0267f955cb7268a6879764ae9` |
| `tools/secure_bench/campaign.py` | `9e279a1a3cb2d01f57d09889eb9c0b75abbf244d250f048aba7d740d4bd510f4` |
| `tools/secure_bench/hardware.py` | `c75d1345917423c2b43b232a4204b7d61192cc15bc2b33892de3e11a94ad9b55` |
| `tools/secure_bench/README.md` | `ee1fbd0fe8bf8079e4312cfdf5239ae4d37fa19234c3ab24ae38164e8a322cc7` |
| `tools/secure_bench/relay_analyze.py` | `40dbc450e9c4d32a0437e59e6d6b5e6606a3ccac46ea58c6205a0f1a51146143` |
| `tools/secure_bench/relay_campaign.py` | `ce6da695cf8ab6a25850e24f73642a8935a423ee3c6d5078d8e61f64f258aff5` |
| `tools/secure_bench/relay_hardware.py` | `d18af51c55d2dd1d68f5c8d56fa79ba2c6761f6bbe2509f02bc8affe9db3fd22` |
| `tools/secure_bench/restart.py` | `1a06a73c3f62fd192dc08b7d619f3131028faf2776ec930ed9e3330a6c46035e` |
| `tools/secure_bench/verify_idle.py` | `0787d656094b3597f5d13dc64fad7dddec5103f5d343c3d553db0bb1063c47f8` |
| `include/ninlil_control_fragment.h` | `516b153db0ca5183b734bd5d149f2246f7f67b11e0bf670daafd8f032902c84b` |
| `src/ninlil_control_fragment.c` | `cd4d9ed5c7729c53f6d9c075372d44731e4330f2bf3bcf2af2e0585ad7d420f7` |
| `tests/test_control_fragment.c` | `4b7d8d04e8bd44bced77020c05d156a38a662a1dba04ecd23b6a39cda5300fda` |
