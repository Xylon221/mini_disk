#include "mini_disk.h"
#include "spdk/stdinc.h"
#include "spdk/nvme.h"
#include "spdk/env.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* 每个 IO 请求的上下文，用于完成回调跟踪 */
enum io_dir {
	IO_READ  = 0,
	IO_WRITE = 1,
};

struct io_ctx {
	struct mini_disk *disk;
	int              is_completed;
	int              status;	/* 0 = 成功, 1 = 注入错误, 2 = NVMe 错误 */
	uint64_t         submit_ticks;
	enum io_dir      dir;
	uint32_t         data_size;
};

static int g_env_initialized = 0;

static void
io_complete(void *arg, const struct spdk_nvme_cpl *completion)
{
	struct io_ctx *ctx = arg;
	uint64_t now_ticks = spdk_get_ticks();
	uint64_t elapsed_ticks = now_ticks - ctx->submit_ticks;
	uint64_t ticks_hz = spdk_get_ticks_hz();
	uint64_t latency_us = (ticks_hz > 0) ? (elapsed_ticks * 1000000) / ticks_hz : 0;

	/* 故障注入：在延迟统计中加入人工延迟 */
	latency_us += ctx->disk->inject_delay_us;

	/* 检查 NVMe 实际完成状态 */
	if (spdk_nvme_cpl_is_error(completion)) {
		ctx->status = 2;
	}

	/* 故障注入：按配置比率模拟错误 */
	if (ctx->disk->inject_error_rate > 0.0f) {
		float r = (float)rand() / (float)RAND_MAX;
		if (r < ctx->disk->inject_error_rate) {
			ctx->status = 1;
		}
	}

	/* 加锁更新统计信息 */
	pthread_mutex_lock(&ctx->disk->stats_lock);

	if (ctx->status == 0) {
		if (ctx->dir == IO_READ) {
			ctx->disk->stats.total_reads++;
			ctx->disk->stats.total_read_bytes += ctx->data_size;
		} else {
			ctx->disk->stats.total_writes++;
			ctx->disk->stats.total_write_bytes += ctx->data_size;
		}
		ctx->disk->stats.total_latency_us += latency_us;
		if (latency_us > ctx->disk->stats.max_latency_us) {
			ctx->disk->stats.max_latency_us = latency_us;
		}
		if (latency_us < ctx->disk->stats.min_latency_us) {
			ctx->disk->stats.min_latency_us = latency_us;
		}
	} else {
		ctx->disk->stats.error_count++;
	}

	pthread_mutex_unlock(&ctx->disk->stats_lock);

	ctx->is_completed = 1;
}

static bool
probe_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid,
	 struct spdk_nvme_ctrlr_opts *opts)
{
	(void)cb_ctx;
	(void)trid;
	(void)opts;
	return true;
}

static void
attach_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid,
	  struct spdk_nvme_ctrlr *ctrlr,
	  const struct spdk_nvme_ctrlr_opts *opts)
{
	(void)trid;
	(void)opts;
	struct mini_disk *disk = (struct mini_disk *)cb_ctx;

	disk->ctrlr = ctrlr;

	/* 挂载到第一个活跃的命名空间 */
	int nsid = spdk_nvme_ctrlr_get_first_active_ns(ctrlr);
	if (nsid != 0) {
		disk->ns = spdk_nvme_ctrlr_get_ns(ctrlr, nsid);
	}
}

int
mini_disk_init(struct mini_disk **disk, const char *pci_addr)
{
	if (disk == NULL || pci_addr == NULL) {
		return -1;
	}

	struct mini_disk *d = (struct mini_disk *)calloc(1, sizeof(struct mini_disk));
	if (d == NULL) {
		return -1;
	}
	d->stats.min_latency_us = UINT64_MAX;
	pthread_mutex_init(&d->stats_lock, NULL);

	/* 初始化 SPDK 环境（每个进程仅一次） */
	if (!g_env_initialized) {
		struct spdk_env_opts opts;
		opts.opts_size = sizeof(opts);
		spdk_env_opts_init(&opts);
		opts.name = "mini_disk";
		opts.shm_id = 0;
		if (spdk_env_init(&opts) < 0) {
			fprintf(stderr, "ERROR: spdk_env_init() failed\n");
			free(d);
			return -1;
		}
		g_env_initialized = 1;
	}

	/* 配置 PCIe 传输层及目标 PCI 地址 */
	struct spdk_nvme_transport_id trid = {};
	spdk_nvme_trid_populate_transport(&trid, SPDK_NVME_TRANSPORT_PCIE);
	snprintf(trid.traddr, sizeof(trid.traddr), "%s", pci_addr);

	/* 探测并挂载 NVMe 控制器 */
	int rc = spdk_nvme_probe(&trid, d, probe_cb, attach_cb, NULL);
	if (rc != 0 || d->ctrlr == NULL) {
		fprintf(stderr, "ERROR: no NVMe controller found at %s\n", pci_addr);
		free(d);
		return -1;
	}

	/* 分配 IO 队列对 */
	d->qpair = spdk_nvme_ctrlr_alloc_io_qpair(d->ctrlr, NULL, 0);
	if (d->qpair == NULL) {
		fprintf(stderr, "ERROR: spdk_nvme_ctrlr_alloc_io_qpair() failed\n");
		spdk_nvme_detach_async(d->ctrlr, NULL);
		free(d);
		return -1;
	}

	*disk = d;
	return 0;
}

static int
mini_disk_io(struct mini_disk *disk, uint64_t lba,
	     uint32_t lba_count, void *buf, int is_write)
{
	if (disk == NULL || buf == NULL) {
		return -1;
	}

	uint32_t sector_size = spdk_nvme_ns_get_sector_size(disk->ns);
	uint32_t data_size = sector_size * lba_count;

	/* 分配 DMA 可访问的 bounce buffer */
	void *bounce = spdk_zmalloc(data_size, sector_size, NULL,
				    SPDK_ENV_NUMA_ID_ANY, SPDK_MALLOC_DMA);
	if (bounce == NULL) {
		return -1;
	}

	if (is_write) {
		memcpy(bounce, buf, data_size);
	}

	struct io_ctx ctx;
	memset(&ctx, 0, sizeof(ctx));
	ctx.disk = disk;
	ctx.submit_ticks = spdk_get_ticks();
	ctx.dir = is_write ? IO_WRITE : IO_READ;
	ctx.data_size = data_size;

	int rc;
	if (is_write) {
		rc = spdk_nvme_ns_cmd_write(disk->ns, disk->qpair, bounce,
					     lba, lba_count, io_complete, &ctx, 0);
	} else {
		rc = spdk_nvme_ns_cmd_read(disk->ns, disk->qpair, bounce,
					    lba, lba_count, io_complete, &ctx, 0);
	}
	if (rc != 0) {
		spdk_free(bounce);
		return rc;
	}

	/* 轮询等待完成（同步模式） */
	while (!ctx.is_completed) {
		spdk_nvme_qpair_process_completions(disk->qpair, 0);
	}

	/* 读请求：将 bounce buffer 拷贝回用户缓冲区 */
	if (!is_write && ctx.status == 0) {
		memcpy(buf, bounce, data_size);
	}

	spdk_free(bounce);

	if (ctx.status == 2) {
		return -1;
	}
	if (ctx.status == 1) {
		return 1;
	}
	return 0;
}

int
mini_disk_read(struct mini_disk *disk, uint64_t lba,
	       uint32_t lba_count, void *buf)
{
	return mini_disk_io(disk, lba, lba_count, buf, 0);
}

int
mini_disk_write(struct mini_disk *disk, uint64_t lba,
		uint32_t lba_count, void *buf)
{
	return mini_disk_io(disk, lba, lba_count, buf, 1);
}

void
mini_disk_get_stats(struct mini_disk *disk, io_stats *stats)
{
	if (disk == NULL || stats == NULL) {
		return;
	}
	pthread_mutex_lock(&disk->stats_lock);
	*stats = disk->stats;
	pthread_mutex_unlock(&disk->stats_lock);
}

void
mini_disk_reset_stats(struct mini_disk *disk)
{
	if (disk == NULL) {
		return;
	}
	pthread_mutex_lock(&disk->stats_lock);
	memset(&disk->stats, 0, sizeof(disk->stats));
	disk->stats.min_latency_us = UINT64_MAX;
	pthread_mutex_unlock(&disk->stats_lock);
}

void
mini_disk_fini(struct mini_disk *disk)
{
	if (disk == NULL) {
		return;
	}
	if (disk->qpair) {
		spdk_nvme_ctrlr_free_io_qpair(disk->qpair);
	}
	if (disk->ctrlr) {
		struct spdk_nvme_detach_ctx *detach_ctx = NULL;
		spdk_nvme_detach_async(disk->ctrlr, &detach_ctx);
		if (detach_ctx) {
			spdk_nvme_detach_poll(detach_ctx);
		}
	}
	pthread_mutex_destroy(&disk->stats_lock);
	free(disk);
}
