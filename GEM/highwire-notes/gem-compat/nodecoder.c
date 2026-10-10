/*
 * nodecoder.c - TEMPORARY, to prove the link is otherwise complete
 *
 * img-dcdr.c is the only source file that does not build yet, because it
 * needs gif_lib.h, png.h and jpeglib.h.  Everything else links, and this
 * one function stands in for it so that the whole can be put together
 * and weighed.
 *
 * DELETE THIS once the image libraries are built.  A browser that cannot
 * decode an image is not the goal.
 */
#include <gem.h>

typedef struct s_img_info * IMGINFO;

IMGINFO get_decoder(const char *file)
{
    (void)file;
    return 0;           /* no decoder for anything, yet */
}
