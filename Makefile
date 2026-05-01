# SPDX-License-Identifier: BSD-3-Clause

SPDK_ROOT_DIR := /home/ubuntu/workspace/spdk
PKG_CONFIG_PATH := $(SPDK_ROOT_DIR)/build/lib/pkgconfig

SRC_DIR  := src
BLD_DIR  := build

CC       := gcc
CFLAGS   := -g -O0 -Wall -I$(SRC_DIR)
CFLAGS   += $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) \
                 pkg-config --cflags spdk_nvme spdk_env_dpdk)

LDFLAGS  := $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) \
                 pkg-config --libs spdk_nvme spdk_env_dpdk spdk_syslibs)

SRCS     := $(wildcard $(SRC_DIR)/*.c)
OBJS     := $(patsubst $(SRC_DIR)/%.c, $(BLD_DIR)/%.o, $(SRCS))
TARGET   := $(BLD_DIR)/mini_disk

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

$(BLD_DIR)/%.o: $(SRC_DIR)/%.c | $(BLD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BLD_DIR):
	mkdir -p $(BLD_DIR)

clean:
	rm -rf $(BLD_DIR)
