#!/bin/sh
#
# build.sh - HighWire for the RP2350
#
# The browser itself, against the compat layer in gem-compat/ and the four
# libraries that GEM/highwire-libs/build.sh produces.  Run that one first;
# this one assumes lib/*.a are there.
#
# Two files are not HighWire's own:
#
#   gem-compat/gemx.c   the AES and VDI calls the board package does not
#                       have, or has with a different signature
#   net/espnet.c        stands in for src/ovl_sys.c, which loads the
#                       network overlay with Pexec.  There is no overlay
#                       loader here, so the module is built in instead.
#                       ovl_sys.c is therefore left out of the file list.
#
# Include order matters: gem-compat must come before the board package's
# own include directory, because gem-compat/gem.h reaches the latter with
# #include_next.

set -e

# Both are overridable, because this gets run from two places: a WSL shell,
# where the tree is under /mnt/d and the toolchain is on PATH, and Git Bash
# on Windows, where a drive-letter path suits both the shell and the
# compiler.  Nothing else in here cares which.
R=${R:-/mnt/d/Develop/GEMbedded}
CC=${CC:-arm-none-eabi-gcc}

HW=$R/GEM/highwire
NOTES=$R/GEM/highwire-notes
LIBS=$R/GEM/highwire-libs
CORE=$R/GEMduino/sketchbook/hardware/gembedded/rp2350/cores/gem
CMINI=$R/pTOS/lib/libcmini
OBJ=$NOTES/obj
OUT=$NOTES/out

CPU="-mcpu=cortex-m33 -mthumb -mfloat-abi=softfp -mfpu=fpv5-sp-d16"

# -fno-short-enums: ARM defaults to sizing an enum by its values, m68k
# does not, and the linker refuses to mix objects that disagree.  The
# four libraries are built with the same flag for the same reason.
#
# The libjpeg quartet has to be repeated here, not just in the library:
# jmorecfg.h only skips its own "typedef enum { FALSE, TRUE } boolean"
# when FALSE or TRUE is already a macro, and by the time img-dcdr.c
# reaches jpeglib.h its own img_gif.c has undef'd both.
CFLAGS="$CPU -O2 -funsigned-char -fno-short-enums -mword-relocations \
        -ffunction-sections -fdata-sections \
        -DUSE_STIK -DUSE_STNG -DUSE_INET -DLIBPNG -DLIBGIF -DLIBJPG \
        -DHAVE_BOOLEAN -Dboolean=int -DFALSE=0 -DTRUE=1 \
        -I$NOTES/gem-compat \
        -I$CORE \
        -I$CMINI/include \
        -I$CMINI/include/mint \
        -I$LIBS/include \
        -I$HW/src \
        -I$R/pTOS/include \
        -iquote $R/pTOS/include"

# -Wl,-Map: the program is linked at address 0, so a fault report's pc,
# less the address it was loaded at, is an offset into this map.  Without
# it a crash in the browser is a bare number with nothing to say.  It
# costs nothing in the image -- the map is a separate file.
LDFLAGS="$CPU -nostartfiles -nostdlib \
         -Wl,-q -Wl,-Ttext=0 -Wl,-e_start -Wl,--gc-sections \
         -Wl,-Map=$NOTES/out/highwire.map"

CFILES="Logging schedule mime inet http cache Location cookie DomBox \
        O_Struct fontbase W_Struct raster image img-dcdr Paragrph list \
        Form Table Frame color encoding scanner parser p_about p_dir \
        render Containr Loader Redraws clipbrd Window formwind fntsetup \
        dl_mngr Widget hwWind av_prot dragdrop olga config bookmark \
        Variable Nice_VDI romvdi keyinput Mouse_R AEI HighWire strtools"

mkdir -p "$OBJ" "$OUT"
rm -f "$OBJ"/*.o

fail=0

echo "=== HighWire ==="
for f in $CFILES; do
    if ! $CC $CFLAGS -c "$HW/src/$f.c" -o "$OBJ/$f.o" 2>"$OBJ/$f.err"; then
        echo "  FAILED  $f.c"
        sed -n '1,12p' "$OBJ/$f.err"
        fail=1
    fi
done

echo "=== the parts that are ours ==="
for src in "$NOTES/gem-compat/gemx.c" "$NOTES/net/noovl.c" "$CORE/gem.c" \
           "$CORE/mathglue.c" "$NOTES/hwstart.c"; do
    b=$(basename "$src" .c)
    if ! $CC $CFLAGS -c "$src" -o "$OBJ/$b.o" 2>"$OBJ/$b.err"; then
        echo "  FAILED  $b.c"
        sed -n '1,12p' "$OBJ/$b.err"
        fail=1
    fi
done

[ $fail -eq 0 ] || { echo "not linking: something did not compile"; exit 1; }

# libcmini's application start-up, which shrinks the memory, sets the
# stack from _stksize (hwstart.c) and builds argv before calling main().
# Not the board package's accstart.S -- see hwstart.c for why.
echo "=== link ==="
$CC $LDFLAGS "$R/pTOS/obj/libcmini-build/objs/crt0.o" "$OBJ"/*.o \
    -L"$LIBS/lib" -lpng -ljpeg -lgif -lz \
    -L"$R/pTOS/obj/libcmini-build" -lcmini \
    -lm -lgcc \
    -o "$OUT/HIGHWIRE.PRG"

# HighWire 0.4.6 moved its resources into rsc/.  Both places are tried so
# this still builds an older checkout.
if   [ -f "$HW/rsc/highwire.rsc" ]; then RSC="$HW/rsc/highwire.rsc"
elif [ -f "$HW/highwire.rsc" ];     then RSC="$HW/highwire.rsc"
else echo "no highwire.rsc under $HW" >&2; exit 1
fi
cp -f "$RSC" "$OUT/HIGHWIRE.RSC"

echo
echo "  HIGHWIRE.PRG  $(stat -c%s "$OUT/HIGHWIRE.PRG") Bytes"
arm-none-eabi-size "$OUT/HIGHWIRE.PRG" 2>/dev/null || true
