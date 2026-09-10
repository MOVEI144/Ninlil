# 統合候補の範囲限定検証 — 2026-09-10

この結果はGitHubへpushしていないローカル変更に対する実行記録。
GCC/Clang各通常・ASan/UBSanの4構成、それぞれ23/23 CTestが通過。
一つのCTest entryは17件のPythonキャンペーン試験を実行する。

実Coordinator/検索/codecと、未変更の既存再起動・準備lease試験を含む。
policyと永続書込みcallbackはメモリ上のfixture。実node/pump入口の試験は
同一bytesのCを使うが、他のCore/暗号/無線/時計および型宣言の一部は明示double。
どの試験も7台実機、完全SDK/ABI、実暗号/ESP-IDFの合格証拠ではない。

静的解析は12 portable単位と6 node/pump境界単位にGCC/Clang各一回、36コマンド。
後者は同じ明示fixture宣言を使う。終了0、診断なし。

fanoutの新しい停止境界試験は、b388基準の実Cで失敗（exit 1）し、修正後に
成功（exit 0）した。保存エラーを通常の宛先待機へ読み替えて次対象へ進まない。

配布ZIPのevidence/native-matrix-finalにはcommands/stdout/JUnit/全入力hash、
evidence/static-analysisには実際の解析コマンドとログ、
evidence/regression-witnessには修正前後の再現ログを含む。
results.jsonはその集約。過去の狭い試験の結果を最新の全体PASSにはしない。

全SDK・ESP-IDF・全体format/LOC/package/fuzz、7台HIL、電源断・電流・fieldはNOT_RUN。
詳細と未完成のPHY/大規模配信統合は [実装記録](../../INTEGRATED_ROUTING_2026-09-10.md)。
