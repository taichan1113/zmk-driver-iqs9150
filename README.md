# IQS9150 EV02 driver / IQS9150 EV02ドライバー

Standalone ZMK/Zephyr module extracted from Travelers Pad at `b0ad83d`.
The driver implementation, EV02 initialization data, CONFIG names and binding
are unchanged. It owns I2C/RDY, initialization/ATI/recovery and contact frames,
not pointer/click/gesture policy.

Travelers Padの`b0ad83d`から切り出した独立ZMK/Zephyr module。
実装・EV02初期化データ・CONFIG名・bindingは変更しない。
I2C/RDY・初期化/ATI/復旧・接触frameを担当し、操作ポリシーは担当しない。

Add this project to west at `module/zmk-driver-iqs9150`. Define an enabled
`azoteq,iqs9150` I2C node with `reg = <0x56>` and active-low `rdy-gpios` in the
keyboard overlay. Set `CONFIG_IQS9150_EV02_DRIVER=y`. No shield or keymap is supplied.
For mouse behavior, additionally import `zmk-input-travelers-pad`.

westで本projectを`module/zmk-driver-iqs9150`へ取り込む。キーボードoverlayで
`azoteq,iqs9150` I2C node、アドレス0x56、active-lowのrdy-gpiosを定義し、
上記CONFIGを有効にする。shield・keymapは提供しない。
マウス操作には別途`zmk-input-travelers-pad`を取り込む。

`include/travelers_pad/multitouch.h` is the shared contact-frame contract and
must have one owner; do not duplicate it in the function module. This driver
uses EV02-specific initialization, not arbitrary IQS9150 sensor geometries.

上記ヘッダは共有frame契約。本repoだけが所有し、機能moduleへ複製しない。
初期化はEV02固有で、任意のセンサ配列対応を保証しない。
