#ifndef MINI_DISK_H
#define MINI_DISK_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SPDK 类型的前向声明（用于内部结构体） */
struct spdk_nvme_ctrlr;
struct spdk_nvme_ns;
struct spdk_nvme_qpair;

/* IO 统计信息 */
typedef struct io_stats {
	uint64_t total_reads;
	uint64_t total_writes;
	uint64_t total_read_bytes;
	uint64_t total_write_bytes;
	uint64_t total_latency_us;	/* 累计延迟，微秒 */
	uint64_t max_latency_us;	/* 最大延迟 */
	uint64_t min_latency_us;	/* 最小延迟 */
	uint64_t error_count;
} io_stats;

/* 磁盘句柄（成员为内部实现，请仅使用指针） */
struct mini_disk {
	struct spdk_nvme_ctrlr    *ctrlr;
	struct spdk_nvme_ns       *ns;
	struct spdk_nvme_qpair    *qpair;

	io_stats                   stats;
	pthread_mutex_t            stats_lock;

	uint32_t                   inject_delay_us;
	float                      inject_error_rate;

	/* mock 模式：不依赖真实 NVMe 硬件 */
	bool                       mock_mode;
	uint8_t                   *mock_data;
	uint64_t                   mock_size;
};

/* 初始化磁盘客户端，挂载到指定 PCI 地址的 NVMe 设备 */
int mini_disk_init(struct mini_disk **disk, const char *pci_addr);

/* 初始化 mock 磁盘客户端，使用内存模拟，不依赖真实 NVMe 硬件 */
int mini_disk_init_mock(struct mini_disk **disk);

/* 从 lba 开始读取 lba_count 个块到 buf */
int mini_disk_read(struct mini_disk *disk, uint64_t lba,
		   uint32_t lba_count, void *buf);

/* 从 buf 写入 lba_count 个块到 lba 起始位置 */
int mini_disk_write(struct mini_disk *disk, uint64_t lba,
		    uint32_t lba_count, void *buf);

/* 获取 IO 统计信息的快照 */
void mini_disk_get_stats(struct mini_disk *disk, io_stats *stats);

/* 重置 IO 统计信息归零 */
void mini_disk_reset_stats(struct mini_disk *disk);

/* 注入人工延迟（微秒），每次 IO 完成统计时累加 */
void mini_disk_inject_set_delay(struct mini_disk *disk, uint32_t delay_us);

/* 注入错误率 (0.0 ~ 1.0)，按比例将 IO 标记为失败 */
void mini_disk_inject_set_error_rate(struct mini_disk *disk, float rate);

/* 清理：释放 qpair、分离控制器、释放资源 */
void mini_disk_fini(struct mini_disk *disk);

#ifdef __cplusplus
}
#endif

#endif /* MINI_DISK_H */
