# M1：2台間の診断通信 — 2026-09-07

結果：実機2台で各方向1,000件の往復通信がすべて成功した。欠番・重複・timeout・
途中の再起動は0件。試験後は2台とも送信無効に戻した。
相手への永続保存、再起動後の配送回復、電源断は次の実機段階であり、M1全体は未受入。

## 目的

ユーザーの「通信のテストまで進めて」という依頼に基づき、識別済みの2台で
PINGと応答PONGを照合する。少数の往復を観察して問題を切り分けた後、各方向1,000件を
測定する。相手が電波を受け取って応答したことと、相手での永続保存は区別する。

この文書は実施条件と証拠の記録。診断通信と永続配送の受入は区別する。

## 使用する構成と判断

- Board A：COM3、`E0:72:A1:F7:FF:0C`、node 1 / peer 2。
- Board B：COM5、`E0:72:A1:D8:3E:74`、node 2 / peer 1。
- ユーザーは両方ともSeeed XIAO ESP32-S3＋Wio-SX1262キットで、
  日本で使用し、両モジュールに`R 201-250230`表示があると確認した。
- アンテナはユーザー指定の公式比較画像「Antenna 2」、SKU 113070002・2dBi。
  2台のUSB同時接続と元のFlashのbackupは[初期化記録](M1_BOARD_INIT_2026-09-07.md)を参照。
- 実装・設定の照合担当はCodex。使用者の構成申告とメーカー資料を根拠として
  下記の限定的な実験設定を準備する。電波試験結果を認証適合や現場運用可の判断に代用しない。

### GPIO38の確認根拠

Seeedの[このキット用の公式コードパッケージ](https://files.seeedstudio.com/wiki/XIAO_ESP32S3_for_Meshtastic_LoRa/Wio_SX1262_XIAO_ESP32S3_code_package_20241025.zip)
はGPIO38をRadioLibの受信側RFスイッチpinに指定している。
[RadioLib 6.6.0の対応する定義](https://github.com/jgromes/RadioLib/blob/6.6.0/src/Module.cpp)
を照合すると、RXでHigh、TXでLowとなる。この既知のB2B構成についてRX active highを
採用する。RadioLibを依存に追加したり、そのコードをNinlilへコピーしたりはしない。
ZIPのSHA-256は`0b4b1131d18e986178a94f567dcf4a77b9a70f089f1fc048fb66e91fd1d5f184`。

### 初回のRF設定

| 項目 | 値 |
|---|---|
| プロファイル名 | JP |
| 中心周波数 | 921.4 MHz、200 kHz単位の1チャネル |
| 出力の設定値 | −9 dBm（約0.126 mW） |
| LoRa設定 | SF7 / BW125 kHz / CR4/5 / preamble 8 / CRCあり |
| GPIO38 | RX High、TX Low |
| DIO2 / TCXO | 自動RF切替有効 / DIO3 1.8 V、既存Semtech設定 |
| 送信前確認 | 250 kHzの受信帯域で5ms以上、RSSIが−80dBm以上なら送信拒否 |
| 1回の電波時間 | 計算値400ms以内に制限 |
| 送信後の休止 | TX完了確認から50ms以上。結果不明時は最大TX待機時間＋50msを予約 |
| PING間隔・応答期限 | 応答／timeout後100ms、応答期限1秒 |
| 開始待ち | 起動から診断開始まで3秒 |
| 件数 | 各方向1,000。少数往復の観測は1,000件の受入を代替しない |

根拠は[ARIBの公開STD-T108 v1.3英訳・第2編3.4節](https://www.arib.or.jp/english/html/overview/doc/5-STD-T108v1_3-E1.pdf)
と[現行版の改定履歴](https://www.arib.or.jp/english/std_tr/telecommunications/desc/std-t108.html)、
[メーカー公開の日本向け証明書](https://files.seeedstudio.com/Seeed_Certificate/documents_certificate/113991436-TELEC.pdf)。
5ms以上のチャネル確認を用いる920.6–922.2MHzの範囲に限定し、125kHz送信帯域より広い
帯域で混雑を確認する。測定器での感度校正・隣接チャネル・スプリアス測定は別の確認事項。

メーカーの公開証明書はアンテナ3種・最大利得8dBiを記載しているが、SKU 113070002と
認証時のアンテナ型式の対応表は確認できていない。キット向け推奨アンテナという情報と
正式な組合せ認証は同一視せず、この残件を記録する。

## 今回の実装

最終ファームウェアSource：`834c62f6b58257f3d116e27937c8803731623805`。
実装開始commitは`c7fb1bee069635787b6a088b6610831c2028ac4d`。

- JP送信設定は指定チャネル列、BW125kHz、最大10dBmに限定。実験値は−9dBm。
- 混雑、RSSI取得失敗、不正なradio status、時計停止の場合に送信しない。
- チャネル確認は最大100サンプル。失敗時に元の受信帯域へ戻す。
- 受信中の通知を送信完了と誤認しないよう、TX直前に通知をクリアする。
- TX待機上限を先に予約し、回復処理でも送信休止を消さない。
- 診断は指定したpeerだけに応答し、source、target、sequence、TX結果、RSSI/SNRを記録。
- 試験全体にも期限を設け、混雑が続いても無期限に再試行しない。

SPI polling APIの内部待機上限やRF測定器での検証など、既存の未検証事項まで
この変更で完了としない。radio送信処理の差分はI/O順序、失敗時の受信復帰、通知の所有、
休止の保持について確認する。

## ローカル検査と実機結果

チャネルの閾値境界、途中での混雑、RSSI失敗・範囲外、不正status、時計停止、時間超過、
休止時間の境界、回復後の休止保持をfake hardwareで検査。対象テストはPASS。

### 開発中に観測した失敗と修正

失敗した試行も同じartifact directoryに保存している。

| 試行 | Source | 観測 | 対応 |
|---|---|---|---|
| `jp-a-to-b-01` | c7fb1b | 最初の送信で4回I/O判定、PONGなし | 状態別の診断ログを追加 |
| `jp-cca-telemetry-a` | 7111b2d | RX mode=5 / command status=1で停止 | `13fe15a`で予約値1と明示的なcommand errorを区別 |
| `jp-a-to-b-02` | 13fe15a | RSSI=-128で停止、PONGなし | `834c62f`でドライバーの丸め後の下限に一致させた |

状態1の扱いは[Seeedが使用するRadioLib 6.6.0のSX126x実装](https://github.com/jgromes/RadioLib/blob/6.6.0/src/modules/SX126x/SX126x.cpp)
の明示的エラー判定と照合した。予約値1そのものをcommand成功や送信成功の証拠にはしていない。
RSSIは[固定したSemtech revisionの実装](https://github.com/Lora-net/sx126x_driver/blob/a10c5dfdf89788c6ac805e9fe98889de44175aa2/src/sx126x.c)
がraw 255を右shiftで−128dBmへ変換する。−128はI/O失敗のsentinelではない。
実エラー値0/3/4/5/7、不正なchip mode、RSSI取得失敗、−129/正のRSSI、
混雑・時計停止を拒否するテストを保持した。TX成功には別途TX_DONE割込みを必要とする。
各失敗試行の後は両MCUをdownload modeへ戻し、再試行を止めてから修正・再書込みした。

### ツールと再現コマンド

- Windows 11 Home 10.0.26200上の実機serial操作：Python 3.12.10 / esptool 5.3.0 / pyserial 3.5。
- ローカルLinux container：Ubuntu 24.04 / GCC 13.3 / Clang 18.1.3。
- Target build：ESP-IDF v6.0.2 / Xtensa GCC 15.2.0 (esp-15.2.0_20251204)、
  Python 3.12.3、CMake 4.0.3、Ninja 1.11.1。
- 公式IDF image：`espressif/idf@sha256:e3d941cb983e028aad1e2f5ecb2837254e467f2b71f3e0af67e7337bd27ae177`。
- Semtech：`a10c5dfdf89788c6ac805e9fe98889de44175aa2`（2.5.0）。依存変更なし。
- Build cacheは上記公式imageからのローカル生成物。各buildはGitの指定commitへcheckoutし、
  tracked sourceがcleanで、唯一のuntracked entryが検証済みSemtech directoryであることを検査する。
  Windows側の記録文書はbuild入力に含めない。Hosted CIは実行していない。

以下はrepository rootから実行。`<variant>`は後述の4種類。
container内の`/source`はrepositoryのreadonly bind、`/evidence`はartifact directory。

```sh
# Linux container: 初期実装の全体検査
NINLIL_BUILD_ROOT=/tmp/ninlil-jp-full ./scripts/ci.sh
# 最終Source: 各 gcc / clang / gcc-sanitize / clang-sanitize
cmake --build /tmp/ninlil-jp-full/<compiler> --parallel 2
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir /tmp/ninlil-jp-full/<compiler> --output-on-failure
./scripts/check_esp_syntax.sh
./scripts/static_analysis.sh gcc clang
# IDF container: 保存した構成へ切替え、検証済みsourceからbuild
source /opt/esp/idf/export.sh
bash /evidence/rebuild-jp-cached.sh 834c62f6b58257f3d116e27937c8803731623805 <variant> <variant>-r3
```

```powershell
.\.verify-m1-tools\Scripts\python.exe .verify-m1-evidence/flash-jp-r3.py <variant>
.\.verify-m1-tools\Scripts\python.exe .verify-m1-evidence/capture-jp-pair-r3.py a-to-b jp-a-to-b-03
.\.verify-m1-tools\Scripts\python.exe .verify-m1-evidence/capture-jp-pair-r3.py b-to-a jp-b-to-a-01
.\.verify-m1-tools\Scripts\python.exe .verify-m1-evidence/analyze-jp-pair.py <campaign>
```

書込みhelperはbackupのsize/hash、USB serial/VID/PID、MCU MAC、flash ID、source commit、
全artifact hashと役割・RF設定を照合する。`write_flash`と独立した`verify_flash`を実行し、
loaderに留める。capture helperが応答側のfresh bootとREADYを確認した後に送信側を起動する。
image header設定はDIO / 8MB / 80MHz、offsetは0x0 / 0x8000 / 0x10000。

`jp-full-local.log`は全体検査PASS（15 CTests×4構成、静的検査、形式、再現性、
10,000 manifest fuzz）。最終RSSI修正後の対象4構成・ESP syntax・静的検査は
`jp-rssi-fix-check-r2.log`の`NINLIL_JP_RSSI_FIX_PASS`で確認する。
最終Sourceの全15 CTests×4構成は`jp-r3-full-<compiler>.log`に記録する。
4構成とも`NINLIL_JP_R3_FULL_PASS`、失敗0件。形式チェックを最初に行った際の
混在改行による失敗も`jp-rssi-fix-check.log`へ保存し、clang-format適用後に再実行した。

### ファームウェアの識別

各`<variant>-r3` directoryにsource、完全なsdkconfig、追加設定、bin、SHA256SUMSを保存。
`<variant>-r3-build.log`、`<variant>-r3-flash.log`が対応するbuildと実機verifyの証拠。

| Variant | Application bin SHA-256 |
|---|---|
| diag-a-init | c711517d0f6b660da7c16be9e6b9ca9279df9d04944bd97c680ce8d90558d9cd |
| diag-b-resp | 34ecb815453270fa364122d5ffa35b43de62df9e1d6180c7a21529c1780e83a1 |
| diag-b-init | e34bdfb9d8c1b095317b1fed1d8a4c4c206d3166b7bc9a5f0e6d06448a1306aa |
| diag-a-resp | 97b6f3c831879ccedfd2bd8648842e452d3caf13dc97218be012cbed795dcb40 |

生ログを解析するhelperは両側のTX/RX件数とsequence集合、source/target、重複、
fresh bootの回数、reset理由、最終summary、stack残量、versionとELF hashを照合する。
結果JSONには生ログhashとfirmware hashを含める。
IDFのbootログにはELF hashの先頭9桁だけが出るため、完全なhashとの照合は
binのimage-info、保存hash、実機verify_flashを合わせて行う。
最初の解析helperは12桁以上を要求して停止したが、実際のIDF出力形式を確認して修正した。
この解析側の停止はRF診断失敗を意味しない。初回解析の出力も保存している。

### 受入範囲

ここで検証するのは`M1_HIL_ACCEPTANCE.md`第3段階の診断通信の観測条件。
Linux containerでbuildしWindowsで実機を操作したため、正本にあるLinux実機hostによる
全campaign完了とは同一視しない。距離・設置場所の詳細・配線revisionは今回未計測／未採録、
実物の表示とアンテナ構成はユーザー申告による。通信距離や屋外の安定性はこの結果から推定しない。

CCAの今回の記録値がすべて−128dBmでも、チャネル感度の校正や実妨害波による拒否動作の
実機受入を済ませたことにはならない。閾値境界とエラー処理のfake-hardware検査、
RF測定器による確認は区別する。

次の実機段階は各方向100件の永続配送（相手側の保存commitと送信側の`REMOTE_STORED` /
`SATISFIED`を照合）、再起動・USB再接続後の配送回復、故障注入、制御された電源断。
Issue #7全体、正式なアンテナ組合せ確認、現場運用・本番securityの受入は残る。
GitHub Issue/Projectへの投稿・状態変更、push、hosted CIは行っていない。

保存先：`C:/dev/job/iot/Ninlil/.verify-m1-evidence/`。

### 実機結果

| 方向・campaign | 測定時間（JST） | PING / 一致したPONG | timeout / 重複 | 結果 |
|---|---|---|---|---|
| A→B `jp-a-to-b-03` | 18:18:46–18:22:42 | 1,000 / 1,000 | 0 / 0 | PASS |
| B→A `jp-b-to-a-01` | 18:24:24–18:28:19 | 1,000 / 1,000 | 0 / 0 | PASS |

各方向とも、送信側の1–1,000全sequenceのTX_DONE、応答側の同じ1,000件の受信・PONG送信、
送信側でのPONG受信を照合した。各boardにfresh bootが1回あり、途中のreset・panic・
未説明のerror-levelログはない。各campaignの最終markerは
`PASS sent=1000 received=1000 timeout=0 minimum=995`。送信側stack最小空きは
14,652 / 16,384 bytes（89%）。今回の診断通信の観測条件は両方向とも満たした。

全受信のRSSIは−38〜−31dBm、SNRは12〜14dB。TX完了ログから同じMCUのPONGログまで
各件60ms。これはログ時刻の差であり、airtimeを含むend-to-end latencyの測定ではない。

生ログは`<campaign>-a.log` / `-b.log`、取得metadataは`<campaign>-capture.json`、
両側照合結果は`<campaign>-analysis.json`。主要hashと集計は
[`M1_RF_RESULTS_2026-09-07.json`](M1_RF_RESULTS_2026-09-07.json)にも収録した。

### 終了時の2台

18:29 JST、検証済みの送信無効・frequency 0の初期化用imageへ戻した。
実機の書込みverifyとfresh bootを再確認し、両方100回初期化PASS後にapp_mainが終了した。
各99件の既知GPIO共有ISRメッセージ以外にerror-levelログはない。
RF診断用imageは保存してあり、現在の書込み済みimageとは区別する。
終了時のsourceは`8e8a6ea13efc8932c0a1508e4cc800b3038e1108`、app hashは
[初期化記録](M1_BOARD_INIT_2026-09-07.md)のinit-a-fix / init-bと同一。
全flash消去はせずbootloader・partition table・appだけを書き戻した。
再度電源を入れても診断の自動送信は開始しない。

操作は`restore-jp-idle-a.py` / `restore-jp-idle-b.py`、取得は`capture-jp-idle.py a|b`。
ログは`jp-idle-<a|b>-flash.log` / `-boot.log`、検証は`-result.json`。
主要artifactの一覧・size・hashは`jp-rf-evidence-manifest.json`に保存した。
同manifestのSHA-256は`3862dc79a90f898815861475b1dc83c1b630a06e443adcb3c3fd6be78ee6aa73`。

実機RF診断：両方向PASS。永続配送・電源断・全体M1受入：未実施／未受入。
