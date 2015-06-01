### Compiler flags
CC = gcc
#MODE = test
ifeq "${MODE}" "test"
CFLAGS = -g -Wall -O0
else
CFLAGS = -g -Wall -O2
endif
override CFLAGS += -fPIC -D_REENTRANT -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE
LDFLAGS += -L . $(LGCOV)

ARCH=$(shell uname -i)
LIBDIR=/usr/lib
ifeq "${ARCH}" "x86_64"
LIBDIR=/usr/lib64
endif
INC = -I../include
LIBD =  -Wl,-Bdynamic -ldl -lvzctl2 -lploop

LIBVER = 1
LIBVER_MINOR=0.3

LOBJ = options.o queue.o util.o template.o tmplset.o config.o \
	transaction.o apt.o yum.o downloader.o md5.o cache.o \
	modify.o show_list.o misc.o upgrade.o cleanup.o \
	info.o lock.o metadata.o appcache.o ploop.o zypper.o env_compat.o \
	list_avail.o

LIB_vztt = libvztt.so.$(LIBVER).$(LIBVER_MINOR)
LIB_vztt_major = libvztt.so.$(LIBVER)

 
all: myinit vzpkgchroot libvztt.a vzpkg vztt_pfcache_xattr \
	libvztt.so $(LIB_vztt) $(LIB_vztt_major)

myinit: init.c
	$(CC) -O2 -static -s -o $@ $<

vzpkgchroot: vzpkgchroot.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

$(LIB_vztt): $(LOBJ)
	$(CC) $(CFLAGS) $(INC) $(LDFLAGS) -shared -Wl,--as-needed \
	-Wl,-soname=$(LIB_vztt_major),--version-script=export.map $^ $(LIBD) -o $@

libvztt.so: $(LIB_vztt)
	ln -sf $(LIB_vztt) libvztt.so
	ln -sf $(LIB_vztt) $(LIB_vztt_major)

libvztt.a: $(LOBJ)
	ar scq $@ $+
	ranlib $@

ifeq "${MODE}" "test"
vzpkg: vztt.o $(PWD)/libvztt.a
	$(CC) $(CFLAGS) $^ $(LDFLAGS) $(LIBD) -o $@
else
vzpkg: vztt.o libvztt.so
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -lvztt -lvzctl2 -o $@
endif

#vztest: ./src/test.o ./libvztt.a
#	$(CC) $(CFLAGS) $^ -Wl,-Bstatic -lcurl -lssl -lcrypto \
	-lgssapi_krb5 -lkrb5 -lz -lk5crypto -lkrb5support \
	-Wl,-Bdynamic -lpthread -lslang -lresolv -lcom_err -lvzctl $(LDFLAGS) -o $@
# $(LIBDIR)/libvzfs.a

vztt_pfcache_xattr : pfcache.o util.o queue.o config.o
	$(CC) $(CFLAGS) $^ -lploop -lvzctl2 -o $@ $(LDFLAGS)

.c.o:
	$(CC) -c $(CFLAGS) $(INC) $< -o $@

clean:
	rm -rf *.o myinit vzpkgchroot libvztt.a \
	vzpkg vztt_pfcache_xattr libvztt.so*

