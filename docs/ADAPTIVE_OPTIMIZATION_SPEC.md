# 適応型通信仕様 v1 — 実装前レビューと共通契約

日付: 2026-09-09。状態: **実装提案・受入条件を定義する仕様**。実装済み、安定版、性能保証を意味しない。

基準ソースは PR #15 の `a950f7cb4628fe8b9c579794ab4bcb04c9b1f05d`、基準 main は `9b00c09b4209a1b5f15336d223a6cbe0333382c9`。仕様追加によって PR #15、M1〜M6、Issue #14 の受入を完了扱いにしない。以下の MUST / MUST NOT は**これから実装する profile の要件**であり、既存αに遡及して実装済みと主張するものではない。

## 1. 読む順序と権限

1. [既存コードのレビューと再現結果](../reviews/2026-09-09-adaptive/README.md)。確認済み挙動、仕様上の制約、未検証を分ける。
2. 本文書: 共通の責任・資源・観測・受入。
3. [自動経路最適化](ROUTE_OPTIMIZATION_SPEC.md)。経路候補、混雑、代替経路、撤去、更新の安全性。
4. [PHY/MAC自動最適化](PHY_MAC_OPTIMIZATION_SPEC.md)。無線設定、受信予定、airtime公平性、復旧。
5. [大規模1対多・多対1](LARGE_SCALE_FANOUT_SPEC.md)。容量、参加集中、宛先別証拠、profileと段階試験。

[FOUNDATIONS](FOUNDATIONS.md)、[FAILURE_MODEL](FAILURE_MODEL.md)、[P0契約](P0_IMPLEMENTATION.md)、[設置ライフサイクル](DEPLOYMENT_LIFECYCLE.md)の安全条件を維持する。旧[ADAPTIVE_NETWORK_CONTRACT](ADAPTIVE_NETWORK_CONTRACT.md)はW01/W02の歴史的なstatic-direct sliceとして残す。本書群が追加する範囲はW03/W07/W08/W12、およびそれらに必要なW09/W10/W11との差分である。既存のSecure Linkや配送Coreを作り直す指示ではない。

Ninlilは製品非依存とする。KGuardの現場、トイレ、DB、クラウド、QR、UIを必須にしない。製品が通信要求と参加権限を渡し、Ninlilが共通の経路・無線制御を実行する。Coordinator/Authorityは論理役割であり、別プロセスやMini PCは必須でない。

## 2. 現在地 — 読んだコードが支える範囲

| 現行実装 | 確認できた意味 | 新仕様で必要な差分 |
|---|---|---|
| `ninlil_node` | 自律ownerのmember上限16、edge64、flow32 | global membershipと常駐neighbor/sessionの分離 |
| `ninlil_coordinator_select` | directed edgeのairtime×attempts/delivered＋queue、4-hop上限、20%改善と2秒hold | 要求・干渉・容量・双方向証拠を含む候補評価 |
| `ninlil_node_links.c` | 8-probe rolling window、queue報告は0 | 実DATA/receipt・待ち時間・原因・profile別観測 |
| `ninlil_airtime` | 32件queue、全体token credit、8:4:3:1の送信件数配分 | airtime単位の階層公平性と緊急待ち上限 |
| `ninlil_radio_adapt` | 許可範囲内の3 dB TX power減少/最大値復帰 | SF/BW/CR/channelの合意付き変更と評価 |
| `ninlil_group` | 固定したshort-address集合の端末別展開 | stable identity/世代、証拠別集計、休眠端末の非閉塞化 |

これは機能の否定ではない。特に「512」という上位モデル上限を、自律ランタイムの512台実機受入に読み替えない。基準コード・関数・検証の詳細はレビューに固定する。

## 3. 不変条件

| ID | 必須条件 |
|---|---|
| A01 | 認証、参加許可、到達性、容量受入、運用可能を別事実として返す |
| A02 | `LOCAL_ACCEPTED`、TX_DONE、hop custody、REMOTE_STORED、APPLICATION_ACCEPTED、業務結果を混ぜない |
| A03 | 最適化、queue圧力、撤去、session入替で最後の未完了payloadや必要な証拠を捨てない |
| A04 | route/session/profile変更後も論理message ID、宛先identity、要求証拠、期限を保持する |
| A05 | 再起動後のsession、plan準備/APPLIEDのlive証拠を永続記録だけから復元しない |
| A06 | region/board能力、認可、clock品質、Flash/RAM、half-duplexを費用関数の重みで相殺しない |
| A07 | 旧送信権のreleaseまたは確実なexpiryより先に排他的な新送信権を有効化しない |
| A08 | timeout、経路消失、実験終了、再送予算枯渇だけから成功/恒久失敗/取消を作らない |
| A09 | 全queue、table、探索、handshake、reassembly、再送、診断、保存履歴を有限にする |
| A10 | optimization OFF/計算timeout/Coordinator停止でも受入済みの所有記録を壊さない |
| A11 | 電池LeafはRelayにしない。参加許可をRSSI、発見packet、予測結果から生成しない |
| A12 | 実装試験、独立モデル、target build、HIL、field、規制上の確認を別に記録する |

## 4. 共通入力と結果

`TrafficContractV1`は登録時の小さな値型とする。各DATAへ全項目を繰り返し載せない。

| 項目 | 型・単位と意味 |
|---|---|
| contract_id / version | 64-bit ID / 16-bit version、domain内で識別 |
| service / direction / class | 既存serviceとtraffic class。上り/下り/要求応答を明示 |
| maximum_payload_bytes | 32-bit。利用する小message/bulk profileとの整合必須 |
| required_evidence | 既存REMOTE_STOREDまたはAPPLICATION_ACCEPTED |
| expected_rate_millimsg_per_s / burst_messages | 非負固定小数rateと32-bit burst上限 |
| latency_target_ms / completion_ratio_ppm | 最適化SLO。messageのabsolute deadlineとは別 |
| maximum_deferral_ms | 明示的に許された待機。無指定は強いSLOなし |
| wake_policy_id / retention_policy_id | version付き有限profileへの参照 |
| allowed_profile_set / hop_limit | board・region・authorityとの積集合で制限 |

未指定workloadはBEST_EFFORT。登録成功だけでradio容量を予約したことにしない。`ADMITTED / BEST_EFFORT_ONLY / CAPACITY_INSUFFICIENT / PATH_UNAVAILABLE / CLOCK_UNCERTAIN / PROFILE_UNSUPPORTED / POLICY_DENIED`を理由とともに返す。容量不足の代案は要求者に返し、センサー頻度、宛先、証拠を無断で減らさない。

`DecisionV1`は入力snapshot ID、policy version、候補と棄却理由、推定値の信頼度、採用plan ID、次回評価時刻を返す。これらは通信診断であり、既存delivery outcomeを置換しない。

## 5. 観測契約 O1

観測のkeyは `(authority_generation, boot_id, from_identity, to_identity, membership/binding epochs, direction, bearer, phy_profile_id, tx_power, frame_kind, sample_window_id)`。時刻だけで新しい独立試行と判断しない。

| イベント | 記録する事実 |
|---|---|
| QUEUED / ELIGIBLE | owner受付と送信可能になった時刻、queue byte/count |
| TX_BEGIN / TX_DONE | 実PHY、実長、airtime、attempt ID。queue受付をTX数に加算しない |
| NOT_TRANSMITTED | CCA_BUSY、sleep、法規待機、権限失効、queue-fullを区別 |
| TX_AMBIGUOUS | timeout/IOで実送信不明。費用は保守的に計上、配送成否は不明 |
| RX_VALID / RX_REJECTED | RSSI/SNR/CRC等と出所。未認証値を認可済みmetricにしない |
| HOP_STORED / FINAL_STORED / APPLICATION_ACCEPTED | どの永続境界を誰が確認したか |
| RX_OPPORTUNITY | 受信予定、sleep、clock不確実、窓のmissを区別 |

実際に期待した応答機会があったattemptだけをRF品質分母に入れる。混雑・sleep・法規待機は利用者側SLOの遅延には含めるが、無線到達失敗と混ぜない。RSSIは成功RXだけに偏るので成功確率の代用にしない。

collectorはwindowごとの有限集計を使い、同じattempt/receiptや複数radio headによる同一受信を重複加算しない。再送で対応関係の曖昧なRTTは別bucketにする。未完了/censoredの件数を分位点から消さない。profile/route変更時は古い窓を分離する。

最初の実装はwindow当たり最大64 attempt、各key直近4 window、EWMA・sample count・最終時刻を保持する。これは候補profile値であり、全peer×全PHYの直積を確保しない。table満杯では未使用候補の計測を拒否/置換できるが、membership fenceや所有記録はLRU削除しない。raw traceは任意observerへ有限queueで出し、drop数を表示する。

## 6. 容量判定 C1

平均bitrateだけで受け入れない。各干渉領域・radio executorについて、対象horizon H内のDATA、hop制御、E2E receipt、再送、Join/Resume、同期、probe、設定変更のradio占有時間を合算する。TX airtimeとRX/CCA/切替/guardを別勘定にし、法規のTX上限へRX時間を誤加算しない。

`U_radio(H) = occupied_radio_time(H) / H` と `TX_budget_used(H)` の両方を検査する。単一radioの全セルは重複不可。未知の干渉関係は競合として扱う。初期の通常admission上限は利用可能なradio時間の60%をLAB候補とし、残り40%は復旧・burst・推定誤差のheadroomとする。60%を法規上限または成功保証と呼ばない。

rateだけでなくburstをhorizon 1/10/60/600秒で検査し、sleepでそれ以上待つ場合は宣言された最大deferralを追加する。交換に必要な最小packetが窓に収まらないprofileは開始前に拒否する。切断時の無期限保持に有限ストレージで無条件受入を約束しない。

推定器は過少推定を検出したら新規SLO admissionを止め、既存所有の進行を優先する。再配置や明示的な再契約を提案するが、既存messageの証拠/期限を下げない。

## 7. 版管理・実装境界

新O1観測、新plan、新group recordは既存NP1/NP2やgroup APIのフィールドへ暗黙に押し込まない。実装PRで公開ABI、control wire、journal、profile、manifestの各version変更を個別宣言し、旧readerのfail-closedとmixed-version試験を必須にする。

共通APIの候補は `observe_transport`、`register_traffic_contract`、`plan_step`、`validate_plan`、`stage_plan`、`reconcile_plan`、`query_health`。名前はABI予約ではない。すべて所有権/lifetime、最大仕事量、同期commitの有無、エラー後の状態を公開する。hot pathでpacketごとのheap allocationを追加しない。

Coreは配送所有、Network Controlはplan/membership、Coordinatorは容量と候補、Bearerはradio executor、Authorityはgrant、Product Adapterは業務判断を所有する。各層が別々の自動最適化器を作って競争しない。決定的なpolicyとvalidatorを先に実装し、ML/RLや汎用plugin frameworkは本仕様に含めない。

## 8. 実装順と完了判定

| 段階 | 変更範囲 | 先に必要な証拠 |
|---|---|---|
| S0 | このレビューの追跡、legacy profile能力表示 | 16/32/512等の境界と非対応理由がテスト可能 |
| S1 | O1観測・実runtime接続simulator | 重複/時間/原因/queue計測の回帰試験 |
| S2 | C1容量判定・airtime scheduler | mixed長、per-peer、緊急/CONTROL進行、飽和試験 |
| S3 | 制約付きroute候補とplan更新 | route/lease/clock/撤去のmodel探索とE2E試験 |
| S4 | 合意付きPHY変更・回復profile | 最終ACK喪失、混在version、sleep復帰の試験 |
| S5 | sparse state・group v2・多数台 | 32→64→128→256→512のhost証拠、実機台数は別記 |
| S6 | profile別HIL/field受入 | 下記matrixと未実施事項の明示、完全な適用CI |

各段階を独立したレビュー可能なPRにする。この仕様コミットはproduction codeを変更しない。50,000非空行のproject ceilingを増やさない。新source/doc/testも予算に含め、全checkoutのsize gateは実装/merge前に実行する。この作業環境では全checkoutのsize gateは未実行である。

## 9. 共通受入matrix

試験manifestにはsource SHA、seed、policy/profile、node/role/radio数、link方向/干渉、PHY、rate/burst/長さ、睡眠、故障時刻、時計誤差、Flash条件、期待結果を固定する。calibration用seed/traceとholdoutを分ける。

| Gate | 必須内容 |
|---|---|
| G01 | A01〜A12を対応づける。成功捏造、所有喪失、認可/nonce違反は1件でも不合格 |
| G02 | loss/duplicate/reorder/corruption、容量上限+1、古い世代、unknown version |
| G03 | hidden terminal、上下非対称、bridge撤去、共通故障点、全台復電、Coordinator停止 |
| G04 | plan更新の全message欠落、最後のACK欠落、各永続commit前後のcrash |
| G05 | fixed conservatively configured baseline、現行policy、経路のみ、MACのみ、統合を同一負荷で比較 |
| G06 | offered/admitted/rejected/active/各証拠/terminal/unknownを保存し、未完了込みでSLOを評価 |
| G07 | CPU/step最大、RAM watermark、queue、Flash write/erase、reopen時間、radio-on、制御airtime |
| G08 | GCC/Clang strict、両sanitizer、static analysis、fuzz、package、target buildを正確なSHAで実行 |
| G09 | HIL結果をtarget buildやhost simulationで代用しない。電源断・電流・長期運用は独立gate |

評価seedは最低30、各セルで最低10,000 offered messageを初期研究基準とする。小さいモデル探索は全状態列挙を別に行う。この回数だけで99.99%等の信頼性を宣言しない。無故障率の信頼区間、試験間相関、未完了率を併記する。

初期LAB採用条件は「固定baselineがSLOを満たすholdoutで、CRITICALの期限内達成率が0.1 percentage point超悪化しない、p99が10%超悪化しない」。不確実性が差を覆う場合は合格でなく追加試験とする。改善対象ではevidence p95を10%以上、またはevidence当たりairtimeを15%以上改善する候補値を使う。全環境で改善を強制せず、退行する条件ではbaseline維持/OFFを正しい結果として認める。

## 10. 外部一次資料の使い方

本書群の数値、API、profile、受入値はNinlil向けの設計提案である。外部資料の規格値やNinlil実測ではない。

- [RFC 6719](https://www.rfc-editor.org/rfc/rfc6719): 加算metricとhysteresisの考え方。RPL互換を主張しない。
- [RFC 9030](https://www.rfc-editor.org/rfc/rfc9030.html): 経路と送受信予約の責務分担の参考。IEEE 802.15.4のslot時間をLoRaへ流用しない。
- [RFC 8480](https://www.rfc-editor.org/rfc/rfc8480.html): 予約transactionの不一致検出/修復の参考。Ninlilが6Pを実装するという意味ではない。
- [RFC 6298](https://www.rfc-editor.org/rfc/rfc6298.html): 再送で曖昧なRTTとbackoffの参考。TCPのRTO定数は採用しない。

公開資料は2026-09-09に確認した。地域別radio条件の法的適合判定はこの仕様の対象外であり、正確な機器/地域/profileのレビューを送信有効化前に要求する。
