DTC = dtc
DTSSRC = clcd-overlay.dts
DTBO = clcd.dtbo
NFS_DIR = /srv/nfs_ubuntu/my_i2c/dt

all: $(DTBO) copy

$(DTBO): $(DTSSRC)
	$(DTC) -@ -I dts -O dtb -o $(DTBO) $(DTSSRC)

copy: $(DTBO)
	mkdir -p $(NFS_DIR)
	cp $(DTBO) $(NFS_DIR)/

clean:
	rm -f $(DTBO)
	rm -f $(NFS_DIR)/$(DTBO)

.PHONY: all copy clean
