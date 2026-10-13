# A Fraa in Orbit - a screensaver of orbital mechanics, after Neal Stephenson's "Anathem"
VERSION  := 0.1.0
PREFIX   ?= /usr
SYSCONFDIR ?= /etc
DESTDIR  ?=
CC       ?= cc
# gnu11 keeps the binary free of very recent glibc symbols, so packages built on a
# new distribution still run on older ones
CFLAGS   ?= -O2 -g
override CFLAGS += -std=gnu11 -Wall -Isrc
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null)

MINGW    ?= x86_64-w64-mingw32
MINGW_ROOT ?= /usr/$(MINGW)/sys-root/mingw

all: build/fraa-in-orbit

# the pictures in assets/art are built into the program, so it must be compiled from here
build/fraa-in-orbit: src/orbit.c src/stb_image.h $(wildcard assets/art/*)
	@mkdir -p build
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SDL_CFLAGS) -o $@ src/orbit.c $(LDFLAGS) $(SDL_LIBS) -lm

check: build/fraa-in-orbit
	SDL_VIDEODRIVER=dummy ./build/fraa-in-orbit --snapshot build/check.bmp --time 70 --size 640x400 && echo "ran a full round"

install: all
	install -Dm755 build/fraa-in-orbit $(DESTDIR)$(PREFIX)/bin/fraa-in-orbit
	install -Dm755 packaging/linux/fraa-in-orbit-screensaver $(DESTDIR)$(PREFIX)/bin/fraa-in-orbit-screensaver
	install -Dm644 packaging/linux/fraa-in-orbit.desktop $(DESTDIR)$(PREFIX)/share/applications/fraa-in-orbit.desktop
	install -Dm644 packaging/linux/fraa-in-orbit-saver.desktop $(DESTDIR)$(PREFIX)/share/screensaver-service/savers/fraa-in-orbit.desktop
	install -Dm644 packaging/linux/fraa-in-orbit-autostart.desktop $(DESTDIR)$(SYSCONFDIR)/xdg/autostart/fraa-in-orbit-screensaver.desktop
	install -Dm644 packaging/fraa-in-orbit.png $(DESTDIR)$(PREFIX)/share/icons/hicolor/256x256/apps/fraa-in-orbit.png
	install -Dm644 README.md $(DESTDIR)$(PREFIX)/share/doc/fraa-in-orbit/README.md
	install -Dm644 LICENSE $(DESTDIR)$(PREFIX)/share/doc/fraa-in-orbit/LICENSE

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/fraa-in-orbit $(DESTDIR)$(PREFIX)/bin/fraa-in-orbit-screensaver
	rm -f $(DESTDIR)$(PREFIX)/share/applications/fraa-in-orbit.desktop $(DESTDIR)$(SYSCONFDIR)/xdg/autostart/fraa-in-orbit-screensaver.desktop
	rm -f $(DESTDIR)$(PREFIX)/share/screensaver-service/savers/fraa-in-orbit.desktop
	rm -f $(DESTDIR)$(PREFIX)/share/icons/hicolor/256x256/apps/fraa-in-orbit.png
	rm -rf $(DESTDIR)$(PREFIX)/share/doc/fraa-in-orbit

# ---- packages (written to dist/) ----
ARCH_DEB := $(shell dpkg --print-architecture 2>/dev/null || echo amd64)

deb: all
	rm -rf build/deb && mkdir -p build/deb/DEBIAN dist
	$(MAKE) install DESTDIR=build/deb PREFIX=/usr
	strip build/deb/usr/bin/fraa-in-orbit
	sed -e 's/@VERSION@/$(VERSION)/' -e 's/@ARCH@/$(ARCH_DEB)/' \
	    -e "s/@SIZE@/$$(du -sk build/deb/usr | cut -f1)/" packaging/linux/control.in > build/deb/DEBIAN/control
	echo /etc/xdg/autostart/fraa-in-orbit-screensaver.desktop > build/deb/DEBIAN/conffiles
	dpkg-deb --root-owner-group --build build/deb dist/fraa-in-orbit_$(VERSION)_$(ARCH_DEB).deb

rpm:
	mkdir -p dist build/rpm/SOURCES
	git archive --format=tar.gz --prefix=fraa-in-orbit-$(VERSION)/ -o build/rpm/SOURCES/fraa-in-orbit-$(VERSION).tar.gz HEAD
	rpmbuild -bb $(RPMFLAGS) --define "_topdir $(CURDIR)/build/rpm" --define "version $(VERSION)" packaging/linux/fraa-in-orbit.spec
	cp build/rpm/RPMS/*/fraa-in-orbit-$(VERSION)-*.rpm dist/

# Windows: the screensaver itself, and a small FraaInOrbit.scr that goes in the
# Windows system folder (so it is listed with the other screensavers) and starts it
windows:
	@mkdir -p build/win
	$(MINGW)-gcc -O2 -std=gnu11 -Wall -Isrc -I$(MINGW_ROOT)/include/SDL2 -Dmain=SDL_main \
	    -o build/win/fraa-in-orbit.exe src/orbit.c \
	    -L$(MINGW_ROOT)/lib -lmingw32 -lSDL2main -lSDL2 -lm -mwindows -static-libgcc
	$(MINGW)-windres packaging/windows/stub.rc -O coff -o build/win/stub.res
	$(MINGW)-gcc -O2 -Wall -o build/win/FraaInOrbit.scr packaging/windows/stub.c build/win/stub.res -mwindows -static
	$(MINGW)-strip build/win/fraa-in-orbit.exe build/win/FraaInOrbit.scr
	cp $(MINGW_ROOT)/bin/SDL2.dll build/win/
	[ ! -f $(MINGW_ROOT)/bin/SDL3.dll ] || cp $(MINGW_ROOT)/bin/SDL3.dll build/win/

installer: windows
	mkdir -p dist
	makensis -V2 -DVERSION=$(VERSION) packaging/windows/fraa-in-orbit.nsi

dist: deb rpm installer

clean:
	rm -rf build dist

.PHONY: all check install uninstall deb rpm windows installer dist clean
