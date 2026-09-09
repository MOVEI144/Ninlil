# 受入・ベンチマーク仕様 v1

[共通仕様](README.md)の実装完了を判定するための要求。以下のT01–T28は**新たに要求する試験であり、この文書を作成した時点では未実行**。過去のCore testsや3台HILを新しい試験のPASSへ転記しない。

## 1. 証拠の段階

| 段階 | 証明すること | 証明しないこと |
|---|---|---|
| 静的レビュー | コード・契約・状態遷移の整合、具体的な反証候補 | 実機動作、バグ不存在 |
| native characterization | 基準版の正確な挙動、回帰入力 | 新設計の実装完了 |
| unit/property/model | 有限条件下の不変条件、crash/drop/reorder探索 | 現場の分布、無条件の到達 |
| 実装接続simulation | 実Core/Node/security/Relay/schedulerの統合、モデル内の性能 | RF校正、Flash電気的安全性 |
| target build | 指定SDK/boardでconfigure・compile・link | 電波が届いたこと |
| HIL | 指定board・firmware・profile・配置での実測 | 試していない台数・環境 |
| field profile | 宣言範囲内の長期運用・性能 | 範囲外での保証 |

証拠はsource SHA、source tree、toolchain、dependency hash、policy/manifest version、seed/trace、機器identity、RF/clock/storage設定、raw log hash、試行数、未実施項目を含む。hashを読んだだけの場合は生ログを検証済みと書かない。

## 2. Simulator v2が実行するもの

現行SIMULATION.md v1は2–5node、実Core+POSIX、固定slotの検査用として残す。v2は同じCの `ninlil_node`、Join/security、routed/Relay、Coordinator、Group owner、airtime schedulerをfake clock/radio/storageへ接続する。似たアルゴリズムを別言語で書き直した結果を本実装の証拠にしない。

radio modelはhalf-duplex、実encoded lengthのairtime、受信profile不一致、直接/隠れた干渉、非対称link、CCA busy、外部干渉、near/farの校正可能な条件を持つ。未モデル化の物理現象は省略を明記する。異なるSFの完全直交や無限に無料なACKを仮定しない。

storage modelはcommit/erase/GCの遅延、容量、torn write、commit済みなのにerrorを返すケース、post-open corruptionを注入する。CPU/radio/Flashを同時に無限並列処理できると仮定しない。実際のPOSIX操作を呼んだことと、virtual clockへその費用を課金したことを区別する。

simulationの資源とworkloadにも上限を置く。time/event上限に達した試行は `TRUNCATED/UNKNOWN` としPASSにしない。イベント駆動で時間を飛ばす場合は、timer・retry・wake・leaseの意味を飛ばしていないことを小規模の逐次実行と比較する。

## 3. Workload manifest v2

必須項目の欠落・未知の必須feature・不正な単位は拒否する。下記はschemaの内容を示す例であり、現行CLIが読める設定ファイルではない。

```yaml
schema: ninlil-network-workload-v2
status: design-example-not-executable-on-v1
source_sha: a950f7cb4628fe8b9c579794ab4bcb04c9b1f05d
profile: STAR64-EXPERIMENTAL
nodes_including_root: 64
max_rf_hops: 1
roles: explicit-per-node
radio_and_regulatory_profile: reviewed-fixture-reference
clock_model: bounded-sync-error-and-drift
storage_model: measured-or-explicitly-synthetic-fixture
payload_bytes: 32
required_evidence: APPLICATION_ACCEPTED
per_node_event_interval_seconds: 600
burst_per_node: 1
group_target_count: 63
group_interval_seconds: 1800
warmup_seconds: 600
measurement_seconds: 3600
critical_latency_target_ms: 10000
normal_latency_target_ms: 60000
whole_group_latency_target_ms: 600000
delivery_probability_target: 0.99
fault_trace: immutable-scenario-reference
policy_variants: [fixed-baseline, current-cost, proposed]
evaluation_seeds: [101, 211, 307, 401, 503, 601, 701, 809, 907, 1009, 1103, 1201, 1301, 1409, 1511, 1601]
```

この数値は初期試験の要求であり、現在の実装の性能値ではない。成立しないprofileでは受付拒否やSLO不達が正しい出力となり得る。全targetを拒否して達成率を高く見せた試行は成功事例にしない。

manifestは別途、全nodeのidentity/role/capacity/wake、方向付きlink/干渉関係、各traffic classとrate/burst、queue/Flash/CPU上限、fault時刻、同時group、replay条件、可観測event、admission目標も明示する。概略のnodesだけからトポロジーを推測しない。

## 4. 評価行列

| 軸 | 必須値・分類 |
|---|---|
| N | 2/3/5/10/16/50/64/100/128/256/512と各上限+1 |
| topology | direct、star、分岐Relay、chain、唯一bridge、共通上位Relay、partition |
| hop | 1/2/3/4。物理受入済み上限とは別に記録 |
| payload | 8/32/64bytes。bulkは別featureの512bytes/最大profile値を別集計 |
| traffic | 1対多、多対1、双方向、criticalとgroup/bulk同時、同時event burst |
| rate | nodeあたり3600/600/60秒間隔と、容量境界の下/一致/上。すべて成功するとは要求しない |
| burst | 1/4/16、同時復電、集中したgroup開始 |
| radio | 良好、非対称、burst loss、hidden terminal、busy継続、profile不一致、外部干渉 |
| sleep/clock | 常時受信、周期sleep、窓の境界、drift、再起動、時刻逆行・不明 |
| storage | 正常、full、GC中、torn write、commit-after-error、referenced read破損 |

全直積を無制限に回さない。各不変条件を狙う小さい決定的ケース、pairwiseの組合せ、受入profileの代表ケース、保持していない環境traceで構成し、除外理由をmanifestに残す。失敗後に都合のよい条件だけを採用しない。

## 5. 必須シナリオと追跡表

| ID | シナリオと判定 | 関連要求 |
|---|---|---|
| T01 | N/K/D/W、group4、target512、flow、counter領域の境界+1。容量拒否が無変更で返り、登録数を通信成功と混同しない | RV01、G01–G03 |
| T02 | queue遅延・Flash遅延・sleep・法規deferを別に注入。未計測0、RSSIだけの成功判定、RF失敗への混入を検出 | RV02、R02、M02 |
| T03 | probeの返信が1〜3秒の間に届く、途中/確定報告がreorderする。同じ完了sampleを二重計上しない | RV03、R02、M03 |
| T04 | 重複8点窓、70秒以上の古いsample、減力前の遅延報告、新session。新設定のgoodへ混入しない | RV04、M03–M04 |
| T05 | SF/BW/channel変更、最終ACK喪失、participantの一部reboot、旧recovery失効。再会できるか安全に停止する | RV05、M05–M09 |
| T06 | 小critical連続＋最大BULK、空credit、各class飽和、slow peer。大packet starvationとcritical boundの両方を検査 | RV06、M10–M13 |
| T07 | 一planがprepare/commit境界で60秒止まる。他flowのrenewalと配送を進め、旧leaseを不正に延ばさない | RV07、R06–R08 |
| T08 | 最大V/E、探索4096上限、RXと同時、snapshot更新、tie。bounded CPUと旧有効plan維持を検査 | RV08、R04–R06 |
| T09 | N台同時boot/Join/Resume/probe、IDを変える未認証flood、metric虚偽。global予算と既存criticalを保護 | RV09、R03、M14、G04 |
| T10 | Group START後のaddress再利用、device交換、binding変更、別domain移設。旧targetの仕事を新identityへ送らない | RV10、G06–G08、G12 |
| T11 | 最初の32targetをsleep/partition、33番目を起床。所有を保持してservice枠を再利用し、戻った相手は同じmessageを回復 | RV11、G09–G11 |
| T12 | retirementの通知/ACK喪失、gap、両端reboot、古いpacket、sequence overflow。過去成功の捏造や未処理inbox削除なし | RV12、G14 |
| T13 | v1とv2の無fault小規模比較、実node/security/Relayを通ることのinstrumentation。別実装の結果を誤って合算しない | RV13、R10 |
| T14 | weak directと良好Relay、逆方向のみ不良、共通Relay故障。hop/RSSIだけで選ばず、下りと証拠経路を検証 | R04–R08、M06 |
| T15 | 混雑→回復を反復し多数nodeが同じRelayへ寄る。hysteresis・予約容量・変更予算で振動を抑える | R05–R07 |
| T16 | Relay DATA/ACK/custody境界の停止・予定drain・唯一bridge撤去。source retentionと孤立診断を確認 | R08、G12 |
| T17 | Group INTENT/Core submit/ADMITTED/OUTCOMEの前後でcrash、commit後error、重複要求。IDを変えず再照合 | G07–G11 |
| T18 | 全DATA/Flash/queueが満杯。receipt、credit、clock、GCを進めるreserve。容量不足でも保管済みpayloadを保持 | M12、G03、G14 |
| T19 | drift、時刻逆行、wake遅延、preamble後の窓延長、Root再起動。half-duplexと時刻品質gateを維持 | M06–M08 |
| T20 | authority停止、planner budget終了、最適化OFF、delegation失効。旧planと所有を保護し、勝手な新Root/leaseを作らない | R07–R09 |
| T21 | protocol/profile/journal version混在、未知capability、更新中停止。旧機器の回復経路と明示拒否を検証 | C07–C10、M09 |
| T22 | 最大4-hop、loop、stale epochを混ぜたpath、偽prepared/applied/release、古いsession fingerprint。権限昇格なし | R07–R08 |
| T23 | PHY fragment0/最大/+1、矛盾duplicate、offset overflow、4再構成枠、timeout引延ばし。未認証による大buffer確保なし | M09、M14 |
| T24 | Kguradのrepo/config/cloudなしでイベント集約と双方向要求/結果の二サンプルを同じruntimeで動かす | 製品非依存、G15 |
| T25 | exact firmwareの実RF、USB再接続、root/relay復旧、子機を触らないspare Root交換、sleep復帰 | RV14、C08、R08 |
| T26 | Flash erase/program/commit/GC/retirement/nonce予約の狙った電源断、消費電流・stack/heap・起床時間実測 | RV14、C01–C04、M08 |
| T27 | 長時間churn、追加/撤去、low-battery、弱電界・干渉、台数×負荷×hop×sleepの宣言範囲。Flash/boot時間が無限成長しない | RV14、G01–G16 |
| T28 | README/STATUS/契約・実装・証拠・未実施が一致。CI skipをpass、設計上限を実測値、all_terminalを全成功と表示しない | RV14、C10 |

## 6. 指標と会計の整合

各logical messageについて、offered、admitted、admission拒否、active、要求証拠達成、terminal非成功、明示UNKNOWNを追跡する。同じlogical IDのretryや、複数Gatewayの同一受信を独立の配送成功として加算しない。

時間はsubmit→receiver stored、submit→receiver application accepted、submit→sender evidence committedを別々に記録する。Groupは最初の対象・p50/p95/p99対象・全対象・未完了対象を併記する。成功sampleだけのp99に加え、timeout、未完了、censoredの件数と期間を報告する。測定終了時のACTIVEをFAILEDへ書き換えない。

radioはDATA/probe/ACK/receipt/Join/plan別airtime、RX-on、CCA、guard、retry、全class/peerのservice ageを報告する。CPU/heap/stack、Flash bytes/erase、GC/reopen時間、session再activation費用、電力測定の条件も記録する。シミュレーションの推定電池寿命を実測寿命と表記しない。

ID/owned payload/要求証拠の保存不変条件には許容違反数を置かず、違反一件で不合格。性能不達は安全違反と区別し、profileの公開範囲を縮めるか最適化を無効化する。

## 7. 比較方法と採用基準

固定経路＋固定PHY、現行cost/power、ETX風単純方式、静的予約、競合方式、提案する混合適応を比較する。すべて同じoffered workloadを受け、queue/Flash/無線予算の上限をそろえる。提案方式だけに無料のACK、無限buffer、正確な時計、既知の将来faultを与えない。

調整用seed/traceと評価用seed/traceを分離する。評価は最低16seedとし、seedだけでなく環境traceそのものを保存する。policyが乱数を消費する回数によって外部干渉まで変わらないよう、外部環境とprotocol内部の乱数streamを分ける。

受入前にmanifestと基準をfreezeする。初期の採用基準は以下とする。これらは目標であり未達を隠さない。

- 安全不変条件違反0。適法性・時刻・認証・保管の全gateを通ること。
- 受入対象profileでは宣言した絶対latency/delivery目標を満たすこと。admission率、offeredあたり成功率、未完了を併記すること。
- 改善対象の事前指定ケースで、主要指標（sender evidence p95、総airtime、回復時間のいずれか一つを事前選択）をbaseline比20%以上改善すること。
- 安定・低負荷ケースでcritical p95がbaseline比10%超悪化しないこと。期限内到達率の許容非劣性幅は絶対1 percentage point以内とし、かつ絶対SLOを満たすこと。満たせない場合はその条件でbaselineを選ぶ。
- peer別の最大service ageと低優先度の進行を確認し、平均だけ改善するstarvationを採用しないこと。

比較はseed単位のpairedな結果と95%区間を報告する。messageをすべて独立試行と扱うことで区間を過度に狭めない。区間が基準を判定できるほど狭くなければUNKNOWNとし試行を増やす。単一seedの偶然や結果を見て選び直した主要指標を採用理由にしない。baselineが0件成功の場合は比率を捏造せず、絶対値と失敗内容を示す。

## 8. 実機受入の追加条件

実RF試験前にboard/antenna/周波数/出力/法規profileと停止手段を確認する。今回の文書作成はRF送信や機器再設定を許可・実行する作業ではない。既存の停止状態を変更しない。

小規模HILでPHY timing、半二重、driver/Flash遅延を校正し、simulationのどの値が実測かを記録する。その後に宣言対象の台数・配置・干渉・負荷で拡張する。3台の成功で64/512台のRF受入を代替しない。最大4-hopも設計上限と実機合格を分ける。

長期gateは、まず24時間のnative/model soakと反復再起動、次に受入profileでの7日以上の連続HIL/field候補運転を初期要件とする。これは一年運用の証明ではない。Flash部品の保証値・測定write/erase頻度・marginから寿命予算を別に算定し、より長期のfield evidenceを継続して追加する。日数だけ満たして、イベント数やchurnがほぼゼロの試験を十分としない。

## 9. この文書追加時点の実施状況

実施したこと：固定SHAのGitHubソースと契約・履歴を読み、RV01–RV14と要求・試験の対応を作成した。新規6文書を既存実装から分離したdocs branchに追加した。

今回未実施：ネイティブcharacterization、上記T01–T28、全体CI、sanitizers、target build、電源断、RF/HIL、性能benchmark、全checkoutのLOC gate。既存のActionsを起動しない方針を維持し、commit messageに `[skip ci]` を付ける。これをCI-greenやmain/PR #15のマージ承認にしない。

実装PRでは各testを `NOT_RUN / PASS / FAIL / UNKNOWN / NOT_APPLICABLE(reason)` で更新し、sourceと証拠へリンクする。仕様書を作ったことだけでチェックをPASSにしない。
