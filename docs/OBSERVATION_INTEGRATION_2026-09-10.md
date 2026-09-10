# 確定観測を既存ノード・無線pump・経路評価へ接続

現在の統合状況は [INTEGRATED_ROUTING_2026-09-10.md](INTEGRATED_ROUTING_2026-09-10.md)。
以下は明記された基準版に対する記録であり、統合後の最新状態ではない。

基準：PR #18、`4357286cf3cff58e9b3413eef45412dc28c4771e`。
この変更は観測経路の実装・接続である。新経路探索、PHY設定交渉、Group/Coreの
永続連携をすべて完成させたという意味ではない。7台実機試験は未実施。

## 今回実行される経路

`ninlil_esp_network_emit` → 初回queue時刻保存 → 実driver結果 →
`ninlil_node_transmitted` → `ninlil_node_probe_measured` →
`ninlil_probe_monitor` → 認証済みneighbor reply → 期限後の確定 →
`ninlil_node_links.c:report` → 既存NODE_OBSERVATION →
`ninlil_coordinator_observe` → 既存経路costのqueue項。

新部品を追加するだけでなく、この既存の呼出し経路へ接続した。
デフォルトは従来方式。ESP-IDFの明示選択は
`CONFIG_NINLIL_CLOSED_PROBES_EXPERIMENTAL=y`。実際のSDK buildは未実施であり、
プリプロセッサ選択のfixture成功と混同しない。

## 測定の意味

| 量 | 今回の扱い |
|---|---|
| queue_us | 最初のscheduler受付から、最後のdriver send呼出し直前まで。以前のBUSY/再試行待ちを含む |
| airtime_us | 固定PHYに対する既存driverのフレーム長計算値。実測オシロスコープ値ではない |
| 完了時刻 | TX_DONEとRX復帰を確認してdriverが返った後の単調時計。IRQそのものの正確な時刻ではない |
| 送信出力 | 送信後のapplied_power_dbm。変更要求値を実適用とみなさない |
| 応答 | 現在のneighbor認証後、対応tokenの3秒窓内。失効した時刻ちょうどは新測定には含めない |
| 不明な待ち時間 | UINT64_MAX。30秒超も未測定扱いとし、0や上限値への丸めで隠さない |
| sleep・再認証・出力変更 | 古い未確定/確定観測を再利用しない。配送の保管責任は変更しない |

新しい測定は実TX成功の補助情報であり、送信受付・BUSY・IO・TIMEOUTを勝手に
成功したRF試行へ数えない。IOなどで実送信の有無が曖昧な場合は既存driver結果に
残し、新しいRF成功率の分母へ推測で追加しない。

## 経路用の窓と出力調整用の窓を分離

link_metricsの独立8試行窓はそのまま保持する。経路監視には、最新8件の
**確定済み**試行を使う別の有限ringを持つ。1試行が閉じるたびに1回だけ追加し、
8件揃った後は毎試行更新できる。通常の10秒probe間隔で、独立8件を待つために
経路の30秒freshnessを超える更新空白を作らないためである。

経路用の窓は重なる。3回の報告を独立24成功と数えてはならない。各窓内の全試行は
120秒以内であることを検査し、Root側には従来どおり最後のRF完了観測時刻を
保守的なlease時刻へ変換して渡す。Root側の30秒検査や法規検査を緩めない。
スリープ端末や混雑下で十分な新しい窓が得られない場合は、未観測のままとする。

NODE_OBSERVATIONの41-byte bodyとACKの11-byte bodyは変更しない。
airtimeは8件の平均を上方丸め、queueはその8件の最大値を送る。
既存reportのtokenとbitmapの欄には確定窓の最終token/bitmapを入れる。
新probeが始まっても、前の報告へのACKをその新tokenと誤比較しない。
重複ACKは同じ窓の報告責任だけを終える。配送証拠や新しい窓を消さない。

## 互換性・資源

Core API/wire/journalと既存暗号処理は変更しない。schedulerの公開構造体は
初回時刻を追加したため `NINLIL_AIRTIME_API_VERSION=3`。
node configには借用monitorを追加し `NINLIL_NODE_CONFIG_API_VERSION=2` とした。
全consumerの再buildが必要。node/pumpを動かしたままmonitorを差し替えない。

通常pumpはmonitorへのpointerだけを持つ。明示APIではcallerがworkspaceを所有し、
Kconfig版では選択時だけpumpにworkspaceを確保する。ネイティブfixtureでmonitorは
10,504 bytes。ESP32のRAM/stack測定ではない。16 peer上限を維持し、古いtelemetryの
再利用枠以外を無制限に増やさない。満杯時は計測のCAPACITYであり、配送を削除しない。

ホストのnode/controlとadaptive libraryは同じninlil_probe_metrics archiveを共有する。
ESP32 componentも同じ二つのCファイルをcompileする。既存の送信出力調整は
従来policyのままで、power_policy.cへの置換やSF/BW/channel変更はしていない。

## 実施した検査・残る検査

`python scripts/run_adaptive.py OUTPUT --jobs 4` は、追加した入力ファイルを
開始前/終了後にhash照合する。実装部品の試験、production node/pumpを使う境界
fixture、7台ランナーのprotocol fixtureを区別して記録する。

境界fixtureのdriver・暗号・Core・Coordinatorは明示的なdoubleである。
実装Cはコピーした同一bytesでcompileするが、実際のSDK ABI・暗号通信・経路選択・
Flashや無線をこのfixtureで証明したとはしない。
[`evidence/2026-09-10-observation`](evidence/2026-09-10-observation/README.md)
に実際の結果・hashと未実施項目を記録する。

次に残るのは、完全checkoutでのSDK/ESP-IDF/LOC/package/format検査、7台での
観測周期・初期経路・遅延測定、新候補探索と共通容量判定の接続、受信側を含む
PHY transaction、identity-bound Group/Core永続adapterである。
この追加だけで64/512台収容、安定版、field qualificationを宣言しない。
