# 7台の実機受入手順

この手順を作成した時点では **RF/HIL NOT_RUN**。既存の保存領域、identity、
秘密鍵、参加資格を保持する。7台のUSBシリアルや役割は未確認であり推測しない。
候補構成はRoot1台、給電Relay2台、Endpoint4台。初期試験は全台を給電し、
sleepの電流・時間受入と分ける。電池roleはUSB host接続中のawake契約を確認する。

## 1. buildと機器同定

先に全SDKの既存検査とESP-IDF 6.0.2 buildを実施する。通常版と実験MAC版は別々の
出力先へ記録する。承認済みのアンテナ・地域・周波数・出力・RF path・sdkconfigを維持し、
この文書から任意の送信設定を採用しない。

```sh
python tools/node_hil/build.py REVIEWED_SDKCONFIG - BUILD_OUTPUT --nodes 1 2 3 4 5 6 7
```

`--nodes` は実際の既存addressを指定する。数字1〜7への再割当を要求しない。
実験MACだけ `CONFIG_NINLIL_ADAPTIVE_MAC_EXPERIMENTAL=y` を指定し、対照版では無効にする。
ビルドは機器への書込みではない。書込み前に既存partition・protected-store hashと
各物理identityを既存の手順で照合する。本ランナーはflashもprovisionも行わない。

既に設定・書込み済みの7台を接続し、公開情報だけを取得する:

```sh
python tools/node_hil/seven.py --inventory > seven-inventory.json
```

I/U/Hの読み取りのみ。GによるRF起動、Pによる初期化、資格uploadはしない。
未登録機器や異なるRoot/roleがあれば、既存issuer/管理の明示手順で別途準備する。
現在のhardware.pyにある3台のIDを、新しい7台のIDとして流用しない。

## 2. manifestを固定する

`tools/node_hil/seven.example.json` のPLACEHOLDERをinventoryの実測値へ置き換える。
placeholderのまま検証を通すことはできない。root/address/role/設定revision、
USBserial、stable identity、公開鍵を記録する。source_commitは実際にbuildしたSHA。
各firmwareはbuild.py出力のapp.bin、隣のsdkconfig.hとhashes.jsonも保持する。
全7台のMAC設定とartifact hashをランナーが確認する。ただし、USBから現在の
flash image hashをattestする機能はない。書込み記録との照合は独立に必要。

一度使ったsequenceを新規配送試験へ再利用しない。12件分（復旧試験は13件分）の
未使用uint32を確保する。無関係なApplication送信を停止し、初期ledgerを保存する。
`radio_profile_reviewed=true` は人が確認した設定の記録であり、自動の法規認証ではない。

```sh
python tools/node_hil/seven.py seven.json
```

既定ではUSBを開かず、12配送の計画を出す。次の実行だけがRF起動を伴う:

```sh
python tools/node_hil/seven.py seven.json --execute --evidence evidence/seven-baseline-001
```

全7台が停止・autorun off・fault無し・設定一致であることを、最初のG前に確認する。
Root→6台と6台→Rootを投入し、個別message IDのSATISFIED/APPLICATION_ACCEPTED、
各受信側の新規ledger増分を検査する。終了時は成功/失敗にかかわらず起動を試みた
全ownerへXを送り停止・autorun offを再確認する。G/Sの返答不明を無条件再送しない。
実行時間上限は120〜570秒。足りなければUNKNOWNで証拠を残し、配送を勝手にcancelしない。

## 3. Relay停止と同じ配送IDの復旧

別manifestで `scenario=relay-stop`、`stop_relay` と `recovery_target` を指定する。
まず12配送を完了させる。対象への現在有効な経路に選んだRelayが実際に含まれることを
Lで確認した後、13件目を作成し、ACTIVEを確認してそのRelayをXで停止する。
同じmessage IDが完了し、停止Relayを含まない別の有効経路が観測された場合だけPASS。
停止前からdirect経路ならFAIL（迂回の証拠ではない）。Lのlocal_ready、lease/expiry、
source/target、経路重複も検査する。

ソフトのXによる停止であり、hard power cut、Flash書込み中断、RF距離の実験ではない。
電波で経路を分ける場合は適法な配置/遮蔽/減衰器の実測と写真を別に記録する。
現行3台用Fフィルターを7台の接続証拠として使わない。

## 4. 合否と残る受入

`result.json`、manifest、console.jsonl、trace.jsonl、SHA-256を保管する。
FAILは矛盾した証拠・型/設定違反、UNKNOWNはタイムアウト/観測不明。停止確認失敗はFAIL。
過去sequenceによる既存SATISFIEDや機器を一部省いた結果を新しい7台PASSにしない。

同じfixture/配置/負荷でlegacyと実験MACを比較する。新規route planner・PHY交渉・
identity-bound fanoutはまだnode/Coreへ未統合であり、このランナーのPASSをそれらの
受入へ流用しない。7台は機能/復旧HILであって64/512台の収容性能証明ではない。
Root交換、7台同時復電、sleep、電源断、長期運転はそれぞれ別の未受入gateに残す。

## 5. 9月10日の確定観測・出力制御を比較する場合

通常版とは別に `CONFIG_NINLIL_RADIO_FEEDBACK_EXPERIMENTAL=y` を指定して全台を
再buildする。macの選択と独立に記録する。manifestへ
`"power_mode": "closed-feedback"` を追加する。対照版は `"power_mode": "legacy"`。
ランナーはsdkconfig.hのhash/defineと、全台起動直後のT応答のmodeを照合し、
不一致なら配送を投入せずFAIL・停止処理へ移る。Tのmodeは制御選択の確認であり、
電波品質・出力校正・全firmware imageのattestationではない。

試験前に[統合範囲](RADIO_FEEDBACK_INTEGRATION_2026-09-10.md)の未実施gateを確認する。
この追記時点でも7台HILは未実行。既存ランナーの12配送PASSだけでは24個の新鮮な
probeや減力・loss復帰を直接観測した証拠にならない。それらは追加計測が必要。
