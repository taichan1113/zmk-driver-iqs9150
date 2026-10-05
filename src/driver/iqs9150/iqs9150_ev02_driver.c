/*
 * IQS9150 EV02 sensor driver.
 *
 * STEP1B: Apply the Azoteq GUI-generated initialization data, run ATI, then
 * enable Event Mode. Running mode reports normalized absolute contact frames;
 * pointer and gesture policy belongs in Travelers Pad function modules.
 */

#define DT_DRV_COMPAT azoteq_iqs9150

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "iqs9150_ev02_init.h"
#include <travelers_pad/multitouch.h>

LOG_MODULE_REGISTER(iqs9150_ev02_driver, CONFIG_IQS9150_EV02_DRIVER_LOG_LEVEL);

#define IQS9150_PRODUCT_NUMBER_REG       0x1000
#define IQS9150_RELATIVE_X_REG            0x1014
#define IQS9150_INFO_FLAGS_REG            0x1020
#define IQS9150_SYSTEM_CONTROL_REG        0x11BC
#define IQS9150_CONFIG_SETTINGS_REG       0x11BE
#define IQS9150_TERMINATE_COMMS_REG        0xEEEE

#define IQS9150_PRODUCT_NUMBER            0x076A
#define IQS9150_REPORT_LEN                 72
#define IQS9150_WRITE_PAYLOAD_MAX          30
#define IQS9150_ATI_POLL_INTERVAL_MS        20

#define IQS9150_SYSTEM_CONTROL_ACK_RESET  BIT(7)
#define IQS9150_SYSTEM_CONTROL_ALP_REATI  BIT(6)
#define IQS9150_SYSTEM_CONTROL_TP_REATI   BIT(5)
#define IQS9150_SYSTEM_CONTROL_SW_RESET    BIT(1)

#define IQS9150_INFO_TP_ATI_ERROR          BIT(3)
#define IQS9150_INFO_TP_REATI_OCCURRED     BIT(4)
#define IQS9150_INFO_SHOW_RESET             BIT(7)
#define IQS9150_ACK_RETRY_MAX               3
#define IQS9150_REPORT_ERROR_RETRY_MAX      3
#define IQS9150_PRODUCT_RETRY_MAX           40
#define IQS9150_PRODUCT_RETRY_INTERVAL_MS   25
#define IQS9150_ATI_RECOVERY_RETRY_MAX       8
#define IQS9150_ATI_RECOVERY_DELAY_MS      250

#define IQS9150_REPORT_1F_GESTURE_OFFSET     8
#define IQS9150_REPORT_2F_GESTURE_OFFSET    10
#define IQS9150_REPORT_FINGER1_OFFSET       16
#define IQS9150_REPORT_FINGER_STRIDE         8
#define IQS9150_REPORT_FINGER_OFFSET(index) \
	(IQS9150_REPORT_FINGER1_OFFSET + ((index) * IQS9150_REPORT_FINGER_STRIDE))

#define IQS9150_TP_FINGER_COUNT_MASK       BIT_MASK(4)
#define IQS9150_TP_FINGER1_CONFIDENCE      BIT(8)
#define IQS9150_TP_FINGER2_CONFIDENCE      BIT(9)
#define IQS9150_TP_FINGER_CONFIDENCE(index) BIT(8 + (index))

#define IQS9150_1F_TAP_MASK                 (BIT(0) | BIT(1) | BIT(2))
#define IQS9150_2F_TAP_MASK                 (BIT(0) | BIT(1) | BIT(2))
struct iqs9150_block {
	uint16_t reg;
	const uint8_t *data;
	size_t len;
};

enum iqs9150_state {
	IQS9150_WAIT_PRODUCT,
	IQS9150_READ_COMMS_CONFIG,
	IQS9150_ACK_RESET,
	IQS9150_VERIFY_RESET_ACK,
	IQS9150_WRITE_CONFIG,
	IQS9150_START_ATI,
	IQS9150_WAIT_ATI,
	IQS9150_ENABLE_EVENT_MODE,
	IQS9150_RUNNING,
	IQS9150_RECOVER_RESET,
	IQS9150_RECOVER_WAIT_RESET,
	IQS9150_ERROR,
};

struct iqs9150_config {
	struct i2c_dt_spec i2c;
	struct gpio_dt_spec rdy_gpio;
	bool two_finger_diagnostics;
	bool multitouch_diagnostics;
};

struct iqs9150_data {
	const struct device *dev;
	struct gpio_callback rdy_callback;
	struct k_work_delayable work;
	enum iqs9150_state state;
	uint8_t ack_attempts;
	uint8_t report_error_count;
	uint8_t product_attempts;
	uint8_t ati_recovery_attempts;
	bool terminate_comms_window;
	int64_t last_two_finger_log_ms;
	int64_t last_multitouch_log_ms;
	bool multitouch_slot_active[TRAVELERS_PAD_MAX_CONTACTS];
	size_t block_index;
	size_t block_offset;
};

struct iqs9150_contact {
	uint16_t x;
	uint16_t y;
	bool valid;
};

static const uint8_t ev02_115c[] = {0};
static const uint8_t ev02_1176[] = {0xFF, 0xFF, 0x03, 0x00};
static const uint8_t ev02_117a[] = {
	0x21, 0x4B, 0x87, 0x5E, 0x87, 0x5E, 0x87, 0x5C, 0x87, 0x5E,
	0x87, 0x5E, 0x87, 0x5E, 0x87, 0x5E, 0x87, 0x5E, 0x87, 0x5C,
	0x87, 0x5C, 0x87, 0x5C, 0x87, 0x5E, 0x87, 0x5E,
};
static const uint8_t ev02_1196[] = {
	0xFA, 0x00, 0x2C, 0x01, 0x32, 0x00, 0x32, 0x00, 0xE8, 0x03, 0x32, 0x14,
};
static const uint8_t ev02_11a2[] = {
	0x0A, 0x00, 0x32, 0x00, 0x32, 0x00, 0x32, 0x00, 0x64, 0x00, 0x0A, 0x00,
	0x3C, 0x00, 0x05, 0x00, 0x28, 0x00, 0xDC, 0x05, 0x05, 0x08, 0x64, 0x00,
	0x14, 0x00,
};
static const uint8_t ev02_11be[] = {
	CONFIG_SETTINGS_0, CONFIG_SETTINGS_1, OTHER_SETTINGS_0, OTHER_SETTINGS_1,
};
static const uint8_t ev02_11c2[] = {0xFF, 0xFF, 0xFF, 0xD3, 0x00, 0x00, 0x00, 0xFC, 0xFF, 0x0F};
static const uint8_t ev02_11cc[] = {0x1A, 0x14, 0x08, 0x08, 0x02, 0x02, 0x32, 0x32};
static const uint8_t ev02_11d4[] = {0x46, 0x04, 0xB4, 0x64};
static const uint8_t ev02_11d8[] = {0x28, 0x02, 0x02, 0x10, 0x07, 0x07, 0x00, 0x44, 0x40, 0x4B};
static const uint8_t ev02_11e2[] = {
	0x28, 0x1A, 0x12, 0x07, 0xD0, 0x07, 0xBD, 0x04, 0x06, 0x00,
	0x7C, 0x00, 0x07, 0x80, 0x14, 0x03, 0x14, 0x14, 0x03, 0x14,
};
static const uint8_t ev02_11f6[] = {
	0x1F, 0xFF, 0xFF, 0x00, 0x96, 0x00, 0x96, 0x00, 0x32, 0x00, 0x2C, 0x01,
	0x96, 0x00, 0x2C, 0x01, 0x96, 0x00, 0x64, 0x00, 0x32, 0x00, 0x25, 0x25,
	0x64, 0x00, 0x0A, 0x00, 0x08, 0x00, 0x05, 0x00, 0x64, 0x00,
};
static const uint8_t ev02_1218[] = {
	0x19, 0x0C, 0x18, 0x0B, 0x17, 0x0A, 0x16, 0x09, 0x15, 0x08, 0x14, 0x13,
	0x0E, 0x02, 0x0F, 0x03, 0x10, 0x04, 0x11, 0x07, 0x05, 0x12, 0x06, 0x01,
	0x0D, 0x00, 0x2B, 0x2A, 0x29, 0x28, 0x27, 0x26, 0x25, 0x24, 0x23, 0x21,
	0x22, 0x20, 0x1B, 0x1C, 0x1A, 0x1E, 0x1D, 0x1F, 0x00, 0x00,
};
static const uint8_t ev02_zero_88[88];
static const uint8_t ev02_zero_506[506];
static const uint8_t ev02_zero_252[252];
static const uint8_t ev02_2000[] = {0x01, 0x00, 0x03, 0x03, 0x14, 0x00};

static const struct iqs9150_block ev02_blocks[] = {
	{0x115C, ev02_115c, 26},
	{0x1176, ev02_1176, sizeof(ev02_1176)},
	{0x117A, ev02_117a, sizeof(ev02_117a)},
	{0x1196, ev02_1196, sizeof(ev02_1196)},
	{0x11A2, ev02_11a2, sizeof(ev02_11a2)},
	{0x11BE, ev02_11be, sizeof(ev02_11be)},
	{0x11C2, ev02_11c2, sizeof(ev02_11c2)},
	{0x11CC, ev02_11cc, sizeof(ev02_11cc)},
	{0x11D4, ev02_11d4, sizeof(ev02_11d4)},
	{0x11D8, ev02_11d8, sizeof(ev02_11d8)},
	{0x11E2, ev02_11e2, sizeof(ev02_11e2)},
	{0x11F6, ev02_11f6, sizeof(ev02_11f6)},
	{0x1218, ev02_1218, sizeof(ev02_1218)},
	{0x1246, ev02_zero_88, sizeof(ev02_zero_88)},
	{0x129E, ev02_zero_88, sizeof(ev02_zero_88)},
	{0x12F6, ev02_zero_506, sizeof(ev02_zero_506)},
	{0x14F0, ev02_zero_252, sizeof(ev02_zero_252)},
	{0x2000, ev02_2000, sizeof(ev02_2000)},
};

static int read_reg(const struct device *dev, uint16_t reg, uint8_t *data, size_t len)
{
	const struct iqs9150_config *cfg = dev->config;
	uint8_t address[] = {reg & 0xff, reg >> 8};

	return i2c_write_read_dt(&cfg->i2c, address, sizeof(address), data, len);
}

static int write_reg(const struct device *dev, uint16_t reg, const uint8_t *data, size_t len)
{
	const struct iqs9150_config *cfg = dev->config;
	uint8_t buffer[2 + IQS9150_WRITE_PAYLOAD_MAX];

	if (len > IQS9150_WRITE_PAYLOAD_MAX) {
		return -EINVAL;
	}

	buffer[0] = reg & 0xff;
	buffer[1] = reg >> 8;
	memcpy(&buffer[2], data, len);
	return i2c_write_dt(&cfg->i2c, buffer, len + 2);
}

/* When Config Settings bit 6 is set, STOP alone does not end a communication
 * window. The EV02 GUI image enables this bit (0x461E), so explicitly write
 * the documented 0xEEEE terminator after every completed transaction.
 * Config Settingsのbit 6が有効な場合、STOPだけでは通信窓が終了しない。
 * EV02 GUI設定(0x461E)はこのbitを有効にするため、各トランザクション後に
 * 規定の0xEEEE終了コマンドを書き込む。 */
static int terminate_comms_if_needed(const struct iqs9150_data *data)
{
	const struct iqs9150_config *cfg = data->dev->config;
	uint8_t command[] = {IQS9150_TERMINATE_COMMS_REG & 0xff,
			     IQS9150_TERMINATE_COMMS_REG >> 8};

	if (!data->terminate_comms_window) {
		return 0;
	}

	return i2c_write_dt(&cfg->i2c, command, sizeof(command));
}

static uint16_t le16(const uint8_t *data)
{
	return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static void report_reset_frame(const struct device *dev)
{
	/* ABS_RZ is an internal Travelers Pad frame marker.  It lets function
	 * modules drop an in-progress touch without turning a sensor reset into a
	 * synthetic click. */
	int rc;

	struct iqs9150_data *data = dev->data;

	rc = 0;
	for (uint8_t index = 0; index < TRAVELERS_PAD_MAX_CONTACTS; index++) {
		if (!data->multitouch_slot_active[index]) {
			continue;
		}
		rc |= input_report_abs(dev, INPUT_ABS_MT_SLOT, index, false, K_NO_WAIT);
		rc |= input_report_abs(dev, TRAVELERS_PAD_INPUT_ABS_MT_TRACKING_ID, -1, false,
				       K_NO_WAIT);
		data->multitouch_slot_active[index] = false;
	}
	rc |= input_report_abs(dev, INPUT_ABS_RZ, 1, false, K_NO_WAIT);

	rc |= input_report_abs(dev, INPUT_ABS_Z, 0, false, K_NO_WAIT);
	rc |= input_report_key(dev, INPUT_BTN_TOUCH, 0, true, K_NO_WAIT);
	if (rc != 0) {
		LOG_ERR("reset frame report failed: %d", rc);
	}
}

static void iqs9150_start_runtime_recovery(struct iqs9150_data *data, const char *reason,
					bool sensor_already_reset)
{
	LOG_WRN("runtime recovery requested: %s", reason);
	report_reset_frame(data->dev);
	data->ack_attempts = 0;
	data->report_error_count = 0;
	data->block_index = 0;
	data->block_offset = 0;
	data->state = sensor_already_reset ? IQS9150_ACK_RESET : IQS9150_RECOVER_RESET;
}

static void report_contact_frame(const struct device *dev)
{
	struct iqs9150_data *data = dev->data;
	uint8_t report[IQS9150_REPORT_LEN];
	int rc = read_reg(dev, IQS9150_RELATIVE_X_REG, report, sizeof(report));
	uint8_t gestures_1f;
	uint8_t gestures_2f;
	uint8_t fingers;
	uint8_t reported_fingers;
	uint16_t trackpad_flags;
	struct iqs9150_contact contacts[TRAVELERS_PAD_MAX_CONTACTS];
	const struct iqs9150_config *cfg = dev->config;

	if (rc != 0) {
		LOG_ERR("report read failed: %d", rc);
		data->report_error_count++;
		if (data->report_error_count >= IQS9150_REPORT_ERROR_RETRY_MAX) {
			iqs9150_start_runtime_recovery(data, "consecutive report read failures", false);
		}
		return;
	}
	rc = terminate_comms_if_needed(data);
	if (rc != 0) {
		LOG_ERR("report communication termination failed: %d", rc);
		return;
	}
	data->report_error_count = 0;
	if (le16(&report[12]) & IQS9150_INFO_SHOW_RESET) {
		iqs9150_start_runtime_recovery(data, "sensor Show Reset flag", true);
		return;
	}

	trackpad_flags = le16(&report[14]);
	reported_fingers = trackpad_flags & IQS9150_TP_FINGER_COUNT_MASK;
	fingers = MIN(reported_fingers, (uint8_t)TRAVELERS_PAD_MAX_CONTACTS);
	for (uint8_t index = 0; index < TRAVELERS_PAD_MAX_CONTACTS; index++) {
		uint16_t offset = IQS9150_REPORT_FINGER_OFFSET(index);

		contacts[index].x = le16(&report[offset]);
		contacts[index].y = le16(&report[offset + 2]);
		contacts[index].valid = index < fingers &&
			(trackpad_flags & IQS9150_TP_FINGER_CONFIDENCE(index)) &&
			contacts[index].x != UINT16_MAX && contacts[index].y != UINT16_MAX;
	}

	LOG_DBG("status=0x%04x tp=0x%04x fingers=%u rel=(%d,%d) f1=(%u,%u,%d) f2=(%u,%u,%d)",
		le16(&report[12]), trackpad_flags, fingers, (int16_t)le16(&report[0]),
		(int16_t)le16(&report[2]), contacts[0].x, contacts[0].y, contacts[0].valid,
		contacts[1].x, contacts[1].y, contacts[1].valid);

	gestures_1f = report[IQS9150_REPORT_1F_GESTURE_OFFSET];
	gestures_2f = report[IQS9150_REPORT_2F_GESTURE_OFFSET];
	if ((gestures_1f & IQS9150_1F_TAP_MASK) || (gestures_2f & IQS9150_2F_TAP_MASK)) {
		LOG_INF("hardware tap gesture: 1f=0x%02x 2f=0x%02x", gestures_1f,
			gestures_2f);
	}

	if (cfg->two_finger_diagnostics && fingers >= 2 &&
	    k_uptime_get() - data->last_two_finger_log_ms >= 100) {
		int32_t geometry_x = (int32_t)contacts[1].x - contacts[0].x;
		int32_t geometry_y = (int32_t)contacts[1].y - contacts[0].y;
		uint32_t distance_squared = geometry_x * geometry_x + geometry_y * geometry_y;

		LOG_INF("2F frame: flags=0x%04x f1=(%u,%u,%d) f2=(%u,%u,%d)",
			trackpad_flags, contacts[0].x, contacts[0].y, contacts[0].valid,
			contacts[1].x, contacts[1].y, contacts[1].valid);
		LOG_INF("2F geometry: vector=(%ld,%ld) distance-squared=%lu", (long)geometry_x,
			(long)geometry_y, (unsigned long)distance_squared);
		data->last_two_finger_log_ms = k_uptime_get();
	}
	if (cfg->multitouch_diagnostics && fingers >= 3 &&
	    k_uptime_get() - data->last_multitouch_log_ms >= 100) {
		LOG_INF("MT frame: count=%u flags=0x%04x f1=(%u,%u,%d) f2=(%u,%u,%d) f3=(%u,%u,%d) f4=(%u,%u,%d) f5=(%u,%u,%d)",
			fingers, trackpad_flags, contacts[0].x, contacts[0].y, contacts[0].valid,
			contacts[1].x, contacts[1].y, contacts[1].valid, contacts[2].x,
			contacts[2].y, contacts[2].valid, contacts[3].x, contacts[3].y,
			contacts[3].valid, contacts[4].x, contacts[4].y, contacts[4].valid);
		if (fingers >= 6) {
			LOG_INF("MT frame: f6=(%u,%u,%d) f7=(%u,%u,%d)", contacts[5].x,
				contacts[5].y, contacts[5].valid, contacts[6].x, contacts[6].y,
				contacts[6].valid);
		}
		data->last_multitouch_log_ms = k_uptime_get();
	}

	/* Publish every contact in a slot frame for gesture modules. The legacy
	 * fields immediately below remain unchanged for the existing pointer
	 * function: two-finger scroll uses only the representative Finger 1 delta,
	 * while pinch/rotation modules consume the slot geometry. */
	rc = 0;
	for (uint8_t index = 0; index < TRAVELERS_PAD_MAX_CONTACTS; index++) {
		if (!contacts[index].valid && !data->multitouch_slot_active[index]) {
			continue;
		}
		rc |= input_report_abs(dev, INPUT_ABS_MT_SLOT, index, false, K_NO_WAIT);
		if (contacts[index].valid != data->multitouch_slot_active[index]) {
			rc |= input_report_abs(dev, TRAVELERS_PAD_INPUT_ABS_MT_TRACKING_ID,
					       contacts[index].valid ? index : -1, false, K_NO_WAIT);
			data->multitouch_slot_active[index] = contacts[index].valid;
		}
		if (contacts[index].valid) {
			rc |= input_report_abs(dev, TRAVELERS_PAD_INPUT_ABS_MT_POSITION_X,
					       contacts[index].x, false, K_NO_WAIT);
			rc |= input_report_abs(dev, TRAVELERS_PAD_INPUT_ABS_MT_POSITION_Y,
					       contacts[index].y, false, K_NO_WAIT);
		}
	}
	/* Legacy compatibility frame: ABS_X/Y is Finger 1; ABS_RX/RY is Finger 2;
	 * ABS_RUDDER is Finger 2 validity; ABS_Z is the contact count. */
	rc |= input_report_abs(dev, INPUT_ABS_X, contacts[0].x, false, K_NO_WAIT);
	rc |= input_report_abs(dev, INPUT_ABS_Y, contacts[0].y, false, K_NO_WAIT);
	rc |= input_report_abs(dev, INPUT_ABS_RX, contacts[1].x, false, K_NO_WAIT);
	rc |= input_report_abs(dev, INPUT_ABS_RY, contacts[1].y, false, K_NO_WAIT);
	rc |= input_report_abs(dev, INPUT_ABS_RUDDER, contacts[1].valid, false, K_NO_WAIT);
	rc |= input_report_abs(dev, INPUT_ABS_Z, fingers, false, K_NO_WAIT);
	rc |= input_report_key(dev, INPUT_BTN_TOUCH, fingers != 0, true, K_NO_WAIT);
	if (rc != 0) {
		LOG_ERR("contact frame report failed: %d", rc);
	}
}

static void iqs9150_fail(struct iqs9150_data *data, const char *operation, int rc)
{
	data->state = IQS9150_ERROR;
	LOG_ERR("STEP1B %s failed: %d", operation, rc);
}

static void iqs9150_work_handler(struct k_work *work)
{
	struct k_work_delayable *delayable = k_work_delayable_from_work(work);
	struct iqs9150_data *data = CONTAINER_OF(delayable, struct iqs9150_data, work);
	const struct device *dev = data->dev;
	uint8_t value[4] = {0};
	int rc;

	switch (data->state) {
	case IQS9150_WAIT_PRODUCT:
		rc = read_reg(dev, IQS9150_PRODUCT_NUMBER_REG, value, 2);
		if (rc == 0 && le16(value) == IQS9150_PRODUCT_NUMBER) {
			data->state = IQS9150_ACK_RESET;
			data->ack_attempts = 0;
			data->block_index = 0;
			data->block_offset = 0;
			data->state = IQS9150_READ_COMMS_CONFIG;
			LOG_INF("IQS9150 product 0x%04x ready after %u startup attempt(s)",
				le16(value), data->product_attempts + 1);
			/* The Config Settings read and following System Control write must
			 * occur in an RDY window. / Config Settings読出しと次のSystem
			 * Control書込みはRDYウィンドウ内で実行する。 */
			if (gpio_pin_get_dt(&((const struct iqs9150_config *)dev->config)->rdy_gpio) ==
			    0) {
				k_work_schedule(&data->work, K_NO_WAIT);
			}
			return;
		}

		data->product_attempts++;
		if (data->product_attempts >= IQS9150_PRODUCT_RETRY_MAX) {
			LOG_ERR("IQS9150 product did not become ready after %u attempts (rc=%d value=0x%02x%02x)",
				data->product_attempts, rc, value[1], value[0]);
			data->state = IQS9150_ERROR;
			return;
		}
		LOG_INF("IQS9150 startup wait: product rc=%d value=0x%02x%02x; retry %u/%u",
			rc, value[1], value[0], data->product_attempts, IQS9150_PRODUCT_RETRY_MAX);
		k_work_schedule(&data->work, K_MSEC(IQS9150_PRODUCT_RETRY_INTERVAL_MS));
		return;

	case IQS9150_READ_COMMS_CONFIG:
		rc = read_reg(dev, IQS9150_CONFIG_SETTINGS_REG, value, 2);
		if (rc != 0) {
			iqs9150_fail(data, "Config Settings read", rc);
			return;
		}
		data->terminate_comms_window = (value[1] & BIT(6)) != 0;
		rc = terminate_comms_if_needed(data);
		if (rc != 0) {
			iqs9150_fail(data, "Config Settings communication termination", rc);
			return;
		}
		data->state = IQS9150_ACK_RESET;
		LOG_INF("IQS9150 communication termination=%s", data->terminate_comms_window ?
			"0xEEEE" : "STOP");
		return;

	case IQS9150_ACK_RESET:
		/* At power-up the documented System Control
		 * value is 0x0002, so acknowledge Show Reset with one complete
		 * 16-bit System Control write (0x0082). Communication termination is
		 * handled explicitly below when the active Config Settings requires it. */
		value[0] = IQS9150_SYSTEM_CONTROL_ACK_RESET | BIT(1);
		value[1] = 0;
		rc = write_reg(dev, IQS9150_SYSTEM_CONTROL_REG, value, 2);
		if (rc != 0) {
			iqs9150_fail(data, "reset ACK", rc);
			return;
		}
		rc = terminate_comms_if_needed(data);
		if (rc != 0) {
			iqs9150_fail(data, "reset ACK communication termination", rc);
			return;
		}
		data->ack_attempts++;
		data->state = IQS9150_VERIFY_RESET_ACK;
		LOG_INF("STEP1B reset ACK requested; verifying on next RDY");
		return;

	case IQS9150_VERIFY_RESET_ACK:
		rc = read_reg(dev, IQS9150_INFO_FLAGS_REG, value, 2);
		if (rc != 0) {
			iqs9150_fail(data, "reset ACK verification", rc);
			return;
		}
		rc = terminate_comms_if_needed(data);
		if (rc != 0) {
			iqs9150_fail(data, "reset ACK verification termination", rc);
			return;
		}
		if (value[0] & IQS9150_INFO_SHOW_RESET) {
			if (data->ack_attempts >= IQS9150_ACK_RETRY_MAX) {
				LOG_ERR("STEP1B Show Reset remains set; Info Flags=0x%02x%02x",
					value[1], value[0]);
				iqs9150_fail(data, "reset ACK", -EIO);
				return;
			}
			data->state = IQS9150_ACK_RESET;
			LOG_WRN("STEP1B Show Reset still set; retrying reset ACK");
			return;
		}
		data->state = IQS9150_WRITE_CONFIG;
		LOG_INF("STEP1B reset acknowledged; writing EV02 initialization data");
		return;

	case IQS9150_WRITE_CONFIG: {
		const struct iqs9150_block *block;
		size_t chunk;

		if (data->block_index == ARRAY_SIZE(ev02_blocks)) {
			data->state = IQS9150_START_ATI;
			LOG_INF("STEP1B initialization data written; starting ATI");
			return;
		}

		block = &ev02_blocks[data->block_index];
		chunk = MIN(IQS9150_WRITE_PAYLOAD_MAX, block->len - data->block_offset);
		rc = write_reg(dev, block->reg + data->block_offset,
			       block->data + data->block_offset, chunk);
		if (rc != 0) {
			iqs9150_fail(data, "initialization data write", rc);
			return;
		}
		/* The write to Config Settings changes termination semantics for
		 * this very communication session. */
		if (block->reg + data->block_offset == IQS9150_CONFIG_SETTINGS_REG) {
			data->terminate_comms_window = (block->data[data->block_offset + 1] & BIT(6)) != 0;
		}
		rc = terminate_comms_if_needed(data);
		if (rc != 0) {
			iqs9150_fail(data, "initialization data communication termination", rc);
			return;
		}

		data->block_offset += chunk;
		if (data->block_offset == block->len) {
			data->block_index++;
			data->block_offset = 0;
		}
		return;
	}

	case IQS9150_START_ATI:
		/* IQS9150 needs only the Trackpad re-ATI action here.  Keep this as a
		 * complete 16-bit write during the RDY window; requesting ALP re-ATI
		 * simultaneously can introduce an unrelated calibration failure. */
		value[0] = IQS9150_SYSTEM_CONTROL_TP_REATI;
		value[1] = 0;
		rc = write_reg(dev, IQS9150_SYSTEM_CONTROL_REG, value, 2);
		if (rc != 0) {
			iqs9150_fail(data, "ATI start", rc);
			return;
		}
		rc = terminate_comms_if_needed(data);
		if (rc != 0) {
			iqs9150_fail(data, "ATI start communication termination", rc);
			return;
		}
		data->state = IQS9150_WAIT_ATI;
		LOG_INF("STEP1B ATI requested; waiting for completion");
		return;

	case IQS9150_WAIT_ATI:
		/* This handler is entered from the RDY falling-edge callback. Do not
		 * poll I2C during ATI: RDY may be low while conversions are running. */
		rc = read_reg(dev, IQS9150_INFO_FLAGS_REG, value, 2);
		if (rc != 0) {
			iqs9150_fail(data, "ATI result read", rc);
			return;
		}
		rc = terminate_comms_if_needed(data);
		if (rc != 0) {
			iqs9150_fail(data, "ATI result communication termination", rc);
			return;
		}
		if (value[0] & IQS9150_INFO_TP_ATI_ERROR) {
			/* A contact placed while the sensor is calibrating can set both
			 * TP_REATI_OCCURRED and TP_ATI_ERROR (for example 0x0118). This is
			 * recoverable startup timing, not a permanent configuration failure.
			 * Reset, reapply the complete EV02 image, and retry ATI after a quiet
			 * interval. / calibration中の接触はTP_REATI_OCCURREDとTP_ATI_ERRORを
			 * 同時に立て得る（例: 0x0118）。これは恒久的な設定不良ではなく復帰可能な
			 * 起動タイミング問題である。software reset後にEV02 imageを再適用し、
			 * 静止intervalを置いてATIを再試行する。 */
			data->ati_recovery_attempts++;
			if (data->ati_recovery_attempts > IQS9150_ATI_RECOVERY_RETRY_MAX) {
				LOG_ERR("STEP1B ATI retry limit reached; Info Flags=0x%02x%02x",
					value[1], value[0]);
				iqs9150_fail(data, "ATI recovery retry limit", -EIO);
				return;
			}
			LOG_WRN("STEP1B ATI error Info Flags=0x%02x%02x; startup recovery %u/%u",
				value[1], value[0], data->ati_recovery_attempts,
				IQS9150_ATI_RECOVERY_RETRY_MAX);
			iqs9150_start_runtime_recovery(data, "startup ATI error", false);
			k_work_schedule(&data->work, K_MSEC(IQS9150_ATI_RECOVERY_DELAY_MS));
			return;
		}

		data->state = IQS9150_ENABLE_EVENT_MODE;
		LOG_INF("STEP1B ATI complete; enabling Event Mode");
		k_work_schedule(&data->work, K_NO_WAIT);
		return;

	case IQS9150_ENABLE_EVENT_MODE:
		value[0] = CONFIG_SETTINGS_0;
		value[1] = CONFIG_SETTINGS_1 | BIT(0);
		value[2] = OTHER_SETTINGS_0;
		value[3] = OTHER_SETTINGS_1;
		rc = write_reg(dev, IQS9150_CONFIG_SETTINGS_REG, value, sizeof(value));
		if (rc != 0) {
			iqs9150_fail(data, "Event Mode enable", rc);
			return;
		}
		data->terminate_comms_window = (value[1] & BIT(6)) != 0;
		rc = terminate_comms_if_needed(data);
		if (rc != 0) {
			iqs9150_fail(data, "Event Mode communication termination", rc);
			return;
		}
		data->state = IQS9150_RUNNING;
		data->ati_recovery_attempts = 0;
		LOG_INF("STEP1B complete: Event Mode active; touch the pad to produce reports");
		return;

	case IQS9150_RUNNING:
		report_contact_frame(dev);
		return;

	case IQS9150_RECOVER_RESET:
		/* A delayed startup-ATI recovery may wake between RDY windows. Wait for
		 * the active-low RDY window before writing System Control; this keeps the
		 * recovery transaction subject to the same protocol timing as startup.
		 * 遅延した起動ATI recoveryはRDY window外で起き得る。System Controlの書込み前に
		 * active-low RDY windowを待ち、recovery transactionも起動時と同じprotocol
		 * timingに従わせる。 */
		if (gpio_pin_get_dt(&((const struct iqs9150_config *)dev->config)->rdy_gpio) != 0) {
			k_work_schedule(&data->work, K_MSEC(IQS9150_PRODUCT_RETRY_INTERVAL_MS));
			return;
		}
		/* Reset only after a failed report sequence. A sensor-originated reset
		 * already has Show Reset asserted, so it can go straight to ACK on the
		 * following RDY window. */
		/* System Control is little-endian. SW_RESET is bit 1 of the low
		 * byte, hence the complete command is 0x0002, not 0x0200.
		 * System Controlはlittle-endianである。SW_RESETは下位byteのbit 1なので、
		 * 完全なcommandは0x0200ではなく0x0002となる。 */
		value[0] = IQS9150_SYSTEM_CONTROL_SW_RESET;
		value[1] = 0;
		rc = write_reg(dev, IQS9150_SYSTEM_CONTROL_REG, value, 2);
		if (rc != 0) {
			iqs9150_fail(data, "runtime software reset", rc);
			return;
		}
		rc = terminate_comms_if_needed(data);
		if (rc != 0) {
			iqs9150_fail(data, "runtime reset communication termination", rc);
			return;
		}
		data->state = IQS9150_RECOVER_WAIT_RESET;
		LOG_INF("runtime recovery: software reset requested");
		return;

	case IQS9150_RECOVER_WAIT_RESET:
		rc = read_reg(dev, IQS9150_INFO_FLAGS_REG, value, 2);
		if (rc != 0) {
			iqs9150_fail(data, "runtime reset verification", rc);
			return;
		}
		rc = terminate_comms_if_needed(data);
		if (rc != 0) {
			iqs9150_fail(data, "runtime reset verification termination", rc);
			return;
		}
		if ((value[0] & IQS9150_INFO_SHOW_RESET) == 0) {
			LOG_WRN("runtime recovery waiting for Show Reset");
			return;
		}
		data->state = IQS9150_ACK_RESET;
		LOG_INF("runtime recovery: Show Reset observed; acknowledging reset");
		return;

	case IQS9150_ERROR:
		return;
	}
}

static void iqs9150_rdy_callback(const struct device *port, struct gpio_callback *callback,
				 uint32_t pins)
{
	struct iqs9150_data *data = CONTAINER_OF(callback, struct iqs9150_data, rdy_callback);

	ARG_UNUSED(port);
	ARG_UNUSED(pins);
	k_work_schedule(&data->work, K_NO_WAIT);
}

static int iqs9150_init(const struct device *dev)
{
	const struct iqs9150_config *cfg = dev->config;
	struct iqs9150_data *data = dev->data;
	int rc;

	if (!i2c_is_ready_dt(&cfg->i2c) || !gpio_is_ready_dt(&cfg->rdy_gpio)) {
		LOG_ERR("I2C or RDY GPIO is not ready");
		return -ENODEV;
	}

	rc = gpio_pin_configure_dt(&cfg->rdy_gpio, GPIO_INPUT);
	if (rc != 0) {
		LOG_ERR("RDY GPIO configuration failed: %d", rc);
		return rc;
	}

	data->dev = dev;
	data->state = IQS9150_WAIT_PRODUCT;
	k_work_init_delayable(&data->work, iqs9150_work_handler);
	gpio_init_callback(&data->rdy_callback, iqs9150_rdy_callback, BIT(cfg->rdy_gpio.pin));
	rc = gpio_add_callback(cfg->rdy_gpio.port, &data->rdy_callback);
	if (rc != 0) {
		LOG_ERR("RDY callback add failed: %d", rc);
		return rc;
	}
	rc = gpio_pin_interrupt_configure_dt(&cfg->rdy_gpio, GPIO_INT_EDGE_TO_ACTIVE);
	if (rc != 0) {
		LOG_ERR("RDY interrupt configuration failed: %d", rc);
		return rc;
	}

	/* USB reconnect can restart the XIAO while the EV02 remains in its own
	 * power-up/reset window. Do not fail device initialisation on one 0xeeee
	 * product read; wait asynchronously for a valid product number instead.
	 * USB再接続時にはXIAOだけ先に再起動し、EV02は電源投入/リセット途中のことがある。
	 * 1回の0xeeee読出しでデバイス初期化を失敗させず、非同期で有効な製品番号を待つ。 */
	LOG_INF("IQS9150 startup probe scheduled at I2C 0x%02x", cfg->i2c.addr);
	k_work_schedule(&data->work, K_MSEC(IQS9150_PRODUCT_RETRY_INTERVAL_MS));
	return 0;
}

#define IQS9150_DEFINE(inst) \
	static struct iqs9150_data iqs9150_data_##inst; \
	static const struct iqs9150_config iqs9150_config_##inst = { \
		.i2c = I2C_DT_SPEC_INST_GET(inst), \
		.rdy_gpio = GPIO_DT_SPEC_INST_GET(inst, rdy_gpios), \
		.two_finger_diagnostics = DT_INST_PROP_OR(inst, two_finger_diagnostics, false), \
		.multitouch_diagnostics = DT_INST_PROP_OR(inst, multitouch_diagnostics, false), \
	}; \
	DEVICE_DT_INST_DEFINE(inst, iqs9150_init, NULL, \
			      &iqs9150_data_##inst, &iqs9150_config_##inst, \
			      POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);

DT_INST_FOREACH_STATUS_OKAY(IQS9150_DEFINE)
