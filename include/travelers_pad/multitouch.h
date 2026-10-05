/* Travelers Pad private multi-touch input-frame contract.
 *
 * Zephyr 3.5 declares ABS_MT_SLOT but not the remaining Linux-compatible
 * multi-touch ABS codes.  Keep the values here so the IQS9150 driver and
 * future function modules share one explicit, device-neutral contract.
 *
 * Zephyr 3.5にはABS_MT_SLOT以外のマルチタッチABSコード定義がないため、
 * IQS9150ドライバと将来の機能モジュールで共有する内部契約として定義する。
 */

#ifndef TRAVELERS_PAD_MULTITOUCH_H_
#define TRAVELERS_PAD_MULTITOUCH_H_

#define TRAVELERS_PAD_INPUT_ABS_MT_POSITION_X 0x35
#define TRAVELERS_PAD_INPUT_ABS_MT_POSITION_Y 0x36
#define TRAVELERS_PAD_INPUT_ABS_MT_TRACKING_ID 0x39

/* Router-to-pointer metadata travels with the queued normalized contact frame.
 * MSC_SCAN is private to this internal device chain; it is never host-facing
 * and carries the arbiter frame sequence needed to validate later REL output. */
#define TRAVELERS_PAD_INPUT_MSC_ROUTED_FRAME_SEQ 0x04

#define TRAVELERS_PAD_MAX_CONTACTS 7

#endif /* TRAVELERS_PAD_MULTITOUCH_H_ */
