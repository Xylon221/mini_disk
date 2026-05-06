# SPDX-License-Identifier: BSD-3-Clause

SPDK_ROOT_DIR := /home/ubuntu/workspace/spdk
PKG_CONFIG_PATH := $(SPDK_ROOT_DIR)/build/lib/pkgconfig

SRC_DIR  := src
BLD_DIR  := build

CC       := gcc
CFLAGS   := -g -O0 -Wall -fPIC -I$(SRC_DIR)
CFLAGS   += $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) \
                 pkg-config --cflags spdk_nvme spdk_env_dpdk)

LDFLAGS  := $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) \
                 pkg-config --libs spdk_nvme spdk_env_dpdk spdk_syslibs)

# 核心库对象（不包含 main.c 和 fio 引擎）
LIB_SRCS := $(SRC_DIR)/mini_disk.c $(SRC_DIR)/monitor.c $(SRC_DIR)/inject.c
LIB_OBJS := $(patsubst $(SRC_DIR)/%.c, $(BLD_DIR)/%.o, $(LIB_SRCS))

# 独立测试二进制
MAIN_OBJ  := $(BLD_DIR)/main.o
TARGET    := $(BLD_DIR)/mini_disk

# fio IO 引擎共享库
FIO_SRC   := /tmp/fio
FIO_OBJ   := $(BLD_DIR)/fio_mini_disk.o
FIO_LIB   := $(BLD_DIR)/libmini_disk_fio.so

# fio 引擎编译需要在 include 路径中添加 fio 源码目录
# 并定义 CONFIG_HAVE_BOOL 以避免与 <stdbool.h> 冲突
$(FIO_OBJ): CFLAGS += -I$(FIO_SRC) -DCONFIG_HAVE_BOOL

.PHONY: all clean fio

all: $(TARGET)

fio: $(FIO_LIB)

$(TARGET): $(LIB_OBJS) $(MAIN_OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

$(FIO_LIB): $(FIO_OBJ) $(LIB_OBJS)
	$(CC) -shared -o $@ $^ $(LDFLAGS)

$(BLD_DIR)/%.o: $(SRC_DIR)/%.c | $(BLD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BLD_DIR):
	mkdir -p $(BLD_DIR)

clean:
	rm -rf $(BLD_DIR)
