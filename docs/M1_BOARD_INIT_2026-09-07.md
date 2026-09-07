# M1：実機の初期化確認 — 2026-09-07

## 目的と確認結果

2台間の通信試験へ進む前に、実機の元の内容を保存し、送信無効の状態で無線チップが
繰り返し初期化できることを確認する。

Board Aでは修正前のfirmwareが初期化1回目に失敗した。SPI（チップ間通信）の
不要なバス占有処理を修正し、同じ個体で100回完了を確認した。
Board Bも元の8MBを検証付きで保存し、同じ修正sourceの別node設定で100回完了を
確認した。これは2台それぞれの初期化処理の観測であり、2台間RF通信やM1全体の
受入完了ではない。

## 個体と保存先

| 項目 | Board A | Board B |
|---|---|---|
| 構成 | ユーザー確認：XIAO ESP32-S3＋Wio-SX1262、アンテナあり | 同構成の2台目、アンテナ接続依頼に対し接続完了の返信 |
| 使用アンテナ（同日追加確認） | 公式キットページ画像のAntenna 2、SKU 113070002 / 2dBi | ユーザーが同じ使用構成として指定。個体ごとの実物写真は未保存 |
| アプリUSB | COM4 / 303A:4001 / E072A1F7FF0C | COM6 / 303A:4001 / E072A1D83E74 |
| ROM USB | COM3 / 303A:1001 / E0:72:A1:F7:FF:0C | COM5 / 303A:1001 / E0:72:A1:D8:3E:74 |
| MCU実測 | ESP32-S3 QFN56 rev v0.2、PSRAM 8MB、40MHz | ESP32-S3 QFN56 rev v0.2、PSRAM 8MB、40MHz |
| factory MAC実測 | e0:72:a1:f7:ff:0c | e0:72:a1:d8:3e:74 |
| Flash実測 | 8MB、manufacturer c8、device 4017 | 8MB、manufacturer c8、device 4017 |
| node / peer | 1 / 2 | 2 / 1 |
| 基板・配線revision、写真 | 未記録 | 未記録 |
| GPIO38極性とRF profile | 未確認、TX無効・frequency 0 | 未確認、TX無効・frequency 0 |

保存先は `C:/dev/job/iot/Ninlil/.verify-m1-evidence/`。
raw backupはGitへ入れず、内容をログやAI入力へ展開していない。
`board-a-artifacts.json`、`board-b-artifacts.json`にファイル名・サイズ・SHA-256を保存した。
2台目接続直後のWindows列挙はBoard Bのみだった。その後の2台同時接続は
下記17:30 JSTの別観測で確認した。

## Board A：backup、書き込み、起動

1. 手動BOOT/RESET後にCOM3へ再列挙。USB serial、MCU、factory MAC、Flash IDを実測。
2. Secure BootとFlash暗号化は無効。通常のesptool保護を解除せずに実行。
3. 高速stubによる全体読み取りは途中停止。32KiB/4KiB分割でも停止し、失敗ログと
   partialを保持した。ROM方式で全8,388,608 bytesを読み取り、実機の全領域MD5と
   保存データを照合し、保存ファイルのSHA-256も再計算した。
4. backup検証後に全Flashをeraseし、全領域がFFであることを実機MD5で照合。
5. bootloader、partition、appをそれぞれ0x0、0x8000、0x10000へ書き込み。
   `dio / 8MB / 80m`を使用し、書き込み時の照合と追加の`verify_flash`が両方成功。
6. 最初のRTS resetはdownload modeに残った。公式のwatchdog resetでUSB再列挙し、
   その後のRTS resetで完全なfresh-bootログを取得した。途中切断した取得試行は
   `UNKNOWN`として保持し、合格ログの代わりにしていない。
7. 修正前imageの初期化失敗を保存。修正版も個体・backup・image hashを再照合し、
   eraseと検証付き書き込みを実行。fresh bootから40秒の取得を完了した。

[esptool公式reset手順](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/advanced-options.html)
にUSB接続時のwatchdog resetが記載されている。最初のwatchdog resetはホストから
意図的に実行した操作であり、firmware中の予期しないwatchdog発生ではない。

## 不具合と修正の根拠

修正前source：`b7b0e90ac120a56082363fd6510682c65cc7ca29`。
実機ログは `spi_device_acquire_bus ... acquire finite time not supported now`、
続いて `SX1262 initialization failed cycle=1 rc=-2`。

[ESP-IDF v6.0.2のSPI driver](https://github.com/espressif/esp-idf/blob/v6.0.2/components/esp_driver_spi/src/gpspi/spi_master.c)
は明示的なバス占有に有限waitを受け付けない。SDK内の実ファイルも確認し、SHA-256
`c57abbe89f9d59730c6dd823eb7c45058118856b7a17d2f6d5a8d71c4232f030`を保存した。
SDK・依存revisionの変更はない。

修正source：`8e8a6ea13efc8932c0a1508e4cc800b3038e1108`。
radioのowner taskが専用SPI hostとdeviceを所有し、別deviceも非同期queueも使わない
ため、不要な明示的acquire/releaseを除いた。command/data間のNSS維持とBUSY待機の
既存期限は維持した。既に他の所有者が初期化したhostは従来どおり初期化に失敗する。

回帰検査では新しいfakeが旧実装を失敗させ、修正版が通ることを確認した。
command/dataそれぞれの失敗、NSSの解除、後続読み取りの回復も検査した。
これはSPIハードウェア自体の停止時限を新設する変更ではない。
既存のSDK polling完了待ちはSDK既定のままで、停止故障時の上限は別途検証が必要。

## ローカル検査とビルド

- Windows 11 Home 10.0.26200、Python 3.12.10、esptool 5.3.0、pyserial 3.5。
- host検査：Docker Ubuntu 24.04、GCC 13.3、Clang 18.1.3。
- target build：ESP-IDF v6.0.2、Xtensa GCC 15.2.0
  (`esp-15.2.0_20251204`)、Python 3.12.3、CMake 4.0.3、Ninja 1.11.1。
- SDK imageとSemtech revisionは[preflight記録](M1_PREFLIGHT_2026-09-07.md)と同一。
- 修正commitをclean cloneしてtarget build。設定はnode 1 / peer 2、100 cycles、
  frequency 0、TX無効、USB Serial/JTAG console。
- 以下はすべてローカル実行。hosted CIは起動していない。

```sh
cmake --build /tmp/ninlil-m1-fix-focus --target test_sx1262_hal
ctest --test-dir /tmp/ninlil-m1-fix-focus -R m1_sx1262_hal --output-on-failure
NINLIL_BUILD_ROOT=/tmp/ninlil-m1-fix-full NINLIL_JOBS=4 bash scripts/ci.sh
idf.py -C embedded/esp32s3 -B /tmp/init-a-fix-build \
  -D SDKCONFIG=/tmp/init-a-fix-sdkconfig \
  -D 'SDKCONFIG_DEFAULTS=/project/embedded/esp32s3/sdkconfig.defaults;/evidence/init-a.defaults' \
  set-target esp32s3 build
```

全体検査：15 CTestsがGCC、Clang、GCC ASan/UBSan、Clang ASan/UBSanで各PASS。
再現性、format、ESP stub syntax、static analysis、manifest fuzz 10,000回、
source規模、shell syntaxもPASS。target build/linkとimage-info検証もPASS。

Windowsでは独立venvのPythonで`backup-board-a-rom.py`、`flash-init-a-fix.py`、
`capture-init-a-fixed.py`を実行した。各scriptの実体とhashは上記保存先にある。
flash scriptは全体検査PASS、個体識別、backup/image照合を必須とし、`force`は使わない。

## Board Aの100回観測

17:01 JST、修正版のfresh bootで以下を観測した。

```text
rst:0x15 (USB_UART_CHIP_RESET),boot:0x8 (SPI_FAST_FLASH_BOOT)
App version: 8e8a6ea
ELF file SHA256: abe6b1994...
profile region='' freq=0 tx=disabled ... gate-confirmed=no
NINLIL_HIL_INIT result=PASS cycles=100
stack phase=radio-init minimum-free=15276/16384 bytes (93%)
operational RF disabled: frequency is unset
main_task: Returned from app_main()
```

生ログの完了markerは1件、fresh bootは1件。初期化失敗・予期しないresetはない。
ただしSDKの `GPIO isr service already installed` が99件ある。2回目以降の初期化で
既存の共有GPIO割り込みserviceに再登録を試み、radio実装が`ESP_ERR_INVALID_STATE`を
明示的に許容する経路によるもの。その他のerror-levelログは0件。これら99件を削除せず
保存し、未説明の無線故障としても、error-freeログとしても扱わない。

## Board Bの100回観測

Board BはCOM6から手動BOOT/RESETでCOM5へ再列挙。同じchip/Flash型番でもMACが異なる
ことを確認し、Secure BootとFlash暗号化が無効であることも実測した。
ROMで全8MBをbackupし、実機MD5・ファイルサイズ・保存後SHA-256の照合が成功した。
その後、個体を再照合し、全eraseのFF照合、書き込み時照合、追加`verify_flash`が成功。

sourceはBoard Aと同じ`8e8a6ea13efc8932c0a1508e4cc800b3038e1108`のclean clone。
node 2 / peer 1に変更した`init-b.defaults`を用い、同じbuild commandの
`init-a-fix`を`init-b`、defaults pathを`/evidence/init-b.defaults`へ変更してbuildした。
100 cycles、frequency 0、TX無効、USB consoleは同じ。SDK・Semtech revisionも同じ。

書き込み後は次の公式コマンドで意図的にwatchdog resetを実行し、USB再列挙後に
別のRTS resetでfresh-boot captureを開始した。

```text
python -m esptool --port COM5 --port-filter serial=E0:72:A1:D8:3E:74 --connect-attempts 3 --before no-reset --after watchdog-reset read-mac
python .verify-m1-evidence/capture-init-b-after-reset.py
python .verify-m1-evidence/capture-init-b-clean.py
```

最初のcaptureには、リセット前の出力64 bytesがROM markerの前に混ざった。
このログも保持し、100回markerだけで完全なfresh-boot証拠とせず、取得をやり直した。
17:16 JSTの再取得はROM markerから始まり、40秒のcapture終了まで欠落を認めなかった。

```text
rst:0x15 (USB_UART_CHIP_RESET),boot:0x8 (SPI_FAST_FLASH_BOOT)
App version: 8e8a6ea
ELF file SHA256: dce4f915b...
NINLIL_HIL_INIT result=PASS cycles=100
stack phase=radio-init minimum-free=15420/16384 bytes (94%)
operational RF disabled: frequency is unset
main_task: Returned from app_main()
```

Board Aと同じ説明可能なGPIO登録済みログ99件、その他のerror-levelログ0件。
最終2ログに対し、ROM/reset marker、source version、ELF hash prefix、送信無効設定、
100回markerと終了markerがそれぞれ1件、stack余裕25%以上を照合した。
結果は`init-log-validation.json`に保存。初期化処理の完了であり、無線応答や相手側の
保存確認を証明したものではない。

## 主要artifactのSHA-256

| Artifact | SHA-256 |
|---|---|
| Board A元の8MB backup | `09d9a0d06fc4511146e1c8a74caee5b93a2f35857d4f9c01665a93b0634be2de` |
| init-a-fix app（211840 bytes） | `408d7c5b50a6f7aa6eeab8c79355c4d671ad5935941581a54a08d7012fb4e408` |
| init-a-fix bootloader | `42f652173a77d7d6551a3f0e88d1e9ce53440cf587308686d18afa0ad1cc2478` |
| partition table | `59514879595ffbb48032cd5ea36f6d7b27479f00b35304ec1a934fef64565a47` |
| init-a-fix sdkconfig | `a4d750d873045905857954a86c551a45e0f99ce4b5eecb5604ebeddfc4fc3618` |
| init-a-fix build log | `b7e051894867825b83cef2d5b84be3d534d4f75f7bf0df4f02eea4ba34a820c1` |
| full local check log | `d2ee6af1df7d35c4f81080db23222a82a2c3eb4732e27ac38d3afd4521b9b63f` |
| 修正前失敗boot log | `ed39b0a92a63d61c12b5cee6f46238adf53852bb164b4030208a223cf14c48eb` |
| 修正版flash log | `0858a0cecafc99fd68e69fd07afe06da900a196aa876965a2e6e47f7c6f2b1ca` |
| 修正版100回boot log（11430 bytes） | `a1d9b3b0dfa175a9ab9977f12da812de5a0e1828a9e5f11600b0f805c27ab908` |
| Board B元の8MB backup | `1911ac3d500bbcef6d1c951d1f58a25757ca787f7023f31b7ee97ab93420819a` |
| init-b app（211840 bytes） | `52770f427ee472d3dfffd197480b12197d2e4f505afeb96f180772410f7f53e2` |
| init-b bootloader | `2e3df4448e154fed6608240c95ae42daffa0692eebf199b2f1512792b7e97ee7` |
| init-b sdkconfig | `ec44e00826bf3af1e42a4d7c72e7520f2a2e195e62538a0a1b9e250aa8ba652a` |
| init-b build log | `f843a6ef7713ed2d7e8a4e05a56e31bf5ed3172268e2983d7c0236d4de41bc7e` |
| Board B flash log | `be88c057eef5814a9948f7e6d2c3523bee9abbb87e405d6cd4e6600c0f0cad48` |
| Board B取得やり直し前のboot log | `45c00fe97c83de7ca4dffb4a262d6c7950457aa710dfc082fc4f4d984f983988` |
| Board B最終100回boot log（11429 bytes） | `5765ca39b59a1a9907fbf005f89f2e6fd1b889ac571db0390a494a5ab81afb32` |

bootloaderはesptoolがflash設定をheaderへ反映するため、元binary hashと実書き込みの
同一性は設定込みの`verify_flash`で確認した。appの完全なELF SHA-256は
`abe6b19942e8dc5083fae0975ccbfd8a06aa764d8e77a0afd36f09d62249b905`。
Board B appの完全なELF SHA-256は
`dce4f915b8016e303ddde6ec4f41898a051d65a7b80bb0f85c8f7e277fc532ce`。

## 2台同時USB接続（17:30 JST）

ユーザーの接続済みとの連絡を受けて再列挙した。Board AはCOM3・USB location 1-2、
Board BはCOM5・USB location 1-1で、両方のVID/PIDとserialが記録済み個体に一致した。
Windows PnP statusは両個体ともOK。両ポートを115200 baudで同時にopenできた。
DTR/RTSを無効として開き、データ送信や意図的なresetはせず、確認後に両方closeした。
これはUSB列挙とポート利用可能性の確認であり、RF送受信や配送回復の証拠ではない。

保存先：`.verify-m1-evidence/dual-usb-confirmation-2026-09-07.json`。
SHA-256：`cd885fe4c323b151612b1db73d9689e08d3f7ce47d695639781abbe805832287`。

## 残る条件

### 使用アンテナの特定（同日追記）

ユーザーが[日本語版キットページ](https://wiki.seeedstudio.com/ja/wio_sx1262_with_xiao_esp32s3_kit/)
の構成と、ページ内画像の「Antenna 2」を使用していると明示した。
[公式比較画像](https://files.seeedstudio.com/wiki/XIAO_ESP32S3_for_Meshtastic_LoRa/37.png)
をブラウザで目視し、次の掲載値を照合した。

- アンテナ：SKU `113070002`、外付け折り畳み型、利得2dBi、195×12×12mm。
- 画像に示された変換ケーブル：SKU `321990397`、SMA–I-PEX、120mm。
  これは掲載セットの構成であり、手元のケーブルの印字まで確認したものではない。
- [アンテナ単体の公式商品ページ](https://jp.seeedstudio.com/External-Antenna-868-915MHZ-2dBi-SMA-L195mm-Foldable-p-5863.html)
  もSKU・利得を確認できる。商品名と説明には868–915MHzの記載がある。

キット向け推奨と、日本向け認証のアンテナ組合せ・920MHz帯での適合確認は別である。
下記の公開証明書だけでは指定3種とSKU 113070002の対応を確定できていない。
総務省の登録詳細ページも確認を試みたが、ブラウザのサイト制限で閲覧できなかった。
この未確認を「使用不可」とも「認証条件一致」とも断定せず、送信有効化の前に残す。
アンテナ特定時点ではBoard BのCOM5のみを認識していたが、17:30 JSTに上記の
2台同時接続を確認した。

### 日本向けRF試験の条件

試験国はユーザーが日本と確認した。Seeedが公開する
[Wio-SX1262の日本向け証明書](https://files.seeedstudio.com/Seeed_Certificate/documents_certificate/113991436-TELEC.pdf)
には`R 201-250230`、125kHz時920.6–928.0MHz、定格最大10mW、指定アンテナ3種の
記載がある。これは手元の個体・アンテナ・自作firmwareの条件一致を確認した証拠ではない。
周波数だけ選んでTXを有効にせず、実物表示、アンテナ、送信時間・チャネル利用条件を
含むRF profileを別途照合する。現時点ではfrequency 0・TX無効を維持する。

両個体の写真と基板／配線revision、GPIO38極性、
地域と周波数・出力などのRF profileを確認してから2台通信へ進む。
RF、永続配送、故障注入、USB再接続後の配送回復、制御された電源断は未実施。
ビルドはLinux、実機接続はWindowsであり、正本手順のLinux実機hostによる全campaignと
同一視しない。Issue #7全体、field readiness、production securityはいずれも未受入。
