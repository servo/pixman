#include <stdlib.h>
#include <stdio.h>
#include "pixman.h"

#include <gtk/gtk.h>

static GdkPixbuf *
pixbuf_from_argb32 (uint32_t *bits,
		    int width,
		    int height,
		    int stride)
{
    GdkPixbuf *pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE,
					8, width, height);
    int p_stride = gdk_pixbuf_get_rowstride (pixbuf);
    guint32 *p_bits = (guint32 *)gdk_pixbuf_get_pixels (pixbuf);
    int w, h;

    for (h = 0; h < height; ++h)
    {
	for (w = 0; w < width; ++w)
	{
	    uint32_t argb = bits[h * stride + w];
	    guint32 rgba;

	    rgba = (argb << 8) | (argb >> 24);

	    p_bits[h * (p_stride / 4) + w] = rgba;
	}
    }

    return pixbuf;
}

static gboolean
on_expose (GtkWidget *widget, GdkEventExpose *expose, gpointer data)
{
    GdkPixbuf *pixbuf = data;
    
    gdk_draw_pixbuf (widget->window, NULL,
		     pixbuf, 0, 0, 0, 0,
		     gdk_pixbuf_get_width (pixbuf),
		     gdk_pixbuf_get_height (pixbuf),
		     GDK_RGB_DITHER_NONE,
		     0, 0);

    return TRUE;
}

static void
show_window (uint32_t *bits, int w, int h, int stride)
{
    GdkPixbuf *pixbuf;

    GtkWidget *window = gtk_window_new (GTK_WINDOW_TOPLEVEL);

    pixbuf = pixbuf_from_argb32 (bits, w, h, stride);

    g_signal_connect (window, "expose_event", G_CALLBACK (on_expose), pixbuf);
    
    gtk_widget_show (window);

    gtk_main ();
}

int
main (int argc, char **argv)
{
    uint32_t *src = malloc (10 * 10 * 4);
    uint32_t *dest = malloc (10 * 10 * 4);
    pixman_image_t *src_img;
    pixman_image_t *dest_img;
    int i, j;
    gtk_init (&argc, &argv);

    for (i = 0; i < 10 * 10; ++i)
	src[i] = 0x7f7f0000; /* red */

    for (i = 0; i < 10 * 10; ++i)
	dest[i] = 0x7f0000ff; /* blue */
    
    src_img = pixman_image_create_bits (PIXMAN_a8r8g8b8,
					10, 10,
					src,
					10 * 4);
    
    dest_img = pixman_image_create_bits (PIXMAN_a8r8g8b8,
					 10, 10,
					 dest,
					 10 * 4);

    pixman_image_composite_rect (PIXMAN_OP_OVER, src_img, NULL, dest_img,
				 0, 0, 0, 0, 0, 0, 10, 10);

    for (i = 0; i < 10; ++i)
    {
	for (j = 0; j < 10; ++j)
	    g_print ("%x, ", dest[i * 10 + j]);
	g_print ("\n");
    }
    
    show_window (dest, 10, 10, 10);
    
    pixman_image_unref (src_img);
    pixman_image_unref (dest_img);
    free (src);
    free (dest);


    
    return 0;
}
