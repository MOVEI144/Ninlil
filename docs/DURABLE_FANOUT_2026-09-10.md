# 多数台配信の永続保存・復旧実装 — 2026-09-10

PR #18 の `b38878e79f1e7b28ecbc195762395b1d67ab6475` を適用基準とする累積パッチ。
前回の [観測・出力・経路統合](INTEGRATED_ROUTING_2026-09-10.md) を保持し、その上に
実際のジャーナル保存アダプターを追加した。GitHubへのcommit/push/mergeは未実施。
今回の接続にGitHub書込み操作が提供されず、公開リポジトリのcloneもDNS解決に失敗した。
既存ファイルを個別取得し、必要なヘッダーとPOSIXジャーナルのGit blob一致を検査している。

## 実装した範囲

`ninlil_fanout_store` は既存の `ninlil_fanout` を実行し、commit/replayを実際の
`ninlil_journal` へ接続する。メモリだけのcommit callbackではない。STARTには完全な
配信契約、固定identity/世代付きの対象集合、本文を保存する。対象ごとのINTENT、
Core受付ID、最終結果も保存し、close/open後に同じidempotency keyと結果へ復帰する。

**完成したのはこの永続保存アダプターとそのホスト検証であり、全Ninlilや数百台RFの完成ではない。**
実際の配送Coreで全再送までidentity/世代を固定する `admit_bound/query_bound` の実装は
引き続き必要。旧address-only `ninlil_submit` をそのまま接続して契約を満たすとはしない。
相手と合意するPHY変更、共有無線容量予約、全SDK/ESP-IDF/7台実機検証も残る。

## 保存と外部処理の順序

1. 契約、全対象の整列・重複・権限関連フィールド、本文のSHA-256を検査する。
2. header → 対象ごとのrecord → 本文record → sealを保存する。
3. sealのcommitと読み戻しに成功して初めてSTARTを公開する。ここまで配送callbackは呼ばない。
4. 宛先が利用可能ならINTENTを保存・読み戻し検査してから `admit_bound` を呼ぶ。
5. 返ったCore message IDをADMITTEDとして保存する。応答不明なら再open後に同じkeyで回復する。
6. `query_bound` の正本証拠を検査し、TERMINALを保存してから完了集計へ反映する。

START途中で停止しても、部分的な宛先集合を配送開始しない。通常のRESUMEは未完了STARTを
EMPTYとして拒否し、ハンドルを返さない。明示的なINITIALIZEと同一の契約・対象・本文を
使った場合のみ保存を継続できる。保持済み部分の変更、別source/operationへの読み替えは拒否する。
**通常起動でRESUME失敗を捕まえて自動INITIALIZEする実装はしない。**

## 永続形式 NFS1

Core/controlとは別の専用journal。既存のjournal envelope、CRC、同期・ロック・tail修復を
再利用し、本文中のmagic/version/operation/順序/個数をこの層で検査する。
C構造体のpaddingやpointerを保存しない。多バイト整数は明示したbig-endian。

| journal内type | body | bytes | 内容 |
|---|---|---:|---|
| 1 | NFH1 | 132 | operation、authority/source、本文digest、authority epoch、deadline、参照ID、service/class/evidence、件数・本文長 |
| 2 | NFT1 | 88 | operation、連続index、無線address、固定identity、membership/binding epoch、idempotency key |
| 3 | NFP1 | 22 + 0..256 | operation、本文長、実本文 |
| 4 | NFC1 | 24 | operation、全対象件数、本文長。STARTの確定点 |
| 5 | NFD1 | 49 | operation、連続sequence、phase、対象index、Core ID、outcome/evidence |

記号末尾の1はASCII文字ではなくversion byte `0x01`。不明version/type、順序違反、途中の
再header、対象の重複、長さ違反、別operation、sequence欠落、逆行するphaseを拒否する。
失敗decodeは出力を変更しない。CRCは破損検出であり署名・悪意ある書換えへの認証ではない。
ファイルと物理Flashのアクセス制御、配送先の現在の認可検査は引き続き別の責任である。
SHA-256は呼出し元の既存暗号backendを必須とし、独自暗号実装は追加していない。

## 読み戻し・破損・曖昧なエラー

保存直後だけでなく、本文を外へ渡す前、対象をCoreへ受付する前、結果を問い合わせる前に
header/seal/該当target/最新state/本文の参照記録を検査する。本文は完全なjournal envelopeと
checksumの検証後にSHA-256を再計算する。集計APIは全保持対象の記録を検査してから結果を返す。

確定済みデータの破損やIOエラーはハンドルを停止状態にする。保存エラーと宛先の一時的な不達を
混同しない。整合性検査backendがBUSYを返した場合も、別宛先の処理へ進む理由にしない。
この分岐について失敗する回帰試験を追加し、ownerとstoreを同時に停止させる修正後に通過した。
停止状態からの回復は、正本をclose/openして読み直す。既存のメッセージは削除・取消しない。
UNKNOWNが保存された対象は `all_terminal` に入るが、`all_satisfied` には入らない。

## 資源とAPI

public headerは `include/ninlil_fanout_store.h`。CMake targetは `Ninlil::fanout_store`。
公開オブジェクトはopaqueで、open時だけ対象容量分の有限メモリを割り当てる。処理ごとのmalloc、
内部thread、timer、RF起動、eraseは追加しない。実行ownerは一つ。callback中の状態を返すAPI再入はBUSY。
callback内からのcloseは禁止し、誤って呼ばれてもメモリを解放しない。
closeはメモリを解放するだけで、専用journalや配送責任は削除しない。

対象上限は512、本文上限は256 bytes、stepは最大32対象。単一operationの記録数上限は4N+3で、
同一STARTや同一terminalを繰り返してjournalを増やさない。保管容量上限は1 MiB。
上限に達したとき既存対象を追い出して空きを作らずCAPACITYを返す。専用journalの回収・
新operationの受入数は上位ownerが決める。この変更には汎用ジョブ管理基盤を追加していない。

64bit hostで容量512を指定したstore本体と配列の要求サイズは78,400 bytes。
allocatorやjournal/backendの追加メモリ、ESP32の実使用量ではない。
511対象・本文256 bytes・全対象terminalのPOSIXファイルは149,177 bytesだった。
これは保存容量の測定であり、511台の無線通信能力やRAM余裕の証明ではない。

```c
/* cfg contains a dedicated journal path, expected source/operation,
 * an audited SHA-256 callback, and an identity-bound delivery adapter. */
ninlil_fanout_store *store = NULL;
int rc = ninlil_fanout_store_open(&store, &cfg, NINLIL_FANOUT_STORE_RESUME);
/* Do not change RESUME to INITIALIZE automatically on EMPTY or corruption. */
if (rc == NINLIL_OK) {
    rc = ninlil_fanout_store_step(store, monotonic_ms, 4u);
    ninlil_fanout_store_close(store);
}
```

初回作成や未完了STARTの明示継続ではINITIALIZEでopenし、`ninlil_fanout_store_start` に
固定契約・全対象・本文を渡す。START APIの成功は保管完了であって相手の受信ではない。

## 検証

今回追加した保存試験は、GitHubのblob SHAと一致した本物の `include/ninlil.h`、
`src/ninlil_journal.h`、`ports/posix/ninlil_journal.c` を使用する。
保存のIO、file lock、readback、CRC、再openは実コード・実ファイルである。
下流は別の実ファイルjournalを使う「identity-bound Core adapter fixture」であり、
本物のNinlil Core/受信アプリケーション/暗号無線を実装したものではない。

GCC14.2/Clang17の通常・ASan/UBSanで各25/25 CTestが通過。既存ノード・無線接続fixtureは
引き続き代替型/暗号/無線/時計を使う。新しい保存テストにその代替型を使ってはいない。
Pythonの7台runner試験17件も維持している。

STARTの10箇所で実child processを `_exit`、INTENT/ADMITTED/TERMINALの3箇所でも同様に停止。
7対象STARTのファイルを全1,191 byte-prefixで切断して再openし、部分集合を配送せず回復することを確認。
実ファイル容量不足、保存成功後のエラー応答、5種類の確定記録破損、別identity/operation、
空本文、出力buffer不足、UNKNOWN保持、511対象中32対象のadmission前不達を検証した。
電気的な電源断やファイルシステム自体の同期保証を破る障害を、この試験で実証したとはしない。

codecはseed=20260910の20,000入力を検査し、成功はcanonicalな往復、失敗は出力不変を確認。
変更した4つのC実装と既存POSIX journalにGCC/Clangの静的解析10コマンドを実行し、最終診断は0。
初期解析のNULL出力境界指摘も記録し、コピー長を固定・再検査する修正後に再実行した。
ログ・入力hash・未実行gateは [evidence](evidence/2026-09-10-fanout-store/README.md)。

## 未実装・未実施

本物のCoreで全自動再送までidentity/epochを保つadapter、Group/Coreの保管枠と送信機会の分離、
相手と合意するPHY設定変更、共有radio容量の受入は未完成。
ESP-IDFのsource登録とCMake packageの接続は追加したが、その全体build/link、installed package、
raw-Flash backendでの新保存層試験、7台RF/HIL、hard power-cut、消費電流、長期fieldは未実施。
全体の50,000行・clang-format gateも完全checkoutで未検証。既存上限・検査・暗号を緩めていない。
GitHub hosted Actions、commit/push/merge、実機の初期化・書込み・無線起動はしていない。
