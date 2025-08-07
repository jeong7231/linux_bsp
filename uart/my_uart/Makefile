APP := my_uart3_app
MOD := my_uart3
SRC := $(APP).c
obj-m := $(MOD).o

CROSS = ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf-
CC := arm-linux-gnueabihf-gcc
KDIR := /home/ubuntu/pi_bsp/kernel/linux
PWD := $(shell pwd)
TARGET_DIR := /srv/nfs_ubuntu/my_uart3

default: clean $(APP)
	$(MAKE) -C $(KDIR) M=$(PWD) modules $(CROSS)
	mkdir -p $(TARGET_DIR)
	cp $(MOD).ko $(TARGET_DIR)/
	cp $(APP) $(TARGET_DIR)/

$(APP): $(SRC)
	$(CC) $< -o $@

clean:
	rm -rf *.ko
	rm -rf *.mod.*
	rm -rf .*.cmd
	rm -rf *.o
	rm -rf modules.order
	rm -rf Module.symvers
	rm -rf $(MOD).mod
	rm -rf .tmp_versions
	rm -rf $(APP)
	rm -rf $(TARGET_DIR)/$(APP)
	rm -rf $(TARGET_DIR)/$(MOD).ko

.PHONY: all clean default
