# Generated automatically from Makefile.in by configure.
#************************************************************************
#*   IRC - Internet Relay Chat, Makefile
#*   Copyright (C) 1990, Jarkko Oikarinen
#*
#*   This program is free software; you can redistribute it and/or modify
#*   it under the terms of the GNU General Public License as published by
#*   the Free Software Foundation; either version 1, or (at your option)
#*   any later version.
#*
#*   This program is distributed in the hope that it will be useful,
#*   but WITHOUT ANY WARRANTY; without even the implied warranty of
#*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#*   GNU General Public License for more details.
#*
#*   You should have received a copy of the GNU General Public License
#*   along with this program; if not, write to the Free Software
#*   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#*/

RM=/bin/rm

# Default CFLAGS
CFLAGS= -g -O2 -D"FD_SETSIZE=1024"
#
# What I used on FreeBSD 2.1.7 with kernel compiled for 4096 descriptors
# the -D"FD_SETSIZE=4096" is only needed if sys/types.h has not been updated
# to match the kernel config --JRL
#CFLAGS= -g -D"FD_SETSIZE=4096" -O2
#
#
# NOTE: The rest of these definitions may or may not work, I haven't tested them --JRL
#
# use the following on MIPS:
#CFLAGS= -systype bsd43 -DSYSTYPE_BSD43
# For Irix 4.x (SGI), use the following:
#CFLAGS= -g -cckr
#
# on NEXT use:
#CFLAGS=-bsd
#on NeXT other than 2.0:
#IRCDLIBS=-lsys_s
#
# AIX 370 flags
#CFLAGS=-D_BSD -Hxa
#IRCDLIBS=-lbsd
#
# Dynix/ptx V2.0.x
#CFLAGS= -O -Xo
#IRCDLIBS= -lsocket -linet -lnsl -lseq
# 
# Dynix/ptx V1.x.x
#IRCDLIBS= -lsocket -linet -lnsl -lseq
#
#use the following on SUN OS without nameserver libraries inside libc
#IRCDLIBS=-lresolv
#
# ESIX
#CFLAGS=-O -I/usr/ucbinclude
#IRCDLIBS=-L/usr/ucblib -L/usr/lib -lsocket -lucb -lns -lnsl
#
# LDFLAGS - flags to send the loader (ld). SunOS users may want to add
# -Bstatic here.
#
#LDFLAGS=-Bstatic
#
#Dell SVR4
#CC=gcc
#CFLAGS= -O2
#IRCDLIBS=-lsocket -lnsl -lucb
#IRCLIBS=-lcurses -lresolv -lsocket -lnsl -lucb



SHELL=/bin/sh
SUBDIRS=src tools

MAKE=make 'CFLAGS=${CFLAGS}' 'INSTALL=${INSTALL}' 'LDFLAGS=${LDFLAGS}'

all:	build

build:
	-@if [ ! -f include/setup.h ] ; then \
		echo "Hmm...doesn't look like you've run configure..."; \
		echo "Doing so now."; \
		sh configure; \
	fi
	@for i in $(SUBDIRS); do \
		echo "Building $$i";\
		cd $$i;\
		${MAKE} build; cd ..;\
	done

clean:
	${RM} -f *~ core
	@for i in $(SUBDIRS); do \
		echo "Cleaning $$i";\
		cd $$i;\
		${MAKE} clean; cd ..;\
	done
	-@if [ -f include/setup.h ] ; then \
	echo "To really restart installation, make distclean" ; \
	fi

distclean:
	${RM} -f Makefile *~ *.rej *.orig core ircd.core
	${RM} -f config.status config.cache config.log
	cd include; ${RM} -f setup.h *~ *.rej *.orig ; cd ..
	@for i in $(SUBDIRS); do \
		echo "Cleaning $$i";\
		cd $$i;\
		${MAKE} distclean; cd ..;\
	done

depend:
	@for i in $(SUBDIRS); do \
		echo "Making dependencies in $$i";\
		cd $$i;\
		${MAKE} depend; cd ..;\
	done

install: all
	@./tools/install_ircd

