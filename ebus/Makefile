# Makefile - The CSMA Elektor Bus
# Copyright (C) 2011 g10 Code GmbH
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <http://www.gnu.org/licenses/>.
#

MCU=atmega88
CC = avr-gcc
OBJCOPY = avr-objcopy
CFLAGS = -Wall -Wno-pointer-sign -mmcu=$(MCU) -g -Os
LIBS =

HOSTCC = gcc
HOSTCFLAGS = -D_GNU_SOURCE -Wall -Wno-pointer-sign -g -O0

sources = ebus.h hardware.c hardware.h protocol.h csma.c \
	  ebusdump.c testnode.c shutter.c proto-busctl.h proto-h61.h \
	  proto-dbgmsg.h onewire.c i2c.c i2c-lcd.c \
	  housed.c housectl.c \
	  hsd-misc.c hsd-misc.h \
          hsd-time.c hsd-time.h

all: housed housectl testnode.hex shutter.hex doorbell.hex

common_node_obj = hardware.o csma.o onewire.o

common_hsd_obj = hsd-misc.o hsd-time.o

.PHONY: FORCE

FORCE:

revision.h: FORCE Makefile
	@set -e; \
        if [ "x$$(git status -uno --porcelain)" = x ]; then \
          git branch -v --no-color 2>/dev/null \
          | awk '/^\* / {printf "#define GIT_REVISION \"%s\"\n",$$3}' \
	   > revision.h.tmp;\
	else \
	  echo '#define GIT_REVISION "unknown"' > revision.h.tmp ;\
	fi;\
	if [ ! -f $@ ]; then \
            mv -f revision.h.tmp $@ ;\
	elif ! cmp revision.h.tmp $@; then \
            mv -f revision.h.tmp $@ ;\
        fi

ebus.h : revision.h

hardware.o : hardware.h ebus.h protocol.h
csma.o : ebus.h protocol.h
testnode.o : hardware.h ebus.h protocol.h
shutter.o : ebus.h protocol.h proto-h61.h proto-busctl.h
onewire.o: hardware.h ebus.h
i2c.o: hardware.h ebus.h
i2c-lcd.o: hardware.h ebus.h

hsd-misc.o: hsd-misc.c hsd-misc.h
hsd-time.o: hsd-time.c hsd-time.h hsd-misc.h
ebusctl.o: hsd-time.h hsd-misc.h

testnode.elf : testnode.o $(common_node_obj)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

shutter.elf : shutter.o $(common_node_obj)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

doorbell.elf : doorbell.o $(common_node_obj) i2c.o i2c-lcd.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)


%.hex : %.elf
	$(OBJCOPY) -O ihex -j .text -j .data  $< $@

$(common_hsd_obj):
	$(HOSTCC) $(HOSTCFLAGS) -o $@ -c $*.c

housed : housed.c protocol.h proto-busctl.h proto-h61.h
	$(HOSTCC) $(HOSTCFLAGS) -o $@ housed.c

housectl : housectl.c protocol.h proto-busctl.h proto-h61.h $(common_hsd_obj)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ housectl.c $(common_hsd_obj)
