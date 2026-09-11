# 既存コードレビュー：適応制御と大規模通信への準備状況

調査日：2026-09-09。対象は `MOVEI144/Ninlil`、PR #15 の `a950f7cb4628fe8b9c579794ab4bcb04c9b1f05d`。main の `9b00c09` ではなく、未マージの自律ノード実装を含む固定スナップショットを読む。実装・試験・設計・実機受入を区別する。

## 1. 結論とレビューの限界

永続配送、暗号化境界、参加状態、経路の世代・lease、Relay custody の分離は維持すべき資産である。一方、現在の小規模αをそのまま大規模適応ネットワークと扱うことはできない。主な不足は「観測の意味と鮮度」「無線時間の配分」「複数経路変更の進行」「機器数と実行資源の分離」「配信対象の世代固定」「実際の自律ランタイムを用いた性能評価」にある。

本記録はソースを追跡した静的レビューであり、377変更ファイル・外部暗号実装の全行を独立監査したとの主張ではない。取得内容が切れた大きなファイルについては下記の範囲のみを根拠とした。新しいネイティブテスト、全体ビルド、ASan/UBSan、ESP-IDF、RF/HIL、電源遮断試験は今回実行していない。リポジトリ取得のためのローカル `git clone` は名前解決で失敗し、GitHub接続から固定版のファイルを読んだ。既存の73項目×4構成という記録は以前の検証であり、今回再実行した結果ではない。

指摘区分：**現行仕様の限界**は既存契約違反を意味しない。**追加検証**は実機障害を再現したという意味ではない。以下に新規の認証回避・データ消失脆弱性を断定する指摘はない。性能・規模・運用範囲を広げる前の設計課題として管理する。

## 2. 読み取り範囲と追跡した境界

全パスは上記SHAに固定する。[固定版のツリー](https://github.com/MOVEI144/Ninlil/tree/a950f7cb4628fe8b9c579794ab4bcb04c9b1f05d)から参照できる。差分のファイル一覧も確認したが、一覧にあるだけのファイルをレビュー済みとは数えない。

| 領域 | 主な読み取り対象 | 確認した境界 |
|---|---|---|
| 契約 | AGENTS、ENGINEERING_STANDARD、CODING_STYLE、FAILURE_MODEL、ARCHITECTURE、TESTING、STATUS、SOURCE_BUDGETS | 所有、有限性、失敗モデル、実装と受入の区別 |
| 経路計算 | `include/ninlil_network.h`、`src/ninlil_network_route.c`、`src/ninlil_network.c` | 観測受付、経路選択、stage/activate、restore/disconnect、retire |
| 自律ノード | `include/ninlil_node.h`、`src/ninlil_node_links.c`、`src/ninlil_node_radio.c`、`src/ninlil_field_plan.c` | probe、実TX、最終受領証、古い送信待ちframe、準備期限 |
| 経路適用 | `src/ninlil_node_routes.c` の先頭側 | PREPARE/APPLY/EFFECTIVE、live fingerprint、release、要求相関。ファイル後半全体の監査は未完了 |
| 無線制御 | `include/ninlil_airtime.h`、`src/ninlil_airtime.c`、`include/ninlil_radio_adapt.h`、`src/ninlil_radio_adapt.c`、`ports/esp32s3/ninlil_network_pump.c` | reserve、公平性の単位、再試行、出力変更、RX情報の受渡し |
| 物理port | `ports/esp32s3/ninlil_sx1262_radio.c` 1–270行 | 固定profile、modem設定、GPIO/owner境界。送受信処理の全体・法規適合の独立監査ではない |
| 暗号・中継 | `include/ninlil_secure.h`、`src/ninlil_secure.c`、`src/ninlil_routed.c` 1–290行 | nonce予約、認証前plaintext、replay、最終Core commit、hop ACK。EDHOC/vendor全体の独立監査ではない |
| 永続履歴 | `src/ninlil_collect.c`、`src/ninlil_maintenance.c`、DEPLOYMENT_LIFECYCLE | GC時の参照再構築、完了履歴の明示退役、pending保護。全journal backendの再検証ではない |
| 配信 | `include/ninlil_group.h`、`src/ninlil_group.c`、`include/ninlil.h` 1–260行 | START/ADMIT/OUTCOME/FORGET、対象固定、容量、Coreへの受渡し |
| 評価・履歴 | SIMULATION、REVIEW_RESOLUTION_2026-09-08、maintenance evidence README | 既存修正との重複回避、シミュレーターの非対象、以前の試験と未実施項目 |

## 3. 維持する実装上の性質

**認証と永続性。** `seal_channel()` は暗号化より先にcounterを予約し、予約失敗でsessionを閉じる。`unseal_channel()` はscratchへ復号し、認証成功後だけreplay bitmapと呼出側出力を更新する。これはwrapperを読んだ結果で、暗号ライブラリの安全性証明ではない。

**中継と最終配送の分離。** `ninlil_routed.c` の `receive_record()` は送信元で受けるhop ACKをE2E証拠へ昇格させない。`final_commit()` はCoreのdurable ingest成功後にlive replay状態を更新する。既知ciphertextの再確認でも現在のAEADとCore整合性確認を通す。

**復旧時の証拠を捏造しない。** Coordinatorのrestore/disconnectは過去のplanを保持する一方、live準備・適用・reconcile証拠をクリアする。route activationは旧経路のreleaseまたはlease expiryを要求する。`ninlil_node_routes.c` は隣接hop fingerprintと両端E2E fingerprintを照合する。

**送信待ちへの再検査。** pumpは送信直前に `ninlil_node_frame_current()` を呼ぶ。`ninlil_field_plan.c` は古いPREPARE/APPLY/EFFECTIVEを除外する。stale envelopeの破棄と、Core/Relayが保管している論理配送の削除は別である。

**収集処理。** `ninlil_collect()` はjournal置換の公開後に参照を再構築し、コピー件数・generation・型・保持内容を検査する。`ninlil_retire_completed()` は未完了作業を拒否し、履歴退役後に再openを要求する。これを無条件の履歴削除へ変更しない。

**以前の欠陥を再掲しない。** `REVIEW_RESOLUTION_2026-09-08.md` のR1–R9は過去の修正記録である。特にR8の大packet starvation対策が現在のairtime予約保持であり、その保持を単に削る変更は退行となり得る。

## 4. 指摘と必要な差分

優先度P1は大規模・適応profileを有効化する前の必須対応、P2は性能品質・検証の拡張を指す。現在のαを直ちに危険な製品と断定するランクではない。

### RV01 / P1：512台という定数は自律ネットワークの対応台数ではない

`NINLIL_NETWORK_NODES_MAX=512` に対し `NINLIL_NODE_MEMBERS_MAX=16`（selfを含む）、flowは32、radio power stateも16枠である。groupの最大512宛先も別の上限である。実際に端末を加入させて通信できる規模はこれらとstorage/CPU/RFを合わせて決まる。
`ninlil_node_config.counter_io` はmember indexごとに8KiBのslotを二つ要求する。この配置を512へ機械的に広げるとcounter領域だけで8MiBとなる（512×2×8192の設計上の算術。実測使用量ではない）。名簿件数、同時session数、隣接数、経路数、保管件数を独立に宣言し、profile単位で検証する。対応：G01–G05、T01。

### RV02 / P1：コスト式にqueue項があっても、現在の観測入力はゼロ

`edge_cost()` はairtime×attempts/deliveredにqueue時間を加えるが、`ninlil_node_links.c:report()` はqueue_usへ常に0を書いている。pumpのRXはRSSI/SNRを含むinfoを受け取るが、nodeに渡していない。現状を「混雑や実受信品質を観測して共同最適化している」と説明できない。送信待ち、TX、応答、Flash、sleep、法規待機を別々に測る。対応：R02、M02、T02。

### RV03 / P1：probeの報告開始と応答窓の終端が一致していない

`ninlil_node_links_step()` はTXから1秒で報告を開始し得る一方、返信は3秒まで受け付ける。8回目の応答が2秒で来る場合、1秒時点の7/8と2秒時点の8/8が同じ観測時刻で送られ得る。Coordinatorは同一timestampの異なる内容を上書き可能。これは読み取りに基づく到達可能なトレースで、今回RF再現は未実施。最終窓と途中経過を型で分け、途中値を経路の確定評価に使わない。対応：R02、M03、T03。

### RV04 / P1：8サンプル全体の年齢・出力世代を表現できない

steady probeは約10秒間隔なので8点の窓は70秒以上に及び得るが、鮮度判定は最新probe時刻に依存する。隣接する8点窓は重複する。`radio_adapt` の3回goodは24個の独立試行ではない。加えて測定にはPHY/power適用世代がなく、出力を下げた後も変更前の成功が窓へ残り得る。各試行のprofile/epochと時間を持ち、新設定の評価を旧設定の成功で代用しない。対応：R02、M03–M04、T04。

### RV05 / P1：出力調整はPHY/MAC全体の適応ではない

`ninlil_radio_adapt.c` は最大出力から開始し、鮮度・8回の成功数・30秒dwell・3dB刻みだけを扱う。SF/BW/CR/channel、受信予定、干渉、容量、payload長との交渉はない。Coordinatorも単一 `permitted_profile` を用いる。既存出力制御をbaselineとして保存し、新しい設定交換を別versionの契約にする。対応：M01、M05–M09、T05。

### RV06 / P1：公平性の単位と緊急通信の遅延上限を明示する必要がある

airtime schedulerの8:4:3:1は選択回数であり、消費airtimeの比率ではない。選択済みjobはcreditが貯まるまで順番を保持する。例えば400,000us/秒のbudgetで、空creditから400,000usのBULKを先に選ぶと、その後に小さいCRITICALを入れてもBULKの予約が先に満たされる。これはヘッダーに書かれた契約で、既存バグと断定しない。最大1秒のrefill待ちと送信中frame、その他待機をcritical SLOの判定へ含める。小packetを無制限に割り込ませて旧R8を再発させない。対応：M10–M13、T06。

### RV07 / P1：経路変更は全体で一件のpendingに直列化される

Coordinatorのpendingは一つで、tick/selectは他のpendingがあると進まない。prepare/commitの窓は最大60秒。部分的に到達しない一経路がある場合の、他の経路更新・lease更新の進行を大規模にはまだ証明できない。`last_change_ms` も全flow共通。安全のための直列化自体は合理的だが、sleep宛先を含む多数flowの可用性には不足する。bounded transaction枠と競合資源の排他、flow別hold、renewal優先度を設計する。対応：R06–R08、T07。

### RV08 / P2：計算量と無線仕事の時間予算がつながっていない

選択は4-hopの有限緩和でloopを拒否するが、edgeごとにnodeを線形検索するため概ねO(H×E×V)である。boundedであることと、MCU上で受信を妨げないことは別。高負荷時に全flowを同期的に再計算しない。dense slotの索引、差分対象flow、snapshot固定、計算の分割実行、期限内に旧有効計画へ戻る契約が必要。対応：R04–R06、T08。

### RV09 / P1：個別backoffだけではネットワーク全体の探索費用を制限できない

probe/reportはpeerごとに有限だが、送信コストをdomain/radio全体で予約する契約はない。既知peer全部を相互認証・probeする形を一般化すると二乗の関係数になり得る。bootstrap/controlは認可されたpowered Relay経由のbounded floodingで、unicast配送が成立する前にも必要である。この回復性を失わず、隣接候補の上限、探索のairtime予算、既知機器復帰と未知Joinの別枠を設ける。対応：R03、M14、G04、T09。

### RV10 / P1：group対象の無線アドレスだけでは世代付き配送契約が完結しない

group STARTはorderedなuint16_t対象集合を保存するが、device identity、membership/binding世代、service、payload、要求証拠をそのrecord自体に保持しない。現在の呼出側境界を含めず誤配送バグと断定はしない。ただし、将来の汎用大規模group ownerはpending対象をアドレスの新所有者へ再解決してはならない。対象identityと世代をSTARTへ固定し、Core submissionとの対応を永続化する。対応：G06–G10、T10。

### RV11 / P1：groupの32件枠と端末の休眠・到達不能を切り離す必要がある

global inflight上限32に対して、partition/sleep端末の配送は正当にACTIVEのまま残る。この32件が埋まると新規対象のpeekがCAPACITYとなる。タイムアウトを成功・失敗へ変える対処は禁止。保管責任はそのまま、実際の送信service枠だけを休眠・再割当できる新しい永続状態が必要。`finished` は全対象がterminalになったという意味であり、全成功を意味しない。対応：G09–G13、T11。

### RV12 / P1：journal GCは永久の重複排除契約ではない

GCは現在保持している記録を安全に集め直す処理であり、任意の過去messageを無限に覚える仕組みではない。完了履歴の退役APIを、自動的な無制限dedupeの代用に使わない。世代・retired-through fence・保持期間と、遅延packet・未受理application inboxの関係を規定し、既存formatとの互換を試験する。対応：G14、T12。

### RV13 / P1：現行シミュレーターを大規模自律網の性能証拠にできない

SIMULATION.mdのv1は2–5台、実Core+POSIX journal、10ms刻みのstatic slot、同時に一frameである。collision/hidden terminal、非対称link、CAD/LBT、clock drift、sleep、secure session、Join、Relay、適応制御は明示的に対象外。journalの実処理時間も仮想時間へ課金されない。この道具は有用だが、仕様の性能判定には実際のnode/relay/security/schedulerを接続した別のversionが必要。対応：T13–T24。

### RV14 / P1：αの記録と運用範囲の公開を分ける

maintenance記録は失敗も残しており、その扱いは維持する。親機交換の核心HIL、timed electrical power cut、消費電流、長時間・弱電界・干渉・多数台は未受入。ローカルにある生ログのhash/所在を読んだことは生ログの独立再検証ではない。今回の設計commit、CodeRabbitのskip status、過去のCTestsのいずれもfield-ready根拠にしない。対応：T25–T28。

## 5. 実装前の再現・反証手順

以下は今後のネイティブ回帰試験への入力であり、今回の実行結果ではない。

| トレース | 入力と期待する確認 |
|---|---|
| C01 / RV03 | 8点目の実TXをt=0、報告可能をt=1000ms、認証済み返信をt=2000ms、終端をt=3000msにする。途中7/8と確定8/8を識別する |
| C02 / RV04 | 変更前profileの成功窓を変更後に遅延到着させる。新profileのgood証拠へ加算されないことを新仕様で要求する |
| C03 / RV06 | 空credit、budget400000、BULK airtime400000をnextで選択後、CRITICAL airtime1000を追加する。t=2500usのcreditが1000あっても現在はBULK待ちとなる。新方式は両classの待ち上限を検査する |
| C04 / RV07 | 一つのplanの最終participantを60秒到達不能にし、別flowのrenewal/critical配送を同時に発生させる。旧leaseを不正延長せず、独立flowの進行を測る |
| C05 / RV10 | group START後、未送信targetのaddressを別identityへ再割当する。新deviceへの自動配送を拒否する |
| C06 / RV11 | 最初の32対象を休眠・partitionにし、33番目を起床・到達可能にする。ownershipを消さずに到達可能対象へserviceできる新状態を検査する |

## 6. 次の仕様への対応

[共通契約と実装順](README.md)、[自動経路最適化](ROUTING.md)、[PHY/MAC最適化](PHY_MAC.md)、[大規模1対多](ONE_TO_MANY.md)、[受入・ベンチマーク](ACCEPTANCE.md)をこのレビューの差分設計とする。実装変更は別PRで行い、この文書commitでM1〜M6やIssue #14を完了扱いにしない。
