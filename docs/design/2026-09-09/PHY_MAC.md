# PHY / MAC 自動最適化仕様 v1

[共通契約](README.md) と [経路仕様](ROUTING.md) に従う。要求IDはM01–M16。対象は単一half-duplex radioから始める。複数radioの同時運転は別の資源として明示した場合だけ扱う。現行TX出力調整の実装と、ここで要求するPHY/MAC共同制御を区別する。

## M01. 三つの運転モード

| モード | 変更可能なもの | 用途 |
|---|---|---|
| FIXED_BASELINE | 承認済み固定PHY、現行scheduler | 比較・復帰用。既存αの挙動を保存 |
| BOUNDED_POWER | 同一受信PHYのまま、承認範囲内の送信出力 | 観測v2と出力世代が使える場合の局所調整 |
| COORDINATED_HYBRID | 合意済み受信機会、予約量、PHY候補、経路 | 新しいtransaction・時刻・容量gate通過後のみ |

Coordinatorがないときに端末が勝手にchannel/SFを自由探索しない。固定baselineを選べることは必須。学習モデル・汎用plugin・重いsolverをランタイム依存にしない。

## M02. 実radioと処理時間の観測

portは `TX_ENQUEUED, TX_STARTED, TX_DONE, RX_PREAMBLE, RX_DONE, CRC_ERROR, CHANNEL_BUSY, REGULATORY_DEFER, RADIO_FAULT, WAKE_READY` を区別する。対応しないイベントはunsupportedとして宣言する。現行pumpで受け取っているRSSI/SNRをprofile・方向・frame種別付きで観測ownerへ渡し、認証済みのpeer統計と未認証の環境診断を分ける。

packet長は実際のencoded lengthを使い、暗号tag・counter・route・fragment・ACKも課金する。Flash commit・GC・radio切替・sleep復帰・暗号処理の時間を計測する。各時間を単なるRF損失へ変換しない。queue受付成功はTX成功ではない。

## M03. 完了sampleと変更世代

probe結果は予定された返信期限までprovisionalとし、閉じた窓だけが最適化の確定入力となる。sampleはPHY ID、power generation、frame size、session、plan epochを持つ。出力変更後は新しいgenerationを発行し、その出力で実TXした試行だけを新評価へ入れる。過去の窓を3回通知してもgood3とは数えない。

重複・遅延・reorderした報告は同じsample IDで照合する。同一完了sampleの矛盾した内容はCONFLICTとし、再報告順序だけで品質が反転しない。先に送った途中報告を最終報告へ変更する場合は、明示されたprovisional→closed遷移で扱う。

## M04. 安全な出力調整

最大/最小出力と変更stepは、機器・antenna・地域・設置について承認されたprofileからだけ取得する。初期比較では既存3dB stepを保つが、独立した新設定sampleを必要とする。変更前sampleで次の減力を決めない。

減力は、現在の要求証拠の到達率・遅延・retryが許容範囲にあることと、十分な新しい観測を条件にする。改善用dwell、最大変更回数、canary対象、復帰閾値をpolicyへ保存する。戻す場合も承認済み範囲内に限る。認証拒否・storage fault・sleep・法規待機が原因なら出力増加で解決しようとしない。

応答不明時は成功を捏造せず、最後の検証済み設定か共通recovery profileへ戻る。最大出力にするだけで到達や性能が改善するという保証をしない。ACK側の出力・受信条件も独立に測定する。

## M05. PHY候補の受入

profileは `profile_id/version, frequency, SF, BW, CR, preamble, header/CRC mode, tx power limits, RF path, MTU, maximum frame airtime, required sensing, post-TX pause, receiver capabilities, wake/guard bounds` を持つ。候補集合は最大8件を初期上限とし、任意の全組合せをオンライン探索しない。

候補ごとにDATA、receipt、hop ACK、Join/control fragmentの最大encoded sizeとairtimeを実際のpinned driver計算で検査する。長いframeが最大送信時間・受信窓・critical blocking上限を越える候補は拒否する。payloadを小さくするには明示的なfragment対応が必要で、途中切り捨ては禁止。

同じpayloadでもoverheadが占める割合は変わる。暗号/route headerだけで候補の最大frame時間を超えるなら、その候補は利用不能である。SFを上げれば常に安全・合法・低消費になると仮定しない。

## M06. 共通アクセス機会と予約機会

回復用の少数固定profileによる `RENDEZVOUS` を残し、そこから通常通信の `SCHEDULED` 予定を取得する。RENDEZVOUSは発見、Resume、時刻不明時の再会、失敗した変更の照合を担当する。いつ起きても全PHYをscanすればよいという方式は標準にしない。

SCHEDULEDでは送信側だけでなく受信側の `(radio, profile, start, duration, guard, authorized peer/direction)` を予約する。Relayでは前段RXと必要commitが終わった後に後段TXを置く。half-duplexの重なりをvalidatorで拒否する。実行時にも現在のclock誤差・radio状態を確認する。

初期の比較対象は固定slot、合法なcarrier senseを使う競合方式、共通アクセス＋予約の混合方式の三つ。混合方式を常に最良と決めず、同じmanifestで比較する。外部干渉やhidden terminalが残ることを評価へ入れる。

## M07. 設定変更の手順

PHY変更はROUTING R07のoperationへ含め、`PROPOSED/PREPARED/COMMITTED/RECONCILING/EFFECTIVE` を共有する。設定digestだけ同じでもpathやmember世代が違えば別operationである。radio設定変更と経路変更を別々に勝手にcommitしない。

受信側の能力、clock、queue、保存領域を検査し、必要な設定を保存してから準備ACKを返す。activation時刻・有効期限・旧設定・失敗時の回復窓を含める。最後のACKを失った側はquery/retryで照合し、相手も切り替えたと推測しない。全台原子的切替が成立したとの主張をしない。

まず一つの競合領域または少数のcanaryを変更する。新旧profileを移行窓内で時分割に扱い、一radioに同時受信を要求しない。旧profileの回復機会を、通常PHY変更と同時に消さない。回復profile自体の更新は別の明示operationとする。

rollbackは新しいepochで旧パラメータへ戻す。旧パラメータが失効・法規変更で使えなくなった場合は戻せないので、別の承認済み回復profileを選ぶか送信停止する。

## M08. 時間とsleep

予定用clockは同期epoch、推定誤差、最大drift、最後の同期時刻を持つ。guardは双方の同期誤差、経過時間×drift、起床時間、radio切替時間、実行jitterを含める。予定窓を超える誤差なら精密予約TXを禁止し、回復機会を使う。

battery LeafのsleepとRoot/Relayの常時受信を分ける。受信窓終了は「preamble捕捉の期限」か「frame完了までのradio-on上限」かをprofileへ明記する。preamble後の延長は電池・time budgetへ課金する。

Light-sleepでは既存のnonce保持・clock/lease無効化・radio回復を維持する。深いsleepでRAMを失う場合は別のfresh-session復帰契約が必要。Rootはsleep中の保管済みdownlinkを捨てず、次の宣言された機会へ配置する。sleep時間を延ばすことを利用製品の承認なしに省電力最適化へ使わない。

## M09. Wire予算とcontrol reassembly

基準実装のNS envelopeはheader32+tag8=40 bytes、RF上限240、plaintext上限200である。NB bootstrap envelope16 bytesを外側に使う場合、新control bodyの上限は184 bytes。現行の最大経路EFFECTIVEはkind1+plan98+5×fingerprint16=179 bytesであり、NB16+NS40を含め235 bytesとなる。この経路に新しいoperation ID等をそのまま継ぎ足さない。

新PHY transaction用の論理recordは下表をv1のcodec要求とする。既存opcodeを流用せず、新capabilityの専用messageとして実装時に登録する。整数はbig-endian、予約flag非ゼロは拒否する。以下のbodyは**認証済みcontrol専用**であり、未認証EDHOCにこの大きなreassembly予算を貸さない。

| offset | field | bytes |
|---|---|---:|
| 0 | format_version / kind | 1 / 1 |
| 2 | flags（v1は0） | 2 |
| 4 | operation ID | 16 |
| 20 | authority epoch | 8 |
| 28 | plan epoch | 8 |
| 36 | PHY profile ID / version | 4 / 4 |
| 44 | activation / expiry（同期済みlease時間） | 8 / 8 |
| 60 | 全local planのcanonical SHA-256 digest | 32 |
| 92 | fragment index / count | 1 / 1 |
| 94 | fragment payload length | 2 |
| 96 | fragment payload | 0..88 |

NB16+NS40+record96+payload88=240。countは1..16、index<count、全体は最大1408bytes、非最終fragmentは88bytes、最終fragmentだけ短縮可能。indexの乗算・長さ・countを検査してからcopyする。重複fragmentはbyte一致のみ許し、矛盾はoperationを隔離する。完全再構成・digest・profile/clock検査が終わるまで適用しない。

同時reassemblyはpeerあたり1件、node全体4件を初期上限とする。本文bufferは最大5632bytesで、metadata・認証stateは別途RAM計上する。受信ごとにdeadlineを無限延長しない。missing fragment要求も認証・byte/airtime quota付き。未完了はtimeoutで準備失敗になり得るが、既存のApplication配送を失敗へ変えない。

全512台分の巨大な予定を一つのreassemblyへ入れない。各participantに必要なlocal planを配り、Coordinatorがその集合の競合・digest対応を保持する。local planが1408bytesを超える場合は現在のprofileではCAPACITY。認証や全体整合検査を省いて分割しない。低速PHYで240byte controlが送れない場合、変更交渉は承認済みの共通control PHYで行う。

## M10. 三層の送信制御

送信判断を、(1)永続所有と権限、(2)class/peer/serviceのairtime配分、(3)実radioの法規・受信状態・予定、に分ける。上位のqueue受付は下位の実送信ではない。緊急classでも法規・暗号・既存RXを無視しない。

classは既存CRITICAL/CONTROL/NORMAL/BULKを維持する。8:4:3:1は最初の比較用weightで、次期方式ではbyte数やpacket回数でなく実airtimeを単位にする。class内はpeer/serviceごとのbounded deficit round-robinを用いる。同じpeerの多数requestで重みが増えないようにする。

## M11. 大packetと緊急packetの両方を進める

global radio creditとclass/peer deficitを別に持つ。frameがまだhardwareに渡っていない間は、全radioを一jobのためにロックしない。ただし小packetがrefillを食い尽くし、大packetが永遠に送れない方式も禁止する。

各classに、受入済みの最低service分とCRITICAL/CONTROL保護分を持つ有限airtime予約を割り当てる。大jobの将来service機会を明示的に確保し、その予約分を後着の小jobへ無制限に貸さない。空きの貸出しは、予約した最遅service時刻に返せる分だけとする。予約総額をglobal/refill能力以上に約束しない。

実装は「選択→全体credit待ち」を「各classの有限予約→実行可能job選択」へ変える。参照アルゴリズムは、periodごとのairtime割当、有限の未使用credit、peer deficit、実送信可能なslotでの選択とする。各creditの上限・期間越え・借入/返済上限はprofileに保持し、最大frameが貯まる条件を検査する。借入不能なら延期を返す。

公平性の保証は、宣言した負荷、合法な送信機会、有限frame長が成立する条件付きとする。悪条件の再送も同じpeer/serviceへ課金し、一peerが全予算を独占しない。radio自体が使えない期間を隠して「待ち上限達成」としない。

## M12. 遅延・reserve・過負荷

CRITICALの最大待ちには、送信中の非preemptible frame、予定/RX guard、法規待機、予約credit、必要なwake、Flash時間を含める。受付前にそのboundがlatency targetへ収まるか判定する。優先queueへ入れたことを期限保証としない。

reserveはqueue件数、永続storage、radio airtimeの三つに必要である。全DATA枠が埋まってもreceipt、credit更新、時刻再同期、失効、drainが進む最小枠を確保する。CONTROLという名前だけで無制限優先にしない。Joinや拒否応答の増幅も同じ全体予算へ入れる。

## M13. 再送と実TX完了

hop retryは隣接custodyの回復、E2E retryは最終証拠の回復である。両方が独立した短周期で同じpayloadを増殖させない。進行段階と次の受信機会を共有し、deadline/clock品質を保ったままbackoffする。

RTOは、明確なsampleのRTTとばらつきに、hop保存・wake・予定・法規待機を含める。再送後に対応が曖昧なRTTは更新へ使わない。radio BUSYは遠隔失敗でもTX成功でもない。実TXか不明なtimeoutはairtimeを保守的に消費し、証拠不明を残す。retry予算終了時はserviceを保留できるが、CoreのACTIVEを自動FAILEDにしない。

設定変更中のstaged frameは送信直前にsession、plan、profile、deadline、保管内容を再検査する。古いenvelopeだけを破棄し、元のownerが必要なら同じmessage IDで作り直す。driver失敗・再起動でnonceを再使用しない。

## M14. 法規・機器能力を探索範囲にしない

最適化へ渡すPHY候補は、検証済みallowlistだけとする。機器種別、antenna、channel/BW、最大出力、carrier sense、最大連続TX、pause、時間窓の制限をportが独立に強制する。設定validatorだけでなく実送信の最後の境界にも置く。

地域名JP、現行driverの5ms/50ms/400ms、あるいはARIBの概要ページだけで、あらゆる設置条件に適合したと判断しない。該当する規則・機器区分・適合資料・profile revisionを受入manifestへ保存する。CADと法規上必要なcarrier sensingを同一視しない。必要な検知機能がない候補はunsupportedとする。

## M15. 新しい診断・資源計上

公開する量はframe種別別のairtime、queue age、reservation debt、regulatory defer、CCA busy、RX機会不足、time uncertainty、PHY変更理由、sample数と期間、pending operation、rollback理由。RSSIのみのhealth表示は禁止しないが、それを配送成功率の代用にしない。

driver callbackはcaller ownership・blocking上限・失敗時のstateを文書化する。長い同期処理は実測して実行予定に含め、必要ならevent-driven start/poll/completeへ分離する。APIを非同期という名前に変えるだけで応答時間が改善したと主張しない。

## M16. 受入

T03–T06、T09、T14–T19、T23–T28を実施する。固定PHY、現行power-only、静的予約、競合方式、混合方式を同じ負荷・同じ故障traceで比較する。減力後のsample隔離、大packet進行、critical latency、最終ACK喪失時の再会、clock不確か時の送信抑止、上限+1のreassembly拒否を必須とする。性能改善でnonce安全性や所有保全を緩めない。
