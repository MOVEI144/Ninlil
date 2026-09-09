# PHY/MAC自動最適化仕様 v1

状態: 実装前仕様。共通の基準・不変条件・O1観測・C1容量判定は[共通仕様](ADAPTIVE_OPTIMIZATION_SPEC.md)。本書のMACはMedium Access Control。現在のTX power policyやqueue schedulerの存在を、この仕様の完成と扱わない。

## P1. 現行baselineと対象

`ninlil_radio_adapt`は3 dB刻みのTX出力調整だけを行う。周波数、SF、BW、CR、受信予定の自動合意はない。`ninlil_airtime`は全体のairtime creditを持つが、class選択は送信件数の8:4:3:1。`ninlil_esp_network_pump`は有限queueとrandom deferを使い、実送信結果をownerへ返す。

これらを比較可能なlegacy profileとして残す。新profileは「合法な設定で受信相手と再会できる」「緊急/制御を進めながら容量を使う」「悪化したら説明可能に戻す」を目標にする。concentrator並みの同時多SF受信、全環境に最良のMAC、送信途中のframe preemptionを仮定しない。

## P2. 許可PHY集合

候補は任意の周波数/SFをその場で生成せず、version付き `RadioProfileSetV1` の最大8 profileから選ぶ。各entryは次を持つ。

| 値 | 内容 |
|---|---|
| profile_id / digest | 一意IDと完全設定のdigest |
| board/region capability | 正確なboard、driver、地域条件の承認参照 |
| frequency_hz / bandwidth_hz / SF / CR | 物理設定。整数と列挙値でwire定義 |
| preamble / header / CRC / IQ / sync word | 長さと受信互換に影響する設定 |
| allowed_tx_power_dbm[] | board/地域/antenna条件内の有限出力集合 |
| max_on_air_bytes / max_airtime_us | 全暗号/制御headerを含む上限 |
| tx_pause / CCA・LBT / cumulative_budget | driverが強制する規制・運用制約 |
| rx_startup / switch / storage_guard | 実測または保守的な上限と出所 |

Coordinatorの提案集合、機器能力、署名済み運用許可、実driver許可の積集合だけを使う。空ならunsupported。法規/board上限を高い配送scoreで相殺しない。現在のJP向け数値や-3 dBm bench設定を他地域・他機器の合法性の根拠にしない。

airtimeは実際にpinされたdriverとwire長から計算し、測定誤差を記録する。64-byte application上限、240-byte RF frame、旧92-byte static simulatorを混ぜない。重いprofileで必要な制御frameが窓/法規上限を超えるならprofileを拒否し、勝手にtag/headerを削らない。

## P3. 常設の再会手段

最初の新profileはcommon access/recovery profileを一つ指定し、通常最適化では消さない。Join、Resume、同期回復、plan照合の有限機会を保証する。ただし無電波経路・継続妨害下の到達保証ではない。

精密clockがない端末はcommon profileから始める。既知の予約profileへscanするだけで全端末が拾えるとしない。recovery profile自体を移行する場合は通常の設定変更と別operationにし、旧機器の救済期間/入口を先に定義する。

単一radioは一時点で一つのprofileの送信または受信だけを行う。送信側だけSF/channelを変えない。全てのprofile変更は受信側の予定と同時に検証する。単なるTX power減少もACK下り、hidden terminal、近遠関係に影響し得るのでO1をprofile/出力別に記録する。

## P4. MAC構成

初版は次のhybridを評価するが、同一workloadで固定競合方式・固定予約方式と比較し、負ける環境では強制採用しない。

| 種別 | 機会 |
|---|---|
| Join/Resume/同期/修復 | common profileの上限付き共有アクセス、jitter/backoff |
| 少量イベント | common profileで小さい遅延の送信機会 |
| 継続的上り・下り・Relay | 送受信の組合せが合意された予約cell |
| CRITICAL / 必須receipt | 待ち時間をadmission時に制約した進行枠 |
| BULK / probe | 他の所有/制御を妨げない残余枠 |

cellは `(radio_id, transmitter, receiver, profile_id, start, duration, epoch, guard, allowed_class)`。Relayでは前段RX/保存後に後段TXを置く。ACK、E2E証拠、Flash処理時間を無料にしない。未知の空間再利用は無効。異なるSF/channelという理由だけでは並行化しない。

最大32 cellのpageとdigest付きmanifestで配布し、nodeは自分のexecutorに関する有限pageだけを保持する。common/recoveryを含む最長アクセス間隔を検査する。巨大な周期に詰めてCRITICALやbattery起床を長く待たせない。周期と窓はrate/PHY/clockから算出し、全環境共通の1秒slotを固定しない。

## P5. airtime階層公平性

新schedulerは `class → peer → service` の階層でairtime単位のdeficitを管理する。8:4:3:1は初期候補weightとして再利用できるが、**件数ではなく検査対象期間のairtime share**へ適用する。余った枠はwork-conservingに貸し出し、貸出を新規SLO受入の恒久容量としない。

- 各queueに最大件数/byte/最長滞留とcreditを持つ。globalおよびpeer単位CRITICAL/CONTROL reserveを維持する。
- quantumは固定小数us、加算/減算はoverflow検査、deficitは宣言上限で飽和させる。
- class内でbacklogの多いpeerがslot数だけで得をしない。service数を増やして公平性を迂回させない。
- 一回の `scheduler_step` は最大queue容量分の候補検査まで。live owned messageは消さず、派生frameの再生成と待機を区別する。
- `ELIGIBLE`時刻を持ち、sleep/法規/受信窓で不適格な先頭を毎step再提示するbusy loopを避ける。

現行の「credit不足でも選択済みBULKが順番を保持する」契約は、長いframeの飢餓防止として意図されたもの。新profileでは、radio送信がまだ始まっていない間のreservationに限り、緊急/CONTROLの保証枠で追い越せる。BULKへminimum quantumとaging予約を与え、追い越しを無限にしない。TX_BEGIN後のpreemptionはしない。

`max_critical_access_wait` はprofileに明示する。最大の非中断frame＋guardがその値を超えるprofileは受入不可。無線異常・外部干渉を除外せず、保証可能な条件とSLOを分けて公表する。

実TX_DONEでは予約費用と実airtimeの差を照合する。明確なNOT_TRANSMITTEDならTX airtimeの予約返却は可能だが、CCA/切替のradio占有費用は残す。TX_AMBIGUOUSではTX費用を保守的に残す。counterの予約や法規の送信履歴をrefundしない。どちらもdelivery outcomeを作らない。

## P6. 制御トラフィックの予算

制御に無制限優先権を与えない。common access、最終receipt、同期/lease、Join、計測に別budgetを持つ。BULKでそれらを使い切らない。初期LABのprobe探索上限は利用可能radio時間の2%、平常時control総量の目標は10%。いずれも法規値/性能保証ではない。

必要なreceiptやlease維持だけで目標を超える場合、受入済み所有を捨てず新規admissionと探索を止め、`CONTROL_OVERLOAD`を返す。必要な制御を黙ってdropして平均airtimeを良く見せない。復旧burst用budgetは共通C1 headroomから事前に割り当てる。

packetごとのrandom jitter、retry上限、peer/globalのJoin quotaを併用する。定常probeは全member対全memberではなくactive neighborと最大二つのbackup候補に限定する。実DATA/receiptから得られる観測を優先し、測定のために本来の通信を圧迫しない。

## P7. 自動PHY選択

まずroute/MACの成立を固定し、許可profile内で一つの変数だけを変更する。初版順序はTX power、次に合意付きPHY profile。channel・SF・BW・CR・route・wakeを同時に自由探索しない。

候補は同一payload/方向/機会で比較する。新しい独立窓を得る前に古い8-probe rolling windowを何度も別試行として数えない。profileまたはpower変更後は関連観測のgenerationを切り替え、過去条件の成功を新条件の成功としない。

LAB開始値は最低3つの重複しない観測窓、各窓8適格attempt、変更間隔30秒、改善下限10%。低頻度/休眠端末はデータ不足なら変更しない。probeを大量発行して条件を強制達成しない。loss、queue増加、clock異常、control負荷を分離して診断する。

TX powerの復帰は**許可された**保守値まで。受信設定を変えないTX-only復帰も費用と反応を記録する。SF/channelの復帰は次節のtransactionで新しいepochとして行い、security/authorityの世代を巻き戻さない。

## P8. 安全な設定変更

変更状態は `PROPOSED → PREPARING → ACTIVATING → OBSERVING → COMMITTED`、または `ABORTED / RECOVERING`。route planのCOMMITTEDと取り違えないよう、別の `radio_plan_phase` 型とする。

operationは対象executor集合、旧/新profile digest、clock誤差、適用時間枠、旧lease終了、fallback、trial期限を持つ。PREPAREDを返す前に能力、規制、clock、容量、永続reserveを検査・記録する。

送信側は受信側がそのcell/profileを準備した証拠がある場合だけtrialを開始する。旧/新profileは同時受信でなく時間分割で移行する。旧権限がまだ排他的に有効ならrelease/expiryを待つ。新profileのtrial失敗時にもcommon recovery窓は残す。

最後のACKを失った側はcommit成功/失敗を推測せず、operation IDとdigestを再照合する。部分適用状態の照会は冪等。新plan採用後に旧設定値へ戻す場合も新epochを発行する。未完了messageは同じID/契約で必要ならfresh sessionによって再暗号化する。

clock誤差がcell guardを超える、lease根拠が失われる、radio設定が不明になる場合は予約送信を停止し、合法なcommon recoveryへ移る。unknown profileを総当たり送信しない。

## P9. Clockとsleep

runtime monotonic、network schedule/lease、restart-safe absolute deadlineを別型で渡す。network時刻があるだけでUTC deadlineを判定しない。

`guard >= sync_error_tx + sync_error_rx + drift_bound * time_since_sync + startup/switch_uncertainty` を保守的に満たす。drift値の出所と再同期期限を記録する。測定上限を超えたらSLO admissionを止める。

batteryの受信窓はpreamble捕捉の窓かframe完了までの窓かを明記する。延長RXを使う場合は延長上限・電池予算に含める。wake時はradio復帰、時刻/lease再取得、現行plan照合の順で行う。USB-host awake guard、既存Light-sleep、Deep-sleepは別機能として扱う。

最大retry待機や同期待機がawake予算を超える場合、起き続けるのでなく合法なwake policyへ戻す。Rootはdownlinkの永続所有を保持する。radio休眠だけを電池寿命の実測と呼ばない。

## P10. Wireと保存

PHY profile/予定は既存の小message DATA headerへ無制限追加しない。新control形式は先頭version/type/length、operation ID、epoch、digest、page index/countを明示し、最大長を事前検査する。

page受理は合計byte、同時operation、期限、参加者、同一pageの矛盾を検査する。未認証pageで大容量RAM/Flashを確保しない。全page digestと署名/認証、現在の権限を検査するまでradioへ適用しない。必要な最小control fragmentationは独立gateとし、bulk転送が存在するだけでbootstrapに使えるとしない。

persistent recordのrewrite/GCは新旧plan、未解決operation、counter、membership fenceを保持する。format変更と移行テストなしで既存journalの意味を変更しない。

## P11. 必須試験

| ID | 試験 | 合格条件 |
|---|---|---|
| PM01 | 10 ms frameと400 ms frameを混在 | frame数比でなく宣言airtime公平性を測定 |
| PM02 | credit不足BULK選択後にCRITICAL到着 | 新profileの待ち上限とBULK最小進行を両立 |
| PM03 | 同classでpeer/service数・queue深さを変える | queue数/サービス乱立で割当を奪えない |
| PM04 | CCA_BUSY / TX_DONE / timeout / IO | 費用とdelivery outcomeを正しく分離 |
| PM05 | 同じ成功窓再送、profile変更、古いmetric | 重複成功や旧条件で出力を下げない |
| PM06 | TX側だけ新SF/channelへ変更したい | 対応RX機会なしでは送信しない |
| PM07 | 同じradioのTX/RX、hidden terminal、外部干渉 | 架空の同時通信/直交性を仮定しない |
| PM08 | 最終ACK欠落、片側crash、時計ずれ | common recoveryと照合で復帰、権限巻戻しなし |
| PM09 | 重いPHYでDATA/制御が窓を超える | 開始前の拒否、tag/証拠の弱体化なし |
| PM10 | battery sleepと親の設定変更 | 有限の再会手順、radio-onと未完了downlinkを記録 |
| PM11 | Join/probe/BULK floodとCRITICAL同時 | global/peer予算、制御/receipt進行、admission理由 |
| PM12 | 同一workloadで競合/予約/hybrid比較 | holdout性能退行時の維持/OFF/復帰が成立 |

host試験後、実driverのTX/RX/CCA/switch/Flash時間をHILで校正する。消費電流、長期干渉、電源断は別gate。共通G01〜G09を満たすprofileだけをenable候補にする。
