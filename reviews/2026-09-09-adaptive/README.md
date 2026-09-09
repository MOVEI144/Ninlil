# 2026-09-09 既存コードレビュー — 適応型通信を拡張する前の監査

基準: `MOVEI144/Ninlil` PR #15、commit `a950f7cb4628fe8b9c579794ab4bcb04c9b1f05d`。mainとの差分は377 file / 38 commitの統合PRであり、このレビューを全377 fileの完全監査やmerge承認として扱わない。

この作業では**以下の主要経路をsource-levelで追跡し、二つのモジュールの挙動を独立に再現した**。本書は確認できた範囲を正確に示す。未読箇所・未実行gateを過去の73-test記録で補完しない。production codeの修正は行っていない。

## 結論

自律owner、認証/配送証拠、Relay、設置ライフサイクルの基盤は存在する。一方で、現在の実装を「512台自律network」「PHY/MAC共同最適化」「E2E容量を考えた経路最適化」と扱う根拠はない。

以下のR01〜R10は、**確認済み制約・設計差分・検証不足**である。すべてを現行契約への違反や新規バグと呼ばない。今回の単独再現では新しいmemory-safety欠陥や暗号突破を立証していない。安全であることの証明でもない。

次の実装契約は[適応型通信仕様](../../docs/ADAPTIVE_OPTIMIZATION_SPEC.md)から読む。

## 1. 読み取り範囲

リンクはレビュー基準のimmutable SHAを指す。関数単位の観察と将来要求を区別する。

| 層 | 読んだ主なファイル/範囲 | 確認内容 |
|---|---|---|
| ルール/現況 | AGENTS、STATUS、ARCHITECTURE、ENGINEERING_STANDARD、CODING_STYLE、FAILURE_MODEL、TESTING | boundedness、commit-before-effect、evidence区分 |
| public/容量 | `include/ninlil_node.h`, `include/ninlil_network.h`, `include/ninlil_airtime.h`, `include/ninlil_radio_adapt.h`; `ninlil.h` 1〜210行 | 上限、APIの責任、time/evidence型 |
| owner state | `src/ninlil_node_internal.h` 1〜220行 | 16-bit bitmap、queue/flow/handshake state |
| 経路 | `ninlil_network_route.c`, `ninlil_field_plan.c`; `ninlil_network.c`のstage/ack/activate/withdrawとrestore冒頭 | cost、hysteresis、pending、commit、proof |
| plan transport | `ninlil_node_routes.c`のrequest/receive/release/proof/pending前半 | live証拠、失効/旧leaseとの関係 |
| 無線観測 | `ninlil_node_links.c`, `ninlil_node_radio.c`, `ninlil_radio_adapt.c` | probe、実送信の区別、観測入力 |
| MAC/port | `ninlil_airtime.c`, `ports/esp32s3/ninlil_network_pump.c`; `ninlil_sx1262_radio.c` 1〜270行 | queue、credit、TX結果、profile設定冒頭 |
| 暗号/制御 | `ninlil_secure.c`、`ninlil_node_control.c` 1〜110行、`ninlil_node_forward.c` | reserve-before-encryption、replay判定、bootstrap転送 |
| 保存/中継 | `ninlil_collect.c`、`ninlil_relay.c` 1〜230行 | retained record参照更新、custody commit/verify |
| 配信/設置 | `ninlil_group.c`、`ninlil_root_replacement.c`、DEPLOYMENT_LIFECYCLE | target snapshot、outcome、Root fence |
| 既存証拠 | maintenance evidence README、SOURCE_BUDGETS、ADAPTIVE_NETWORK_CONTRACT、PR metadata/comments | software/HILの境界、旧simulator範囲 |

完全には監査していないもの: Core send/receive/replay全経路、全storage port、EDHOC upstreamと適合patch全体、全issuer/USB管理tool、全test/fuzz、physical driver後半、全decommission/sleep経路。これらを「レビュー済み」にはしない。レビュー対象の限定は仕様作成を止める理由ではないが、PR #15を安全にmergeできるという根拠にもならない。

## 2. 指摘

### R01 — 大規模化の上限は一つではない（scale enableのblocker）

[public node](https://github.com/MOVEI144/Ninlil/blob/a950f7cb4628fe8b9c579794ab4bcb04c9b1f05d/include/ninlil_node.h)は16 member。[internal](https://github.com/MOVEI144/Ninlil/blob/a950f7cb4628fe8b9c579794ab4bcb04c9b1f05d/src/ninlil_node_internal.h)は64 edge、32 flow、16-bit `member_acks`/`dynamic_members`、8-bit cursorなどを持つ。[network header](https://github.com/MOVEI144/Ninlil/blob/a950f7cb4628fe8b9c579794ab4bcb04c9b1f05d/include/ninlil_network.h)の512 node/2048 edgeは別層。

影響: 定数16だけ変えるとbitmap/索引/常駐session/永続slot/制御量の契約が崩れる。16台runtimeが契約違反という意味ではない。

対処: F2/F3でmembership、neighbor、session、attachment、waveを分離。boundary+1、16→17、255→256、511→512の型/容量試験を要求する。

### R02 — 混雑観測が実際のroute costへ入っていない（最適化品質の高優先差分）

[`ninlil_node_links.c:report`](https://github.com/MOVEI144/Ninlil/blob/a950f7cb4628fe8b9c579794ab4bcb04c9b1f05d/src/ninlil_node_links.c)はqueue_us欄を0として送る。[`network_route.c:edge_cost`](https://github.com/MOVEI144/Ninlil/blob/a950f7cb4628fe8b9c579794ab4bcb04c9b1f05d/src/ninlil_network_route.c)にはqueue加算があっても、その経路で実queue情報は供給されない。pumpのRX infoもこの経路のmetricへ渡されていない。

影響: probeで良いRelayが混雑していても、式だけから混雑回避できるとは言えない。RSSI/SNR収集済みとは主張できない。

対処: O1、R2/R4/R5。queue/eligible/TX/receipt/commitを実測し、同一RSSI・異なるqueueの反証試験RT02。

### R03 — 最短の片方向probe費用とE2E証拠最適化は別（高優先差分）

[`ninlil_coordinator_select`](https://github.com/MOVEI144/Ninlil/blob/a950f7cb4628fe8b9c579794ab4bcb04c9b1f05d/src/ninlil_network_route.c)はdirected edgeの費用を加算する。逆向きreceipt、wake、radio干渉、共通故障点、他flowの予約需要はこのcostにない。一つのpathを返し、backup候補の共同容量を評価するAPIではない。

対処: R3〜R5。片方向良好/逆方向不良、共通bridge、共有受信機、hidden terminalを別に評価。安全性bugとは断定しない。

### R04 — 一つのpending planが他flowの計画を止める（scaleの高優先差分）

[`network_route.c`](https://github.com/MOVEI144/Ninlil/blob/a950f7cb4628fe8b9c579794ab4bcb04c9b1f05d/src/ninlil_network_route.c)のselect/tickは `c->pending.epoch` が存在すると新規計画を拒否する。[`network.h`](https://github.com/MOVEI144/Ninlil/blob/a950f7cb4628fe8b9c579794ab4bcb04c9b1f05d/include/ninlil_network.h)はpending一個、lease最大60秒。32 flowもroot-star双方向で15子機なら30 flowとなる。

影響: 少数構成の保守的直列化として理解できるが、sleep/不達を含む大規模profileへそのまま適用できない。全DATAが必ず60秒止まるとは主張しない。

対処: R7。独立した競合key、有限並列transaction、独立deadline/proof、未完了所有の保全。RT10。

### R05 — 8:4:3:1は送信件数比でありairtime比ではない（再現済み・仕様差分）

[`ninlil_airtime.c`](https://github.com/MOVEI144/Ninlil/blob/a950f7cb4628fe8b9c579794ab4bcb04c9b1f05d/src/ninlil_airtime.c)のclass選択は16要素schedule。全体creditはairtimeを引くがclass deficitではない。

再現: 常時backlogを維持した16回の選択でframe数8/4/3/1。前三classを各10,000 us、BULKを400,000 usにするとclass airtimeは80,000/40,000/30,000/400,000 us。BULKは件数6.25%でも、この合計airtimeの約72.7%。これはscheduler accountingのモデル入力であり、RF実測ではない。

対処: P5/PM01。現行のframe公平性を壊れた実装と呼ばず、airtime公平性の新契約を明示する。

### R06 — credit待ちBULKが未送信のCRITICALを待たせる（再現済み・意図された現契約）

同モジュールは `waiting` の選択jobを保持する。400,000 us/s budgetで400,000 us BULKを先に選択し、後から10,000 us CRITICALを入れる。110,000 us時点でcreditは44,000 usあるが `next` はEMPTY、BULKが先頭のまま。

[`ninlil_airtime.h`](https://github.com/MOVEI144/Ninlil/blob/a950f7cb4628fe8b9c579794ab4bcb04c9b1f05d/include/ninlil_airtime.h)がこの予約を明記しており、新規bugとはしない。短い緊急待ちSLOを追加する際のblocking条件である。

対処: P5/PM02。TX前の限定追越しとBULK agingの両方を試験。TX中preemptionを仮定しない。

### R07 — PHY最適化の実装範囲と観測の意味（高優先差分）

[`ninlil_radio_adapt.c`](https://github.com/MOVEI144/Ninlil/blob/a950f7cb4628fe8b9c579794ab4bcb04c9b1f05d/src/ninlil_radio_adapt.c)は出力だけを変更する。8-probe窓、3回の新しいtimestamp、30秒dwell、6/8以下やstaleで最大値復帰。下げ幅は3 dB。

このpolicy単体にはprofile/route/attempt集合IDがなく、独立窓かrolling windowの重複かを証明できない。実callerはrolling 8-bit windowを使う。これを「独立した24成功で品質保証」と解釈しない。SF/BW/CR/channelの合意もない。

対処: O1/P2/P7/P8。profile/出力別の観測世代、重複sample排除、対向の受信合意。単独試験では下げ・loss/stale復帰・同一timestamp重複非加算を確認した。

### R08 — 平常時controlの拡大費用（scaleの高優先差分）

[`ninlil_node_links.c`](https://github.com/MOVEI144/Ninlil/blob/a950f7cb4628fe8b9c579794ab4bcb04c9b1f05d/src/ninlil_node_links.c)はmemberごとのprobe/reportを走査する。[`ninlil_node_control_send`](https://github.com/MOVEI144/Ninlil/blob/a950f7cb4628fe8b9c579794ab4bcb04c9b1f05d/src/ninlil_node_control.c)はbootstrap transportを利用し、[`ninlil_node_forward.c`](https://github.com/MOVEI144/Ninlil/blob/a950f7cb4628fe8b9c579794ab4bcb04c9b1f05d/src/ninlil_node_forward.c)はhop制限・digest cache8・pending1で有限転送する。

boundedであることと数百台で効率的なことは別。denseな常駐相互neighborをそのまま拡大するとprobe対象は二次的に増え得る。有限cacheがあるだけで平常時control overheadやtail latencyの目標を証明したとはしない。

対処: P6/F2/F8/F9。sparse neighbor、O1受動観測、global control budget、Join storm試験。

### R09 — Group helperはidentity-bound大規模配信の完成形ではない（高優先差分）

[`ninlil_group.c`](https://github.com/MOVEI144/Ninlil/blob/a950f7cb4628fe8b9c579794ab4bcb04c9b1f05d/src/ninlil_group.c)のtargetはuint16 address。terminal処理はUNKNOWN等でもcompletedへ加算する。固定target集合、commitエラー時poisonは確認できるが、helper自体にstable identity/世代snapshotやE2E group-to-Coreの二重書込みtransactionはない。

これだけで現行runtimeに誤配送があるとは断定しない。現行のaddress-idle/fenceなど別境界による防御を無視しない。ただし新規large-scale APIでshort addressだけを永続targetの意味にしない。

対処: F5〜F7/F10。identity-bound target、durable intentとCore key照合、receipt種類別集計、sleep targetがwaveを永久占有しない試験。

### R10 — 試験の範囲と性能主張の差（受入blocker）

[旧adaptive contract](https://github.com/MOVEI144/Ninlil/blob/a950f7cb4628fe8b9c579794ab4bcb04c9b1f05d/docs/ADAPTIVE_NETWORK_CONTRACT.md)のsimulatorは2〜5台、fixed direct、92-byte、理想的固定calendar。新secure/Relay runtimeの512台干渉モデルではない。[maintenance記録](https://github.com/MOVEI144/Ninlil/blob/a950f7cb4628fe8b9c579794ab4bcb04c9b1f05d/docs/evidence/2026-09-09-maintenance/README.md)も実機Root交換、電源断、電流/寿命、field等を未実施と区別する。

対処: G01〜G09とF11。正確なsource SHAで全適用gateを実行し、物理台数/仮想台数、過去記録/今回再実行を区別する。

## 3. 維持する安全な実装方針

[`ninlil_secure.c`](https://github.com/MOVEI144/Ninlil/blob/a950f7cb4628fe8b9c579794ab4bcb04c9b1f05d/src/ninlil_secure.c)はcounter予約後に暗号化し、復号成功後にreplay bitmapとplaintext出力を更新する。新しいprofileやcacheはこれを迂回しない。upstream crypto全体の監査を行ったという意味ではない。

[`ninlil_collect.c`](https://github.com/MOVEI144/Ninlil/blob/a950f7cb4628fe8b9c579794ab4bcb04c9b1f05d/src/ninlil_collect.c)はlive記録をrewriteし、公開後にRAM参照を再配置・検証する。[`ninlil_relay_receive`](https://github.com/MOVEI144/Ninlil/blob/a950f7cb4628fe8b9c579794ab4bcb04c9b1f05d/src/ninlil_relay.c)はcustody commitとverify後に受付成功を返す。単に容量が苦しいからこの保管責任を弱めない。

[`ninlil_root_replacement.c`](https://github.com/MOVEI144/Ninlil/blob/a950f7cb4628fe8b9c579794ab4bcb04c9b1f05d/src/ninlil_root_replacement.c)は新Root member記録を保存してから旧sessionを閉じ、古い物理Rootが新Rootの秘密鍵を持つふりをして再開しない。generationの境界を性能最適化で巻き戻さない。Root交換全体のHILを実施したという意味ではない。

## 4. 今回の実行証拠

containerからGitHubへの直接cloneはDNS解決失敗。connectorでsourceを取得し、`ninlil_airtime.c`、`ninlil_radio_adapt.c`とそれぞれのpublic headerをローカルへ置いた。4 fileのGit blob SHAが基準と完全一致することを検査した。

完全なSDK checkoutではないため、ローカルの基底 `ninlil.h` は実ソースから使用するerror定数、traffic enum、標準型だけを取り出した宣言header。これを完全なSDK integration buildと呼ばない。監査対象の二つのC実装と二つのpublic構造体headerは未変更。

| gate | 今回の結果 |
|---|---|
| GCC 14.2.0 strict C11による単独witness | PASS |
| Clang 17.0.0 strict C11による単独witness | PASS |
| GCC ASan/UBSan、leak検査あり | PASS |
| Clang ASan/UBSan、leak検査あり | PASS |
| 全73 CTest、Python、static analysis、fuzz、package、全size gate | 今回は未実行 |
| ESP-IDF build、RF/HIL、hard-power、電流、field | 今回は未実行 |

[witness.c](witness.c)はbaselineの挙動を再現するもので、新schedulerの受入testではない。将来改善したsourceへ同じ期待値を強制しない。[run_witness.sh](run_witness.sh)は完全なrepoで再実行するための手順で、Git blob identityを先に照合する。今回実行したローカルcompileは同じ二つのC実装・witnessと上記縮小headerを使用した。

出力とsource identityは[results.json](results.json)。基準commitのCodeRabbitコメントはreview skipped、hosted CIは意図的skipという既存状態であり、この仕様作成で「CI green」「全コードレビュー完了」に昇格させない。

## 5. 次のレビューgate

まずR01〜R10を参照するS0〜S2で、観測と反証試験を実装する。別途PR #15全体について、未監査のCore/保存/認証port/全diffとfull CIを確認する。重大な現行契約違反がそこで見つかったら、性能拡張と別の修正PRで先に閉じる。仕様の合意とαのmerge承認を混ぜない。
