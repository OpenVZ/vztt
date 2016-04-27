ARCH=$(shell uname -i)
DESTDIR=
BINDIR=$(DESTDIR)/usr/bin
SBINDIR=$(DESTDIR)/usr/sbin
DATADIR=$(DESTDIR)/usr/share/vztt
LIBDIR=$(DESTDIR)/usr/lib
_LIBDIR=/usr/lib
ifeq "${ARCH}" "x86_64"
LIBDIR=$(DESTDIR)/usr/lib64
_LIBDIR=/usr/lib64
endif
MANDIR=$(DESTDIR)/usr/share/man
MAN8DIR=$(MANDIR)/man8
MAN5DIR=$(MANDIR)/man5
CONFDIR=$(DESTDIR)/etc/vztt
VZCONFDIR=$(DESTDIR)/vz/template/conf/vztt
INCDIR=$(DESTDIR)/usr/include/vz
LIBEXECDIR=$(DESTDIR)/usr/libexec


SBIN_FILES = src/vzpkg
BIN_FILES = scripts/vzmktmpl
LIB_FILES = src/myinit
VZTT_LIBS = src/libvztt.a src/libvztt.so*
VZTT_BINS = $(SBIN_FILES) $(LIB_FILES) $(VZTT_LIBS)
VZTT_INCLUDES = include/vztt_error.h include/vztt_types.h include/vztt.h include/vztt_options.h
# vztt name2veid
MAN8_FILES = man/vzpkg.8 man/vzmktmpl.8
MAN5_FILES = man/vzpkg.metafile.5
NOJQUOTA_CONF_FILE = etc/nojquota.conf
CONF_FILE = etc/vztt.conf
URL_MAP = etc/url.map
SAMPLES_DIRS = samples/fedora-core-4-x86 samples/ubuntu-6.06-x86_64 samples/centos-5-x86_64 samples/fedora-core-12-x86 samples/suse-11.2-x86 samples/ubuntu-10.04-x86_64
VZTT_LIBEXEC = src/vztt_pfcache_xattr scripts/ovz-template-converter

#############################
default: all

all: VZTT_BINS

VZTT_BINS:
	(cd src && $(MAKE))

install: install-sbin install-bin install-bin install-lib install-man install-conf install-samples install-includes install-libexec

install-sbin: $(SBIN_FILES)
	mkdir -p $(SBINDIR)
	for f in $(SBIN_FILES); do \
		install -m 755 $$f $(SBINDIR); \
	done

install-bin: $(BIN_FILES)
	mkdir -p $(BINDIR)
	for f in $(BIN_FILES); do \
		install -m 755 $$f $(BINDIR); \
	done

install-lib: $(LIB_FILES) $(VZTT_LIBS)
	mkdir -p $(LIBDIR)/vztt
	for f in $(LIB_FILES); do \
		install -m 755 $$f $(LIBDIR)/vztt; \
	done
	for f in $(VZTT_LIBS); do \
		cp -a $$f $(LIBDIR); \
	done

install-man: install-man8 install-man5
	mkdir -p $(MANDIR)

install-man8: $(MAN8_FILES)
	mkdir -p $(MAN8DIR)
	for f in $(MAN8_FILES); do \
		install -m 644 $$f $(MAN8DIR); \
	done

install-man5: $(MAN5_FILES)
	mkdir -p $(MAN5DIR)
	for f in $(MAN5_FILES); do \
		install -m 644 $$f $(MAN5DIR); \
	done

install-conf: $(CONF_FILE) $(URL_MAP) $(NOJQUOTA_CONF_FILE)
	mkdir -p $(CONFDIR)
	install -m 644 $(CONF_FILE) $(CONFDIR)
	install -m 644 $(NOJQUOTA_CONF_FILE) $(CONFDIR)
	mkdir -p $(VZCONFDIR)
	install -m 644 $(URL_MAP) $(VZCONFDIR)
	ln -sf $(subst $(DESTDIR),,$(VZCONFDIR))/url.map $(CONFDIR)/url.map

install-samples: $(SAMPLES_DIRS)
	for f in $(SAMPLES_DIRS); do \
		mkdir -p $(DATADIR)/$$f; \
		cp -a $$f/* $(DATADIR)/$$f || :; \
	done

install-includes: $(VZTT_INCLUDES)
	mkdir -p $(INCDIR)
	for f in $(VZTT_INCLUDES); do \
		install -m 644 $$f $(INCDIR); \
	done

install-libexec: $(VZTT_LIBEXEC)
	mkdir -p $(LIBEXECDIR)
	for f in $(VZTT_LIBEXEC); do \
		install -m 755 $$f $(LIBEXECDIR); \
	done

clean:
	(cd src && $(MAKE) clean)
 
