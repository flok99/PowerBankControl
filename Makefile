VERSION=0.1

DEBUG=-g -W -pedantic #-pg #-fprofile-arcs
LDFLAGS=$(DEBUG)
CXXFLAGS+=-O3 -Wall -DVERSION=\"$(VERSION)\" $(DEBUG)

OBJS=error.o pbc.o
TRANSLATIONS=nl.mo

all: powerbankcontrol

nl.mo: nl.po
	msgfmt -o nl.mo nl.po

powerbankcontrol: $(OBJS) $(TRANSLATIONS)
	$(CXX) $(OBJS) $(LDFLAGS) -o powerbankcontrol

install: powerbankcontrol $(TRANSLATIONS)
	cp powerbankcontrol $(DESTDIR)/usr/local/sbin
	mkdir -p $(DESTDIR)/usr/share/locale/nl/LC_MESSAGES
	cp nl.mo $(DESTDIR)/usr/share/locale/nl/LC_MESSAGES/powerbankcontrol.mo

uninstall: clean
	rm -f $(DESTDIR)/usr/local/sbin/powerbankcontrol

clean:
	rm -rf $(OBJS) powerbankcontrol

package: clean
	# source package
	rm -rf powerbankcontrol-$(VERSION)*
	mkdir kliertjes-$(VERSION)
	cp -a *.c* *.h readme*.txt Makefile license.txt powerbankcontrol-$(VERSION)
	tar czf powerbankcontrol-$(VERSION).tgz powerbankcontrol-$(VERSION)
	rm -rf powerbankcontrol-$(VERSION)

check:
	cppcheck -v --enable=all --inconclusive -I. . 2> err.txt
