#include <stdlib.h>
#include "pixman.h"

uint32_t
compute_crc32 (uint32_t    in_crc32,
	       const void *buf,
	       size_t      buf_len);
