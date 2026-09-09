# 適応通信実装と7台試験 — 2026-09-09

基準はPR #17の `8c37df522b8cc4ccbd58010fb11f909d0049f9ed`。設計は
[`design/2026-09-09/README.md`](design/2026-09-09/README.md)。これは段階的実装であり、
S0〜S5すべての完了、全SDK統合、7台RF受入、512台自律通信を宣言しない。

## 実装と統合の境界

| 分野 | この変更で動くコード | まだ接続・検証していないもの |
|---|---|---|
| MAC | 既存schedulerにopt-inのairtime DRR、peer公平性、限定critical追越し。既存pumpが呼ぶopenへKconfigを接続 | 実IDF build、RF時間校正、peer/service階層のservice別配分 |
| 観測 | 同一session/PHY/出力世代の8試行を確定後に一度だけ集計。sleep無効化、queue/airtime、全窓鮮度 | 現行nodeのrolling probe/reportとRX情報への接続。現在のqueue_us=0はまだ変わらない |
| 出力 | 独立3窓とdwell、設定世代、承認範囲への復帰、driver失敗時の停止 | 現行pumpの旧radio_adapt呼出しへの接続。新policyが実機で自動作動するとはしない |
| 経路 | 512node/2048edge/4hop、有限step探索、候補3、世代確認、候補validator | node/Coordinatorへの差替え、実測snapshot生成、共通容量validator、並行plan transaction、局所修復 |
| PHY | 予定の競合・guard・共通復旧窓の検査、240byte内codec、16断片の再構成 | 無線dispatch、認可/時刻交渉、永続PREPARE/COMMIT、SHA-256 backend接続、実radioのSF/BW/channel変更 |
| 多数台配信 | 最大512targetの固定identity/世代、INTENT→ADMITTED→TERMINAL、再開と結果集計 | 永続codec/backend、Coreの全再送までidentityを固定するadmit_bound/query_boundアダプター、Core保管枠とserviceの分離 |
| HIL | 明示identityによる7台preflight、双方向12配送、同一IDのRelay停止復旧、証拠と停止処理 | 実機実行。型付きUSB fixtureは本物のMCUや無線の証拠ではない |

現行自律ownerの16member/32flow上限は増やしていない。登録N、session K、
neighbor D、service Wを一つの定数へ押し込む変更はしていない。
新fanoutを旧address-only `ninlil_submit`へラップするだけでは契約を満たさない。
第一段階の512グラフ・511配信試験は、その統合を代替しない。

## APIと安全条件

`ninlil_adaptive` / `Ninlil::adaptive` は呼出し駆動のC11 static library。
新関数はthread、timer task、暗号鍵、無線送信を作らない。callbackとworkspaceは
呼出し元が所有し、reentrant/concurrent呼出しを禁止する。宣言は各public headerにある。
容量不足、古いsnapshot、不確かな結果は成功に変換しない。

`ninlil_airtime_scheduler` は公開構造体を拡張したため全利用者の再buildが必要。
`NINLIL_AIRTIME_API_VERSION=2`。既存のwire/journalは変更しない。
legacy openの挙動は保持し、空のschedulerへの `ninlil_airtime_enable_drr` か
実機Kconfigでのみ選ぶ。CRITICAL追越しはTX前の有限airtimeに限る。
TX中preemptionはせず、大packetの予約と曖昧TXの保守的課金を保持する。

PHY codec dispatch 32はこの実験codecの識別子であり、現行nodeは送受信しない。
認証済みpeer限定。再構成1件1408bytes、owner全体最大4件の制約は統合側も検査する。
受信完了は適用権限ではない。独立したradio/干渉領域の宣言は測定済みである必要がある。
共通recovery slotは全予定と競合する保守的な初期契約とした。

fanoutのcommit callbackはSTARTの完全な対象集合とpayload所有を一括保存する。
Coreとの二store境界をatomicと仮定せず、固定keyのINTENTを保存してからadmitする。
IOの成功/失敗が曖昧ならpoisonし、正本をreplayする。sleep/不達による待機はcancelではない。
`all_terminal` と `all_satisfied` は別。期限やevidenceを勝手に弱めない。

## 再現試験

完全checkoutがなくても、基準版と一致を検査した元の `ninlil.h` を含むfixtureで
実際の新C実装を直接compileする。模倣したPython版を実装の証拠にはしていない。

```sh
python scripts/run_adaptive.py /tmp/ninlil-adaptive-check --jobs 4
# 中断後も以前のログを残し、番号付きattemptへ再検証する:
python scripts/run_adaptive.py /tmp/ninlil-adaptive-check --resume --jobs 4
```

GCC/Clangの通常・ASan/UBSanを実行する。10個のCTest entryに、11個のPython
USB protocol/cleanup試験を一つのentryとして含む。個別のcompiler・終了コード・
source/log SHA-256は `docs/evidence/2026-09-09-adaptive/` の記録を参照する。

測定例はnative合成入力でありRF実測ではない。class airtimeは
64,000,000 / 32,000,000 / 24,000,000 / 8,000,000 us（8:4:3:1）。
最初の32targetがadmission前に到達不能なfixtureで後続479targetが完了し、
後で同じ対象を再開する。すでにCoreへ32件保管済みの状態からの無線進行を
この試験で証明したとはしない。20,000入力のcodec mutationはfull protocol fuzzではない。

## 未実行のgate

全checkoutのCTest/Python、clang-format、全体static/fuzz/package/50,000行LOC、
実IDF configure/compile/link、RF/HIL、電源断、消費電流、range/soak/field受入は未実行。
コンポーネントCIのPASSを全SDKのCI-greenに置き換えない。GitHub Actionsを起動しない。
7台試験の操作・対象・制限は [SEVEN_BOARD_TEST.md](SEVEN_BOARD_TEST.md) を参照。
