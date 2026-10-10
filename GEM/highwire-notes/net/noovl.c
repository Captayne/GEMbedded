/*
 * noovl.c - there are no overlays on this machine
 *
 * Copyright (C) 2026 Andreas Keibel
 *
 * HighWire can load its network as an overlay: a separate program that
 * ovl_sys.c starts with Pexec and asks for a function table. That is how
 * it chooses between STinG, MiNTnet, MagiCNet and the rest at run time.
 *
 * Here the choice is made when it is built. The transport is the one the
 * machine publishes under the cookie 'STiK' (pTOS's include/stik.h), and
 * HighWire's own src/inet.c talks to it directly -- the same code it
 * uses on an Atari with STinG, unchanged.
 *
 * So nothing is ever loaded, and these two are what is left of
 * ovl_sys.c: the bookkeeping the About box calls to list what is loaded.
 * They answer "nothing", which is true.
 */

#include <stddef.h>

#include "hw-types.h"
#include "ovl_sys.h"

void kill_ovl(void *this_N_all)
{
    (void)this_N_all;
}

size_t module_info(MODULINF *p_info)
{
    if (p_info)
        *p_info = NULL;
    return 0;
}
