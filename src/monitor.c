#include "monitor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

static pthread_t          g_monitor_thread;
static int                g_monitor_running = 0;
static struct mini_disk  *g_monitor_disk = NULL;

static void *
monitor_loop(void *arg)
{
	(void)arg;
	io_stats prev, curr;
	memset(&prev, 0, sizeof(prev));

	while (g_monitor_running) {
		sleep(1);

		if (g_monitor_disk == NULL) {
			continue;
		}
		mini_disk_get_stats(g_monitor_disk, &curr);

		uint64_t total_ios = curr.total_reads + curr.total_writes;

		/* 跳过第一次迭代——还没有 delta 可报告 */
		if (total_ios == 0 ||
		    (prev.total_reads == 0 && prev.total_writes == 0 &&
		     curr.total_reads == 0 && curr.total_writes == 0)) {
			prev = curr;
			continue;
		}

		uint64_t delta_ios = (curr.total_reads - prev.total_reads) +
				     (curr.total_writes - prev.total_writes);
		double iops    = (double)delta_ios;
		double avg_lat = (total_ios > 0)
			? (double)curr.total_latency_us / (double)total_ios
			: 0.0;
		double max_lat = (double)curr.max_latency_us;
		double err_rate = (total_ios > 0)
			? ((double)curr.error_count / (double)total_ios) * 100.0
			: 0.0;

		printf("[MONITOR] IOPS=%.0f | avg_lat=%.0fus | max_lat=%.0fus | "
		       "errors=%lu/%lu (%.1f%%)\n",
		       iops, avg_lat, max_lat,
		       (unsigned long)curr.error_count,
		       (unsigned long)total_ios,
		       err_rate);

		if (avg_lat > 10000.0) {
			printf("[MONITOR] WARN: 平均延迟 %.0fus 超过 10ms 阈值\n",
			       avg_lat);
		}
		if (err_rate > 50.0) {
			printf("[MONITOR] FATAL: 错误率 %.1f%% 超过 50%% 阈值\n",
			       err_rate);
		}

		prev = curr;
	}

	return NULL;
}

int
monitor_start(struct mini_disk *disk)
{
	if (g_monitor_running) {
		return -1;
	}
	g_monitor_disk = disk;
	g_monitor_running = 1;
	if (pthread_create(&g_monitor_thread, NULL, monitor_loop, NULL) != 0) {
		g_monitor_running = 0;
		return -1;
	}
	return 0;
}

void
monitor_stop(void)
{
	if (g_monitor_running) {
		g_monitor_running = 0;
		pthread_join(g_monitor_thread, NULL);
	}
}
