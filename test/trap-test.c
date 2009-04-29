#include <stdio.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <string.h>
#include "pixman.h"

GdkPixbuf *
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
	    guint r, g, b, a;
	    char *pb = (char *)p_bits;

	    pb += h * p_stride + w * 4;

	    r = (argb & 0x00ff0000) >> 16;
	    g = (argb & 0x0000ff00) >> 8;
	    b = (argb & 0x000000ff) >> 0;
	    a = (argb & 0xff000000) >> 24;

	    if (a)
	    {
		r = (r * 255) / a;
		g = (g * 255) / a;
		b = (b * 255) / a;
	    }

	    pb[0] = r;
	    pb[1] = g;
	    pb[2] = b;
	    pb[3] = a;
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
    g_signal_connect (window, "delete_event", G_CALLBACK (gtk_main_quit), NULL);
    
    gtk_widget_show (window);
    
    gtk_main ();
}

int
main (int argc, char **argv)
{
#define WIDTH 200
#define HEIGHT 200

    pixman_image_t *src_img;
    pixman_image_t *mask_img;
    pixman_image_t *dest_img;
    pixman_trap_t trap;
    pixman_color_t white = { 0x0000, 0xffff, 0x0000, 0xffff };
    uint32_t *bits = malloc (WIDTH * HEIGHT * 4);
    uint32_t *mbits = malloc (WIDTH * HEIGHT);

    memset (mbits, 0, WIDTH * HEIGHT);
    memset (bits, 0xff, WIDTH * HEIGHT * 4);
    
    trap.top.l = pixman_int_to_fixed (50) + 0x8000;
    trap.top.r = pixman_int_to_fixed (150) + 0x8000;
    trap.top.y = pixman_int_to_fixed (30);

    trap.bot.l = pixman_int_to_fixed (50) + 0x8000;
    trap.bot.r = pixman_int_to_fixed (150) + 0x8000;
    trap.bot.y = pixman_int_to_fixed (150);

    mask_img = pixman_image_create_bits (PIXMAN_a8, WIDTH, HEIGHT, mbits, WIDTH);
    src_img = pixman_image_create_solid_fill (&white);
    dest_img = pixman_image_create_bits (PIXMAN_a8r8g8b8, WIDTH, HEIGHT, bits, WIDTH * 4);
    
    pixman_add_traps (mask_img, 0, 0, 1, &trap);

    pixman_image_composite (PIXMAN_OP_OVER,
			    src_img, mask_img, dest_img,
			    0, 0, 0, 0, 0, 0, WIDTH, HEIGHT);
    
    gtk_init (&argc, &argv);
    
    show_window (bits, WIDTH, HEIGHT, WIDTH);
    
    pixman_image_unref (src_img);
    pixman_image_unref (dest_img);
    free (bits);
    
    return 0;
}
