BASEDIR=..

include $(BASEDIR)/Make.defs

###############################################################################
#
# Include standard packaging commands.

include $(BASEDIR)/Make.rules

###############################################################################

PROGS=fdisk ramdisk mkfs fsck partype

SPROGS=mkfs

all: $(PROGS)

max_rfs: install

rfs: install

min_rfs: install

net_rfs: install

smin_rfs: install_sibo

install_sibo: all
	cp -p $(SPROGS) $(TARGET_MNT)/sbin

install: all
	cp -p $(PROGS) $(TARGET_MNT)/sbin

fsck: fsck.o

fdisk: fdisk.o

mkfs: mkfs.o

partype: partype.o

ramdisk: ramdisk.o

clean:
	rm -f *.o $(FORMATMOD) core $(PROGS)

