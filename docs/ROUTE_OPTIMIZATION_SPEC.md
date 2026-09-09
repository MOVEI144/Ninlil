# 自動経路最適化仕様 v1

状態: 実装前仕様。基準SHA、共通不変条件、観測O1、容量C1、受入G01〜G09は[共通仕様](ADAPTIVE_OPTIMIZATION_SPEC.md)に従う。以下は既存機能の説明ではなく、既存αに追加する要件である。

## R1. 目標と非目標

最適化対象は「最終宛先にpacketが届く」ではなく、**送信元が要求されたE2E証拠を得るまでの時間・費用・達成率**である。DATAの上りが良くてもreceiptの下り、sleep、保存、queueが悪ければ候補を下げる。

全環境の大域最適や妨害下のhard realtimeは約束しない。経路のないbridge撤去を自動回復と呼ばない。無制限flooding、電池Relay、全端末間の全経路保持、独自leader選挙は導入しない。現在の4-hop設計上限を維持し、4-hop実機受入前にその性能を宣伝しない。

既存 `src/ninlil_network_route.c` は `ceil(airtime_us * attempts / delivered) + queue_us` を加算し、単一の選択経路を返す。新実装はこのpolicyを比較baselineとして残す。単なる名前変更で共同最適化としない。

## R2. グラフ・identity・観測の所有

Coordinatorはversion付き不変snapshotから計算する。頂点keyはstable identityとmembership/binding世代。short addressだけで古いedgeを新deviceへ付け替えない。edgeは方向・PHY・測定窓を含むO1値とする。権限失効、役割変更、Root世代変更はsnapshot invalidation reasonとして残す。

到達グラフと競合制約を分ける。競合には共通送信機/受信機、half-duplex、hidden terminal、未知の同時SF/channel、共通電源/上位Relay依存を含める。最後の共通故障点は可用性情報であり、RF競合とは別fieldで保持する。

新peerは未測定候補として登録できるが、未測定をdelivery=100%に補完しない。既存の安全な共通profileから有限probeで確認する。source/targetの参加許可と途中Relayのpowered capabilityを毎回検査する。

## R3. 候補生成

一つのactive経路と最大二つのbackupを保持する。各経路は最大4-hop、重複identityなし。上り候補DAGの安全な順序と、変動する性能scoreを分離する。backupは事前に認可・能力・ループ条件を検査するが、保存してあるだけでactive送信権を持たない。

初期アルゴリズムは有限のmulti-label探索とする。

1. 安全条件、MTU、受信窓、hop、世代、禁止node/profileで不適格edgeを除外する。
2. `(node, hop_count)` ごとに最大3つのラベルを保持する。ラベルはpath、airtime、証拠遅延推定、競合領域別需要、故障依存を持つ。
3. 明らかに劣るラベルを除き、同値はstable identity列・PHY IDで決定的にtie-breakする。backupsの多様性を残すため、費用だけで全ラベルを一本へ潰さない。
4. source-targetごとに最大3候補を完成させる。逆向き証拠経路、wake予定、session/creditの成立性を検査する。
5. C1で候補を実際の予約集合へ入れ、他flowの負荷増分を反映して選ぶ。

探索上限はsnapshot当たり `3 * H * E` のラベル/edge緩和を基本予算とし、追加のbackup除外再探索は最大2回。各 `plan_step` は最大256緩和またはportのCPU予算の先に達した側で中断する。根の多数宛先にはrooted reachabilityを共用し、512宛先ごとに全計算を最初から繰り返さない。

現在のnode検索は線形である。新profileはaddress/identity→indexの有限indexを持ち、計算量を宣言する。N/E/ラベル容量を超えたら `PLANNING_CAPACITY`。途中結果をradioへ適用しない。

## R4. 費用と採用順序

推定値は最低限次を別々に持つ。

`evidence_latency = queue + wake_wait + forward_DATA + storage + retries + reverse_evidence + sender_commit`

`radio_cost = DATA_airtime + hop_control_airtime + E2E_receipt_airtime + expected_retry_airtime + allocated_control_overhead`

hop平均のp99を足してE2E p99と呼ばない。相関損失、有限retry、half-duplex、受信窓は実runtimeを動かす評価器に含める。初期の解析値は候補比較用であり、SLO達成の証明ではない。

採否は次の順序で決定する。

1. A01〜A12とC1を満たさない候補を棄却。
2. 要求証拠・latency目標を満たす見込みがあり、測定の信頼度を満たす候補を優先。
3. worst bottleneck occupancy、evidence遅延上位分位、総airtimeの順に比較。
4. 同程度なら共通故障点が少なく、電池radio-onとFlash負荷が小さく、現経路から変更が少ない候補を選ぶ。

単位の異なるdBm、ms、bytesを根拠なく一つの加算scoreにしない。係数が必要なprofileは正規化、単位、version、根拠をmanifestに固定する。予測不能な候補を最良値で埋めない。

## R5. 混雑・公平性・変更振動

queueの実待ち、credit、frame長、retry費用を観測する。無線が良好でqueueだけ増える場合、SF/出力を上げず、admission・送信機会・経路を見直す。

一つの計算round内ではsnapshotを固定し、採用した需要をshadow reservationへ加えて次候補を評価する。全端末が同じ空いていたRelayへ一斉に移る判断を防ぐ。roundは最大4回の局所改善で終了し、最後の検証済み解を返す。

通常の改善切替は、初期LAB値として改善20%以上、重複しない3観測窓、各窓8回以上の適格attempt、route単位30秒の最小滞在を要求する。2秒holdという現行値を黙って書き換えず、別policy versionにする。低頻度peerを無理にprobeして条件を満たさない。改善の根拠不足なら現経路を維持する。

障害/失効による修復は改善待ちを省けるが、送信権・clock・capacity検査は省けない。通常migrationは各競合領域で一度に一つを初期制限とし、最終的な並列数はPHY/MAC予約と一緒に決める。

## R6. 経路planのtransaction

既存NP2のSTAGED/COMMITTED/EFFECTIVEを互換baselineにし、新wireは以下を追加する別versionとする。

| field | 意味 |
|---|---|
| operation_id / plan_epoch / authority_generation | それぞれ再試行、設定版、発行権限を識別 |
| source/target identity + binding/membership | 宛先再利用を防ぐ。短縮handleは永続辞書の世代付き |
| forward_path / evidence_path | 最大4-hopずつ。下り成立を上りから推測しない |
| radio_plan_id / digest / traffic_contract_id | PHY/MAC予約・要求への参照 |
| prepare_deadline / valid_from / valid_until | network lease時計と誤差を明示。absolute message deadlineとは別 |
| previous_plan / participant_set / fallback_id | 旧権限、合意対象、回復先 |

候補の状態は `PROPOSED → PREPARING → COMMITTED → VERIFYING → EFFECTIVE`。COMMITTED前はABORT可。それ以後は旧権限を復活させず、新しいversionでwithdraw/recoveryする。

PREPAREDは能力/資源/clock/世代を検査し、必要なprepare記録をcommitした証拠。APPLIEDは現在のboot/sessionの設定と隣接contextが一致する証拠。EFFECTIVEは必要参加者のlive証拠を照合した結果。どの段階もアプリ配送成功ではない。

旧planを利用するexecutor全員の解放が確認できるか、旧leaseが誤差込みで確実に満了するまでは排他的な新送信権を有効化しない。最後のACK喪失ではdigest/epoch/bootを照会してreconcileする。二相風の手順だけで全node同時切替が保証されたとしない。

永続化失敗が「commit後に失敗と返る」可能性を含むならownerをpoisonし、reopenで確定する。queue内の旧暗号frameは実TX直前に再検査し、obsolete frameだけ破棄できる。Core/Relayの所有は保持する。

## R7. transactionの局所性と期限

現行のCoordinator全体で一つのpending planという制約を、将来profileで全flowへ引き継がない。ただし単純な無制限並列化もしない。

競合keyはsource/target attachment、参加radio、lease対象、authority generation。重ならないkeyだけ並列にし、初期ROOT128で最大4、ROOT512で最大8 transactionとする。各transactionが独立したdeadline、ACK bitmap、proof、commit recordを持つ。sleep/offline端末の待ちが無関係なflowを占有しない。

lock取得順はstable executor ID順。片方だけ準備した状態をtimeout後に再照合できる。pending table満杯では新提案を延期し、既存planの再開/receipt/失効を進めるための枠を別に残す。期限を延期して永久にslotを占有させない。

## R8. 障害・撤去・Root交換

通信健康状態は `HEALTHY / SUSPECT / REPAIRING / PARTITIONED`、membershipは別軸。受信を期待していないsleep時間だけでSUSPECTにしない。

突然消失では、事前認可backupのsession、clock、受信予定、空き、世代を確認してから移る。待機中は元の論理IDで保存する。backupがなくても無限探索しない。探索の次回時刻・試行予算・理由を公開する。

予定撤去では現在のdependency snapshotを保存し、対象を除いた全影響flowのC1を検査する。新custody受付停止、既存custody排出、子の上り/下り移行、最終コピーの存続、未解決一覧を確認してからREADY_TO_REMOVE。途中の新Join/group変更でsnapshotが古くなれば再評価する。

唯一のbridge、共通上位Relay、同じ電源への依存は明示的に `TOPOLOGY_INSUFFICIENT` または `COMMON_FAILURE_DEPENDENCY`。代替経路が存在するふりをしない。

Root交換は既存独立issuerとgeneration fenceを維持する。新Rootが学習したmetricは初期はunknown。古いlease/planを新Rootが自分の送信権として継承しない。孤立した旧Rootの物理停止は論理epochでは証明できない。旧Rootだけに保存された業務データの複製はNinlilのこの機能で保証しない。

## R9. RTOと負荷制御

既存の `cost / 500`、100〜30,000 ms clampを一般SLO設計として流用しない。新profileはhop retryとE2E evidence retryを分け、予定された受信機会、保存時間、RTT/ばらつき、clock guard、法規待ちを含める。

再送に対応するRTTが曖昧なら推定更新を抑制する。sleep待ちをRF lossとして指数backoffしない。retry budget枯渇はPAUSED/REPAIRINGでありFAILEDではない。quotaはairtime・件数・peerの複数軸とし、receipt/credit/GCの最小進行を確保する。

## R10. 必須試験

| ID | 条件 | 合格条件 |
|---|---|---|
| RT01 | weak directと強い2-hop、逆向きだけ損失 | RSSI/hop数だけで選ばずE2E証拠で比較 |
| RT02 | 良いRSSIだがqueue満杯のRelay | queue=0補完なし。容量不足を正しく検出 |
| RT03 | 1/2/3/4-hop、ループ候補、異なるepoch | 禁止edge/loop/世代混合を適用しない |
| RT04 | 0成功/少数sample/古い窓/profile切替 | 未測定を最良とせず、観測を誤再利用しない |
| RT05 | 多数peerが同一Relayを選びたい | shadow reservationと移行予算内、振動を記録 |
| RT06 | 二つのbackupが同じbridgeへ依存 | 単一故障耐性を宣言しない |
| RT07 | DATA/ACK/custody各境界でRelay停止 | 同じIDで再開、未配送の成功捏造なし |
| RT08 | 撤去中のJoin/group/crash/cancel | 影響snapshot再検証、READY誤判定なし |
| RT09 | PREPARE/APPLY/最終ACK欠落と両端再起動 | 重複commit、期限延長、旧送信権復活なし |
| RT10 | offline flowと無関係な正常flow | transaction枠の局所性と制御の進行 |
| RT11 | 計算budget尽き/OFF/Coordinator停止 | 最後の有効計画と所有を維持 |
| RT12 | 小グラフの全合法path列挙との比較 | 選択解・gap・CPUを報告し、最適を偽称しない |

RT01〜RT12は実C control/ownerを使う統合simulatorで実施する。純グラフモデルの合格だけではruntime routingの合格にしない。最終性能・CI・HIL基準は共通G01〜G09を使う。
