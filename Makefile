CPU              = MC68020
TOOL             = gnu


ADDED_CFLAGS	= -g

CFLAGS		= $(CC_OPTIM) $(CC_WARNINGS) $(CC_INCLUDE) $(CC_COMPILER) \
			$(CC_DEFINES) $(ADDED_CFLAGS)

CASFLAGS	= -E $(CC_INCLUDE) $(CC_DEFINES)
CC_WARNINGS	= $(CC_WARNINGS_ALL)
CC_OPTIM	= $(CC_OPTIM_NORMAL)
CC_INCLUDE	= -I$(UP)/h $(INCLUDE_CC) $(EXTRA_INCLUDE) \
		  -I$(VX_BSP_BASE)/src/config/ \
		  -I$(VX_BSP_BASE)/src/drv/
CC_DEFINES	= -DCPU=$(CPU) $(DEFINE_CC) $(EXTRA_DEFINE)

RM		= rm -f
CP		= cp
SHELL		= /bin/sh
# override things below this point

.s.o :
	@ $(RM) $@
	cat $< > $*.S
	$(CC) $(CFLAGS) -c $*.S
	@ $(RM) $*.S

.c.o :
	@ $(RM) $@
	$(CC) $(CFLAGS) -c $<

CPU=MC68020
TOOL=gnu

CC_OPTIM_DRIVER = -fno-builtin
CC_OPTIM_NORMAL = -O -fstrength-reduce -fno-builtin 
CC_OPTIM_TARGET = -O -fvolatile -fno-builtin

AR=ar68k
AS=as68k
CC=cc68k -m68030 -msoft-float
LD=ld68k
RANLIB=ranlib68k

TOOLENV= VX_CPU_FAMILY=68k
EXTRA_INCLUDE  = -I. -I$(CONFIG_ALL) -I$(VX_BSP_BASE)/h -I$(VX_VW_BASE)/h/


COMPILE_SYMTBL  = $(CC) -c $(CFLAGS)

CC_OPTIM = $(CC_OPTIM_TARGET)

OBJS = nfsd.o nfs_prot_svc.o nfs_prot_xdr.o mount_svc.o mount_xdr.o

nfsserver.o: $(OBJS)
	$(LD) -X -r -o nfsserver.o $(OBJS)

clean:
	/bin/rm -f *.o nfsserver
	/bin/rm -f *.BAK

tarstuff:
	(cd ..; tar cvf nfsd.tar nfsd)

nfs_prot.h: nfs_prot.x
	rpcgen4.0 -h nfs_prot.x -o nfs_prot_unix.h
	sed -f nfsdrpcgen.sed nfs_prot_unix.h > nfs_prot.h
	rm -f nfs_prot_unix.h

nfs_prot_svc.c: nfs_prot.x
	rpcgen4.0 -s udp nfs_prot.x -o nfs_prot_svc_unix.c
	sed -f nfsdrpcgen.sed nfs_prot_svc_unix.c > nfs_prot_svc.c
	rm -f nfs_prot_svc_unix.c

nfs_prot_xdr.c: nfs_prot.x
	rpcgen4.0 -c nfs_prot.x -o nfs_prot_xdr_unix.c
	sed -f nfsdrpcgen.sed nfs_prot_xdr_unix.c > nfs_prot_xdr.c
	rm -f nfs_prot_xdr_unix.c

mount_svc.c: mount.x
	rpcgen4.0 -s udp mount.x -o mount_svc_unix.c
	sed -f mountrpcgen.sed mount_svc_unix.c > mount_svc.c
	rm -f  mount_svc_unix.c

mount_xdr.c: mount.x
	rpcgen4.0 -c mount.x -o mount_xdr_unix.c
	sed -f mountrpcgen.sed mount_xdr_unix.c > mount_xdr.c
	rm -f  mount_xdr_unix.c

mount.h: mount.x
	rpcgen4.0 -h mount.x -o mount_unix.h
	sed -f mountrpcgen.sed mount_unix.h > mount.h
	rm -f  mount_unix.h
