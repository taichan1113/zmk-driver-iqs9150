[English](#english) | [日本語](#日本語)

# zmk-driver-iqs9150

## English

A ZMK/Zephyr input driver for the Azoteq IQS9150EV02 capacitive trackpad.

The driver initializes the sensor, handles its active-low RDY signal and I2C
transactions, runs ATI calibration, and publishes normalized contact frames.
It does not convert contacts to pointer movement, clicks, scrolling, or gestures.

### Compatibility

- Sensor: IQS9150EV02 with the bundled EV02 settings
- I2C address: 0x56
- Validated target: Seeed XIAO nRF52840 with ZMK v0.3
- Up to seven internal contact slots; hardware interaction checks cover up to five fingers
- Other board pin configurations and newer ZMK versions are not verified

The bundled settings are specific to the tested EV02 configuration. Other
electrode layouts and other sensor models are not covered.

### Installation

Add this module to the ZMK workspace manifest used by your keyboard. Keep the
existing ZMK project and other workspace entries.

```yaml
manifest:
  projects:
    - name: zmk-driver-iqs9150
      url: https://github.com/taichan1113/zmk-driver-iqs9150.git
      revision: main
      path: module/zmk-driver-iqs9150
```

Run `west update zmk-driver-iqs9150` from the workspace. For repeatable builds,
select a reviewed release or revision.

### Wiring and Devicetree

Connect VDD, GND, SDA, SCL, and RDY according to the sensor board's electrical
specification. The validated XIAO/EV02 setup uses 3V3 and a common ground.
Configure I2C pin control and the RDY GPIO for your board.

Example sensor node:

```dts
#include <zephyr/dt-bindings/gpio/gpio.h>

&i2c0 {
    status = "okay";
    clock-frequency = <400000>;

    iqs9150: trackpad@56 {
        compatible = "azoteq,iqs9150";
        reg = <0x56>;
        rdy-gpios = <&gpio1 13 GPIO_ACTIVE_LOW>;
    };
};
```

This example does not configure SDA/SCL pin control. Avoid assigning another
peripheral to the same pins. The validated firmware does not use MCLR.

Enable the driver in the keyboard configuration:

```ini
CONFIG_IQS9150_EV02_DRIVER=y
CONFIG_INPUT_QUEUE_MAX_MSGS=64
```

The 64-message input queue is the validated setting for multi-contact input.
Retain the keyboard's other settings.

### Configuration

| Setting | Purpose |
| --- | --- |
| `reg` | Sensor I2C address; validated at 0x56 |
| `rdy-gpios` | Required active-low RDY input |
| `two-finger-diagnostics` | Optional two-contact coordinate logs |
| `multitouch-diagnostics` | Optional multi-contact coordinate logs |
| `CONFIG_IQS9150_EV02_DRIVER` | Enable the driver |
| `CONFIG_IQS9150_EV02_DRIVER_LOG_LEVEL_*` | Driver log level |

See the Devicetree binding at `dts/bindings/input/azoteq,iqs9150.yaml` for the
complete sensor-node interface. Pointer speed, tap behavior, and gesture
recognition belong to the application that consumes contact frames.

### Contact interface

The driver emits Zephyr input events, not host-facing multi-touch HID reports.
Public frame definitions are in `include/travelers_pad/multitouch.h`.

| Events | Meaning |
| --- | --- |
| `ABS_MT_SLOT`, tracking ID, position X/Y | Slot-based contacts |
| `ABS_X/Y`, `ABS_RX/RY` | Compatibility coordinates for contacts one and two |
| `ABS_RUDDER` | Validity of the second contact |
| `ABS_Z`, `BTN_TOUCH` | Contact count and touch state |
| `ABS_RZ` | Sensor-reset notification |

### Startup and troubleshooting

The runtime recovery command uses System Control bit 9 (`0x0200`), as specified
in the [IQS9150/IQS9151 datasheet](https://www.azoteq.com/images/stories/pdf/IQS9150_IQS9151_datasheet.pdf).
The communication-termination flag is Configuration Settings bit 6 in the low
byte; with the supplied `0x461E` settings, STOP ends the window. Runtime register
transactions wait for logically active RDY, including recovery transactions.

`rdy-watch-ms` sets a GPIO-only level check interval (default 250 ms, zero
disables). It schedules work for an asserted RDY that has no pending work, but
does not force I2C or reset the sensor simply because event mode is quiet.
`runtime-diagnostics` enables a health log every ten seconds for investigating
IRQ/report progress and recovery. Normal reset verification has a two-second
deadline and at most three software-reset requests.

On an MCU-only restart, the sensor may retain Event Mode and its alternate
Force Comms setting. Startup requests a communication window with `0xFF`, waits
for RDY, reads product/configuration data, and explicitly resets the sensor before
reinitialization. Window requests are bounded and are not sent during normal idle.

The driver writes the bundled EV02 settings at initialization. Keep the pad
untouched until initialization and ATI calibration complete. If calibration
repeatedly fails, remove all fingers and check power, RDY, and I2C wiring.

A working sensor alone does not move the host pointer. An application must
consume its contact frames and produce the desired host input.

For an issue report, include your board, ZMK revision, relevant overlay and
configuration, and startup log. Remove credentials and personal information
from logs.

### License and third-party material

Original project contributions are licensed under the MIT License in
[`LICENSE`](LICENSE). The Azoteq-derived initialization settings are not
covered by that grant because their redistribution terms have not been confirmed.
Read [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) before redistributing
source or firmware.

## 日本語

Azoteq IQS9150EV02静電容量式トラックパッド向けのZMK/Zephyr入力ドライバーです。

センサー初期化、active-low RDY信号とI2C通信、ATIキャリブレーションを行い、
正規化した接触フレームを出力します。接触情報をポインタ移動、クリック、
スクロール、ジェスチャーへ変換する機能は含みません。

### 対応範囲

- センサー: 同梱EV02設定を使用するIQS9150EV02
- I2Cアドレス: 0x56
- 動作確認済み: ZMK v0.3を使用するSeeed XIAO nRF52840
- 内部接触スロットは最大7。実機操作確認は最大5本指
- 他基板のピン設定と新しいZMKバージョンは未確認

同梱設定は確認済みのEV02構成向けです。異なる電極配列や別のセンサーモデルには対応していません。

### 導入

キーボードで使用するZMK workspaceのmanifestに、このmoduleを追加します。
既存のZMK projectやworkspace設定は維持してください。

```yaml
manifest:
  projects:
    - name: zmk-driver-iqs9150
      url: https://github.com/taichan1113/zmk-driver-iqs9150.git
      revision: main
      path: module/zmk-driver-iqs9150
```

workspaceで `west update zmk-driver-iqs9150` を実行します。再現可能なビルドでは、
確認済みのreleaseまたはrevisionを指定してください。

### 配線とDevicetree

センサー基板の電気仕様に従い、VDD、GND、SDA、SCL、RDYを接続します。
動作確認済みのXIAO/EV02構成では3V3と共通GNDを使用します。
基板に合わせてI2C pinctrlとRDY GPIOを設定してください。

センサーnodeの例:

```dts
#include <zephyr/dt-bindings/gpio/gpio.h>

&i2c0 {
    status = "okay";
    clock-frequency = <400000>;

    iqs9150: trackpad@56 {
        compatible = "azoteq,iqs9150";
        reg = <0x56>;
        rdy-gpios = <&gpio1 13 GPIO_ACTIVE_LOW>;
    };
};
```

この例にはSDA/SCLのpin control設定がありません。同じピンを別の周辺機器に割り当てないでください。
確認済みファームウェアはMCLRを使用しません。

キーボード設定でdriverを有効にします:

```ini
CONFIG_IQS9150_EV02_DRIVER=y
CONFIG_INPUT_QUEUE_MAX_MSGS=64
```

64件のinput queueは多点接触で確認した設定です。キーボードの他の設定は維持してください。

### 設定項目

| 設定 | 用途 |
| --- | --- |
| `reg` | センサーI2Cアドレス。確認値は0x56 |
| `rdy-gpios` | 必須のactive-low RDY入力 |
| `two-finger-diagnostics` | 任意の2点座標ログ |
| `multitouch-diagnostics` | 任意の多点座標ログ |
| `CONFIG_IQS9150_EV02_DRIVER` | driverの有効化 |
| `CONFIG_IQS9150_EV02_DRIVER_LOG_LEVEL_*` | driverのログレベル |

センサーnodeの全設定は `dts/bindings/input/azoteq,iqs9150.yaml` を参照してください。
ポインタ速度、タップ動作、ジェスチャー認識は、接触フレームを使うアプリケーション側の機能です。

### 接触インターフェース

driverはZephyr input eventを出力し、ホスト向けマルチタッチHIDレポートは出力しません。
公開frame定義は `include/travelers_pad/multitouch.h` にあります。

| イベント | 意味 |
| --- | --- |
| `ABS_MT_SLOT`、tracking ID、position X/Y | slot単位の接触情報 |
| `ABS_X/Y`、`ABS_RX/RY` | 1点目・2点目の互換座標 |
| `ABS_RUDDER` | 2点目の有効状態 |
| `ABS_Z`、`BTN_TOUCH` | 接触本数とタッチ状態 |
| `ABS_RZ` | センサーreset通知 |

### 起動とトラブルシューティング

runtime復旧コマンドは、[IQS9150/IQS9151データシート](https://www.azoteq.com/images/stories/pdf/IQS9150_IQS9151_datasheet.pdf)に従って
System Controlのbit 9（`0x0200`）を使います。通信終了フラグはConfig Settingsの
下位byteのbit 6で、付属の`0x461E`設定ではSTOPで通信窓が終了します。
runtimeのレジスタ通信と復旧処理は、論理的に有効なRDYを待って実行します。

MCUだけの再起動では、センサーのEvent Modeと代替Force Comms設定が残る場合があります。
起動時は `0xFF` で通信窓を要求し、RDYを待って製品番号・設定を読み、
センサーを明示的にリセットして再初期化します。窓要求は回数を制限し、通常の無操作時には送りません。

`rdy-watch-ms`はGPIOだけを監視する間隔で、既定250 ms、0で無効です。
RDY有効のまま処理が予約されていない場合にworkを起こしますが、Event Modeで
静かなことだけを理由にI2C通信やセンサーリセットは行いません。
`runtime-diagnostics`で10秒ごとの状態ログを有効にし、IRQ・レポート・復旧の進行を
調べられます。リセット確認は2秒を期限とし、ソフトリセット要求は最大3回です。

driverは起動時に同梱EV02設定を書き込みます。初期化とATIキャリブレーションが完了するまで、
パッドに触れないでください。キャリブレーションに繰り返し失敗する場合は指を離し、
電源・RDY・I2C配線を確認してください。

センサーが動作しても、ポインタは自動では動きません。アプリケーションが接触フレームを受け取り、
必要なホスト入力を生成する必要があります。

不具合報告には、基板、ZMK revision、関係するoverlayと設定、起動ログを添付してください。
ログから認証情報や個人情報を除いてください。

### ライセンスと第三者由来の内容

本プロジェクト独自の成果は[`LICENSE`](LICENSE)に記載したMIT Licenseです。
Azoteq由来の初期化設定は再配布条件が未確認のため、この許諾対象には含まれません。
ソースやファームウェアを再配布する前に[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)を確認してください。
