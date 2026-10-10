#!/bin/sh
#
# build.sh - zlib, libpng, libjpeg and giflib for the RP2350
#
# Not a port: these four are portable C and they compile as they are.
# What they do not have is a configure run that knows about a freestanding
# ARM target with libcmini, so this skips configure entirely and compiles
# the sources directly, with the one generated header each of them needs
# taken from the copy the tarball ships for exactly this purpose.
#
# The flags have to match HighWire's, in particular -fno-short-enums: the
# linker refuses to mix objects that disagree about how wide an enum is.

set -e

R=/mnt/d/Develop/GEMbedded
# the toolchain on PATH: ptos-env.sh puts the Linux one there.  The
# copy under GEMduino/toolchain is the Windows build, for the IDE.
CC=arm-none-eabi-gcc
AR=arm-none-eabi-ar
HERE=$R/GEM/highwire-libs
OUT=$HERE/lib
INC=$HERE/include

CPU="-mcpu=cortex-m33 -mthumb -mfloat-abi=softfp -mfpu=fpv5-sp-d16"
CFLAGS="$CPU -O2 -funsigned-char -fno-short-enums -mword-relocations \
        -ffunction-sections -fdata-sections \
        -I$R/GEM/highwire-notes/gem-compat \
        -I$R/pTOS/lib/libcmini/include \
        -I$INC"

mkdir -p "$OUT" "$INC"

say() { printf '\n=== %s ===\n' "$1"; }

# ---- zlib ------------------------------------------------------------
say zlib
cd "$HERE/zlib-1.3.1"
cp -f zconf.h.in zconf.h 2>/dev/null || true
cp -f zlib.h zconf.h "$INC/"
rm -f /tmp/zl/*.o; mkdir -p /tmp/zl
# no gz* here: -DZ_SOLO is what keeps zlib clear of stdio, and the
# gz family is exactly the part that needs it.  libpng uses the raw
# deflate interface and never asks for a gzFile.
for f in adler32 compress crc32 deflate infback inffast inflate \
         inftrees trees uncompr zutil; do
    $CC $CFLAGS -DZ_SOLO -c $f.c -o /tmp/zl/$f.o 2>&1 | grep -E "error" || true
done
$AR rcs "$OUT/libz.a" /tmp/zl/*.o
echo "  libz.a  $(stat -c%s "$OUT/libz.a") Bytes, $(ls /tmp/zl/*.o | wc -l) Objekte"

# ---- libpng ----------------------------------------------------------
say libpng
cd "$HERE/libpng-1.6.43"
cp -f scripts/pnglibconf.h.prebuilt pnglibconf.h
cp -f png.h pngconf.h pnglibconf.h "$INC/"
rm -rf /tmp/pn; mkdir -p /tmp/pn
for f in png pngerror pngget pngmem pngpread pngread pngrio pngrtran \
         pngrutil pngset pngtrans pngwio pngwrite pngwtran pngwutil; do
    $CC $CFLAGS -c $f.c -o /tmp/pn/$f.o 2>&1 | grep -E "error" || true
done
$AR rcs "$OUT/libpng.a" /tmp/pn/*.o
echo "  libpng.a  $(stat -c%s "$OUT/libpng.a") Bytes, $(ls /tmp/pn/*.o | wc -l) Objekte"

# ---- libjpeg ---------------------------------------------------------
say libjpeg
cd "$HERE/jpeg-9f"
# boolean as a plain int, not as an enum.  libjpeg's own header only
# avoids the enum when FALSE or TRUE is already a macro, and by the
# time HighWire's img-dcdr.c gets to jpeglib.h its img_gif.c has just
# #undef'd both -- so the enum would collide with the FALSE and TRUE
# that hw-types.h declares.  The library has to agree with the
# application here, so the flag belongs in both places.
JPEGDEF="-DHAVE_BOOLEAN -Dboolean=int -DFALSE=0 -DTRUE=1"
cp -f jconfig.txt jconfig.h
cp -f jpeglib.h jconfig.h jmorecfg.h jerror.h "$INC/"
rm -rf /tmp/jp; mkdir -p /tmp/jp
for f in jaricom jcapimin jcapistd jcarith jccoefct jccolor jcdctmgr \
         jchuff jcinit jcmainct jcmarker jcmaster jcomapi jcparam \
         jcprepct jcsample jctrans jdapimin jdapistd jdarith jdatadst \
         jdatasrc jdcoefct jdcolor jddctmgr jdhuff jdinput jdmainct \
         jdmarker jdmaster jdmerge jdpostct jdsample jdtrans jerror \
         jfdctflt jfdctfst jfdctint jidctflt jidctfst jidctint jmemmgr \
         jmemnobs jquant1 jquant2 jutils; do
    [ -f $f.c ] || continue
    $CC $CFLAGS $JPEGDEF -c $f.c -o /tmp/jp/$f.o 2>&1 | grep -E "error" || true
done
$AR rcs "$OUT/libjpeg.a" /tmp/jp/*.o
echo "  libjpeg.a  $(stat -c%s "$OUT/libjpeg.a") Bytes, $(ls /tmp/jp/*.o | wc -l) Objekte"

# ---- giflib ----------------------------------------------------------
say giflib
cd "$HERE/giflib-5.2.2"
cp -f gif_lib.h "$INC/"
rm -rf /tmp/gi; mkdir -p /tmp/gi
for f in dgif_lib egif_lib gifalloc gif_err gif_font gif_hash openbsd-reallocarray; do
    [ -f $f.c ] || continue
    $CC $CFLAGS -c $f.c -o /tmp/gi/$f.o 2>&1 | grep -E "error" || true
done
$AR rcs "$OUT/libgif.a" /tmp/gi/*.o
echo "  libgif.a  $(stat -c%s "$OUT/libgif.a") Bytes, $(ls /tmp/gi/*.o | wc -l) Objekte"

say fertig
ls -la "$OUT"
