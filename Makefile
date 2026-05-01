# SPDX-License-Identifier: BSD-3-Clause

SPDK_ROOT_DIR := /home/ubuntu/workspace/spdk
PKG_CONFIG_PATH := $(SPDK_ROOT_DIR)/build/lib/pkgconfig

CC      := gcc
CFLAGS  := -g -O0 -Wall
CFLAGS  += $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) \
	       pkg-config --cflags spdk_nvme spdk_env_dpdk)

LDFLAGS := $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) \
	       pkg-config --libs spdk_nvme spdk_env_dpdk spdk_syslibs)

OBJS   := mini_disk.o monitor.o inject.o main.o
TARGET := mini_disk

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)
