# 自動経路最適化仕様 v1

状態・基準SHA・共通不変条件は [README](README.md) に従う。これは次期実装の要求R01–R10であり、現行Coordinatorを完成扱いする文書ではない。関連レビュー：RV01–RV09、RV12–RV14。

## R01. 最適化の対象と保証境界

目的は、要求された最終証拠へ到達するまでの遅延・airtime・電力・変更費用を、認証、所有、法規、資源、半二重動作の制約内で改善することである。RSSI最大、hop最小、bitrate最大のいずれかを単独の目的にしない。

経路候補の採否は順序付きとする。まず権限と安全条件、次に容量と要求SLOの実行可能性を検査する。実行可能な候補内では、期限内証拠到達率の保守的な予測、証拠遅延、airtime、電池負担、変更回数の順に比較する。同値はcanonicalなnode/profile ID順で決め、同じsnapshotから同じ結果を得る。予測に必要な情報がない候補を「コスト0」として勝たせない。

全環境での大域最適、任意干渉下の到達、唯一のbridge撤去後の接続を保証しない。測定不十分な経路は `PROVISIONAL_PATH` としてbest-effortで探索できるが、十分な根拠を要するSLO受入には使わない。

## R02. 観測v2：意味・世代・時間を固定する

観測keyは `observer_identity, observer_membership_epoch, peer_identity, peer_membership_epoch, direction, bearer, phy_profile_id, tx_power_generation, session_fingerprint, plan_epoch` とする。全部を毎packetで送る必要はなく、認証済みcontextから復元してよい。ただしradio addressだけで履歴を別機器へ引き継がない。

試行には少なくとも `attempt_id, frame_kind, payload_size_bucket, enqueued_at, tx_started_at, tx_done_at, response_due_at, response_at, result, actual_airtime, observation_clock_quality` を対応づける。途中段階は上書き可能なprovisional telemetry、最終段階は閉じた一試行として一回だけ計上する。観測のACKは観測を保管した証拠に限り、Application配送の証拠ではない。

| 分離する量 | 処理 |
|---|---|
| DATA/probe/reply/最終受領証 | 成功分母・長さ・方向を混ぜない |
| enqueue拒否、法規defer、sleep、予定外RX | RF失敗分母へ入れない。利用者視点の遅延・拒否には残す |
| TX_DONE後の応答なし | 予定された受信・返信窓が閉じてから分類する |
| queue、RX待ち、Flash commit、CPU待ち | 実測値またはUNKNOWN。未計測を0で送らない |
| RSSI/SNR/CRC/CCA busy | profileと方向付きの補助情報。成功packetだけを観測する偏りを明示 |
| 再送後の応答 | 一意に対応できないRTTを新鮮な独立sampleとして使わない |

各keyは有限ring、試行数、成功数、時間分布、queue watermarkを持つ。全sampleの最古/最新時刻と最終化時刻を公開する。stale判定は最新時刻だけでなく、window幅・各sampleの有効性で行う。session/PHY/power世代変更後は別bucketへ切り替え、古いsampleは診断用に限る。

初期検証設定は一key最大32完了試行、通常評価は8以上の有効試行とする。8個だけで高信頼率を断定しない。改善用の独立窓を要求する場合はsample IDが重ならないことを検査する。window期間・最低sample数はversion付きpolicyに含め、測定密度不足時はconfidence低下として扱う。古いbucketの回収は配送履歴・権限fenceへ影響しない。

## R03. 隣接発見と探索費用

名簿と無線隣接集合を分け、名簿全件への常時all-to-all probeを禁止する。既存の認可済み通信からpassiveに学べる量を優先し、未知・失敗・予備経路だけを有限予算でactive probeする。観測可能な相手であることは中継能力の許可ではない。

受入profileに `N, E, D_by_role, active_probe_airtime, bootstrap_airtime, report_airtime, pending_observations` を必須にする。starのRootは多数の直接子機を持ち得るため、Leafと同じD上限を強制しない。初期設計上限はN=512、方向付きedge合計E=2048、Leaf候補D<=8、経路のactive1+backup最大2。Root/RelayのDは宣言されたmemoryとairtimeから別に決める。上限は収容保証ではない。

全探索はradio/domainの共通予算から課金する。安定時は報告を差分化・間引きできるが、失効、参加、重大な経路断、重要受領証を同じ抑制で消さない。未認証bootstrapにはpeer別とradio全体の両quotaを置き、IDを変えるだけで予算を増やせない。認証済みでも申告metricを法規・権限変更の根拠にしない。

## R04. 到達グラフと競合グラフ

到達グラフのedgeは `from -> to @ PHY/受信機会`。認可・role・epoch・MTU・wake・clock・保管容量を属性に持つ。上りが測れたことは下りの成立を意味しない。上りと下りは独立の経路・受信予定として検証し、逆順pathを無条件に流用しない。

競合グラフは、同一radio、同一受信先、half-duplex、hidden terminal、共有電源/Relay、未確認のSF/channel分離を表す。未校正の同時通信は競合するものとして扱う。周波数やSFが違うだけで空間再利用を許可しない。

一つの候補経路の概算は、各hopのDATA/reply/再送airtimeと待ち時間、保存時間、最終受領証の返送を含める。単純な比較用近似は `expected exchange airtime = exchange airtime / exchange success probability`。これは独立定常損失を仮定する近似であり、相関損失・retry上限・窓待ちを含む保証式ではない。p99をhopごとに足したり、少数sampleの成功率を掛けて全体保証にしない。

候補を選んだ後はPHY_MACのscheduler validatorで具体的な送受信機会を配置する。経路コストの低さだけでは `CAPACITY_ADMITTED` に進めない。Groupのwaveと既存flowを同じ競合資源へ合算する。

## R05. 有限候補探索アルゴリズム

新plannerは固定snapshotを入力とし、各source/targetに最大3本のloop-free経路を保持する。現行4-hop上限を維持する。以下を初期方式とする。

1. identityをdense slotへ解決し、source/targetと中継候補の権限、世代、clock、wake、MTUを検査する。edge走査のたびに全nodeを線形検索しない。
2. activeと前回backupを先に再評価する。単純なstale metrics、未知の下り、不足した保管枠を除外する。
3. 有限priority queueに部分pathを置き、同一node再訪とhop超過を拒否して候補を探索する。非加算の遅延/競合はこの探索だけで保証せず、完全path候補ごとにvalidatorで確認する。
4. 各候補のairtime、待ち、失敗依存点を評価する。最安候補だけでなく、source/target以外の共通Relay・電源・radio依存が少ない予備を選ぶ。独立性を測れない場合はUNKNOWNとする。
5. 既存通信を含む予定を組み、候補の差し替えを最大2回だけ局所改善する。全体を何度も収束するまで回さない。
6. budget終了時は検証済みの最良候補を返す。実行可能候補がない場合は旧有効計画を維持し、なければpartitionを返す。途中の未検証結果を適用しない。

初期計算profileは一plan探索あたり最大4096状態展開、候補3、最大4-hop、max2回の局所改善とする。これは完全探索・最適性の保証ではない。途中状態のmemory上限も宣言し、上限に達したら `SEARCH_BUDGET_EXHAUSTED` を返す。64展開ごとにcallerへ戻れるstep APIとし、実時間上限はtarget校正結果によりさらに小さくできる。RXやFlash予約を侵害しない。探索完了後、stage直前にsnapshotの権限・時刻・容量がまだ有効か再検査する。

## R06. 更新の安定性・公平性

障害修復と性能改善を別のqueueにする。認可失効、完全な経路断、radio故障は通常の改善待ちを省けるが、安全validatorとlease条件は省けない。品質改善にはflow別hold、改善幅、観測confidence、変更予算が必要。

初期比較profileは既存の20%改善閾値・2秒holdをbaselineとして保存し、新policy側では独立した完了観測窓による確認を追加する。最終の閾値は同一manifest群での感度分析後にfreezeする。別flowの変更が無関係なflowのholdを延長してはならない。

影響を受けたflowだけを再計算し、同じRelayへ一斉移行しない。移行先の予約済み容量を候補評価へ反映する。queueはround-robinと期限付きrenewal優先度を組み合わせる。到達不能な一件を永久に先頭へ置かない。優先度を上げても探索/制御のglobal上限は越えない。

## R07. 経路変更transaction

公開状態は `PROPOSED -> PREPARED -> COMMITTED -> RECONCILING -> EFFECTIVE`、準備中のみ `ABORTED`、有効経路の終了は `RETIRED` とする。各operationは `operation_id, authority_epoch, plan_epoch, source/target identity, path, membership/binding snapshot, schedule_digest, profile_version, prepare_until, valid_from/until, old_epoch` を持つ。node住所・session IDだけでは同一operationとみなさない。

| 境界 | 必須条件・永続化 |
|---|---|
| PROPOSED | 候補・要求・資源snapshotを検証し、operationを保存してからPREPARE |
| PREPARED | 各participantがrole、保管、clock、profile、scheduleを検証し、準備状態を保存した後のACK |
| COMMITTED | 全参加者の現在の準備証拠と旧経路のrelease/expiryを確認。commit前の最終ACK喪失で成功を推測しない |
| RECONCILING | 適用recordだけでlive動作を推測せず、hop/E2E context、予定digest、実効世代を照合 |
| EFFECTIVE | 全必須participantの照合完了。通知を失ったparticipantはquery/retryで同じoperationを回復 |
| RETIRED | 明示releaseまたは安全なexpiry。Core/Relayの未完了保管はこの操作で消さない |

同じIDで異なるdigestはCONFLICT。再起動後は永続phaseから再開するが、live proofは再取得する。timeoutだけで旧有効経路の権限を戻さない。rollbackは古いパラメータを新しい承認済みepochで再適用する操作であり、epochを減らす操作ではない。

初版の新transaction poolは最大4件。source/target、共用participantの変更可能state、radio受信予定・送信予約が競合するtransactionは同時activateしない。並行化の安全性をモデルで示せない組合せは直列に落とす。単にpendingを配列にするだけの変更は禁止。prepare timeout中も、競合しないlease renewalと既存deliveryは進める。

## R08. 突然故障・Relay撤去・Root不在

通信状態を `HEALTHY, SUSPECT, REPAIRING, PARTITIONED` とし、membershipのREVOKEDから分離する。受信予定があった機会での不応答を数える。sleepや法規待機を故障検出の失敗分母に混ぜない。

局所修復は同一authority/plan世代で事前認可されたbackup edgeとradio機会だけを使える。edge集合はDAGとして検証し、古いepochとの混成を禁止する。activeとbackupは同時に自由送信するmulti-parent権限ではない。下りのattachmentと受信予定も別に照合する。

予定撤去は依存flow、孤立予定peer、pending custody、代替容量をsnapshot化し、新規custody受付を止め、移行・drain・確認後にREADYとする。突然消えたRelayは、残存する送信元の同じmessage IDから回復する。唯一のbridgeがなくなった場合は `TOPOLOGY_INSUFFICIENT`。新しい電波経路を捏造しない。

Root/Coordinator不在時は期限内の既存委任計画だけを実行する。leaseを自動延長しない。Root交換はDEPLOYMENT_LIFECYCLEのissuer/generationと旧Root撤去条件を維持する。新Rootの存在は古い孤立Rootの送信停止証拠ではない。

## R09. API・診断・公開資源

新しいAPI名は実装時に確定するが、責任と効果は以下に固定する。

| 操作 | 入力→出力 | 効果 |
|---|---|---|
| observe | 認証済み試行/event→統計更新結果 | 測定だけ。membership/所有/routeを変えない |
| plan_begin / plan_step | immutable snapshot・要求・budget→候補/継続/不足理由 | 分割計算。RF送信も永続権限変更もしない |
| validate | 候補＋現在の資源snapshot→採否と内訳 | 満たさない条件を列挙。未計測はUNKNOWN |
| stage / reconcile | 検証済み候補・operation ID→phase | commit-before-effect、重複に冪等、曖昧書込みでreopen |
| prepare_remove / query_remove | identity＋期待世代→依存先・保管・準備状態 | 未確認をREADYへ昇格しない |
| explain | operation/snapshot ID→候補、採否理由、観測期間、費用、次回機会 | plaintext/鍵を出さない有限診断 |

配置・arenaはopen時に上限付きで確保し、packetごとの無制限allocationを禁止する。失効・stale edgeのGCとdurable plan fenceの退役は別処理。GCによって古いnode addressを別identityとして再有効化しない。ソート・tie-break・時間量子・overflow時の拒否をテストで固定する。

## R10. 受入

最低限T02–T09、T13–T20、T25–T28を実施する。固定経路、現行ETX風コスト、提案方式を同じ実装接続simulator・manifest・seedで比較する。改善しない環境ではbaselineを選べること、重要通信の退行を観測して停止/回復できること、未配送を成功に変えず説明できることを完了条件とする。
