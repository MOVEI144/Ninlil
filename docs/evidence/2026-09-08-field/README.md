# 増設・撤去の実装とベンチ証拠

開始2026-09-08、最終確認2026-09-09。Ninlilは製品非依存。
KGuard、Mini PC、漏水センサーを必須条件にしない。
対応する実装は[実装記録](../../FIELD_DEPLOYMENT_2026-09-08.md)、
全試行の合否と時間は[結果JSON](../../FIELD_RESULTS_2026-09-08.json)を参照。
本記録は現場投入や全体設計の完成を宣言するものではない。

## 証拠の保持

元の作業用記録は削除・移動していない。公開応答、失敗した試行、firmware、入力hash、
書込み前後の保存領域照合、ローカル試験ログをGit履歴に固定した。
秘密鍵の書き出しと機器バックアップは行っていない。
固定revision: `c1d737e1af085073d7a6eca6375314c7cc350b23`

[INDEX.csv](INDEX.csv)は試行・build単位の索引。各行のmanifestに全artifactの
Git内path、SHA-256、byte数、blob IDがある。全ファイルをGitから読み戻して照合済み。
個別ファイルは次で取り出せる。SHA-256は元のbyte列に対する値であり、
PowerShellでテキストとして再保存すると改行が変わる可能性がある。

```text
git show <fixed-revision>:<manifest-path-from-INDEX.csv>
git show <fixed-revision>:<artifact-path-from-manifest>
```

raw証拠は最新checkoutに複製しない。実装・テスト・検証ツールは通常のrepository fileに置く。
v4〜v18の途中版は実測binary/hashで特定する。未commitだった各途中版のソース再現を保証しない。
最終v19の全target入力は `field-node-v19/inputs.json` で照合した。
v18→v19の入力差分はUSB未設定状態の2行とnative test登録だけで、無線処理の変更はない。

## 機器・条件

- Wio-SX1262 + XIAO ESP32S3の3台。Root=1、Relay=2、Endpoint=3。
- USB識別番号: 1=`E0:72:A1:F7:FF:0C`、2=`E0:72:A1:D8:3E:74`、3=`E0:72:A1:D7:77:28`。
- USB給電、アンテナ装着。距離・USBハブの配置条件は未確認で、測定済みと推定しない。
- 検証済み日本profileを維持: 921.4 MHz、SF7、BW125 kHz、CR4/5、preamble 8、最大−3 dBm。
- Root/EndpointのF5で直通NS/NBを遮断。RelayはF1で取り次ぐ。実際の距離による圏外試験とは異なる。
- 既存Flash領域を移動せず、末尾へ設定領域を追加。書込みは保存領域の前後MD5を照合。
- 最後はv19、保存設定revision 18、全台autorun解除・停止・F0。`field-final-state`に公開応答を保存。

## ローカル確認の再実行

hostはUbuntu 24.04/WSL2 Linux 6.6.87.2、GCC13.3、Clang18.1.3、CMake3.28.3、Ninja1.11.1。
targetはESP-IDF6.0.2、Xtensa GCC15.2.0（esp-15.2.0_20251204）。
USB操作はWindows、Python3.12、pyserial3.5、esptool5.3。toolchain変更は行っていない。
すべてローカル検証。hosted CIを実行・再実行していない。

Docker `ninlil-static-star-verify` 内の `/work` で、各variantの構成はDebug、
compilerはGCCまたはClang、sanitizeはON/OFF。次を4構成で実行した。

```sh
cmake --build /tmp/ninlil-oss-final/<variant> -j4
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir /tmp/ninlil-oss-final/<variant> --no-tests=error --output-on-failure
```

最終logは `local/unconfigured-final-<variant>.log`、CTest詳細は同名の `LastTest.log`。
GCC/Clangの通常版とASan/UBSan版を区別する。新規USBケースは修正前の失敗も残す。
`python tests/test_manage.py` は新品認可と、Root更新前の変更拒否を確認する。
format/static/ESP syntax/package/各scopeと全体行数の結果は `local/unconfigured-extra-v2.log`。
最初のextra実行はClang解析にGCC構成を指定した操作誤りで失敗し、Clang構成で再実行した。
制御parserとsimulatorの各10,000 fuzz、711 vendor tests、simulatorの再現性は
今回の変更中に実行した `remaining-gates.log` と `control-admission-extra.log` に保持する。
v18で確認した12 scheduling seedsは、無線処理が変わらないv19で再実行したとは記載しない。

target buildはDocker `ninlil-456-target-v2` 内で実行した。

```sh
source /opt/esp/idf/export.sh
python tools/node_hil/build.py .verify-m1-evidence/extensions-node-v3/1/sdkconfig .verify-m1-evidence/oss-node-roster.h .verify-m1-evidence/field-node-v19 --nodes 1 2 3 --build-directory /tmp/ninlil-node-builds
cd embedded/esp32s3
idf.py -B /tmp/ninlil-oss-default-build -D SDKCONFIG=/tmp/ninlil-oss-default-sdkconfig -D NINLIL_NODE_ROSTER_HEADER= reconfigure build
```

## 実機確認と解釈

Windowsのhardware Pythonで、各台を順に `hardware.py <node> <build-folder>` で書き込んだ。
`--initial`を使わず、既存identity・Core・Relay・アプリケーション履歴を維持した。

```powershell
.verify-m1-tools/Scripts/python.exe tools/node_hil/deployment_test.py .verify-m1-evidence/field-setup-hil-v6
.verify-m1-tools/Scripts/python.exe tools/node_hil/campaign.py .verify-m1-evidence/field-receipt-recovery-v12 --sequence 26090876 --existing-message d5d772e9e266e218809701b286235933 --seconds 570 --block-bootstrap --diagnostics
.verify-m1-tools/Scripts/python.exe tools/node_hil/campaign.py .verify-m1-evidence/field-fresh-delivery-v4 --sequence 26090877 --count 3 --seconds 570 --block-bootstrap --diagnostics
```

各captureのconsoleを `tools/node_hil/field_evidence.py` で独立に再集計した。
時間は最初のG応答から最初のUSB観測まで。機器内のpacket到着時刻ではなく、
通常約4秒のpoll間隔と追加診断の遅延を含む。各件の投入時刻も別に保持する。
既存IDの回復では受信履歴が増えないこと、新規3件ではちょうど3件増えることを照合する。

最終連続送信は3/3件合格だが、最大約271秒/件で、終了時Relayに4件の預かりが残った。
単一試行の成功から遅延・長期容量・安定性を保証しない。送信元未確定の最新messageは残っていない。
初回setup試験器の停止中台帳の比較不備、Relayまで遮断した中断試行、期限超過も全て残す。

## 差分レビューと未完了

認可前検証、保存してから署名・使用、重複・失効・世代後退、queue/packet上限、
再起動・破損時の停止、秘密情報、旧形式移行、実TX前の現行権限をレビューした。
レビューで見つけた新品の仮番号問題は実USB consumerをnativeで再現して修正した。
既存3台は設定済みであり、工場出荷状態の4台目を物理登録した証拠ではない。

残る実装は別実物へのRoot引継ぎ、別network/役割への移設、アドレス履歴の再使用、
実際のbattery sleep/wake統合。通信遅延と連続運転中の残留custodyも継続課題。
4台以上の代替Relay実機、制御した書込み途中電源断、反復・72時間運転・現場試験は未実施。
OTAは要求外。業務ロジック、特定ホスト、センサー周期、電池寿命は利用側の条件として扱う。
