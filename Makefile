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
LIB_FILES = src/myinit
VZTT_LIBS = src/libvztt.a src/libvztt.so*
VZTT_BINS = $(SBIN_FILES) $(LIB_FILES) $(VZTT_LIBS)
VZTT_INCLUDES = include/vztt_error.h include/vztt_types.h include/vztt.h include/vztt_options.h
# vztt name2veid
MAN8_FILES = man/vzpkg.8
NOJQUOTA_CONF_FILE = etc/nojquota.conf
CONF_FILE = etc/vztt.conf
URL_MAP = etc/url.map
VZTT_LIBEXEC = src/vztt_pfcache_xattr src/run_from_chroot
VZTT_LIBEXEC_REBRAND = scripts/ovz-template-converter
include Makefile.incl

#############################
default: all

all: VZTT_BINS

define install_rebrand
	l=`basename $(1)`; \
	cat $(1).in | sed -e "s,@PRODUCT_NAME_SHORT@,$(PRODUCT_NAME_SHORT),g" > $(2)/$$l; \
	chmod $(3) $(2)/$$l;
endef

VZTT_BINS:
	(cd src && $(MAKE))

install: install-sbin install-lib install-man install-conf install-includes install-libexec

install-sbin: $(SBIN_FILES)
	mkdir -p $(SBINDIR)
	for f in $(SBIN_FILES); do \
		install -m 755 $$f $(SBINDIR); \
	done

install-lib: $(LIB_FILES) $(VZTT_LIBS)
	mkdir -p $(LIBDIR)/vztt
	for f in $(LIB_FILES); do \
		install -m 755 $$f $(LIBDIR)/vztt; \
	done
	for f in $(VZTT_LIBS); do \
		cp -a $$f $(LIBDIR); \
	done

install-man: install-man8
	mkdir -p $(MANDIR)

install-man8:
	mkdir -p $(MAN8DIR)
	for f in $(MAN8_FILES); do \
		$(call install_rebrand,$$f,$(MAN8DIR),0644) \
	done

install-conf: $(CONF_FILE).in $(URL_MAP) $(NOJQUOTA_CONF_FILE)
	mkdir -p $(CONFDIR)
	$(call install_rebrand,$(CONF_FILE),$(CONFDIR),0644)
	install -m 644 $(NOJQUOTA_CONF_FILE) $(CONFDIR)
	mkdir -p $(VZCONFDIR)
	install -m 644 $(URL_MAP) $(VZCONFDIR)
	ln -sf $(subst $(DESTDIR),,$(VZCONFDIR))/url.map $(CONFDIR)/url.map

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
	for f in $(VZTT_LIBEXEC_REBRAND); do \
		$(call install_rebrand,$$f,$(LIBEXECDIR),0755) \
	done

clean:
	(cd src && $(MAKE) clean)
 
