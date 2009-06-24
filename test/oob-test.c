#include <stdio.h>
#include <stdlib.h>
#include "pixman.h"
#include "utils.h"

int
main (int argc, char **argv)
{
#define DWIDTH 3
#define DHEIGHT 6
#define DSTRIDE 16

#define SWIDTH 5
#define SHEIGHT 7
#define SSTRIDE 20
    
    uint32_t *src = malloc (SHEIGHT * SSTRIDE);
    uint32_t *dest = malloc (DHEIGHT * DSTRIDE);
    pixman_image_t *simg, *dimg;

    int i;

    for (i = 0; i < (SHEIGHT * SSTRIDE) / 4; ++i)
	src[i] = 0x7f007f00;

    for (i = 0; i < (DHEIGHT * DSTRIDE) / 4; ++i)
	dest[i] = 0;

    simg = pixman_image_create_bits (PIXMAN_a8r8g8b8, SWIDTH, SHEIGHT, src, SSTRIDE);
    dimg = pixman_image_create_bits (PIXMAN_x8r8g8b8, DWIDTH, DHEIGHT, dest, DSTRIDE);

    pixman_image_composite (PIXMAN_OP_SRC, simg, NULL, dimg,
			    1, 8, 0, 0, 1, -1, 1, 8);

    return 0;
}
