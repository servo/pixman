typedef union  image image_t;
typedef struct source_image source_image_t;
typedef struct solid_fill solid_fill_t;
typedef struct gradient gradient_t;
typedef struct linear_gradient linear_gradient_t;
typedef struct horizontal_gradient horizontal_gradient_t;
typedef struct vertical_gradient vertical_gradient_t;
typedef struct conical_gradient conical_gradient_t;
typedef struct radial_gradient radial_gradient_t;
typedef struct bits_image bits_image_t;
typedef struct gradient_stop gradient_stop_t;
typedef struct circle circle_t;

typedef enum
{
    BITS,
    LINEAR,
    CONICAL,
    RADIAL,
    SOLID
} image_type_t;

struct gradient_stop
{
    pixman_fixed_t x;
    pixman_color_t color;
};

typedef enum
{
    SOURCE_IMAGE_CLASS_UNKNOWN,
    SOURCE_IMAGE_CLASS_HORIZONTAL,
    SOURCE_IMAGE_CLASS_VERTICAL
} source_pict_class_t;

struct source_image
{
    image_type_t	type;
    unsigned int	class;		/* FIXME: should be an enum */
};

struct solid_fill
{
    source_image_t	common;
    uint32_t		color;		/* FIXME: shouldn't this be a pixman_color_t? */
};
    
struct gradient
{
    source_image_t	common;
    int			n_stops;
    gradient_stop_t *	stops;
    int			stop_range;
    uint32_t *		color_table;
    int			color_table_size;
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
    image_type_t		type;
    pixman_format_code_t	format;
    int				width;
    int				height;
    uint8_t *			bits;
    int				rowstride; /* in bytes */
};

union image
{
    image_type_t		type;
    bits_image_t		bits;
    linear_gradient_t		linear;
    conical_gradient_t		conical;
    radial_gradient_t		radial;
    solid_fill_t		solid;
};




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

