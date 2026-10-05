/*
 * gemset.c - GEMSET.PRG: read and change what the machine was told it is
 *
 * Until now the kept settings could be read by the kernel and written by
 * nobody, so every fact in them -- which pin carries the PSRAM chip
 * select, what the machine calls itself, how big its screen is -- was
 * something you rebuilt the image to change.  This is the smallest thing
 * that closes that circle, and it is deliberately a program rather than
 * a menu in the firmware: the knowledge of what a setting means belongs
 * where it can be fixed without reflashing.
 *
 *     GEMSET            show what is in force
 *     GEMSET -w         write it back unchanged (proves the path)
 *     GEMSET -n NAME    give the machine a name
 *     GEMSET -p PIN     the GPIO carrying the PSRAM chip select
 *
 * A changed record takes effect at the NEXT start, not now: the pin is
 * read while the PSRAM is being set up, long before a program runs.  So
 * the honest order is write, restart, look -- and if the machine comes
 * back without its Alt-RAM, the pin was wrong and the fix is to write
 * the old value again.  Nothing here can brick a board: a record that
 * makes no sense costs the Alt-RAM, never the start.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <osbind.h>
#include <mint/mintbind.h>      /* Ssystem() */

#include "rp2350_settings.h"

static void show(const struct rp2350_settings *s, long got)
{
    char name[sizeof(s->board) + 1];

    memcpy(name, s->board, sizeof(s->board));
    name[sizeof(s->board)] = '\0';

    printf("record      %ld of %ld bytes this program knows\r\n",
           got, (long)sizeof(*s));
    printf("version     %u\r\n", (unsigned)s->version);
    printf("board       \"%s\"\r\n", name);
    printf("psram_cs    GPIO %u\r\n", (unsigned)s->psram_cs);
    printf("screen      %ux%u, %u bpp\r\n",
           (unsigned)s->scr_w, (unsigned)s->scr_h, (unsigned)s->bpp);
    printf("refresh     %u Hz%s\r\n", (unsigned)s->refresh,
           s->refresh ? "" : "  (as fast as the path allows)");
    printf("vram        %s\r\n",
           s->vram_where == VRAM_STRAM ? "ST-RAM" :
           s->vram_where == VRAM_PSRAM ? "PSRAM" : "AUTO");
    printf("profile_sum 0x%08lx%s\r\n", (unsigned long)s->profile_sum,
           s->profile_sum ? "" : "  (not known)");
}

int main(int argc, char **argv)
{
    struct rp2350_settings s;
    long got, ksize, rc;
    int write = 0, i;

    ksize = Ssystem(S_SETTINGS_GET, 0L, -1L);
    if (ksize < 0)
    {
        printf("This machine does not keep settings (Ssystem gave %ld).\r\n",
               ksize);
        printf("Either it is not an RP2350 or CONF_WITH_RP2350_NVRAM is off.\r\n");
        goto done;
    }
    if (ksize != (long)sizeof(s))
        printf("note: kernel record is %ld bytes, this program knows %ld.\r\n"
               "      Only the part both agree on is read or written.\r\n",
               ksize, (long)sizeof(s));

    memset(&s, 0, sizeof(s));
    got = Ssystem(S_SETTINGS_GET, (long)&s, (long)sizeof(s));
    if (got < 0)
    {
        printf("reading failed: %ld\r\n", got);
        goto done;
    }

    for (i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-w"))
            write = 1;
        else if (!strcmp(argv[i], "-n") && i + 1 < argc)
        {
            /*
             * memcpy and not strncpy: the field holds up to 24
             * characters and carries no terminator when it is full,
             * which is the record's contract, not an oversight.  Reading
             * it back goes through rp2350_nvram_board().
             */
            size_t n = strlen(argv[++i]);

            if (n > sizeof(s.board))
                n = sizeof(s.board);
            memset(s.board, 0, sizeof(s.board));
            memcpy(s.board, argv[i], n);
            write = 1;
        }
        else if (!strcmp(argv[i], "-p") && i + 1 < argc)
        {
            s.psram_cs = (unsigned char)atoi(argv[++i]);
            write = 1;
        }
        else
        {
            printf("usage: GEMSET [-w] [-n NAME] [-p PIN]\r\n");
            goto done;
        }
    }

    show(&s, got);

    if (write)
    {
        printf("\r\nwriting...\r\n");
        rc = Ssystem(S_SETTINGS_PUT, (long)&s, (long)sizeof(s));
        if (rc == -15L)             /* ERANGE */
            printf("refused: this machine scans out of the PSRAM, and a\r\n"
                   "flash write takes both QSPI chip selects for 23 ms --\r\n"
                   "the picture would not come back straight.  Stop the\r\n"
                   "panel first.\r\n");
        else if (rc < 0)
            printf("writing failed: %ld\r\n", rc);
        else
            printf("kept %ld bytes.  It takes effect at the next start:\r\n"
                   "the pin is read while the PSRAM is being set up.\r\n", rc);
    }

done:
    printf("\r\n-- any key --");
    Cconin();
    return 0;
}
