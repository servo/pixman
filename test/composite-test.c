#include <stdlib.h>
#include <stdio.h>
#include "pixman.h"

int
main ()
{
    uint32_t *src = malloc (10 * 10 * 4);
    uint32_t *dest = malloc (10 * 10 * 4);
    pixman_image_t *src_img;
    pixman_image_t *dest_img;
    uint32_t real;

    int i;

    for (i = 0; i < 10 * 10; ++i)
	src[i] = 0x7f7f0000; /* red */

    for (i = 0; i < 10 * 10; ++i)
	dest[i] = 0x7f0000ff; /* blue */
    
    src_img = pixman_image_create_bits (PIXMAN_a8r8g8b8,
					10, 10,
					src,
					10 * 4, NULL);
    
    dest_img = pixman_image_create_bits (PIXMAN_a8r8g8b8,
				       10, 10,
				       dest,
				       10 * 4, NULL);

    pixman_image_composite (PIXMAN_OP_OVER, src_img, NULL, dest_img,
			    0, 0, 0, 0, 0, 0, 10, 10);

    return 0;
}
