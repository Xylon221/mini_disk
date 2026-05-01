#include "mini_disk.h"
#include "monitor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

static volatile int running = 1;

static void
sig_handler(int sig)
{
	(void)sig;
	running = 0;
}

int
main(int argc, char **argv)
{
	signal(SIGINT, sig_handler);

	struct mini_disk *disk = NULL;
	int ret;

	if (argc >= 2 && strcmp(argv[1], "--mock") == 0) {
		printf("Running in mock mode (no real NVMe hardware)\n");
		ret = mini_disk_init_mock(&disk);
	} else if (argc >= 2) {
		ret = mini_disk_init(&disk, argv[1]);
	} else {
		printf("Usage: %s <pci_addr | --mock>\n", argv[0]);
		printf("Example: %s 0000:00:04.0\n", argv[0]);
		printf("Example: %s --mock\n", argv[0]);
		return 1;
	}

	if (ret != 0) {
		printf("Failed to init disk\n");
		return 1;
	}
	printf("Disk initialized successfully\n");

	io_stats stats;
	mini_disk_get_stats(disk, &stats);
	printf("Initial stats: reads=%lu\n", (unsigned long)stats.total_reads);

	/* 启动健康监控线程 */
	monitor_start(disk);

	/* 分配 IO 缓冲区（4KB 对齐） */
	void *read_buf = NULL;
	void *write_buf = NULL;
	posix_memalign(&read_buf, 4096, 4096);
	posix_memalign(&write_buf, 4096, 4096);
	memset(write_buf, 0xAB, 4096);

	int io_count = 0;

	/* 阶段 1：正常 IO */
	printf("\n=== Phase 1: Normal IO ===\n");
	for (int i = 0; i < 100 && running; i++) {
		mini_disk_write(disk, (uint64_t)i * 8, 8, write_buf);
		mini_disk_read(disk, (uint64_t)i * 8, 8, read_buf);
		io_count += 2;
		usleep(10000);
	}

	/* 阶段 2：注入 50ms 延迟——模拟慢盘 */
	printf("\n=== Phase 2: Inject 50ms delay ===\n");
	mini_disk_inject_set_delay(disk, 50000);
	for (int i = 0; i < 50 && running; i++) {
		mini_disk_write(disk, ((uint64_t)100 + i) * 8, 8, write_buf);
		mini_disk_read(disk, ((uint64_t)100 + i) * 8, 8, read_buf);
		io_count += 2;
		usleep(10000);
	}

	/* 阶段 3：注入 30% 错误率 */
	printf("\n=== Phase 3: Inject 30%% error rate ===\n");
	mini_disk_inject_set_delay(disk, 0);
	mini_disk_inject_set_error_rate(disk, 0.3f);
	for (int i = 0; i < 100 && running; i++) {
		mini_disk_write(disk, ((uint64_t)200 + i) * 8, 8, write_buf);
		mini_disk_read(disk, ((uint64_t)200 + i) * 8, 8, read_buf);
		io_count += 2;
		usleep(10000);
	}

	/* 阶段 4：恢复——清除所有故障注入 */
	printf("\n=== Phase 4: Recovery ===\n");
	mini_disk_inject_set_error_rate(disk, 0.0f);
	for (int i = 0; i < 100 && running; i++) {
		mini_disk_write(disk, ((uint64_t)300 + i) * 8, 8, write_buf);
		mini_disk_read(disk, ((uint64_t)300 + i) * 8, 8, read_buf);
		io_count += 2;
		usleep(10000);
	}

	/* 最终统计汇总 */
	printf("\n=== Test Complete ===\n");
	mini_disk_get_stats(disk, &stats);
	uint64_t total_ios = stats.total_reads + stats.total_writes;
	printf("Total IOs: %d\n", io_count);
	printf("Reads: %lu, Writes: %lu\n",
	       (unsigned long)stats.total_reads,
	       (unsigned long)stats.total_writes);
	printf("Avg Latency: %lu us\n",
	       total_ios > 0
	       ? (unsigned long)(stats.total_latency_us / total_ios)
	       : 0);
	printf("Max Latency: %lu us\n", (unsigned long)stats.max_latency_us);
	printf("Errors: %lu\n", (unsigned long)stats.error_count);

	monitor_stop();
	free(read_buf);
	free(write_buf);
	mini_disk_fini(disk);

	return 0;
}
