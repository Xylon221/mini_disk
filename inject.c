#include "inject.h"

/* 无需额外注释——函数功能已在 mini_disk.h 中声明 */

void
mini_disk_inject_set_delay(struct mini_disk *disk, uint32_t delay_us)
{
	if (disk) {
		disk->inject_delay_us = delay_us;
	}
}

void
mini_disk_inject_set_error_rate(struct mini_disk *disk, float rate)
{
	if (disk) {
		if (rate < 0.0f) {
			rate = 0.0f;
		}
		if (rate > 1.0f) {
			rate = 1.0f;
		}
		disk->inject_error_rate = rate;
	}
}
