# M1：相手への保存と再起動後の配送確認 — 2026-09-07

結果：両方向とも100件の相手側保存と送信側の完了記録を実際のFlashから確認した。
両方の再起動後も100件のIDとjournalが一致し、RF再送は0件。
応答待ちで送信側をresetした試験も、同じメッセージIDで再開して100件の保存まで完了した。
終了時は2台とも送信無効に戻した。M1全体の受入は残る。

## 目的と範囲

診断PING/PONGの次に、各方向100件について「相手のFlashへ保存されたこと」と
「送信側が保存確認を受け取り、完了を永続化したこと」を確認する。
試験用consumerが受信データを検証して受け取りを記録するところまで扱い、
製品アプリケーションの処理完了や物理機器への反映とは区別する。

再起動確認は、完了済み100件を読み戻す場合と、応答がない未完了1件を保持して
送信側を再起動する場合に分ける。USB経由のMCU resetであり、給電断やFlash書込み途中の
電源断を再現したものではない。

## 構成と変更点

個体・アンテナ・USB identity・日本での限定したRF設定は
[先行の通信試験](M1_RF_CAMPAIGN_2026-09-07.md)と同じ。
Board AはCOM3 / `E0:72:A1:F7:FF:0C` / node 1、
Board BはCOM5 / `E0:72:A1:D8:3E:74` / node 2。
周波数921.4 MHz、出力−9dBm、SF7/BW125/CR4/5、5ms以上のチャネル確認、送信休止50ms。
正式なアンテナ型式対応、RF感度校正、現場距離での性能など、先行記録の残件は引き継ぐ。

Source：`80ab9acd444b577fc687bf6dbcddbf7e13ec2fa3`。

- HIL payloadを12 bytesにし、試験ID・送信元・宛先・sequenceを含めた。
  試験用idempotency keyは`HIL2`＋同payload。再起動時は同じ試験IDを維持する。
  wire/永続journalのフォーマットは変更していない。旧HILの8-byte payloadとは区別する。
- 入力を検証する小さな試験用moduleを追加し、誤った試験ID、方向、sequence、
  所有形態・必要な証拠段階を拒否する。peer policyは設定した相手だけに限定した。
- `HIL_SUBMIT`（ローカルcommit後）、`HIL_STORED`（Runtimeが保存した受信offer）、
  `HIL_CONSUMED`（試験consumerの受取り記録）、`HIL_SATISFIED`（相手の保存確認を
  受けて永続化した結果）にメッセージIDを記録する。
- 必要な証拠を明示的に`REMOTE_STORED`へ設定し、完了判定ではoutcomeだけでなく
  required/latest evidenceも確認する。送信側の試験期限は10分。
- Flash読出し用の`m1_flash_inspect`は実際のFlash parserを使ってCRC、commit marker、
  sequenceを検査する。入力は128 KiBの読取専用imageで、write/erase callbackは常に拒否する。

変更対象は試験用payload・観測と検証手順に限定した。
永続配送Core、RFの通信条件、Flash永続フォーマット、依存ライブラリは変更していない。

## ローカル検査

`scripts/ci.sh`：16 CTests × GCC / Clang / GCC ASan+UBSan / Clang ASan+UBSanの
4構成がすべてPASS。再現性、ESP strict syntax、静的解析、形式、10,000件のmanifest fuzz、
LOC budgetもPASS。Hosted CIは実行していない。

追加テストは実際のFlash-file Runtimeで100件を保存し、最初の未完了データを保持した
再open、最初の応答の欠落、全100件の再open後のidempotency照合を行う。
受信側の100件も再open後に同一IDでqueryでき、再offerされないことを確認する。
これはhost上の検査であり、下記の実機のresetや読出しとは別の証拠。

ツールは前回と同じ：Windows 11 Home 10.0.26200、Python 3.12.10 / esptool 5.3.0 /
pyserial 3.5、Ubuntu 24.04 container、GCC 13.3 / Clang 18.1.3、
ESP-IDF v6.0.2 / Xtensa GCC 15.2.0、Semtech revision
`a10c5dfdf89788c6ac805e9fe98889de44175aa2`。
公式IDF imageとcacheの来歴は先行記録を参照。Windows実機hostをLinux実機hostの
正本campaignと同一視しない。

## 実機の試験手順と証拠

試験ID：A→B `2026090701`、B→A `2026090702`、未完了からの回復 `2026090703`。
それぞれ1–100のsequence、固定したA/Bの役割、個別の設定・image hashを持つ。

1. 元の8MB backupと個体の照合を保持したまま、既知のpartition tableを実物と照合する。
   対象は`ninlil_journal`、offset `0x200000`、size `0x20000`のみ。
2. 初回は両領域が全`0xFF`であることをROM経由の読出しとdevice MD5で確認した。
   後続の新試験前は前の内容をdiskへ保存・fsyncし、size/SHA-256/device MD5が一致してから
   対象journalだけを消去する。無関係な領域や元のbackupは消去しない。
3. build source・全artifact hash・完全なsdkconfig・USB serial・MCU MAC・flash IDを
   照合して書込み、`verify_flash`を実施。loaderに保持し、応答側READY後に送信側を起動する。
4. 両側のfresh bootから100件の完了まで記録し、受信consumerの後続ログも回収する。
   完了後は両MCUをdownload modeへ戻して自動送信を止め、journalを読出す。
5. parserが検査したcommit済み100件のoutbound / inbound payload・ID・契約、
   全IDのattempt / receipt handoff / consumer記録 / remote evidenceを照合する。
   アプリのsummaryだけで成功とは判定しない。
6. 同じimage・同じjournalで両方を再起動し、100件のID・完了状態が保持され、
   RF再送がなく、journalがbyte単位で不変であることを確認する。
7. 回復試験ではBをloaderで保持してAを起動。Aのローカルcommitと最初のTX_DONEを
   観測してからAをresetする。未完了snapshotにOUT_CREATE / OUT_ATTEMPTがあり、
   REMOTE_STOREDがないことを検査した後、両方を起動して同じ1件から100件完了まで確認する。

全artifact保存先：`C:/dev/job/iot/Ninlil/.verify-m1-evidence/`。
`<label>-capture.json`、両側の`<label>-a.log` / `-b.log`、
`<label>-<board>-journal.bin`とdevice hash照合JSON、
`<label>-<board>-journal-inspect.log`、`<label>-analysis.json`を保持する。
読み出しや観測が不完全な場合は、失敗出力も保持して再試行を別labelで記録する。

## 実行コマンド

```sh
# repository root, local Linux container
NINLIL_BUILD_ROOT=/tmp/ninlil-delivery-full NINLIL_JOBS=2 ./scripts/ci.sh
# cached IDF container: exact clean source + saved full sdkconfig
source /opt/esp/idf/export.sh
bash /evidence/build-delivery-cached.sh 80ab9acd444b577fc687bf6dbcddbf7e13ec2fa3 <variant> <variant>-r1
# Read-only journal validation
/tmp/ninlil-delivery-full/gcc/m1_flash_inspect /work/.verify-m1-evidence/<label>-<board>-journal.bin
```

```powershell
.\.verify-m1-tools\Scripts\python.exe .verify-m1-evidence/delivery-journal.py <a|b> <label> <read|clean>
.\.verify-m1-tools\Scripts\python.exe .verify-m1-evidence/flash-delivery.py <variant>
.\.verify-m1-tools\Scripts\python.exe .verify-m1-evidence/capture-delivery.py <sender-variant> <receiver-variant> <label>
.\.verify-m1-tools\Scripts\python.exe .verify-m1-evidence/analyze-delivery.py <label> <new|replay>
.\.verify-m1-tools\Scripts\python.exe .verify-m1-evidence/capture-delivery-pending.py
```

## 結果

| 試験 | 時間（JST） | 結果 |
|---|---|---|
| A→B、試験ID 2026090701 | 19:08:14–19:08:40 | 100件の相手保存・送信側完了、PASS |
| 同100件、両方を再起動 | 19:09:27–19:09:31 | 全ID・journal不変、RF再送0、PASS |
| B→A、試験ID 2026090702 | 19:12:51–19:13:16 | 100件の相手保存・送信側完了、PASS |
| 同100件、両方を再起動 | 19:14:16–19:14:20 | 全ID・journal不変、RF再送0、PASS |
| Aの未完了から再開、試験ID 2026090703 | 19:18:16–19:18:41 | 先頭IDを保持し100件完了、PASS |

各新規試験の送信側journalにはOUT_CREATE / OUT_ATTEMPT / OUT_EVIDENCEが各100件、
受信側にはIN_ACCEPT / IN_RECEIPT_HANDOFF / IN_APPLICATION_ACCEPTが各100件あり、
すべて同じ100メッセージのpayload・IDと一致した。OUT_EVIDENCEは相手保存を示す4。
所有権の重複記録、欠番、未説明のreset・firmware errorはない。
IN_APPLICATION_ACCEPTはこの試験consumerによる受取り記録であり、製品アプリの
業務処理完了を証明したものではない。

再起動試験では新規RF TX_DONEログは両側0件、再offer / consumerの二重記録も0件。
100件のidempotency再提出はすべて既存IDとSATISFIEDを返した。
両方向の各boardで、再起動前後の128 KiB journalのSHA-256が一致した。
送信側stack最小空きは12,876 / 16,384 bytes（78%）。

未完了試験で停止したメッセージIDは`dcefbe0c26ce504323a6bbd994d86253`。
reset後のAのsnapshotにはこの1件のOUT_CREATEとOUT_ATTEMPTだけがあり、
相手保存の証拠はなかった。Bのsnapshotは全消去状態。
再起動後の最初のHIL_SUBMITとHIL_SATISFIEDは同じIDで、最終的に各側のFlashに
100件が揃った。送信が試行されたことと、相手が保存したことを区別できている。

### 観測スクリプトの停止と照合

未完了captureの最初の実行では、スクリプトが一般的な`result=PASS`を検出して
初期化PASSを配送PASSと誤認し、assertionで停止した。finallyによるresetは実行され、
後続ROM読出しでAの未完了2レコードを確認した。保存済みraw logには配送PASSや
HIL_SATISFIEDがない。判定を配送固有markerへ限定し、元ログとdevice snapshotを
`validate-delivery-recovery.py`で再照合した。元ログを保持し、
観測スクリプトの不具合と、保存状態・実際の回復結果を分けて記録している。
最初のtracebackも`delivery-recovery-pending-monitor.log`へ残した。

Flash検証ツールは、実機journalのコピーの1 byteを変えた場合にexit 1で拒否し、
VALIDATEDを出さないことも確認した。元のsnapshotは不変。
結果は`delivery-inspector-negative-check.json`。

### ファームウェアと保存先

| Variant | Application bin SHA-256 |
|---|---|
| delivery-a-init | 417a981b55955a9115feda15a7109646827889616523f8734e7c8099e8bee5c6 |
| delivery-b-resp | b89cda050f883a51563bac762d18b8c8f6977ea06d89dc822464555bbd7d021c |
| delivery-b-init | d1e514a306a1a57e18fd525752c230e6ac493592ff6c406b2491564fbd75c32b |
| delivery-a-resp | bb05f558b3ec2833e65b7f888bf084531b6abca7dcad8d32d1f126da9be6a7c1 |
| recovery-a-init | 36ec0d5f98f618168cc27511969ff1ee0103c5b9c46686ddec78d580fcfef03d |
| recovery-b-resp | 1727aa1017decb2bf33f090bee37d189caf662a2970a5db073d1af26614efa66 |

完全なsdkconfig、source、bootloader・partition・appのhashは各`<variant>-r1`に保存。
個別のFlash hash、再起動照合、検査結果、終了状態は
[`M1_DURABLE_RESULTS_2026-09-07.json`](M1_DURABLE_RESULTS_2026-09-07.json)に収録した。
195 artifactの一覧は`.verify-m1-evidence/delivery-evidence-manifest.json`。
manifest SHA-256：`d78872990859c30be5e26d7cea743312ec7f199717df409b04130a73b9eb6a94`。
一覧内の各size/hashは保存後にもう一度照合した。

### 終了時と残件

19:20 JST、両方を元のTX無効・frequency 0のinit-a-fix / init-bへ戻した。
source / app hashは[初期化記録](M1_BOARD_INIT_2026-09-07.md)と同じ。
書込みverify、fresh boot、100回初期化PASSを再確認した。既知GPIO ISR共有メッセージ
99件ずつ以外のerror-levelログはない。journal領域を消去せず、回復試験の保存内容を残した。
自動送信を再開するには明示的な試験用imageへの書換えが必要。

今回満たしたのは、M1の各方向100件の永続配送の観測条件と、完了済み／送信側未完了の
制御されたMCU resetからの回復。実際のUSB抜き差し、受信側の保存直後・応答喪失を
狙ったreset、CRC/BUSY/DIO1などの物理fault注入、制御されたFlash書込み途中の
電源断は未実施。正式なRF構成確認、M1全体、現場運用、production securityは未受入。
GitHub Issue/Projectへの投稿・更新、push、hosted CIは行っていない。
