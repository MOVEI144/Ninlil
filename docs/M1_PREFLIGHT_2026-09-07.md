# M1 実機接続・ビルド準備記録 — 2026-09-07

この文書は16:35 JST時点の準備記録。以後のbackup・実機起動・不具合修正と
Board A/B各100回の初期化結果は[実機初期化記録](M1_BOARD_INIT_2026-09-07.md)を参照。

## 目的と現在地

Issue #7の固定2台試験に向け、最初の1台を識別し、送信無効の初期化用imageを準備する。
16:35 JST時点でUSB接続とローカルtarget buildを確認。実機のチップ識別は未成功で、
backup、erase、書き込み、無線通信、初期化100回の実行はまだ行っていない。

## 実機について確認できた情報

| 項目 | 観測／確認 |
|---|---|
| ユーザー確認 | XIAO ESP32-S3＋Wio-SX1262、アンテナ取り付け済み |
| Windows port | COM4、1台 |
| USB VID/PID | 303A:4001 |
| USB serial descriptor | E072A1F7FF0C |
| USB parent | `USB\VID_303A&PID_4001\E072A1F7FF0C` |
| 仮の役割 | Board A候補（node 1 / peer 2） |
| MCU型番、factory MAC、実Flash容量 | 未確認。USB descriptorから推測して確定しない |
| 基板／配線revision、GPIO38極性 | 未確認 |
| Board B | 未接続／未識別 |

`esptool 5.3.0 --port COM4 --connect-attempts 3 --after no-reset flash-id`は
`No serial data received`でexit 2。Flashには書き込んでいない。
次にBOOTを保持しながらRESETを押して離し、その後BOOTを離す手動bootloader切替を
依頼した。USBが再列挙される場合は、port番号を再確認してから読み取りを再開する。
[Seeedの手動bootloader手順](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/#reset)
と[esptool reset mode](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/advanced-options.html)を参照。

## ビルド元と実行環境

- 正本repositoryのclean commit：`b7b0e90ac120a56082363fd6510682c65cc7ca29`。
- ローカルDocker内へ同commitをcloneし、sourceのclean状態を確認してから依存を取得した。
- ESP-IDF v6.0.2の公式amd64 image digest：
  `sha256:e3d941cb983e028aad1e2f5ecb2837254e467f2b71f3e0af67e7337bd27ae177`。
- Semtech revision：`a10c5dfdf89788c6ac805e9fe98889de44175aa2`。既存fetch scriptと
  provenance checkerを実行した。採用revision変更なし。
- esptool：SDK内・Windowsの独立venvとも5.3.0。Windows側Python 3.12。
- [公式Docker build手順](https://docs.espressif.com/projects/esp-idf/en/v6.0.2/esp32s3/api-guides/tools/idf-docker-image.html)を使用。
- 既存のlocal host検査は[W01/W02記録](W01_W02_LOCAL_EVIDENCE_2026-09-07.md)を参照。
  それ以後、runtime・firmware・protocol sourceの変更なし。hosted CIは実行していない。

Default設定は既存の `scripts/build_esp32s3.sh` でbuild/link成功。
Board A初期化variantは同じsourceに次のlocal defaultsを加えた。

```ini
CONFIG_NINLIL_NODE_ID=1
CONFIG_NINLIL_PEER_ID=2
CONFIG_NINLIL_RADIO_INIT_CYCLES=100
CONFIG_NINLIL_RF_FREQUENCY_HZ=0
# CONFIG_NINLIL_RF_TX_ENABLE is not set
# CONFIG_ESP_CONSOLE_UART_DEFAULT is not set
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
```

```sh
idf.py -C embedded/esp32s3 -B /tmp/init-a-build \
  -D SDKCONFIG=/tmp/init-a-sdkconfig \
  -D 'SDKCONFIG_DEFAULTS=/project/embedded/esp32s3/sdkconfig.defaults;/evidence/init-a.defaults' \
  set-target esp32s3 build
```

生成sdkconfigで100回、frequency 0、TX無効、USB Serial/JTAG consoleを照合した。
後者は[ESP-IDF公式console設定](https://docs.espressif.com/projects/esp-idf/en/v6.0.2/esp32s3/api-guides/usb-serial-jtag-console.html)に従う。
`esptool image-info`でESP32-S3 image、IDF v6.0.2、App version b7b0e90、
checksumとvalidation hashがvalidであることを確認した。これは実機起動の証拠ではない。

## 保存した証拠

ローカル保存先：`C:/dev/job/iot/Ninlil/.verify-m1-evidence/`。
各build folderに全sdkconfig、bootloader/app/partition binary、flasher_args.json、
SHA256SUMS、Semtech provenanceを保存。backupデータはまだ存在しない。

| Artifact | SHA-256 |
|---|---|
| default-build.log | `68085a176ead927d25a3acd7ef9bedb272c04bbd05106f8c2dcdd53483a7de37` |
| default-build/ninlil_m1.bin | `2b12f4541e29582080decd63fa3c5b256325f5fce1b7dc7864545e2e96731fb8` |
| init-a-build.log | `31ce8a23fb4d0a684e19aec270af696168d91cd811a59b92579a4d66f5c17a8c` |
| init-a-build/ninlil_m1.bin | `dd9463951895e1e49c5b1e1025d83048c2632bf67cc53c592475a185a5f09739` |
| init-a-build/bootloader.bin | `8e5aaa37460a9d0b21628260ad7792468d73b45ad501602a2b335d2063575711` |
| init-a-build/partition-table.bin | `59514879595ffbb48032cd5ea36f6d7b27479f00b35304ec1a934fef64565a47` |
| init-a-build/sdkconfig | `a4d750d873045905857954a86c551a45e0f99ce4b5eecb5604ebeddfc4fc3618` |
| init-a.defaults | `02d368dc8eb16858704b74457d5b0654fca3a973522aa3e787201026edc380cb` |
| board-a-identify.log（未接続の結果） | `54d3c7e1f69f1ddd60d434ea0d75733f5f30705d26b41d6769b9ce1e40e72935` |

## 次の順番

1. 手動bootloader切替後、USBの再列挙と実チップ・Flash容量を確認する。
2. 既存Flashをbackupし、サイズとhashを保存する。
3. 対象個体とfirmware hashを再照合してから、送信無効の初期化variantを書き込む。
4. 起動から100回完了までのログを保存し、実測結果を追記する。
5. Board Bを識別する。RF profile・GPIO38極性・配線など、M1の残る条件を確認して
   初めて送受信試験へ進む。

この記録では[M1物理受入](M1_HIL_ACCEPTANCE.md)のどのゲートも完了にしていない。
