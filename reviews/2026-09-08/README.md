**Ninlil コードレビュー — 2026-09-08**

対象は `f5b5b7eb84f7ad830cb2bbba2a9a7820820d53d4`。修正対象を **9件（P1: 7件、P2: 2件）** 確認した。P1はデータ保持、期限、復旧または通信継続に影響するため、自律運用への組込みより先に修正する。P2は条件つきの停止・設定不整合として修正する。製品ソースと既存テストは変更していない。

全9件を実装そのものを呼ぶ再現コードで確認した。GCC、Clang、それぞれのASan/UBSan構成でも同じ結果になった。`REPRODUCED` は不具合を再現したという意味であり、正しい動作や修正完了を示さない。再現コードはこのレビュー時点の挙動を検査する独立した証拠で、既存CTestへの登録や正常動作の期待値の変更はしていない。修正時には期待値を安全な動作へ反転した回帰テストを対応モジュールへ統合する。

**R1 / P1 — 保存済みFlash記録の破損を読み飛ばし、成功として再開する**

場所: [ninlil_flash_store.c:138](C:/dev/job/iot/Ninlil/ports/flash/ninlil_flash_store.c:138)、読み飛ばす処理は同ファイル244行。

`classify_commit` は、commit markerと補数の一部が未プログラム状態に見えると `INCOMPLETE` を返す。しかし、既に書込み成功を返した記録の0ビットが1に化けても同じ判定になる。再開処理はそのセクタの残りを読み飛ばし、正常終了する。末尾セクタなら後続sequenceとの不一致も発生しない。

再現: 記録を正常にappendした後、marker/補数の0ビットを1つだけ1に変更した。32通りすべてで、reopenがOK、復元件数が0になった。さらに新規記録のappendとreopenもOKになり、元の記録だけが復元対象から消える。送信元の唯一のoutbox記録や中継の保管記録にも同じストレージ処理が使われるため、引き受けたデータを失う可能性がある。

修正方針: 「書込み途中」と「確定後の破損」を区別できないmarkerは明示的な復旧エラーにする。自動継続が必要なら冗長な確定情報を含む形式・移行方針を設計する。カウンタ用ストレージは既に曖昧なmarkerを破損として扱っている。既存 `test_flash` のmarker破損ケースは1→0を検査しており、逆方向の32通りを検査していない。

証拠: [storage_probe.c](C:/dev/job/iot/Ninlil/reviews/2026-09-08/storage_probe.c)。メモリ上のNORモデルでの再現であり、今回の実機に破損を注入した結果ではない。

**R2 / P1 — 一時的なサービス別受信枠不足を永久拒否として確定する**

場所: [ninlil_policy.c:50](C:/dev/job/iot/Ninlil/src/ninlil_policy.c:50)。`ninlil_receive.c` の `handle_data` がこのUNAUTHORIZEDを永久拒否の記録へ変換する。

権限不足と `live_messages >= maximum_live_messages` が同じUNAUTHORIZEDになる。受信側では一時的に処理待ちが多いだけでも永久拒否を保存し、送信元はFAILEDとして再送を終了する。

再現: 同じ許可済みサービスの上限を1件にし、1件目をREMOTE_STOREDまで届ける。送信元では1件目が完了しているので2件目のsubmitは成功するが、受信アプリが1件目をまだ引き取っていないため2件目が永久拒否になる。1件目を消費して空きを作り、両Coreを再起動しても2件目はFAILEDのままで配送されない。通常のメッセージとPOSIXジャーナルで再現した。

修正方針: サービス・能力・方向の拒否は認可エラー、現在の使用枠超過はCAPACITY/BUSYとして分離する。容量が戻れば同じ論理IDで再送できることを送受信Coreの組合せで検査する。

証拠: [quota_probe.c](C:/dev/job/iot/Ninlil/reviews/2026-09-08/quota_probe.c)。

**R3 / P1 — 適用開始済みの経路をabortすると、有効期限による切替制限が消える**

場所: [ninlil_network.c:257](C:/dev/job/iot/Ninlil/src/ninlil_network.c:257)。

`ninlil_coordinator_abort` はSTAGEDとCOMMITTEDを区別せずpendingを消す。新規flowではまだactiveがないため、その後のactivateは、一部参加機器が適用した旧COMMITTED経路を参照しない。

再現: epoch 1の経路1→2→4を期限50,000までcommitし、node 2だけが適用報告する。abort後、別経路1→3→4をepoch 2として準備すると、旧経路の解放報告なしで時刻103にactivateが成功する。旧経路の有効期限より前に競合する計画を開始できる。

修正方針: 適用指示を開始した計画は、全参加者の解放確認または有効期限まで切替制限を保持する。単純なabortをSTAGEDまでに制限するか、COMMITTEDの撤回を独立した状態遷移にする。既存abortテストはSTAGEDだけを扱う。

証拠: [control_probe.c](C:/dev/job/iot/Ninlil/reviews/2026-09-08/control_probe.c) の `abort_committed`。

**R4 / P1 — 再起動後、置換対象とは別flowの期限でactivateを許可する**

場所: [ninlil_network.c:320](C:/dev/job/iot/Ninlil/src/ninlil_network.c:320)、期限判定は245行。

通常のstageは対象flowのactiveを `c->active` に選ぶ。restoreはEFFECTIVEを読むたびにこの値を更新する一方、後続のSTAGEDを復元しても対象flowへ戻さない。activateはこの共有のactiveを期限判定に使う。

再現: flow 1→4の期限を50,100、次に記録したflow 2→3の期限を1,000とする。その後1→4の置換計画をstageして再起動する。再起動前は時刻1,001のactivateが拒否されるが、同じ記録をreplayした後は成功する。期限が切れた2→3を見ているため、まだ有効な1→4との切替制限を飛ばす。

修正方針: pendingの始点・終点に対応するflowをactivate時に直接参照する。復元時の補助的な選択状態に安全性を依存させない。複数flow、異なる期限、置換準備中の再起動を回帰テストにする。

証拠: `control_probe.c` の `restore_wrong_active`。

**R5 / P1 — 部分適用中の再起動では、古い適用報告が再確認なしに有効になる**

場所: [ninlil_network.c:325](C:/dev/job/iot/Ninlil/src/ninlil_network.c:325)、全確認扱いにする箇所は217行。

EFFECTIVEを復元した場合はreconciledを0に戻すが、COMMITTEDのpendingではprepared/appliedをそのまま戻す。その後、残りの1台が報告すると、全参加者をreconciled済みとしてEFFECTIVEにする。記録済み報告が現在も適用されているという確認はない。

再現: 1→2でnode 1だけappliedを保存してCoordinatorを再開する。node 2からの報告だけでroute_checkがOKになる。node 1は再開後にreconcileしていない。node 1自身も再起動して計画を失っている場合にも、このAPI列では有効扱いになる。

修正方針: 永続化された過去の適用事実と、再開後の確認済み集合を分ける。COMMITTEDの復元にも新しい確認を要求し、残りの報告だけで確認済み集合を全ビットにしない。

証拠: `control_probe.c` の `restore_partial_application`。Coordinatorのcommit callbackが正常保存した記録列をそのまま再生している。物理再起動試験ではない。

**R6 / P1 — 経路が未確定の1件によって、制御用の送信待ちまで止まる**

場所: [ninlil_network_pump.c:90](C:/dev/job/iot/Ninlil/ports/esp32s3/ninlil_network_pump.c:90)、経路エラーの発生元は `ninlil_routed.c:68`。

Coreの送信処理は経路lookupのNOT_FOUND/STATEを返す。pumpはこれを直ちに返し、後段のairtime schedulerと物理送信を実行しない。まだ経路がない1件は毎回試されるため、その経路を成立させるための制御フレーム自体が送れなくなる。

再現: 送信元Coreに、認証済みだが経路のないnode 3宛てメッセージを保持させる。別peerへの現在有効な制御fragmentをqueueに入れる。十分な送信予算と時間がある10回のpump呼出しすべてがNOT_FOUNDで終わり、radio sendは0回だった。

修正方針: 経路待ちを明示的な再試行可能状態に変換し、別peerや制御フレームの送信機会を確保する。破損・永続化失敗まで一律に無視しない。既存pumpテストはCoreをattachしておらず、host exampleはSTATE/NOT_FOUNDを待機として扱うため、この差を検出していない。

証拠: [pump_probe.c](C:/dev/job/iot/Ninlil/reviews/2026-09-08/pump_probe.c) の `missing_route_blocks_control`。

**R7 / P1 — 送信待ちの間に期限・経路が失効しても、送信直前の確認を通る**

場所: [ninlil_routed.c:265](C:/dev/job/iot/Ninlil/src/ninlil_routed.c:265)、送信呼出しは `ninlil_network_pump.c:112`。

Coreはqueueへ渡す前にdeadlineを検査するが、送信直前の `ninlil_routed_frame_current` はhopの相手・membership・鍵fingerprintしか確認しない。DATAの期限、route epoch・leaseをqueue滞留後に検査できる情報が保持されていない。

再現: 時刻100でdeadline 150のDATAをCoreからqueueへ入れ、予算が貯まるまで送信させない。時刻200のpumpでradio sendが呼ばれた。実際に生成された暗号フレームをhop・E2Eとも復号し、deadlineが150のままであることを確認した。同じframeはroute lookupが期限切れを返す状態でもframe_currentがOKになる。後者は検証関数単体の再現で、期限切れ経路による実機送信を観測したという意味ではない。

修正方針: queueに元の契約・経路を検証できるメタデータを持たせ、物理送信直前に適切な品質の時計、deadline、現在のrouteと権限を再検証する。期限切れの派生フレームの破棄と、元のCore/Relay保管責任の終了を混同しない。

証拠: `pump_probe.c` の `staged_deadline`。実Core、実routed、実AES-CCM、実pumpと、時計・NOR・radioのテスト用境界を使用。

**R8 / P2 — 短い通常通信が継続すると、長い緊急フレームが無期限に待つ**

場所: [ninlil_airtime.c:111](C:/dev/job/iot/Ninlil/src/ninlil_airtime.c:111)。

必要な送信時間に対してcreditが足りないジョブは飛ばされる。小さい通常フレームが新しく貯まったcreditを毎回使い切ると、重み8のCRITICALでも必要量を貯められない。予約queue枠や選択回数の重みではこの待ちを解消できない。

再現: 予算200,000µs/秒、CRITICAL 1件200,000µs、NORMAL 1件10,000µsを50msごとに追加する。100秒で通常フレーム2,000件を送信しても、最初から待っているCRITICALは1回も選ばれなかった。同じ状態が周期的に繰り返されるため、試行時間を延ばしても解消しない。

修正方針: クラス別の送信予算や不足分の繰越し等で、異なるフレーム長でも有限回で送信機会が来るようにする。待機中の緊急フレームに必要なcreditを、後着の小さいフレームが毎回消費する構造を解消する。

証拠: [contract_probe.c](C:/dev/job/iot/Ninlil/reviews/2026-09-08/contract_probe.c) の `airtime_starvation`。

**R9 / P2 — Joinが確定した設定を、Coreは不正な設定として拒否する**

場所: [ninlil_join.c:24](C:/dev/job/iot/Ninlil/src/ninlil_join.c:24)、対になる検証は `ninlil_authorization.c:13` と28行。

Joinのgrant検査とCoreのpolicy検査が一致していない。Joinはmaximum_payload_bytes=0、および通常EndpointへのGATEWAY_RADIO_HEAD能力付与を受け入れるが、Coreはどちらも不正とする。

再現: それぞれの設定でbegin→authenticated→prepare→endpoint commit→confirmが成功し、3回のcommitとsession-readyまで進む。その後、返されたpolicyをCoreと同じ `ninlil_policy_validate` に渡すとINVALIDになる。実際のCore認可処理はこれをCORRUPTとして扱う。

修正方針: grantとpolicyの共通部分の検証を統一し、利用できない設定を永続化前に拒否する。0-byte専用サービスを許可するかは両側で同じ契約にする。

証拠: `contract_probe.c` の `join_invalid_policy`、2種類の設定で再現。

**確認範囲と検証結果**

Coreの所有・受信・再送・期限・replay、POSIX/raw-Flash journal、カウンタ・membership保存、EDHOC/PSA境界、Join、Coordinator、Relay、routed、airtime、fragment、ESP network pump、SX1262 HAL/driver、Host custody/topology/group/Leaf、bench firmwareの選択・初期化・鍵・中継処理を確認した。対応するテスト、host example、HIL記録、Flash保護手順も照合した。第三者暗号ライブラリの全行監査や新たな実機試験はこのレビューに含まれない。

| 検証 | 今回の結果 |
|---|---|
| 既存CTest / GCC・Clang・両ASan/UBSan | 27 × 4 = 108/108成功 |
| 適合パッチ込みupstream tests | 711/711成功 |
| manifest / Join・plan・Relay・fragment fuzz | 各10,000実行成功 |
| ESP stub / 固定Semtech APIの構文検証、依存pin・パッチhash | 成功 |
| Clang静的解析、EDHOC/PSA境界の静的解析 | 成功 |
| GCC `-fanalyzer`、実際のobject生成を伴う40翻訳単位 | 成功 |
| シミュレータの再現性・未完了終了・不正CLI入力 / 全4構成 | 成功 |
| 既存C/Hと再現コードのclang-format | 成功 |
| レビュー再現コード | 5実行ファイル × 4構成、9件すべて再現 |
| 新しいESP-IDF link / 実機Flash / RF / 電源断 | 今回は実施していない |
| hosted CI / GitHubへの書込み | 実施していない |

既存の `scripts/static_analysis.sh` のGCC部分は `-fsyntax-only` 併用なので、今回のGCC解析では実際にobjectを生成する [run_analysis.py](C:/dev/job/iot/Ninlil/reviews/2026-09-08/run_analysis.py) を追加実行した。コンパイラ警告・サニタイザを緩めた処理はない。

ローカル環境はUbuntu 24.04 / WSL2 Linux 6.6.87.2 / x86_64、GCC 13.3.0、Clang/clang-format 18.1.3、CMake 3.28.3、Python 3.12.3。既存検証は `ninlil-static-star-verify` コンテナのビルド `/tmp/ninlil-secure-hil-local/{gcc,clang,gcc-sanitize,clang-sanitize}` を再ビルドして使った。

再現コマンド（リポジトリを `/work` に置いた同コンテナ内）:

```sh
cd /work
python3 -B reviews/2026-09-08/run_probes.py /tmp/ninlil-secure-hil-local
python3 -B reviews/2026-09-08/run_analysis.py /tmp/ninlil-secure-hil-local/gcc
```

別環境では固定依存を取得し、`NINLIL_BUILD_ROOT=/tmp/ninlil-review-build bash scripts/ci.sh` で4構成を作成してから、run_probes.pyへそのbuild rootを渡せる。将来の修正後にREPRODUCED条件が成立しなくなるのは期待される結果であり、その場合は修正後の回帰テストで安全な動作を検証する。

今回の生ログ:

- [既存検証](C:/dev/job/iot/Ninlil/.verify-m1-evidence/review-20260908-local.log)
- [全構成の不具合再現・最終コード](C:/dev/job/iot/Ninlil/.verify-m1-evidence/review-20260908-probes-final.log)
- [再現コードの初回検証](C:/dev/job/iot/Ninlil/.verify-m1-evidence/review-20260908-probes.log)
- [GCC追加解析](C:/dev/job/iot/Ninlil/.verify-m1-evidence/review-20260908-gcc-analyzer.log)
- [シミュレータ再現性](C:/dev/job/iot/Ninlil/.verify-m1-evidence/review-20260908-reproducibility.log)

機械可読の検証結果とログhashは [results.json](C:/dev/job/iot/Ninlil/reviews/2026-09-08/results.json) に保存する。このディレクトリはレビュー証拠であり、製品のsecure network profileへの機能追加ではない。50,000行のプロジェクト全体の上限は適用したまま、個別profileの上限は変更していない。

自律的な参加・再認証・計画配布の制御処理、実機Coreへの完了通知接続、実運用の鍵管理は、従来から残っている統合実装の課題として扱う。今回の9件を修正・回帰検証してから、その統合を進める。
