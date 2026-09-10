# 観測・実適用出力・候補経路の統合（2026-09-10）

後続の永続保存実装は [DURABLE_FANOUT_2026-09-10.md](DURABLE_FANOUT_2026-09-10.md) を参照。
この文書の試験結果は作成時点の範囲を表す。

適用基準は PR #18 の `b38878e79f1e7b28ecbc195762395b1d67ab6475`。
PR #19 の `55bbb4fdcf78f15b38db403f257031c808d74c02` の機能を、前者の
観測報告を消さずに統合したローカル変更である。**GitHubへのpush・マージは未実施**。
前の二つの統合報告はそれぞれの基準commitに対する歴史的記録で、本書がこの変更の現在地。

## 実装した経路

1. キューの最初の受付時刻を `ninlil_airtime_job.queued_at_us` に一本化。
   重複受付、BUSY再試行で更新しない。別配列の二重管理を除去した。
2. 送信前の既存権限確認後、直接隣接相手の現在sessionから出力を提案する。
   設定予約だけで測定世代を進めず、driverの成功と適用出力の一致後に確定する。
3. `ninlil_node_receive` の認証済みNS channel 2入口からのみ、両観測ownerへ返信を渡す。
   一般の制御dispatch/経路経由から来た返信を、直接電波の証拠として数えない。
4. 経路報告は最新8件の閉じた試行、出力判断は独立した8件の窓を使う。
   世代変更を通常DATA送信で実行した場合も経路測定を無効化し、次のprobeへ古い
   評価を持ち越さない。出力policyの重複比較も新しいtoken/bitmap欄を含める。
5. 確定観測は既存41-byte報告からCoordinatorへ入り、新しい有限候補探索を利用できる。
   `node_step` が探索を進め、`coordinator_select` と既存のstage/prepare/activate/
   applied/reconcile処理がその結果を扱う。候補の生成だけでは有効経路にならない。

キュー計測不能・30秒超はUNKNOWNであり0にしない。観測テーブル不足時は出力を
承認済み最大値へ戻して測定を不採用にする。配送CoreやRelayの所有データを削除して
容量を作らない。初期接続/復旧floodは最終宛先への低出力評価を流用しない。

## 経路候補と実際のCoordinator

新しい `ninlil_route_optimizer` は既存の観測をコピーしたsnapshotで検索する。
現在のreference ownerでは16ノード/64方向edgeのworkspaceを使う。APIの設計上限は
512ノード/2048edgeだが、runtimeの登録上限を引き上げた変更ではない。

候補は最大3本、4-hop以内、探索仕事量は一回64単位・全体4096単位。一つの探索を
別flowの呼出しで毎回リセットしない。未回収の要求は5秒で失効させる。
参加世代、Relay権限、両方向の新しい観測を検査し、読み取り専用の追加資源validator
を呼び出せる。検索結果を取り出す時とstage直前に再検査する。親機の準備・適用証拠、
旧経路のrelease/期限切れ、保存失敗時のpoison処理を迂回しない。

**現時点では固定PHY・best-effortのprobeコスト最適化**である。
既存報告が計測しないwake/commit時間を0の実測値として偽装しないため、
`probe_cost_only` を明示的な別モードにした。通常のstrict検索は引き続き全項目の
known状態を要求する。このmodeをSLO容量受入、最終受領証までの遅延予測、
PHY/MAC共同最適化の完成と読み替えない。追加validatorがNULLのreference設定は
共有airtime容量の予約を行わない。

停止/分断時の予備経路に物理的独立性を推測で付けない。新候補が得られなくても
既存の保管責任は保持される。現時点でRootに既に届いた旧出力の観測を瞬時に失効させる
新しいwire通知は実装していない。既存の鮮度制約内で古い報告が残り得る。

## 既存の安全・公平性修正

- 候補に記録された非ゼロの参加世代を、stage時に現在値へ黙って置換しない。
  アドレスの機器が変更された場合は、永続書込み前にUNAUTHORIZEDを返す。
- 変更抑制の時刻をflowごとに分離。一つのflowの更新で、無関係なflowの改善が
  抑制され続ける問題を避ける。再起動時のlive proofは従来どおり再確認が必要。
- 7台ランナーは両PRの設定項目を保持し、追加の `route_candidates` とbuild設定を
  照合する。trueには `closed_probes=true` が必要。停止要求の数値にboolを許可しない。
- キャンペーン中のKeyboardInterruptもUNKNOWNの証拠と停止処理を残す。
  Ctrl-Cから送信済みデータの失敗/成功を推測しない。

## 多数台配信の停止境界の修正

保存callbackがBUSY/CAPACITYを返してownerがpoisonedになった場合、以前のstepは
それを通常の宛先待機と同じように処理し、同じ呼出し内で他対象へ進んでいた。
今回、poisonの直後にbatchを打ち切るようにした。修正前の実Cで新テストが失敗し、
修正後には最初の失敗以降に保存・admission callbackを呼ばず、正本replay後に同じ
対象へ進行することを検査した。これは既存fanout componentの安全修正であり、
未実装の永続journal/Core binding adapterが完成したという意味ではない。

## ビルド・資源・互換性

すべての実験設定は既定OFF。

```text
CONFIG_NINLIL_ADAPTIVE_MAC_EXPERIMENTAL=y
CONFIG_NINLIL_CLOSED_PROBES_EXPERIMENTAL=y
CONFIG_NINLIL_RADIO_FEEDBACK_EXPERIMENTAL=y
CONFIG_NINLIL_ROUTE_CANDIDATES_EXPERIMENTAL=y
```

個別比較でもよい。route candidatesはclosed probesに依存する。7台manifestの
`mac`, `closed_probes`, `power_mode`, `route_candidates` を実際のbuildと一致させる。
`T`コマンドは出力ownerの選択だけを確認する。manifestとartifact hashは、機器の
全flash内容のattestationや経路探索が実際に選ばれたという測定ではない。

`NINLIL_NODE_CONFIG_API_VERSION=3`。公開config、Coordinator/flow、pumpのstructと
実験用feedback open API（workspace引数）は変わるため、全consumerを再buildする。
Core API、NP1/NP2、観測wire、既存journal形式は変更しない。
通常pumpは観測ownerへの借用pointerを持つだけで、大きなworkspaceを既定で確保しない。
Kconfig選択時は対応workspaceがcompileされる。Root以外もそのbuildのworkspace分の
構造体サイズを持つため、MCUのheap/stack/map確認は必須。

CMakeでは実Coordinator/searchを `ninlil_network_planning` の一つのarchiveにまとめ、
既存controlと新adaptiveの両方が同じ実装を参照する。元のsource listから同じ3つの
Coordinator単位だけを除き、二重にcompile/linkしない。完全SDKでのlink/package検査は未実施。

## 検証とその境界

`python scripts/run_adaptive.py OUTPUT --jobs 4` はGCC14.2/Clang17の通常・ASan/UBSan
の4構成で、それぞれ23/23 CTest entryが通過した。DRR・確定観測・出力制御・
候補経路の四機能を同時に有効にした境界試験を含む。一つのentryはPythonの7台ランナー
試験を含む。最終ログ・JUnit・入力SHA-256は配布bundleの `evidence/` に含める。
36回の静的解析（GCC/Clang各18単位）は終了0・診断なし。詳細は同梱reportを参照。

| 実行範囲 | 本物の処理 | 代替・未確認の境界 |
|---|---|---|
| portable components | 実際のC、元の公開ninlil.h | PHY/fanoutのcallbackには既存fixtureを含む |
| Coordinator | 実Coordinator、検索、codec、stage、適用、再起動復元 | policy/保存callbackはメモリ上。実Flashではない |
| 既存回帰 | 未変更のtest_network_restart.c / test_prepare_lease.c | 同じcallback境界。実機ではない |
| node/pump接続 | production Cの同一bytes、両観測、実IO入口、実Coordinator | 他のnode/Core/暗号/radio/時計と一部型宣言は明示double |
| 7台ランナー | 実Python、誤機器/設定/配送ID/停止/割込の検査 | USB応答fixture。7台の物理試験ではない |

範囲限定checkoutを使用しており、完全SDK・実暗号library・ESP-IDF・全体ABI・
package consumer・全体format/LOC・full protocol fuzzはNOT_RUN。
過去の狭い型宣言fixtureのPASSを、本物のSDKでのリンク成功へ置き換えていない。

## 完成していない範囲 / 有効化しないもの

受信側と合意してSF/BW/channelを変更する永続PHY transaction、共有無線資源への
容量予約、並行plan transaction、大規模fanoutの永続codec/backend、全Core再送まで
stable identity/世代を固定するadapterは未完成。fanoutのstate machine試験を
実際の数百台通信の証拠にしない。自律nodeの16member/32flow上限もそのまま。

7台実機でのRF、Relay停止/再起動、電源断、電流、長期field受入は未実行。
この変更で機器のflash/erase/再登録/無線起動、hosted Actions、GitHub commit/PR変更を
実行していない。7台の試験は既存手順と本書の残項目を分けて実施する。
50,000行のhard ceilingや既存警告・失敗検査を緩めていない。全checkoutでの予算確認は残る。
