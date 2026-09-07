# W01・W02 ローカル検証記録 — 2026-09-07

## 目的と対象

製品非依存の責任分担を具体化し、既存Cコアによる一対一・一対多の模擬試験を
再現できる状態にする。次は固定した2台でM1の実機確認に着手する。

- 対象ブランチ：`codex/static-star-foundation`
- 開始点：`9b00c09b4209a1b5f15336d223a6cbe0333382c9`
- 検証した実装revision：`82f05438aa40f2f75e67c8ff1d4d85fc9b9f9297`。
  テストした作業ツリーの実装をこのcommitに記録し、この検証文書は後続の文書commitで追加した。
- 契約：[ADAPTIVE_NETWORK_CONTRACT.md](ADAPTIVE_NETWORK_CONTRACT.md)
- 実行手順とモデルの限界：[SIMULATION.md](SIMULATION.md)
- 既存の `src/`、`include/`、`ports/`、`embedded/` に変更なし。

## 実行環境

Windows上のローカルDockerを使用。GitHub Actionsなどのhosted CIは実行していない。

| 項目 | 実測したversion |
|---|---|
| Docker Engine | 29.7.2 |
| Linux container | Ubuntu 24.04.4 LTS / x86_64 |
| Kernel | 6.6.87.2-microsoft-standard-WSL2 |
| glibc | 2.39 |
| GCC | 13.3.0 |
| Clang / clang-format | 18.1.3 |
| Clang sanitizer/fuzzer runtime | libclang-rt-18-dev 1:18.1.3-1ubuntu1 |
| CMake | 3.28.3 |
| Ninja | 1.11.1 |
| Git in container | 2.43.0 |

container名は `ninlil-static-star-verify`。リポジトリを `/work` にbind mountした。
Linux側のGitは `safe.directory=/work` と `core.autocrlf=true` をcontainer内の
global設定に限定して使用した。Windowsの既存checkoutの正規化と同じ扱いにするためである。
`.gitattributes`でshell script、manifest、依存revision台帳のLFを固定した。
依存ライブラリ・ESP-IDFの採用revisionは変更していない。

containerの検査ツール導入コマンド：

```sh
apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
  build-essential clang clang-format cmake ninja-build git ca-certificates python3
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends libclang-rt-18-dev
```

Pythonはこの実装の必須依存ではない。新しいrunner・モデル・検査はCとshellで動く。

## 検査コマンドと結果

最終の一括検査：

```sh
NINLIL_BUILD_ROOT=/tmp/ninlil-final NINLIL_JOBS=4 bash scripts/ci.sh
```

最終一括実行はexit 0、末尾の `Ninlil project CI PASS` を確認した。
名称にCIを含む既存scriptをローカルで実行した結果であり、hosted CIは実行していない。

| 検査 | 結果 |
|---|---|
| GCC Debug | CTest 15/15 PASS |
| Clang Debug | CTest 15/15 PASS |
| GCC AddressSanitizer / UndefinedBehaviorSanitizer | CTest 15/15 PASS |
| Clang AddressSanitizer / UndefinedBehaviorSanitizer | CTest 15/15 PASS |
| 3つのmanifestの完全な出力再現性、CLIの不正入力・未完了exit | 4構成でPASS |
| clang-format | PASS |
| ESP32-S3のstubを使う厳格な構文検査 | PASS。実際のESP-IDF linkとは別 |
| GCC analyzer / Clang static analyzer | PASS |
| manifest parser fuzz | Clang ASan/UBSan、seed 20260907、10000実行 PASS |
| Semtech provenance checker | 設定されたpinの整合を確認。driver未取得のためvendored source検査はdeferred |
| 差分の空白、shell構文、変更文書のローカルリンク | PASS |

各compiler構成はCMakeの `NINLIL_SANITIZE=OFF/ON`、`CMAKE_BUILD_TYPE=Debug`、
既存のstrict warning flagsを使用。メモリ検査は `detect_leaks=1:halt_on_error=1`、
UBSanは `halt_on_error=1`。既存の失敗試験や警告を削除・弱化していない。

初回の一括実行は、4構成のテスト完了後にWindowsのCRLF shebangで停止した。
LF固定後の追加検査では、Linux Gitの改行正規化設定の差による空白検査の誤差も
解消した。これらの途中停止を一括検査の成功とは扱わず、最終コマンドを再実行した。

## 確認できた動作

- 親1台・子4台の双方向32件を、宛先とpayloadを取り違えず配送できる。
- 子1台の停止中に、残る3台との24件が完了する。停止端末に関係する未完了8件は
  成功に変換されず、復旧後に完了する。
- 親または子のruntime再起動で、同じ依頼を再提示しても元のmessage IDを維持する。
- 別processを正常closeなしで終了しても、journalを開き直して配送を再開できる。
- 受領証を最後まで遮断すると、32件が受信側に保存・提示されても送信側は全て
  `ACTIVE`のまま。未観測の証拠時刻は`NA`、CLIは未完了を表すexit 2になる。
- 容量試験は192件を提示し、160件を受理、32件を明示拒否。受理済み160件は完了する。
- 最大長frameの送信時間、途中切断、受信queue満杯、送信待ち枠満杯、不正な相手や
  不正packetの拒否を検査した。

復旧manifest（seed 42）の集計：32受理、32完了、拒否0、RX overflow 0。
完了分の最大遅延は仮想時間69.600秒。これは故障時間と固定calendarを含む模擬値であり、
実電波の速度や製品SLOを示すものではない。

## 証拠の保存先とhash

raw log・reportはローカルの `C:/dev/job/iot/Ninlil/.verify-sim-evidence/` に保存。
Gitにはこの説明とhashを残し、生成binary・raw logは通常のignore規則に従う。

| 完全なreport | SHA-256 |
|---|---|
| direct.report | `013bfebbeb26675ea0805cec7083d64fa43b4c8a7c1cb6f9732e36fa2b4d82f7` |
| star.report | `1d4f41b7a22d490ab8c2c049f821e9f679bafe7461ace98427ddf90570d6a25a` |
| recovery.report | `c703b99d781be47e043b0b6148e511c41e8f51f970e18bc2559a224d827253ed` |

`final-host-matrix.log` のSHA-256：
`7a41d4a9c45c2424cb36cf606ec8b5f06390d4074e93783ef2c7e2fb6f21f35b`。

検査用containerは作業後に停止する。環境を再使用する場合は
`docker start ninlil-static-star-verify` の後、`docker exec`で上のコマンドを実行できる。

## 未実施と③への引継ぎ

- 今回の変更はhost用の契約・simulator・検査。ESP-IDF v6.0.2の実ビルドは今回未実施。
  ③の開始時にローカルで実行し、firmware hashを固定する。
- ボードへの書き込み、RF送受信、USB接続、配線確認、GPIO38極性確認、実Flashの
  電源断試験は未実施。Issue #7の物理チェックは完了扱いにしない。
- 模擬radioの8枠FIFO・再送coalescing・固定calendarは、現行SX1262の実装そのもの
  ではない。実機一対多の公平性、隠れ端末や通信衝突、実測保存時間は後続検証が必要。
- 認証、Join、Relay、Coordinator、自動最適化、長期GCは今回有効化していない。

次は [M1_HIL_ACCEPTANCE.md](M1_HIL_ACCEPTANCE.md) の準備から始める。
cleanなsource revision、ローカルtarget build、Board A/BとUSBの識別、アンテナ・配線・
RF条件をそろえてから、初期化と一対一通信の証拠を集める。
