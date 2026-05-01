# mini_disk — SPDK 用户态 NVMe 磁盘客户端库

基于 [SPDK](https://spdk.io/) 的最小用户态磁盘客户端库，演示三个核心概念：

1. **用户态 NVMe IO 路径** — 绕过内核，通过 DPDK 和 SPDK 直接驱动 NVMe 设备
2. **IO 统计与健康监控** — 跟踪延迟、吞吐量和错误指标；定时打印健康报告
3. **故障注入** — 注入人工延迟或错误率，验证监控行为并观察性能退化

## 架构

```
┌─────────────────────────────────────────────┐
│                  main.c                      │
│             （四阶段 IO 测试驱动）              │
├────────┬──────────┬────────────┬─────────────┤
│mini_disk│ monitor │   inject   │             │
│  .c/.h  │  .c/.h  │   .c/.h   │             │
│  核心   │  健康    │  故障注入   │  Bounce     │
│  NVMe   │ 监控线程 │（延迟、    │  Buffer     │
│  路径   │         │  错误率）   │  管理       │
├─────────┴──────────┴────────────┴─────────────┤
│              SPDK (libspdk_nvme)              │
│              DPDK (rte_eal, rte_bus_pci)      │
│              NVMe SSD (QEMU 模拟)             │
└───────────────────────────────────────────────┘
```

| 模块 | 职责 |
|------|------|
| `mini_disk` | 探测 NVMe 控制器、分配 IO 队列对、通过 SPDK 提交读写命令、轮询完成、跟踪单 IO 统计 |
| `monitor` | 后台 pthread 每秒读取 `io_stats`，报告 IOPS、平均/最大延迟、错误率。平均延迟 > 10ms 时告警，错误率 > 50% 时打印 FATAL |
| `inject` | 设置故障注入参数：人工延迟（累加到延迟统计中）和概率性错误返回 |

## 编译

**前提条件：** SPDK 必须已在 `/home/ubuntu/workspace/spdk` 中完成编译（包含 DPDK 和 NVME 驱动），且已配置大页内存（通过 SPDK 的 `setup.sh` 脚本）。

```bash
cd /home/ubuntu/workspace/mini_disk
make
```

底层使用 `pkg-config` 自动解析所有 SPDK、DPDK 和系统库的编译链接参数，生成 `mini_disk` 二进制文件。

## 运行

```bash
sudo LD_LIBRARY_PATH=/home/ubuntu/workspace/spdk/build/lib:/home/ubuntu/workspace/spdk/dpdk/build/lib:/home/ubuntu/workspace/spdk/isa-l/.libs:/home/ubuntu/workspace/spdk/isa-l-crypto/.libs \
  ./mini_disk 0000:00:04.0
```

请将 PCI 地址替换为你 NVMe 设备的 BDF 地址。

测试会自动执行四个阶段（见下方**预期输出**）。

## 预期输出

### 阶段 1 — 正常 IO
```
=== Phase 1: Normal IO ===
[MONITOR] IOPS=2 | avg_lat=200us | max_lat=200us | errors=0/...
```
延迟低（微秒级），错误计数始终为 0。

### 阶段 2 — 注入 50ms 延迟
```
=== Phase 2: Inject 50ms delay ===
[MONITOR] IOPS=2 | avg_lat=50200us | max_lat=50200us | errors=0/...
[MONITOR] WARN: 平均延迟 50200us 超过 10ms 阈值
```
人工延迟将报告延迟推高到 10ms 阈值以上，触发 WARN 消息。

### 阶段 3 — 注入 30% 错误率
```
=== Phase 3: Inject 30% error rate ===
[MONITOR] IOPS=2 | avg_lat=... | max_lat=... | errors=60/200 (30.0%)
```
约 30% 的 IO 失败。监控报告非零错误计数。错误率未超过 50%，不打印 FATAL。

### 阶段 4 — 恢复
```
=== Phase 4: Recovery ===
[MONITOR] IOPS=2 | avg_lat=200us | max_lat=... | errors=60/400 (15.0%)
```
错误不再增加（注入已清除）。更多成功 IO 完成后，整体错误率被稀释。
