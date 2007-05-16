#include "pixman.h"

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

#define DEBUG 1

#if DEBUG

#define return_if_fail(expr)							\
	do									\
	{									\
	    if (!(expr))							\
	    {									\
		fprintf(stderr, "In %s: %s failed\n", __FUNCTION__, #expr);	\
		return;								\
	    }									\
	}									\
	while (0)

#define return_val_if_fail(expr, retval) 					\
	do									\
	{									\
	    if (!(expr))							\
	    {									\
		fprintf(stderr, "In %s: %s failed\n", __FUNCTION__, #expr);	\
		return (retval);						\
	    }									\
	}									\
	while (0)

#else

#define return_if_fail(expr)
#define return_val_if_fail(expr, retval)

#endif

typedef struct image_common image_common_t;
typedef struct source_image source_image_t;
typedef struct solid_fill solid_fill_t;
typedef struct gradient gradient_t;
typedef struct linear_gradient linear_gradient_t;
typedef struct horizontal_gradient horizontal_gradient_t;
typedef struct vertical_gradient vertical_gradient_t;
typedef struct conical_gradient conical_gradient_t;
typedef struct radial_gradient radial_gradient_t;
typedef struct bits_image bits_image_t;
typedef struct circle circle_t;
typedef struct point point_t;

/* FIXME - the types and structures below should be give proper names
 */

#define FASTCALL
typedef FASTCALL void (*CombineMaskU) (uint32_t *src, const uint32_t *mask, int width);
typedef FASTCALL void (*CombineFuncU) (uint32_t *dest, const uint32_t *src, int width);
typedef FASTCALL void (*CombineFuncC) (uint32_t *dest, uint32_t *src, uint32_t *mask, int width);

typedef struct _FbComposeFunctions {
    CombineFuncU *combineU;
    CombineFuncC *combineC;
    CombineMaskU combineMaskU;
} FbComposeFunctions;

typedef struct _FbComposeData {
    uint8_t	 op;
    pixman_image_t	*src;
    pixman_image_t	*mask;
    pixman_image_t	*dest;
    int16_t	 xSrc;
    int16_t	 ySrc;
    int16_t	 xMask;
    int16_t	 yMask;
    int16_t	 xDest;
    int16_t	 yDest;
    uint16_t	 width;
    uint16_t	 height;
} FbComposeData;

#define fbGetDrawable 


/* end */

typedef enum
{
    BITS,
    LINEAR,
    CONICAL,
    RADIAL,
    SOLID
} image_type_t;

#define IS_SOURCE_IMAGE(img)     (((image_common_t *)img)->type > BITS)

typedef enum
{
    SOURCE_IMAGE_CLASS_UNKNOWN,
    SOURCE_IMAGE_CLASS_HORIZONTAL,
    SOURCE_IMAGE_CLASS_VERTICAL
} source_pict_class_t;

struct point
{
    int16_t x, y;
};

struct image_common
{
    image_type_t		type;
    int32_t			ref_count;
    pixman_region16_t		clip_region;
    pixman_transform_t	       *transform;
    pixman_repeat_t		repeat;
    pixman_filter_t		filter;
    pixman_fixed_t	       *filter_params;
    int				n_filter_params;
    bits_image_t	       *alpha_map;
    point_t			alpha_origin;
    pixman_bool_t		component_alpha;
    pixman_read_memory_func_t	read_func;
    pixman_write_memory_func_t	write_func;
};

struct source_image
{
    image_common_t	common;
    unsigned int	class;		/* FIXME: should be an enum */
};

struct solid_fill
{
    source_image_t	common;
    uint32_t		color;		/* FIXME: shouldn't this be a pixman_color_t? */
};
    
struct gradient
{
    source_image_t		common;
    int				n_stops;
    pixman_gradient_stop_t *	stops;
    int				stop_range;
    uint32_t *			color_table;
    int				color_table_size;
};

struct linear_gradient
{
    gradient_t			common;
    pixman_point_fixed_t	p1;
    pixman_point_fixed_t	p2;
};

struct circle
{
    pixman_fixed_t x;
    pixman_fixed_t y;
    pixman_fixed_t radius;
};

struct radial_gradient
{
    gradient_t	common;

    circle_t	c1;
    circle_t	c2;
    double	cdx;
    double	cdy;
    double	dr;
    double	A;
};

struct conical_gradient
{
    gradient_t			common;
    pixman_point_fixed_t	center;
    pixman_fixed_t		angle;
}; 

struct bits_image
{
    image_common_t		common;
    pixman_format_code_t	format;
    pixman_indexed_t	       *indexed;
    int				width;
    int				height;
    uint32_t *			bits;
    int				rowstride; /* in bytes */
};

union pixman_image
{
    image_type_t		type;
    image_common_t		common;
    bits_image_t		bits;
    linear_gradient_t		linear;
    conical_gradient_t		conical;
    radial_gradient_t		radial;
    solid_fill_t		solid;
};

void pixmanCompositeRect (const FbComposeData *data,
			  uint32_t *scanline_buffer);

#if 0
typedef struct _Picture {
    DrawablePtr	    pDrawable;
    PictFormatPtr   pFormat;
    PictFormatShort format;	    /* PICT_FORMAT */
    int		    refcnt;
    CARD32	    id;
    PicturePtr	    pNext;	    /* chain on same drawable */

    unsigned int    repeat : 1;
    unsigned int    graphicsExposures : 1;
    unsigned int    subWindowMode : 1;
    unsigned int    polyEdge : 1;
    unsigned int    polyMode : 1;
    unsigned int    freeCompClip : 1;
    unsigned int    clientClipType : 2;
    unsigned int    componentAlpha : 1;
    unsigned int    repeatType : 2;
    unsigned int    unused : 21;

    PicturePtr	    alphaMap;
    DDXPointRec	    alphaOrigin;

    DDXPointRec	    clipOrigin;
    pointer	    clientClip;

    Atom	    dither;

    unsigned long   stateChanges;
    unsigned long   serialNumber;

    RegionPtr	    pCompositeClip;

    DevUnion	    *devPrivates;

    PictTransform   *transform;

    int		    filter;
    xFixed	    *filter_params;
    int		    filter_nparams;
    SourcePictPtr   pSourcePict;
} PictureRec;
#endif

