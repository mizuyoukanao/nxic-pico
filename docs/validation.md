# DHCP・混合入力の検証 (2026-10-17)

## Windows NCM コード10の修正 (2026-10-19)

接続中の `NXIC NXIC Network` (`2E8A:10C0`) を読み取り確認し、Microsoft `usbncm.inf`、ドライバー `10.0.26100.9711` でProblemCode 10、ProblemStatus `0xC0000483` を確認しました。

SDK 2.2.0内のTinyUSBはGET_NTB_PARAMETERSで `wNdbInDivisor=1` / `wNdbOutDivisor=1` を返していました。これは更新後のWindowsの検証条件に合わず、[TinyUSB #3913](https://github.com/hathach/tinyusb/issues/3913) で同じコード10が報告されています。[上流修正 #3914](https://github.com/hathach/tinyusb/pull/3914) に合わせ、両値を4へ変更しました。ドライバーの実際のデータ配置は元から4バイト整列です。

- `cmake/ncm_windows_compat.cmake` がSDKのNCMソースを読み取り、ビルドディレクトリーへ修正版を生成します。ファームウェアはそのファイルを使い、共有SDKのファイルは変更しません。
- 想定したフィールドが見つからないSDKでは、修正を省略せずCMakeエラーにします。既に両値が4のソースも処理できます。
- USBディスクリプターと実際のNCMクラスドライバーを使う `ncm` / `ncm_before` テストを追加。修正前の制御要求応答がWindowsのdivisor条件を満たさず、修正後が満たすことを確認しました。USBハードウェア転送だけをモック化しています。
- MAC文字列、制御・データインターフェース、alternate setting切替、リンク・速度通知、送信NTBの単独・集約パケットの整列、最大Ethernetフレームの送受信、送信中のバックプレッシャーも確認しました。
- 既存のDHCP／混合入力テストを含め、CTest **5/5成功**。RP2350向けUF2のビルド成功。

修正後ファームウェアの実機書き込みとWindowsでのコード10解消・DHCP取得・HTTP表示は未確認です。BOOTSELで `build/nxic_pico.uf2` を書き込み、通常モードからCtrl+Alt+Wで設定モードに入り直し、USBを再接続して確認してください。

## 検証済み

- Windows上で pico-sdk 2.2.0、Arm GNU 14.2.1、`pico2` / RP2350向けにビルド成功。成果物は `build/nxic_pico.uf2`。
- TinyUSB: `71ed9d1498e04f99f69fc8c0336ca41ec7a576af`、Pico-PIO-USB: `5a37a66dc5d3fbe0ef3cdbeda923a757440f984f`。
- DHCPの純粋関数テスト: DISCOVER/OFFER、REQUEST/ACK、NAK、更新、別サーバーの選択、PAD、長いオプション、不正長、Cookie、最小応答長。ゲームパッドと合わせて不正パケット2万件も入力。
- 実際のlwIP IPv4/UDPと `config_mode.c` のNCMグルーを用いた統合テスト。修正前の設定で送信元0.0.0.0の要求が破棄されることを再現し、修正後は同じフレームからOFFER/ACKが返ることを確認。
- NCM受信のバックプレッシャー、連結pbufへのコピー、メモリ不足後の復帰、送信タイムアウト時のエラーをテスト。
- 実際のHIDコールバック・初期化タスク・マッピングを使用し、USB転送APIのみをモック化。初代Proコンのハンドシェイク、SPI較正要求、充電グリップ左右の合成、Switch 2のBulkコマンドによるUSB入力開始・レポート選択、転送投入の再試行、無関係なACKの無視をテスト。
- スティック連続値、Switch 2のボタン各bit、左右Joy-Conの不要軸除外、ボタン合成、軸ごとのキー優先・解放、マウスエイムの併用、USB取り外し・500ms通信途絶時の入力解除をテスト。

再実行:

```powershell
# SDK不要のパーサー／合成テスト (ホスト用GCC)
.\tests\run.ps1

# 同じSDKのlwIP/TinyUSBヘッダーを使う統合テスト (ホスト用GCC)
cmake -S tests -B build/host-tests/integration -G Ninja -DPICO_SDK_PATH=C:/Users/mizu/.pico-sdk/sdk/2.2.0
cmake --build build/host-tests/integration
ctest --test-dir build/host-tests/integration --output-on-failure

# ファームウェア
cmake -S . -B build -G Ninja -DPICO_SDK_PATH=C:/Users/mizu/.pico-sdk/sdk/2.2.0 -DPICO_TOOLCHAIN_PATH=C:/Users/mizu/.pico-sdk/toolchain/14_2_Rel1
cmake --build build
```

## 実機検証待ち

基板への書き込み、PCのNCMドライバーでの実DHCP取得、HTTP表示、各コントローラー／充電グリップ／Joy-Con 2接続機器での動作は未確認です。ホストテストはUSB電気特性、ハブの電源、PIOの帯域・タイミングを検証しません。

1. 設定モードでPCへ接続。IPv4をDHCPにし、`192.168.7.16/24` を取得してLEDがシアンになることを確認。
2. `http://192.168.7.1` と `/api/config` を開き、LEDが緑になり、保存・再起動が動作することを確認。
3. 通常モードでUSBハブにキー・マウス・各機種を接続。小さなスティック入力、各ボタン、マウスエイムの同時使用、方向キーを離した後のアナログ値への復帰を確認。
4. Joy-Con／Joy-Con 2は左右を個別・同時に確認。初代はUSB通信対応充電グリップを使用し、Joy-Con 2はUSB列挙される物理接続を確認。純正以外の変換アダプターはVID/PID・レポート形式が異なる場合があり、未対応。
5. 押下中に取り外して入力が残らないこと、再接続後に復帰することを確認。中心ドリフトとスティック端までの可動域も確認。

現実装では初代の工場較正のみを取得します。Switch 2系は既定較正を使用するため、個体差による中心位置・可動域は実機で確認が必要です。コントローラーのIMU、追加ボタン、Joy-Con 2マウスセンサーは転送しません。

## プロトコルの参照資料

入力・DHCPの追加コードは本プロジェクト向けに実装し、以下の参考プロジェクトの実装コードは取り込んでいません。NCM互換修正は既存SDKのTinyUSBへ上流の2フィールド修正をビルド時に適用するもので、生成ソースには元のMITライセンス表記を保持しています。

- [Switch 1 USB HIDと充電グリップ](https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering/blob/master/USB-HID-Notes.md)
- [Switch 1 入力・SPI読み出し](https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering/blob/master/bluetooth_hid_subcommands_notes.md)
- [Switch 1 スティック較正](https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering/blob/master/spi_flash_notes.md)
- [Switch 2 HIDレポート](https://github.com/ndeadly/switch2_controller_research/blob/master/hid_reports.md)
- [Switch 2 USB初期化コマンド](https://github.com/ndeadly/switch2_controller_research/blob/master/commands.md#subcommand-0x0d---initialise-usb)
- [Switch 2 USBインターフェース](https://github.com/ndeadly/switch2_controller_research/blob/master/descriptors.md)
- [SDLのSwitchドライバー](https://github.com/libsdl-org/SDL/blob/main/src/joystick/hidapi/SDL_hidapi_switch.c): 充電グリップ右側でハンドシェイクACKが返らない場合を確認し、タイムアウト後もUSB専用モードへの初期化を続行。

提示されたNS-LIB-HID、PicoSwitch2、NS-PC-Controlは主にコントローラー出力・エミュレーションの参考です。joycon-quatの姿勢デコードは今回のスティック・ボタン混合には使用していません。出力は既存の初代Proコンのままで、Switch 2ネイティブ出力への変更は行っていません。
