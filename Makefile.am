LANG=C
LC_ALL=c

AM_CFLAGS=-Wall -O2 -W -Wshadow -Werror -Wextra -fvisibility=hidden \
	  	$(PTHREAD_CFLAGS)
AM_LDFLAGS=-Wl,-O2,--no-undefined-version,--enable-new-dtags,--as-needed,--warn-common
AM_CC=$(PTHREAD_CC)
AM_CPPFLAGS=-I $(SRCDIR)/src/lib

LIBS+=$(PTHREAD_LIBS) 

dist_man_MANS = libraptorial.3

doc_DIR=doc
doc_DATA=
doc_DATA+=README

dist_doc_DATA=
dist_doc_DATA+=COPYING libraptorial.xhtml

bin_PROGRAMS=raptorial-tester rapt-show-versions

lib_LTLIBRARIES = libraptorial.la
libraptorial_la_SOURCES = src/lib/packages.c
     
raptorial_tester_SOURCES = src/tester/main.c
raptorial_tester_LDADD = libraptorial.la

rapt_show_versions_SOURCES = src/bin/rapt-show-versions.c
rapt_show_versions_LDADD = libraptorial.la

check: $(srcdir)/tests/Packages $(bin_PROGRAMS)
	./raptorial-tester $(srcdir)/tests/Packages

%.3: $(doc_DIR)/man/man3/%.xml $(doc_DIR)/@PACKAGE_NAME@.ent
	$(XSLTPROC) --path $(doc_DIR) -o $@ --nonet /usr/share/xml/docbook/stylesheet/docbook-xsl/manpages/docbook.xsl $<

%.xhtml: $(doc_DIR)/man/man3/%.xml $(doc_DIR)/@PACKAGE_NAME@.ent
	$(XSLTPROC) --path $(doc_DIR) -o $@ --nonet /usr/share/xml/docbook/stylesheet/docbook-xsl/xhtml-1_1/docbook.xsl $<

$(doc_DIR)/@PACKAGE_NAME@.ent:
	@mkdir -p $(@D)
	echo -e '<!ENTITY dhfirstname "Nick">\n<!ENTITY dhsurname "Black">\n<!ENTITY dhemail "nick.black@sprezzatech.com">\n<!ENTITY dhusername "&dhfirstname; &dhsurname;">\n<!ENTITY dhrelease "$(VERSION)">' > "$@"

clean-local:
	rm -rf $(MANS) $(doc_DIR)/@PACKAGE_NAME@.ent

uninstall-local:
	rmdir "$(DESTDIR)$(docdir)"
