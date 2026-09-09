# 大規模1対多・多対1 通信仕様 v1

[共通契約](README.md)に従う。要求IDはG01–G16。対象は固定した多数の相手への確実な配送と、その応答・イベント集約である。RF broadcastと全対象の処理完了を同一視しない。関連レビュー：RV01、RV07、RV09–RV14。

## G01. 台数とprofileを正しく宣言する

NはRootを含む登録機器数。検証点は16、64、128、256、512とし、比較用の10/50/100台も評価する。Root発の最大対象数はN−1。既存group APIの512 target、Coordinatorの512 node、実node ownerの16 memberは別々の上限である。

大規模profileは登録可能数に加え、同時認証session、直接隣接、active route、relay custody、Core owned message、group operation、inflight service、reassembly、journalsの上限を公開する。64/512の定数だけを増やして完成としない。未実装・未測定のprofileはopt-inの実験用とし、通常設定から選べない。

## G02. 名簿と実行stateを分離する

全登録機器のstable identity、権限・世代を保持するregistryと、現在使うsession/route/probeのworking setを分ける。Rootは全体のregistryを持てるが、Leafへ全機器の秘密stateや全対全routeを複製しない。Leafは自分、authority、必要な親/予備/通信相手を中心に有限のstateを持つ。

sessionを回収する際はlive keyを安全に閉じ、暗号counterを再利用しない。再activationはfresh EDHOCと必要なmembership/lease照合を行う。session回収は論理配送のID、payload、要求証拠、保管責任を消す操作ではない。切断中peerのcredentialを別identityへ差し替えてpendingを継承しない。

512peerを現在の二つの8KiB counter slotへそのまま割り当てると8MiBが必要となる。この配置を維持するprofileは実際にその容量を確保する。省メモリprofileでは同時session Kを制限し、inactive→activeの再認証費用を容量/SLO計算に含める。counter backend変更は別version・独立の電源断/nonce検証を必要とし、単純なslot再初期化で容量を節約しない。

## G03. 資源計算と開設時の拒否

実装は次をchecked arithmeticで計算し、足りなければopen/admissionを拒否する。計測前の推定値をboard上の実測値と混ぜない。

`RAM_required = registry_cache + K×session_state + E×edge_state + route_working_set + scheduler_jobs + group_indexes + reassembly + crypto_peak + worst_stack + safety_reserve`

`Flash_required = identity/authority/counters + owned_payloads + group_target_records + progress/receipt_reserve + GC_copy_space + retirement_fences`

| 資源 | 初期仕様の上限・扱い |
|---|---|
| 登録N | 最大512。selfを含む |
| graph edge E | 最大2048方向付きedge。全対全保持はしない |
| 同時group | 既存と同じ最大4を初期上限 |
| Group target | API上限512、domain内Root発は最大N−1 |
| service枠W | 初期比較は32。ただし保管件数と独立にし、理由付きで休止可能 |
| plan transaction | 新設最大4、競合する資源は直列化 |
| local PHY reassembly | peer1、node全体4、本文合計最大5632bytes |
| session K / route数 | role/board/profileで明示。省略・無制限を禁止 |

flowが上り/下りで別物なら、その両方を計上する。全N−1子機の双方向routeを常時保持するには最大2(N−1)本相当のstateが必要になり得る。現行32flowを増やすときはcursor、配列、loop、codec、復旧・保存予算まで監査する。疎なworking setを使う場合は、再設定費用と休止中のSLOを公開する。

## G04. Join/Resumeと一斉復電

既知機器Resume、未知機器Join、時刻再同期、未配送replayを別queueにする。Root/Relay/Leafの起動jitter、有限同時handshake、global ingress byte/airtime上限、再試行backoffを設ける。device IDを変えるだけでquotaを回避できない。

復帰した全nodeが全peerへprobeしない。認可済みの親/予備から開始し、別途許可された探索予算内で隣接集合を広げる。既知機器の重要配送に最低service枠を確保し、新規Join floodが全体を占有しない。同時復電はN台すべてで試験し、一部だけ先に起動して成功扱いしない。

## G05. 大規模容量受入

同一radio・競合領域について、DATA、各hop、受領証、Join、route/clock renewal、探索、再送、wake/guard、Flash/CPUを合算する。rootへの多対1は上りDATAだけでなく下りreceiptが必要であり、1対多も同様に往復コストを持つ。

初期の下界確認は `total_airtime >= Σ_target Σ_hop (DATA + 必要なACK/receipt/control)` とする。これはloss/retryや待機を含まない下界であり、実性能ではない。例えば実際のprofileで一対象あたり往復占有が0.5秒なら、511対象には理想条件でも合計255.5 radio秒以上を要する。これは仮定した算術例であって、Ninlilの測定値ではない。複数radioで短縮するには独立受信・干渉・scheduleの証拠が必要。

新しい要求は、既に受け入れたcritical/receipt予約と永続保管余力を侵害できない。容量不足時は受付不可または開始可能時刻の候補と支配資源を返す。未受入要求を多く拒否して、受入済み成功率だけを高く見せない。

## G06. GroupのSTART契約

Group ownerはoperation IDと以下のimmutable snapshotを、成功を返す前にdurable commitする。target列挙はcanonical順に正規化し、重複stable identityは拒否する。同じIDを別内容に再利用するとCONFLICT。

- network/authority identityと世代、sender identity、operation ID、schema version。
- payload本体または同じownerが保持責任を持つimmutable referenceとdigest、service、class、要求証拠、期限/最大延期の契約。
- 各targetのstable device identity、現時点のradio address、membership/binding世代、必要capability、対象順序。
- target集合全体のdigest、総件数、受付時の容量判断とpolicy version。

Group STARTの成功はGroup ownerが配信を引き受けたことを意味し、全targetへのCore submit完了・RF送信・REMOTE_STOREDを意味しない。新しくJoinしたnodeを実行中snapshotへ追加しない。撤去されたtargetを無言で対象外にしない。

## G07. Message bindingをCore/送信境界まで保持する

radio addressは配送identityではない。新Group ownerは各targetの固定identity/世代を、Coreのmessage IDに対応するdurable bindingとして保持する。Core submissionまたは統合ownerのjournalにその対応を保存し、送信直前まで検査できなければGroup v2を有効化しない。

START時だけidentityをチェックし、その後Coreの自動再送が裸のuint16 targetへ解決し直す構成は禁止する。address再利用時のpeer変更は、pending bindingと照合して拒否または明示保留する。core-only legacy配送を新しい世代保証付き配送と偽装しない。bindingの保存format追加はversionを上げ、POSIX/Flashのreplay・corruption・migration試験を持つ。

## G08. Group journalとCore journalの間のcrash境界

二つのstoreを一つのatomic transactionと仮定しない。次の順に実行し、どの境界でも再開可能にする。

1. Group journalへ `TARGET_INTENT` を保存する。target binding、完全なsubmission契約、固定idempotency keyを含む。
2. 現在の認可と固定bindingを同じexecution ownerで再検査し、そのkeyでCoreへdurable submitする。
3. 返されたCore message IDを `TARGET_ADMITTED` としてGroup journalへ保存する。
4. 再起動時にINTENTのみ残っていれば同じkey/同じ内容で照会または再submitし、既存Core messageとの対応を回復する。別keyを作って二重配送しない。

idempotency keyは保存済みの一意値、またはdomain-separatedなcanonical契約digestから作る。短いkeyだけで同一内容と推測せず、衝突時は完全な契約比較で拒否する。submissionが返答前にcommitされ得ることを前提にする。曖昧なstorage失敗時はauthoritative reopenが必要で、未実行と決めつけない。

## G09. 所有状態とservice状態を分離する

各targetのdurable状態は `PENDING -> INTENT -> ADMITTED -> TERMINAL`。結果は別軸でSATISFIED/EXPIRED/FAILED/CANCELLED/UNKNOWN等を保持する。service状態は `ELIGIBLE, SERVICING, WAIT_WAKE, WAIT_ROUTE, BACKOFF, WAIT_RECEIPT, STORAGE_BLOCKED` として独立に持つ。

W=32は同時に無線・Core進行をserviceする枠であり、長期保管責任を負うtarget総数ではない。sleep/partitionでservice枠を返しても、message ID、payload、INTENT/ADMITTED、receipt待ちを保存する。現在のgroup engineのinflight意味を黙って変えず、新schema/APIで区別する。

既にCoreへ渡したtargetを再度扱う際は同じmessage ID/契約へ戻る。WAIT_RECEIPTの短期応答機会やcriticalは優先できるが、永久に到達しないtargetが全枠を独占しない。pauseをCANCELLED/UNKNOWN/SATISFIEDへ変換しない。

## G10. 公平なwave生成

waveは固定32件を一斉投入するだけでなく、競合領域、次のwake、class、必要なroute/session、downstream creditから選ぶ。group間round-robin、group内peer/経路ごとのairtime配分を行い、group要求数の多い呼出側に優先度が自動増加しないようにする。

一つのRelayのslow pathだけで全体を止めない。一方、速い相手だけを終わらせ、遅い相手を無期限に放置しない。各targetに次回試行機会とservice ageを保持し、bounded探索予算内で再評価する。上位schedulerと二重にretry stormを起こさない。

## G11. 結果と受領証

各targetのSATISFIEDは、そのtargetからの要求証拠が認証・契約照合・永続化された場合だけ成立する。Relayの「送った」「預かった」や、他targetの結果を代用しない。REMOTE_STOREDで十分な要求とAPPLICATION_ACCEPTEDを必要とする要求を混ぜない。

Groupは `all_terminal` と `all_satisfied` を別に公開する。件数はtotal、pending、active、satisfied、terminal_non_success、unknown、capacity_deferred等を定義し、状態軸と結果軸の重複を説明する。`finished=true`を全成功と表示しない。個別結果を保持・照会できることが先で、aggregate表示はその導出値とする。

ALL/quorum/部分成功を業務上どう扱うかはProduct adapterが決める。quorumが成立しても残りの未完了targetの保管責任を消さない。取消・忘却は別の明示operationである。

## G12. 撤去・交換・取消・再参加

START後に同じaddressを別deviceへ再割当しても新deviceへ配送しない。固定identityが別addressへ移動した場合も、authorityが承認したbinding変更と元の契約を照合し、未検証の単純置換をしない。別domain移設へ古い命令を持ち込まない。

予定撤去はGroupのPENDING/INTENT/ADMITTEDも影響分析へ含める。強制撤去は通信成功ではなく未解決を返す。取消は未送信が証明できるtargetと、遠隔効果の可能性があるtargetを区別する。部分的に実行したgroupを原子的に巻き戻せると約束しない。

## G13. 大きなpayloadとbroadcastの範囲

初期公開対象は既存small-message profileに収まるpayloadとし、8/32/64bytesで評価する。Coreの256byte API上限や別bulk profileの64KiB上限を、同じradio frameへ直接送れると解釈しない。bulk利用は各targetに再開可能なobject/offset/receiptを持つ別のfeatureとして容量計算する。OTAの適用は対象外。

初版の確実配送はtarget別unicastを正本とする。共通payloadを保管時に共有してよいが、各targetの契約と所有refを維持する。将来のon-air共有配信は、共通本体＋時分散した個別証拠＋missing-only repairを比較する別gateとする。全台同時ACKは禁止し、group鍵の参加/撤去/失効/更新費用を先に定める。暗号保護を外して効率化しない。

## G14. 長期運用・有限履歴

未配送payload、未処理application inbox、拒否/失効fence、counter、必要なreceiptをGCで捨てない。progress/完了記録とGC copy用reserveを通常受付が使い尽くさない。

完了履歴の整理には、senderがそのgeneration/sequence以下を再送しないとdurable commitし、receiverがretired-through境界を保存する契約を用いる。holeのあるsequenceを飛び越して退役しない。通知喪失・両端再起動・遅延packet・overflowを試験する。crypto counterと論理message sequenceを兼用しない。

receiverが過去の個別証拠を整理した後の照会には、保存していない成功を作らず `RETIRED_EVIDENCE_UNAVAILABLE` 相当を返す。retirementは未受理application inboxを削除する権限ではない。既存の明示的transport retirementを、この新しい世代付き契約なしでlive自動GCへ流用しない。

Group FORGETは全保管責任の所在、対象のterminal/未解決、idempotency保持契約を検査する。履歴slotを空けたことだけで、同じoperation IDを新しい仕事として受け付け直さない。製品のexactly-once物理効果は保証しない。

## G15. APIと運用者に返す説明

新しい汎用Group ownerは `describe_limits / submit_group / query_group / query_target / pause_service / resume_service / request_cancel / prepare_remove / retire_history` を責任として持つ。API名・型番号は実装時に確定するが、同期/非同期、borrowed buffer lifetime、永続commitの意味、再起動後の効果を必ず文書化する。

表示すべき理由は「このRelay経路のcapacity不足」「次のwake待ち」「旧bindingの未解決配送」「受領証待ち」「unknown clock」「storage reserve不足」のように分ける。operatorが経路やSFを手動設定しないと復旧できない状態を標準にせず、自動でできない理由と必要な配置/容量条件を返す。秘密鍵やpayloadを通常診断へ出さない。

## G16. 受入

T01、T07、T09–T13、T16–T18、T20–T28が必須。特に最初の32targetが到達不能で33番目が到達可能なケース、group journal/Core journal間の全crash境界、START後のaddress交換、全N台復電、criticalとbulk fan-outの同時負荷、全資源上限+1を検査する。N台の名簿を読み込めたことをN台通信の合格にしない。
