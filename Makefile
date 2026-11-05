# WHDUpdate - keeps your WHDLoad games up to date, on the Amiga itself.
# Build under WSL with the bebbo amiga-gcc toolchain on PATH.
#
# -m68000 so one binary runs on every Amiga in the house, A500 included.
#
#   make          build out/WHDUpdate
#   make test     host checks of the platform-neutral code
#   make clean

VERSION = 0.10
CC      = m68k-amigaos-gcc
CFLAGS  = -m68000 -O2 -noixemul -Wall -Wno-pointer-sign -fomit-frame-pointer \
          -DWHDU_VERSION=\"$(VERSION)\"

CORE    = src/slave.c src/inflate.c src/dat.c src/match.c
AMIGA   = src/main.c src/net.c src/scan.c src/update.c src/engine.c src/gui.c src/guiprefs.c
HDRS    = src/slave.h src/inflate.h src/dat.h src/match.h
AHDRS   = src/amiga.h src/update.h src/engine.h src/guiprefs.h

out/WHDUpdate: $(CORE) $(AMIGA) $(HDRS) $(AHDRS) Makefile
	mkdir -p out
	$(CC) $(CFLAGS) -s -o $@ $(CORE) $(AMIGA) -lamiga

# The DAT, inflate and matching code is plain C, so it is checked on the
# build host against the real DAT - much quicker than a trip to the Amiga.
test: out/test_host
	out/test_host
out/test_host: $(CORE) tests/test_host.c $(HDRS)
	mkdir -p out
	gcc -O2 -Wall -Wextra -o $@ $(CORE) tests/test_host.c

clean:
	rm -rf out
