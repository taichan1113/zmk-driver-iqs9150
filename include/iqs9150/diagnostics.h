#pragma once
#include <stdint.h>
struct device;

/* Cached observations only: never opens a sensor communication window.
 * RAM採取値のみ。センサー通信を開始しない。 sampled_ms=0 means no observation. */
struct iqs9150_power_observation {
	int64_t sampled_ms;
	uint16_t info_flags;
	uint32_t mode_reports[8];
};
int iqs9150_get_power_observation(const struct device *dev,
				struct iqs9150_power_observation *out);
