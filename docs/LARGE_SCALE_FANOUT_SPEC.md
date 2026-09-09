# 大規模1対多・多対1仕様 v1

状態: 実装前仕様。基準SHAと共通条件は[適応型通信仕様](ADAPTIVE_OPTIMIZATION_SPEC.md)。**512台は将来profileの検証上限候補であり、既存自律runtimeの対応台数や保証性能ではない。** 台数NはRootを含む参加機器総数とする。

## F1. 配信の定義

1対多は固定target snapshotに対する個別の論理配送。多対1は独立したsourceの集中送信であり、上りDATAと下りreceiptの両方を容量計算する。packetを一回broadcastしたことを全宛先REMOTE_STOREDにしない。

初版の大規模profileはpairwise暗号のunicast展開を正本とする。共有payloadのon-air multicast/group keyは別version・別セキュリティ/失効/性能gateまで無効。集約receiptも各宛先の認証済み事実へ遡れる場合だけ将来追加できる。ALL/quorumや業務上の成功判断はProduct Adapterの責務。

## F2. 現行限界と単純拡大の禁止

基準 `include/ninlil_node.h` は16 member。内部には64 edge、32 flow、16-bit membership bitmap、8-bit cursor、一つのhandshakeとpending planがある。`include/ninlil_network.h` の512 node/2048 edgeは別層の容量である。

各member indexに二つの8 KiB counter slotを要求する現行port契約をそのまま512へ伸ばすと、counter slotだけで8 MiBとなる。これは単純な必要量計算であり、実際に割り当てられたRAM/Flash量ではない。`NINLIL_NODE_MEMBERS_MAX`だけ変更して対応完了としない。

新profileでは以下を分離する。

- Authorityのglobal membership/世代fence。
- 有限のlocal neighborとbackups。
- 現在常駐するE2E/hop session。
- rootへのattachmentと必要な例外flow。
- 同時実行中のdelivery、group wave、plan/handshake。

Endpointへ全networkのrosterを配布せず、Root/選択neighbor/必要相手の認証可能な資格情報だけを渡す。通常controlも全Relayへのfloodを避ける。有限bootstrap/recoveryのための経路は残し、製品側に共通ネットワーク制御を再実装させない。

## F3. Profile候補と資源公開

| 上限 | LEGACY16 | ROOT128候補 | ROOT512候補 |
|---|---:|---:|---:|
| global membership（Root含む） | 16 | 128 | 512 |
| local active neighbor | 現行member表の範囲 | 16 | 16 |
| Coordinator directed edge | ownerは64 | 1024 | 4096 |
| active/backup root attachment | 現行flow利用 | 最大127、各backup最大2 | 最大511、各backup最大2 |
| explicit exception flow | 32 | 64 | 128 |
| 同時plan transaction | 1 | 4 | 8 |
| Root同時handshake | 1 | 4 | 8 |
| Root常駐E2E sessionの候補上限 | member表の範囲 | 64 | 128 |
| group operation | 既存group契約 | 8 | 8 |
| group target数/operation | 自律ownerの到達範囲内 | 127 | 511 |
| group全体実行wave | 既存group契約 | 最大32 | 最大32 |

新値は設計・試験対象であり、同じESP32-S3で動作するという主張ではない。任意のtargetで小さい上限を選べる。ROOT512には既存モデル2048 edgeを超えるversion付きprofileが必要。compatibleなfallbackは明示し、対向が理解しない新上限を強要しない。

各build artifactは、役割別の静的RAM、open時heap、暗号処理peak、stack high-water、identity/counter/control/delivery/bulkのFlash配分、最悪reopen時間、CPU/step上限を出力する。platformの実容量を越えるprofileはconfigure/open時に拒否する。メモリ予算を足りないまま推測で成功扱いしない。

session cacheはmembershipのLRUではない。session evictionは新TXを止め、queued暗号frameを無効化し、counter予約と必要な永続境界を保った後に鍵を消去する。次回はfresh EDHOCで再開し、payloadと論理IDは保持する。counter slot再利用は旧fingerprintの再開禁止とatomicな新bindingを証明してから可能にする。既存セッションを単にメモリから捨ててslotを他peerへ渡さない。

active session数がmember数より少ないprofileでは、rekey/Resumeのairtimeと待ちもC1へ入れる。全512台に同時secure sessionがあるふりをしない。cache churnで要求SLOを満たせない場合は拒否し、常駐数を増やせる別artifactまたは負荷/設備変更を提案する。

## F4. 容量は台数だけで決まらない

容量manifestは `(N, powered_relays, radios, hop_distribution, PHY, payload, rate, burst, wake, required_evidence, loss/interference)` を一組で公開する。「最大512台」だけを性能表示に使わない。

手計算例: 1回の宛先別交換にDATA 100 ms＋receipt 50 msのTXが必要と仮定すると、Root以外511宛先には76.65秒のTXが必要。これは暗号/再送/CCA/guard/Relay/Flash/wakeを無視した架空の下限例でありNinlil実測ではない。一つのhalf-duplex radioで同じ交換を1秒以内に完了できるとは言えない。

全radio headを同じ競合領域に置いた場合、台数を増やしても容量が線形増加するとは限らない。複数headの相互干渉、所有の排他、fencing、receipt重複排除を別に検証する。最初のlarge profileは一つの論理Root、multi-Gatewayは別gateで有効化する。

## F5. Group v2の永続target snapshot

既存 `ninlil_group` はshort addressの配列とpending/inflight/terminalを管理する。完成したtargetとは成功したtargetという意味ではなく、UNKNOWNを含む終端もある。このAPIを業務成功集計として使わない。

新しい永続recordは少なくとも次を持つ。

| record | 必須内容 |
|---|---|
| GROUP_BEGIN | operation ID、authority/domain、request digest、payload reference/digest、service、要求証拠、期限/SLO、target数とsnapshot digest |
| TARGET_PAGE | operation ID、page番号、stable identity、snapshot時short address、membership/binding、個別logical message ID/冪等key |
| TARGET_INTENT | 発行予定target、個別contract digest、再開可能なsubmission intent |
| TARGET_ADMITTED | Coreのdurable acceptanceの参照 |
| TARGET_EVIDENCE | 送信元Coreがcommitした宛先別REMOTE_STORED/APPLICATION_ACCEPTED等の事実 |
| TARGET_TERMINAL | outcomeと理由、未達証拠を保存。UNKNOWNを成功へ変換しない |
| GROUP_RETIRED | 全必要保管条件と明示的retentionを満たした整理記録 |

同じoperation IDで内容/target snapshotが異なればCONFLICT。新Joinで実行中groupの対象を増やさず、撤去で未配送targetを消さない。単なるshort address再利用で別deviceへ配送しない。資格更新で同じidentityのbindingが変わった場合も元contractと照合し、再承認/再契約が必要なものを自動移送しない。

target manifestは16 target/page、最大32 pageを候補とする。全ページとpayloadのdurable commit・digest検査後だけGROUP_BEGINの受付成功を返す。途中crashは未公開manifestとして回復/整理し、部分配信を開始しない。未完了operationのGCは禁止。

## F6. GroupとCoreの二重書込み

別々のjournalでgroup admissionとCore submitを行う場合、どちらか片方の成功を推測しない。

1. GroupがTARGET_INTENTと個別idempotency keyを先にcommit。
2. Coreへ同じkey/contractでsubmit。既存一致なら同じmessage ID、内容が違えばCONFLICT。
3. Coreのdurable acceptanceを確認してTARGET_ADMITTEDをcommit。
4. crash復旧ではINTENTを再送せず、まず同じCore keyを照会/再submitして整合する。
5. evidence/terminalはCoreの確定事実から取り込み、重複OUTCOMEで二重集計しない。

Coreのterminal/dedupe履歴がGroupの照合より先にretireされないよう、保持参照または明示的retirement barrierを追加する。この条件が実装されるまでgroup v2をenableしない。payloadの共有保存は可能だが、最後の必要targetが責任を移すまで消さない。

## F7. Wave、sleep、無応答target

wave32は未完了の所有数ではなく、いまradio資源を与える実行機会の上限とする。実際のwave幅はC1、peer/Relay credit、session枠、class予算から32以下へ縮める。group展開が全32枠を奪って通常CRITICAL/CONTROLを止めない。

target状態を `PENDING / ELIGIBLE / OFFERING / OWNED_WAITING / EVIDENCE_SATISFIED / TERMINAL_OTHER` として別に観測する。sleep/offlineのOWNED_WAITINGはpayloadとcontractを保持したまま実行waveから外せる。再開機会を再割当しても新しい論理IDを作らない。

同じgroupで最初の32targetがofflineでも、到達可能な33番目以降が進めることを要求する。これは所有の取消/UNKNOWN化ではない。peer単位の再探索budgetと次回時刻を持ち、可能なtargetをagingで公平に選ぶ。全targetが無経路なら保存して明示的な容量backpressureを返す。

## F8. 多対1・一斉復電・Join storm

既知memberのResume、新規Join、critical application、通常telemetryを別queue/budgetにする。未認証requestはglobal/per-radio quotaも使い、identityの詐称でpeer quotaを回避させない。

起動jitterと指数backoffは宣言範囲内。全台へ同じ固定再送周期を配らない。backoff開始/成功/再起動でbudgetが無制限にリセットされないようにする。membership commit前のpeerはapplication通信へ昇格させない。

各sourceの定期報告offsetを分散するが、製品イベント発生を勝手に遅らせる権限はTrafficContractに従う。burstが容量を超えるときは、受入済みイベントは保持し、新規受付は理由を返す。latest値への置換は製品が明示した別delivery semanticsだけで可能。

Rootは下りreceipt/credit/同期の予約も持つ。上り成功数だけでmany-to-one性能を示さない。省電力nodeの応答機会を含めてE2E証拠達成率を計測する。

## F9. 制御・保存の漸近的コスト

steady-state controlはactiveなsparse neighbor edgeに比例させる。全memberへのroster再送や全組合せprobeを標準運用にしない。bootstrapの有限floodは常時経路の代替でなく、quota付き復旧手段に限定する。

ROOT512でも各Endpointが512個のsession/counter/routeを保持しない。RootはmembershipとattachmentをO(N)、neighbor観測をO(E)、active groupを最大8×511 targetで有限にする。N²サイズの常設all-pairs flow tableを作らない。値の一覧取得APIはpaginationとsnapshot versionを持つ。

stale観測と派生frameは安全に再生成できるものだけevict可能。未完了delivery、未確認custody、membership/revocation fence、counter、group intentはevict不可。長期切断下で有限記憶を使い切ったら受入停止が正しい結果。無期限の全履歴照会や永久の任意ID dedupeは別のretirement契約なしに約束しない。

## F10. 結果API

`query_group`はtarget snapshot、各証拠数、各terminal outcome数、owned waiting、admission拒否、未発行、最古待機、次回機会を返す。`completed`という一つの成功風の値だけにしない。

初期保存が完了した後、各targetは一つのprogress状態に所属する。`target_total = not_admitted + owned_active + terminal_total` のaccountingを保ち、evidence別の内訳は重複可能なので別軸とする。SATISFIEDは要求証拠を満たした宛先だけ。UNKNOWN/FAILED/CANCELLED/EXPIREDは別の終端。

Group操作のtimeoutは照会待ちのtimeoutとmessage deadlineを区別する。responseを失っても同じoperation IDで再照会できる。cancelは未試行部分だけ安全に取消可能かを検査し、試行済みtargetの効果を消せたとしない。

## F11. 段階試験と公開条件

| 軸 | 必須値/条件 |
|---|---|
| N（Rootを含む） | 2, 3, 16, 32, 64, 128, 256, 512、各profile上限+1 |
| 構造 | star、分岐Relay、1/2/3/4-hop、単一bridge、共通障害点 |
| 方向 | 1対1、1対多、多対1、group＋CRITICAL同時 |
| workload | 周期、同期burst、大小frame混在、bulk、低頻度 |
| fault | loss、reorder、duplicate、非対称、干渉、Root/Relay reboot |
| lifecycle | Join/revoke/撤去/移設/address再利用、旧packet帰還 |
| power/time | sleep、drift、全台復電、clock不明、GC中crash |

実装C Coreだけでなく、新owner/control/scheduler/cryptoの実際の状態機械をsimulatorへ接続する。重いcryptoをstub化した容量モデルは補助結果と明記し、暗号ありのCPU/RAM/handshake評価も別に実行する。既存の2〜5台static-direct simulatorを512台secure networkの証拠にしない。

特に、全wave対象offline、最終ACK喪失、Core submit直後group commit前crash、retirement先行、session slot再利用、511target中1target撤去、上限+1を必須反証試験にする。

HILはまず3台の既存再現、次に8/16/32台で校正し、物理台数と仮想台数を必ず別記する。128/512台は初期はhost qualificationのみと表示する。十分な実機台数なしに128/512 RF受入と呼ばない。長期試験は初期72時間、field候補7日を入口とし、それだけで1年運用やFlash寿命を保証しない。

公開artifactはnode数だけでなく、その負荷/PHY/hop/sleep/干渉範囲とp50/p95/p99、未完了込み期限達成率、admission拒否、control airtime、RAM/Flash/CPU、復旧時間を掲載する。共通G01〜G09未達のprofileはexperimentalのまま、出荷/現場投入の安定版にしない。
