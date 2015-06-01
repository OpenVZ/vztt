# $Id$
#
# This makefile is not needed for building the package. It just gives
# document writer an easy ability to generate man pages in various formats,
# in order to check the *roff/man markup.

MANS = vzpkg.8 repkg.8 vzpkgrepo.8

TXTS = $(patsubst %, %.txt, $(MANS))
PSS  = $(patsubst %, %.ps, $(MANS))
PDFS = $(patsubst %, %.pdf, $(MANS))
HTMLS = $(patsubst %, %.html, $(MANS))

all: txt ps pdf html

txt:	$(TXTS)
ps:	$(PSS)
pdf:	$(PDFS)
html:	$(HTMLS)

clean:
	-rm -f $(TXTS) $(PSS) $(PDFS) $(HTMLS)

%.txt: %
	groff -Tascii -man $< > $@

%.ps: %
	groff -Tps -man -ma4 $< > $@

%.pdf: %.ps
	ps2pdf $<

%.html: %
	true

.PHONY: txt html pdf ps
