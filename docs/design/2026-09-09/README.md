# Ninlil 適応通信・大規模配信仕様 v1

2026-09-09 / 設計レビュー用。実装基準：`a950f7cb4628fe8b9c579794ab4bcb04c9b1f05d`（PR #15）。この仕様は実装済み機能や公開ABIではなく、次の実装が満たす要求を定める。基準実装を読んだ[レビュー](CODE_REVIEW.md)を先に参照する。

| 文書 | 内容 |
|---|---|
| [CODE_REVIEW](CODE_REVIEW.md) | 読み取り範囲、維持する性質、RV01–RV14、追加試験 |
| [ROUTING](ROUTING.md) | 観測、経路候補、共同容量判定、障害修復・安全な変更 |
| [PHY_MAC](PHY_MAC.md) | PHY交渉、radio時間、公平性、出力調整、回復用通信 |
| [ONE_TO_MANY](ONE_TO_MANY.md) | 台数・資源、世代付き対象、永続fan-out、休眠と再開 |
| [ACCEPTANCE](ACCEPTANCE.md) | 実装接続シミュレーター、反証シナリオ、性能・実機受入 |

## 1. 採用する責任境界

Ninlilは製品非依存のC11通信ランタイムである。Kguradは利用側の一つであり、現場・トイレ・表示板・業務DB・製品の安全判断を本仕様へ取り込まない。Linux、Python、SQLite、特定Host/クラウドを必須にしない。既存のCore、Join、secure、Relay、Coordinatorを全面置換せず、明示的な呼出しとcallbackで拡張する。

| 所有者 | 責任 | 所有しないもの |
|---|---|---|
| Delivery Core | message ID、outbox/inbox、要求証拠、再起動・重複・期限 | 経路スコア、業務上の成功 |
| Authority adapter | stable identityと参加権限・失効・世代 | radio資源の存在を保証すること |
| Coordinator | 観測snapshot、経路・通信予定、容量判定、変更の調停 | 勝手な参加許可、Application payloadの解釈 |
| Node / Relay | 有効な委任計画の実行、隣接状態、有限な保管と局所修復 | 新しいauthorityの推測、最後のpayloadコピーの黙示破棄 |
| PHY/MAC port | 半二重radio、時刻、合法profileの強制、実TX/RX | TX_DONEから遠隔保存を推論すること |
| Group owner | 固定対象snapshotと論理配送の対応、進行・結果 | quorumを製品の物理成功と解釈すること |
| Product adapter | 通信要求、payload、業務冪等性、Application受理 | 共通の経路/MACアルゴリズムの再実装 |

これらは論理的責任であり別プロセスを要求しない。同じ組込み実行ownerに同居してよい。高容量profileで外部Coordinatorを使う場合も、無線側が独立に権限・lease・法規・保管を検証する。

## 2. 全仕様に適用する不変条件

**C01** ローカル受付、Link受付、TX_DONE、受信、hop custody、REMOTE_STORED、APPLICATION_ACCEPTED、業務結果を区別する。必要な永続commitより先に回復不能な外部効果を実行しない。
**C02** 経路・session・PHY変更、retry、Groupのservice休止を越えて、同じ論理messageのIDと元の要求を保持する。各attemptの暗号counterは別であり巻き戻さない。
**C03** 受領証不着、sleep、再送予算終了、lease切れ、探索失敗から成功・恒久失敗を推測しない。期限を持つattempt済み配送の曖昧性は既存P0契約のまま扱う。
**C04** 各table、queue、history、探索、plan transaction、reassembly、retry、仕事量、Flash使用量に上限と超過時の戻り値を持つ。保管済みデータの削除を混雑制御に使わない。
**C05** Relayは認可された給電ノードだけ。最終宛先の認証済み証拠を中継者が代替できない。Application plaintextを中継へ公開しない。
**C06** 一つのradioの同時TX/RX・複数PHY同時受信を仮定しない。異なるSF/channelを使ったという理由だけで干渉しないと扱わない。
**C07** ランタイム単調時刻、予定用ネットワーク時刻、再起動安全な期限/lease時刻を別の品質付き値で扱う。不確かな時計をゼロとして有効化しない。
**C08** authority/membership/binding/plan世代を区別する。新世代を知ったことは分断された旧Rootの物理的停止証拠ではない。Root交換の既存手順と外部fencingを守る。
**C09** 自動最適化OFF、計算予算切れ、Coordinator再起動時も既存の保管責任を維持する。有効な旧計画がないときは安全な回復通信か保管に戻る。
**C10** 確認できない測定はUNKNOWN/未実施でありゼロコスト・PASSではない。設計目標、モデル、native test、target build、HIL、field profileを区別する。

## 3. 共通の要求・状態モデル

通信要求は `service_id, direction, max_payload, required_evidence, class, expected_rate, burst_bound, latency_target, delivery_target, maximum_deferral, wake_policy, retention_policy` とする。所有者は値・単位・観測期間・要求versionを保存し、未宣言の負荷はbest-effortとして扱う。latency targetは最適化の目標であり、Coreのabsolute deadlineを書き換える権限ではない。

状態軸は `AUTHORIZED / PATH_KNOWN / CAPACITY_ADMITTED / PLAN_EFFECTIVE / DELIVERY_PROGRESS` を独立に返す。Joinしただけで容量受入済みとしない。候補経路の発見だけで配送可能としない。大規模groupの保存受付と、今すぐ全対象へ送信できることも分ける。

容量判定は同一snapshotに対して、DATA、hop ACK、最終受領証、Join/Resume、計画更新、探索、再送、RX/wake、Flash/CPU、guardを合算する。PHY担当が高速化できると判断しても、経路側が受信予定を置けなければ不採用。Groupが全対象を保管できても、criticalの既存予約を壊すwaveは投入しない。

戻り値は意味として `ADMITTED, DEFERRED, UNSUPPORTED_PROFILE, TOPOLOGY_INSUFFICIENT, CAPACITY_PRESSURE, CLOCK_UNCERTAIN, STALE_OBSERVATION, POLICY_DENIED, STORAGE_FAULT` を分離する。既存 `NINLIL_ERR_*` の数値を無断で変更しない。新しい診断構造で理由・支配資源・次回機会を添える。

## 4. 有限profileと性能宣言

`N` はRootを含む登録済み機器数、`K` は同時に保持するsession数、`D` は測定対象の直接隣接数、`W` は同時にserviceする配送数とする。これらを一つのnode-count定数へまとめない。

| profile名 | 用途 | 状態 |
|---|---|---|
| BASELINE16 | 既存の16-member、小message、固定PHY/現行scheduler | 既存αとの比較基準。field認定を追加しない |
| STAR64-EXPERIMENTAL | 64台までを対象とする試験profile、N/K/D/Wを別管理 | 新実装と受入試験が揃うまで有効化不可 |
| DOMAIN512-EXPERIMENTAL | 最大512台、最大4-hop、複数Relayでの容量探索 | 性能保証・ESP32単体対応の宣言ではない |

上限512の最大fan-outは、Root発なら511対象である。Group APIの512 target配列という設計上限と混同しない。さらに多い台数は明示CAPACITYとし、暗黙の動的拡張をしない。

公開profileは台数だけでなく、board/clock/storage/RF、payload/rate/burst/hop/sleep、要求証拠、遅延・到達率、同時group、RAM/Flash/CPU、再起動・故障回復の範囲を記録する。必要項目が未測定のprofileは実験用のままにする。設定者へ無線調整を丸投げせず、成立しない要求には理由と成立可能な代替条件を返す。製品の同意なく頻度や意味を変えない。

## 5. 互換性と変更の扱い

API、wire、journal、resource profile、測定schema、policyを別versionで管理する。旧 `NP1/NP2` や既存group recordにフィールドを黙って足さない。新featureはcapabilityと明示profileの両方でopt-inとする。未知の必須featureは拒否し、既存仕様でpreserveすべき未知bitはその契約を維持する。

この版は論理データ構造・状態遷移・上限・受入条件を確定する設計案で、未割当のwire opcodeやjournal type番号を既存ABIへ予約しない。実装PRではcodec表とmigration fixtureを追加し、ここに定める240-byte上限等の予算を自動検査する。互換gateを通らない機能はon-airで使えない。

## 6. 実装順と完了判定

| 段階 | 成果物 | 次段階へ進む条件 |
|---|---|---|
| S0 | このレビュー、現行挙動のcharacterization test、契約対応表 | RV03/RV04/RV06/RV10/RV11のトレースが再現・分類できる |
| S1 | 観測v2、原因別統計、実node接続simulation、baseline manifest | 最適化OFFで同じ配送安全性と再現性、測定の水増しなし |
| S2 | 隣接上限・snapshot・有限候補経路・容量validator・変更transaction | R01–R10、故障/旧lease/最終ACK喪失の試験合格 |
| S3 | airtime公平性とPHY予定交渉、共通回復機会、出力sample世代 | M01–M16、旧大packet starvationの再発なし |
| S4 | 世代付きGroup、durable intentとCore対応、service休止、64→512容量探索 | G01–G16、対象交換・partition・一斉復帰で所有保存 |
| S5 | 実機校正・独立レビュー・性能profile公開 | ACCEPTANCEの該当gateを満たし、未対応条件を明示 |

設計・simを先行することは、未受入機能を実機で有効化する許可ではない。暗号・保管・Join・法規の受入を性能改良で置き換えない。全変更を一つの巨大実装PRにせず、上記段階を依存関係付きでレビューする。

50,000 nonblank first-party linesの既存hard ceilingは不変。本仕様も計上対象。実装で予算に近づいたら重複した計測・controller・文書を整理し、上限や検査を緩めない。今回の文書追加後の全体LOC gateは未実行であり、マージ前の確認事項とする。

## 7. 既存設計・一次資料との関係

Issue [#14](https://github.com/MOVEI144/Ninlil/issues/14) の製品非依存性、I01–I14、W01–W12を引き継ぎ、実装との差分をこの版で具体化する。既存FOUNDATIONS/P0/FAILURE_MODELの保証を弱めない。衝突した場合は実装PRのADRで差分を明示し、既存契約を黙って置換しない。

[RFC 6719](https://www.rfc-editor.org/info/rfc6719/) は加算コスト・hysteresisの参考であり、Ninlilのwire互換や全環境最適性を意味しない。[ARIB STD-T108公式概要](https://www.arib.or.jp/english/std_tr/telecommunications/desc/std-t108.html) は機器区分ごとの確認が必要であることの出発点であり、概要や既存driverの定数だけを個別設置の適法性証拠にしない。
