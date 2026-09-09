# 確定した無線観測と送信出力制御の統合

2026-09-10。基準は PR #18 の `4357286cf3cff58e9b3413eef45412dc28c4771e`。
この変更は、独立モジュールだった観測・出力制御を実際の node と ESP32 radio pump
へ接続する。経路共同最適化・PHY交渉・512台通信の完成や実機合格を意味しない。

## 実行経路

`ninlil_node_frame_current` による送信直前の権限確認 →
`ninlil_node_radio_tx_context` による直接相手・公開session fingerprintの取得 →
`ninlil_radio_feedback_begin` による出力の提案 → 既存driverへの設定予約 →
既存driverの送信 → TX_DONEと `applied_power_dbm` の確認 → 測定開始。

返信は `ninlil_node_receive` の NS channel 2 で認証され、既存のmembership/challenge
検査が成功した後だけobserverへ入る。E2E/経路経由の制御返信や、単に下位関数へ
渡されたデータを直接無線の成功として数えない。暗号方式・鍵・counterは変更しない。

8回の閉じた、重複しない試行窓を利用し、session・PHY profile・実出力世代を分離する。
出力変更前の成功を次の出力設定の証拠へ持ち越さない。返信窓は
`[TX_DONE, TX_DONE+3000ms)`。queue受付やcarrier sensingのBUSYはRF試行にならない。

## 設定予約と物理適用を区別する

既存 `ninlil_sx1262_radio_power` のOKは「次のTXに使う出力の予約」であり、即時の
物理適用ではない。追加した `ninlil_power_policy_plan` は副作用のない提案だけを返す。
`ninlil_power_policy_step` の従来callback契約も引き続き実適用を要求する。

新ownerは、TX_DONEの成功とdriverが報告した適用出力の一致を確認してから提案を
publishする。BUSYなら提案を取り消し、測定・出力世代を進めない。IO/TIMEOUT・
実出力の不一致はownerをfaultにし、次の送信を止める。再開にはradioの回復とownerの
再openが必要。保管済みCore/Relayの配送を削除・取消・成功扱いする操作ではない。

BootstrapのNB envelopeの宛先は最終宛先であり、物理的な次hopとは限らない。
その宛先に対する低出力の測定をflood/recoveryへ流用せず、承認済み最大出力を使う。
最大出力に戻すこと自体を到達保証や法規認証とは扱わない。

## 休眠・待ち時間・資源

実 `ninlil_node_suspend` は睡眠に入る前にobserverを無効化する。保持された鍵が同じでも、
未確定の測定をRF失敗にせず破棄し、復帰後の最初の直接TXは最大出力を再確認する。
ノードの再認証によるfingerprint変更も、次の送信提案前に旧測定を無効化する。

pumpはqueueのtokenに初回受付時刻を対応づける。同内容の受付をまとめた場合も時刻を
更新せず、実際に待った時間を保持する。取得不能・30秒超の値はUNKNOWNとして測定を
採用しない。これはqueue受付からdriver呼出しまでの時間であり、CCA/Flash時間や
E2E遅延ではない。休眠をまたぐqueue待ち時間も含み得る。

one-radio owner、同時に一つの提案、peer slot最大16。open中に別addressへslotを
黙って再利用せず、満杯ならCAPACITY。同じaddressでもsessionが違えば別測定にする。
この追加owner単体は今回の64bit host fixtureで6064bytes。pumpのqueue時刻配列などは
別に増える。MCUの実測heap/stack値、機器全体のRAM余裕や512台対応の証拠ではない。

## 有効化・互換性

既定は旧方式を維持する。実験用の新制御は次で明示する。

```text
CONFIG_NINLIL_RADIO_FEEDBACK_EXPERIMENTAL=y
```

既存node_mainから呼ばれる `ninlil_esp_node_adaptive_power` が新ownerを選択する。
新API `ninlil_esp_node_feedback_open` での明示選択も可能。queueに仕事がある状態で
切り替えない。MACのDRR設定とは別の選択で、比較では一度に変える要因を記録する。
`ninlil_node_config` とpump構造体は拡張されたため、全consumerの再buildが必要。
公開structのbinary互換は主張しない。既存wire/journal formatは変更しない。

7台ランナーの任意field `power_mode` は `legacy` または `closed-feedback`。
指定時はbuild.pyのsdkconfig.h/hashと起動後の既存Tコマンドを全7台で照合する。
違うmodeならSによる配送投入前にFAILにし、起動した全ownerを停止する。
fieldを省略した既存manifestは使えるが、新制御の確認済みとは扱わない。

## 検証範囲と残り

今回の検証は実C実装を使った限定試験。元のnode IO/link/radio/sleep、pumpと新helperを
一緒にcompileするが、残りのCore、暗号、radio、clockは明示されたdoubleである。
完全checkoutを取得できず、検証環境では再構成した基底宣言とsliced declaration fixtureを
使った。SDKの本物のstruct layout、全ABI、全体linkやESP-IDFの合格証拠にはしない。
通常checkout向けのCMake test targetも登録したが、その全SDK構成は未実行。
正確なコマンド、入力hash、終了コードは [検証記録](evidence/2026-09-10-feedback/README.md)。

未接続のままのもの：Rootへの旧観測reportはrolling窓/queue_us=0のまま。新しい
queue/window値を遠隔の経路評価へ供給したとはしない。RSSI/SNRの評価、新候補探索の
Coordinatorへの差替え、並行plan transaction、相手と合意するSF/BW/channel変更、
identity-bound fanoutの永続backendとCore再送の統合は別途必要。

7台RF/HIL、全SDK回帰・package・clang-format・全体LOC、ESP-IDF build、電源断、
電流・長期・field qualificationは未実行。50,000行の上限と既存検査は緩めていない。
この変更だけでmergeや実機releaseを承認しない。hosted Actionsは起動しない。
