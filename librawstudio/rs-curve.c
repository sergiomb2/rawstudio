/*****************************************************************************
 * Curve widget
 * 
 * Copyright (C) 2007 Edouard Gomez <ed.gomez@free.fr>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 ****************************************************************************/

#include <rawstudio.h>
#include <math.h>
#include <libxml/encoding.h>
#include <libxml/xmlwriter.h>
#include <string.h> /* memset() */

struct _RSCurveWidget
{
	GtkDrawingArea parent;
	RSSpline *spline;
	gint active_knot;
	gfloat *array;
	guint array_length;
	gulong size_signal;

	/* For drawing the histogram */
	guint histogram_data[256];
	RSFilter *input;
	GdkPixbuf *bg_buffer;
	RSColorSpace *display_color_space;
	gfloat rgb_values[3];

	gint last_width[2];
	PangoLayout* help_layout;
	gboolean histogram_uptodate;
	gulong delay_update;
};

struct _RSCurveWidgetClass
{
	GtkDrawingAreaClass parent_class;
};

static void rs_curve_widget_class_init(RSCurveWidgetClass *klass);
static void rs_curve_widget_init(RSCurveWidget *curve);
static void rs_curve_widget_destroy(GtkWidget *widget);
static gboolean rs_curve_size_allocate_helper(RSCurveWidget *curve);
static void rs_curve_size_allocate(GtkWidget *widget, GtkAllocation *allocation, gpointer user_data);
static void rs_curve_changed(RSCurveWidget *curve);
static gboolean rs_curve_widget_draw(RSCurveWidget *curve, cairo_t *cr);
static gboolean rs_curve_widget_button_press(GtkWidget *widget, GdkEventButton *event);
static gboolean rs_curve_widget_button_release(GtkWidget *widget, GdkEventButton *event);
static gboolean rs_curve_widget_motion_notify(GtkWidget *widget, GdkEventMotion *event);
static void rs_curve_widget_size_allocate(GtkWidget *widget, GtkAllocation *allocation, gpointer user_data);

enum {
  CHANGED_SIGNAL,
  RIGHTCLICK_SIGNAL,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

/* Define the boiler plate stuff using the predefined macro */
G_DEFINE_TYPE (RSCurveWidget, rs_curve_widget, GTK_TYPE_DRAWING_AREA);

/**
 * Class initializer
 */
static void
rs_curve_widget_class_init(RSCurveWidgetClass *klass)
{
	GtkWidgetClass *widget_class;
	widget_class = GTK_WIDGET_CLASS(klass);

	signals[CHANGED_SIGNAL] = g_signal_new ("changed",
		G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
		0, /* Is this right? */
		NULL, 
		NULL,                
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);
	signals[RIGHTCLICK_SIGNAL] = g_signal_new ("right-click",
		G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
		0, /* Is this right? */
		NULL,
		NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);

	widget_class->destroy = rs_curve_widget_destroy;
	widget_class->draw = rs_curve_widget_draw;
	widget_class->button_press_event = rs_curve_widget_button_press;
	widget_class->button_release_event = rs_curve_widget_button_release;
	widget_class->motion_notify_event = rs_curve_widget_motion_notify;
}

/**
 * Instance initialization
 */
static void
rs_curve_widget_init(RSCurveWidget *curve)
{
	curve->array = NULL;
	curve->array_length = 0;
	curve->spline = rs_spline_new(NULL, 0, NATURAL);
	curve->bg_buffer = NULL;

	/* Let us know about pointer movements */
	gtk_widget_set_events(GTK_WIDGET(curve), 0
		| GDK_BUTTON_PRESS_MASK
		| GDK_BUTTON_RELEASE_MASK
		| GDK_POINTER_MOTION_MASK);

	curve->size_signal = g_signal_connect(curve, "size-allocate", G_CALLBACK(rs_curve_size_allocate), NULL);
	g_signal_connect(G_OBJECT(curve), "size-allocate", G_CALLBACK(rs_curve_widget_size_allocate), NULL);

	/* Initialize help */
	curve->help_layout = gtk_widget_create_pango_layout(GTK_WIDGET(curve), "Mouse Controls:\nLeft: New point\nShift+Left: Delete point\nRight: Load/Save/Reset Curve");
	PangoFontDescription *font_desc =   pango_font_description_from_string("sans light 7");
	pango_layout_set_font_description(curve->help_layout, font_desc);
	pango_layout_context_changed(curve->help_layout);
	curve->rgb_values[0] = -1;
	curve->rgb_values[1] = -1;
	curve->rgb_values[2] = -1;
	curve->delay_update = 0;
}

/**
 * Instance Constructor
 */
GtkWidget *
rs_curve_widget_new(void)
{
	return g_object_new (RS_CURVE_TYPE_WIDGET, NULL);
}

float
rs_curve_widget_get_marker(RSCurveWidget *curve)
{
	g_return_val_if_fail (curve != NULL, -1.0f);
	g_return_val_if_fail (RS_IS_CURVE_WIDGET(curve), -1.0f);

	gfloat position = MAX(MAX(curve->rgb_values[0], curve->rgb_values[1]),curve->rgb_values[2]);

	/* Clamp values above 1.0 */
	if (position > 1.0)
		position = 1.0;

	if (curve->display_color_space && position >= 0.0f) 
	{
		const RS1dFunction *func = rs_color_space_get_gamma_function(curve->display_color_space);
		position = rs_1d_function_evaluate_inverse(func, position);
		position = sqrtf(position);
	}
	else
		position = -1.0;

	return position;
}

/**
 * Sets sample array for a RSCurveWidget, this array will be updates whenever the curve changes
 * @param curve A RSCurveWidget
 * @param array An array of gfloats to be updated or NULL to unset
 * @params array_length: Length of array or 0 to unset
 */
void
rs_curve_widget_set_array(RSCurveWidget *curve, gfloat *array, guint array_length)
{
	g_return_if_fail (curve != NULL);
	g_return_if_fail (RS_IS_CURVE_WIDGET(curve));

	if (array && array_length)
	{
		curve->array = array;
		curve->array_length = array_length;
	}
	else
	{
		curve->array = NULL;
		curve->array_length = 0;
	}
}
#define LUM_PRECISION 15
#define LUM_FIXED(a) ((guint)((a)*(1<<LUM_PRECISION)))
#define RLUMF LUM_FIXED(0.212671f)
#define GLUMF LUM_FIXED(0.715160f)
#define BLUMF LUM_FIXED(0.072169f)
#define HALFF LUM_FIXED(0.5f)

void
rs_curve_set_histogram_data(RSCurveWidget *curve, const gint *input)
{
	g_return_if_fail (RS_IS_CURVE_WIDGET(curve));

	gint i;
	gdk_threads_enter();
	for (i = 0; i < 256; i++)
		curve->histogram_data[i] = input[i];

	if (curve->bg_buffer)
		g_object_unref(curve->bg_buffer);
	curve->bg_buffer = NULL;
	curve->histogram_uptodate = TRUE;
	gtk_widget_queue_draw(GTK_WIDGET(curve));
	gdk_threads_leave();
}

static void filter_changed(RSFilter *filter, RSFilterChangedMask mask, RSCurveWidget *curve)
{
	if (curve->bg_buffer)
		g_object_unref(curve->bg_buffer);
	curve->bg_buffer = NULL;
	curve->histogram_uptodate = FALSE;
}

/**
 * Set an image to base the histogram of
 * @param curve A RSCurveWidget
 * @param image An image
 * @param display_color_space Colorspace to use to transform the input or NULL.
 */
void
rs_curve_set_input(RSCurveWidget *curve, RSFilter* input, RSColorSpace *display_color_space)
{
	g_return_if_fail (RS_IS_CURVE_WIDGET(curve));
	g_return_if_fail (RS_IS_FILTER(input));
	g_return_if_fail (RS_IS_COLOR_SPACE(display_color_space) || display_color_space == NULL);

	if (input != curve->input)
	{
		g_signal_connect(input, "changed", G_CALLBACK(filter_changed), curve);
	}

	curve->input = input;
	curve->display_color_space = display_color_space;
}

/**
 * Draw a histogram in the background of the widget
 * @param curve A RSCurveWidget
 * @param image A image to sample from
 * @param setting Settings to use, curve and saturation will be ignored
 */
void
rs_curve_draw_histogram(RSCurveWidget *curve)
{
	g_return_if_fail(RS_IS_CURVE_WIDGET(curve));

	if (curve->input && !curve->histogram_uptodate)
	{
		RSFilterRequest *request = rs_filter_request_new();
		rs_filter_request_set_quick(RS_FILTER_REQUEST(request), TRUE);
		rs_filter_param_set_object(RS_FILTER_PARAM(request), "colorspace", curve->display_color_space);
		rs_filter_set_recursive(RS_FILTER(curve->input), "read-out-curve", curve, NULL);
		gdk_threads_leave();
		RSFilterResponse *response = rs_filter_get_image8(curve->input, request);
		gdk_threads_enter();
		g_object_unref(request);
		g_object_unref(response);
	}
	gtk_widget_queue_draw(GTK_WIDGET(curve));
}

void 
rs_curve_set_highlight(RSCurveWidget *curve, const guchar* rgb_values)
{
	g_return_if_fail (RS_IS_CURVE_WIDGET(curve));
	if (rgb_values)
	{
		curve->rgb_values[0] = (float)rgb_values[0]/255.0f;
		curve->rgb_values[1] = (float)rgb_values[1]/255.0f;
		curve->rgb_values[2] = (float)rgb_values[2]/255.0f;
	} 
	else
	{
		curve->rgb_values[0] = -1;
		curve->rgb_values[1] = -1;
		curve->rgb_values[2] = -1;
	}
	gtk_widget_queue_draw(GTK_WIDGET(curve));
}

/**
 * Instance destruction
 */
static void
rs_curve_widget_destroy(GtkWidget *widget)
{
	RSCurveWidget *curve = NULL;

	g_return_if_fail (widget != NULL);
	g_return_if_fail (RS_IS_CURVE_WIDGET(widget));

	curve = RS_CURVE_WIDGET(widget);

	if (curve->spline != NULL) {
		g_object_unref(curve->spline);
	}
	g_object_unref(curve->help_layout);
	if (curve->input)
		rs_filter_set_recursive(RS_FILTER(curve->input), "read-out-curve", NULL, NULL);
	if (curve->delay_update > 0)
		g_source_remove(curve->delay_update);

}

/**
 * Add a knot to a curve widget
 * @param widget A RSCurveWidget
 * @param x X coordinate
 * @param y Y coordinate
 */
void
rs_curve_widget_add_knot(RSCurveWidget *curve, gfloat x, gfloat y)
{
	g_return_if_fail (curve != NULL);
	g_return_if_fail (RS_IS_CURVE_WIDGET(curve));

	/* Reset active knot */
	curve->active_knot = -1;

	/* Add the knot */
	rs_spline_add(curve->spline, x, y);

	/* Redraw the widget */
	gtk_widget_queue_draw(GTK_WIDGET(curve));

	/* Propagate the change */
	rs_curve_changed(curve);
}

/**
 * Move a knot of a RSCurveWidget
 * @param curve A RSCurveWidget
 * @param knot Knot to move or -1 for last
 * @param x X coordinate
 * @param y Y coordinate
 */
void
rs_curve_widget_move_knot(RSCurveWidget *curve, gint knot, gfloat x, gfloat y)
{
	g_return_if_fail (curve != NULL);
	g_return_if_fail (RS_IS_CURVE_WIDGET(curve));

	/* Do we want the last knot? */
	if (knot < 0)
		knot = rs_spline_length(curve->spline)-1;

	/* Check limits */
	if (knot >= rs_spline_length(curve->spline))
		knot = rs_spline_length(curve->spline)-1;

	/* Move the knot */
	rs_spline_move(curve->spline, knot, x, y);

	/* Propagate the change */
	rs_curve_changed(curve);

	/* Redraw everything */
	gtk_widget_queue_draw(GTK_WIDGET(curve));

	return;
}

/**
 * Get samples from curve
 * @param widget A RSCurveWidget
 * @param samples Pointer to output array or NULL
 * @param nbsamples number of samples
 * @return An array of floats, should be freed
 */
gfloat *
rs_curve_widget_sample(RSCurveWidget *curve, gfloat *samples, guint nbsamples)
{
	g_return_val_if_fail (curve != NULL, NULL);
	g_return_val_if_fail (RS_IS_CURVE_WIDGET(curve), NULL);

	samples = rs_spline_sample(curve->spline, samples, nbsamples);

	return(samples);
}

/**
 * Set knots of a RSCurveWidget
 * @param curve A RSCurveWidget
 * @param knots An array of knots (two values/knot)
 * @param nknots Number of knots
 */
void
rs_curve_widget_set_knots(RSCurveWidget *curve, const gfloat *knots, const guint nknots)
{
	gint i;

	g_return_if_fail(RS_IS_CURVE_WIDGET(curve));
	g_return_if_fail(knots != NULL);

	/* Free thew current spline */
	if (curve->spline)
		g_object_unref(curve->spline);

	/* Allocate new spline */
	curve->spline = rs_spline_new(NULL, 0, NATURAL);

	/* Add the knot */
	for(i=0;i<nknots;i++)
		rs_spline_add(curve->spline, knots[i*2], knots[i*2+1]);

	/* Redraw the widget */
	gtk_widget_queue_draw(GTK_WIDGET(curve));

	/* Propagate the change */
	rs_curve_changed(curve);
}

/**
 * Get knots from a RSCurveWidget
 * @param curve A RSCurveWidget
 * @param knots An array of knots (two values/knot) (out)
 * @param nknots Number of knots written (out)
 */
extern void
rs_curve_widget_get_knots(RSCurveWidget *curve, gfloat **knots, guint *nknots)
{
	g_return_if_fail(RS_IS_CURVE_WIDGET(curve));
	g_return_if_fail(knots != NULL);

	rs_spline_get_knots(curve->spline, knots, nknots);
}

/**
 * Resets a RSCurveWidget
 * @param curve A RSCurveWidget
 */
extern void
rs_curve_widget_reset(RSCurveWidget *curve)
{
	g_return_if_fail (curve != NULL);
	g_return_if_fail (RS_IS_CURVE_WIDGET(curve));

	/* Free thew current spline */
	if (curve->spline)
		g_object_unref(curve->spline);

	/* Allocate new spline */
	curve->spline = rs_spline_new(NULL, 0, NATURAL);

	/* Redraw changes */
	gtk_widget_queue_draw(GTK_WIDGET(curve));

	/* Propagate changes */
	rs_curve_changed(curve);
}

/**
 * Saves a RSCurveWidgets knots to a XML-file.
 * @param curve A RSCurveWidget
 * @param filename The filename to save to
 * @return TRUE if succeded, FALSE otherwise
 */
extern gboolean
rs_curve_widget_save(RSCurveWidget *curve, const gchar *filename)
{
	xmlTextWriterPtr writer;
	guint nknots, i;
	gfloat *curve_knots;

	g_return_val_if_fail(RS_IS_CURVE_WIDGET(curve), FALSE);
	g_return_val_if_fail(filename != NULL, FALSE);

	rs_curve_widget_get_knots(curve, &curve_knots, &nknots);
	
	if ((writer = xmlNewTextWriterFilename(filename, 0)))
	{
		xmlTextWriterStartDocument(writer, NULL, "ISO-8859-1", NULL);

		xmlTextWriterStartElement(writer, BAD_CAST "Curve");
		xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "num", "%d", nknots);
		for(i=0;i<nknots;i++)
			xmlTextWriterWriteFormatElement(writer, BAD_CAST "AnchorXY", "%f %f",
				curve_knots[i*2+0],
				curve_knots[i*2+1]);

		xmlTextWriterEndElement(writer);
		xmlTextWriterEndDocument(writer);
		xmlFreeTextWriter(writer);
		return(TRUE);
	}
	else
		return(FALSE);
}

/**
 * Loads knots to a RSCurveWidgets from a XML-file.
 * @param curve A RSCurveWidget
 * @param filename The filename load from
 * @return TRUE if succeded, FALSE otherwise
 */
gboolean
rs_curve_widget_load(RSCurveWidget *curve, const gchar *filename)
{
	xmlDocPtr doc;
	xmlNodePtr cur;
	xmlChar *val;

	g_return_val_if_fail(RS_IS_CURVE_WIDGET(curve), FALSE);

	if (!filename) return FALSE;
	if (!g_file_test(filename, G_FILE_TEST_IS_REGULAR)) return FALSE;
	doc = xmlParseFile(filename);
	if(doc==NULL) return FALSE;

	cur = xmlDocGetRootElement(doc);

	while(cur)
	{
		if ((!xmlStrcmp(cur->name, BAD_CAST "Curve")))
		{
			gchar **vals;
			gfloat x,y;
			guint nknots;
			gfloat *knots;
			xmlNodePtr curknot = NULL;

			rs_curve_widget_get_knots(curve, &knots, &nknots);

			while (nknots--)
			{
				rs_spline_delete(curve->spline, nknots);	
			}
			g_free(knots);

			curknot = cur->xmlChildrenNode;
			while (curknot)
			{
				if ((!xmlStrcmp(curknot->name, BAD_CAST "AnchorXY")))
				{
					val = xmlNodeListGetString(doc, curknot->xmlChildrenNode, 1);
					vals = g_strsplit((gchar *)val, " ", 4);
					if (vals[0] && vals[1])
					{
						x = rs_atof(vals[0]);
						y = rs_atof(vals[1]);
						rs_curve_widget_add_knot(curve, x,y);
					}
					g_strfreev(vals);
					xmlFree(val);
				}
				curknot = curknot->next;
			}
		}
		cur = cur->next;
	}
	xmlFreeDoc(doc);
	
	return TRUE;
}

static void
rs_curve_draw_background(GtkWidget *widget, cairo_t *cr)
{
	gint i, j, x, y;
	gint max[3];

	memset(max, 0, 3*sizeof(gint));

	/* Width */
	gint width;

	/* Height */
	gint height;
	RSCurveWidget *curve;
	GdkWindow *window;

	/* Get back our curve widget */
	curve = RS_CURVE_WIDGET(widget);

	/* Get the drawing window */
	window = gtk_widget_get_window(widget);

	if (!window) return;

	/* Width and height */
	width = gdk_window_get_width(window);
	height = gdk_window_get_height(window);

	/* Scaled histogram */
	gint hist[width];

	if (!curve->bg_buffer)
	{
		curve->bg_buffer = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, width, height);

		/* Clear the window */
		memset(
			gdk_pixbuf_get_pixels(curve->bg_buffer),
			0x99,
			gdk_pixbuf_get_height(curve->bg_buffer) * gdk_pixbuf_get_rowstride(curve->bg_buffer));

		/* Prepare histogram */
		if (curve->histogram_data)
		{
			/* find the third largest value */
			for (i = 0; i < 256; i++)
				for (j = 0; j < 3; j++)
					if (curve->histogram_data[i] > max[j])
					{
						/* Move subsequence entires one down the stack */
						for (x = 1; x >= j; x--)
							max[x+1] = max[x];
						max[j] = curve->histogram_data[i];
						j = 3;
					}

			/* Find height scale factor */
			gfloat factor = (gfloat)height * (1.0f /(gfloat)(max[2]));

			/* Find width scale factor */
			gfloat source, scale = 253.0/width;
			gint source1, source2;
			gfloat weight1, weight2;
			for (i = 0; i < width; i++)
			{
				source = ((gdouble)i)*scale;
				source1 = floor(source);
				source2 = source1+1;
				weight1 = 1.0 - (source-source1);
				weight2 = 1.0 - weight1;

				hist[i] = MIN(height-1, (curve->histogram_data[1+source1] * weight1
					+ curve->histogram_data[1+source2] * weight2) * factor);
			}

			for (x = 0; x < width; x++)
			{
				for (y = 0; y < hist[x]; y++)
				{
					guchar *p = gdk_pixbuf_get_pixels(curve->bg_buffer)
						+ ((height-1)-y) * gdk_pixbuf_get_rowstride(curve->bg_buffer)
						+ x * 3;
//					guchar *p = curve->bg_buffer + ((height-1)-y) * width*3 + x * 3;
					p[R] = 0xB0;
					p[G] = 0xB0;
					p[B] = 0xB0;
				}
			}
		}
	}

	/* Draw histogram to screen */
	gdk_cairo_set_source_pixbuf(cr, curve->bg_buffer, 0.0, 0.0);
	cairo_paint(cr);

	/* Draw all lines */
	cairo_set_line_width(cr, 1.0);
	cairo_set_source_rgba(cr, 0.47, 0.47, 0.47, 1.0);
	for (i=1; i<4; i++)
	{
		gint x = i*width/4;
		gint y = i*height/4;
		cairo_move_to(cr, x+0.5, 0.5);
		cairo_line_to(cr, x+0.5, height+0.5);

		cairo_move_to(cr, 0+0.5, y+0.5);
		cairo_line_to(cr, width+0.5, y+0.5);

		cairo_stroke(cr);
	}

	/* Help text */
	cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
	cairo_move_to(cr, 2.0, 2.0);
	pango_cairo_show_layout(cr, curve->help_layout);
}

static void
rs_curve_draw_knots(GtkWidget *widget, cairo_t *cr)
{
	gfloat *knots = NULL;
	guint n = 0;
	gint width;
	gint height;
	guint i;
	RSCurveWidget *curve;
	GdkWindow *window;

	/* Get back our curve widget */
	curve = RS_CURVE_WIDGET(widget);

	/* Get the drawing window */
	window = gtk_widget_get_window(widget);

	if (!window) return;

	/* Get the knots from the spline */
	rs_spline_get_knots(curve->spline, &knots, &n);

	/* Get the width and height */
	width = gdk_window_get_width(window);
	height = gdk_window_get_height(window);

	/* Put the right bg color */
	cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);

	/* Draw the stuff */
	for (i=0; i<n; i++) {
		gint x = (gint)(knots[2*i + 0]*width);
		gint y = (gint)(height*(1-knots[2*i + 1]));
		cairo_rectangle(cr, x-2, y-2, 4, 4);
		cairo_fill(cr);
	}

	/* Draw the active knot using red */
	cairo_set_source_rgba(cr, 1.0, 0.0, 0.0, 1.0);
	cairo_set_line_width(cr, 1.0);
	if ((curve->active_knot>=0) && (n>0))
	{
		gint x = (gint)(knots[2*curve->active_knot + 0]*width);
		gint y = (gint)(height*(1-knots[2*curve->active_knot + 1]));
		cairo_rectangle(cr, x-3, y-3, 6, 6);
		cairo_stroke(cr);
	}

	g_free(knots);
}

static void
rs_curve_draw_spline(GtkWidget *widget, cairo_t *cr)
{
	RSCurveWidget *curve;

	/* Get back our curve widget */
	curve = RS_CURVE_WIDGET(widget);

	/* Get the drawing window */
	GdkWindow *window = gtk_widget_get_window(widget);

	if (!window) return;

	/* Curve samples */
	gfloat *samples = NULL;

	/* Width and height */
	gint width;
	gint height;
	gint i;

	/* Width and height */
	width = gdk_window_get_width(window);
	height = gdk_window_get_height(window);

	/* Put the right bg color */
	cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);

	samples = rs_curve_widget_sample(curve, NULL, width);

	if (!samples) return;

	cairo_set_line_width(cr, 1.0);
	for (i=0; i<width; i++)
	{
		gint y = (gint)(height*(1-samples[i])+0.5);
		if (y < 0)
			y = 0;
		else if (y > (height-1))
			y = height-1;
		if (i==0)
			cairo_move_to(cr, i, y);
		else
			cairo_line_to(cr, i, y);
	}
	cairo_stroke(cr);

	/* Draw current luminance */
	gfloat marker = rs_curve_widget_get_marker(curve);
	gint current = (int)(marker*(height-1));

	if (current >=0 && current < height)
	{
		cairo_set_source_rgba(cr, 0.94, 0.56, 0.56, 1.0);
		gint x = 0;
		while ((samples[x] < marker) && (x < (width-1)))
			x++;
		current = height - current;
		cairo_move_to(cr, width, current);
		cairo_line_to(cr, x, current);
		cairo_line_to(cr, x, height);
		cairo_stroke(cr);
	}

	g_free(samples);
}
/**
 * Draw everything
 */
static gboolean
rs_curve_widget_draw(RSCurveWidget *curve, cairo_t *cr)
{
	GtkWidget *widget;
	g_return_if_fail (curve != NULL);
	g_return_if_fail (RS_IS_CURVE_WIDGET(curve));

	widget = GTK_WIDGET(curve);

	if (gtk_widget_get_visible(widget) && gtk_widget_get_realized(widget))
	{
		/* Draw the background */
		rs_curve_draw_background(widget, cr);

		/* Draw the control points */
		rs_curve_draw_knots(widget, cr);

		/* Draw the curve */
		rs_curve_draw_spline(widget, cr);
	}
}
  
static gboolean
rs_curve_size_allocate_helper(RSCurveWidget *curve)
{
	gboolean ret = FALSE;

	gdk_threads_enter();
	GtkAllocation allocation;
	GtkWidget *widget = GTK_WIDGET(curve);
	gtk_widget_get_allocation(widget, &allocation);
	gint width = allocation.width;
	gint height = allocation.height;
	if (width != height)
	{
		if (ABS(width - height) > 10)
		{
			gint new_height = width;

			if (width == curve->last_width[0])
				new_height = height;

			g_signal_handler_block(RS_CURVE_WIDGET(curve), RS_CURVE_WIDGET(curve)->size_signal);
			gtk_widget_set_size_request(GTK_WIDGET(curve), -1, new_height);
			GUI_CATCHUP();
			g_signal_handler_unblock(RS_CURVE_WIDGET(curve), RS_CURVE_WIDGET(curve)->size_signal);

			curve->last_width[0] = curve->last_width[1];
			curve->last_width[1] = width;
		}
	}
	gdk_threads_leave();

	return ret;
}

/**
 * Make the curve widget squared
 */
static void
rs_curve_size_allocate(GtkWidget *widget, GtkAllocation *allocation, gpointer user_data)
{
	RSCurveWidget *curve = RS_CURVE_WIDGET(widget);
	rs_curve_size_allocate_helper(curve);
}

/**
 * Propagate changes
 */
static void
rs_curve_changed(RSCurveWidget *curve)
{
	g_return_if_fail (curve != NULL);
	g_return_if_fail (RS_IS_CURVE_WIDGET(curve));

	if (curve->array_length>0)
		rs_curve_widget_sample(curve, curve->array, curve->array_length);

	g_signal_emit (G_OBJECT (curve), 
		signals[CHANGED_SIGNAL], 0);
}

static gboolean
delayed_update(gpointer data)
{
	g_return_val_if_fail (data != NULL, FALSE);
	RSCurveWidget *curve = RS_CURVE_WIDGET(data);
	g_return_val_if_fail (RS_IS_CURVE_WIDGET(curve), FALSE);
	g_source_remove(curve->delay_update);
	curve->delay_update = 0;
	gdk_threads_enter();
	rs_curve_changed(curve);
	gdk_threads_leave();
	return TRUE;
}

/**
 * Handle button press
 */
static gboolean
rs_curve_widget_button_press(GtkWidget *widget, GdkEventButton *event)
{
	gint w, h;
	gfloat x,y;
	RSCurveWidget *curve;

	g_return_val_if_fail(widget != NULL, FALSE);
	g_return_val_if_fail(RS_IS_CURVE_WIDGET (widget), FALSE);
	g_return_val_if_fail(event != NULL, FALSE);

	/* Get back our curve widget */
	curve = RS_CURVE_WIDGET(widget);

	GdkWindow *window = gtk_widget_get_window(widget);
	w = gdk_window_get_width(window);
	h = gdk_window_get_height(window);
	x = event->x/w;
	y = 1.0 - event->y/h;

	gint button = event->button;
	/* Shift+Left = Middle button */
	if (button == 1 && (event->state&GDK_SHIFT_MASK))
		button = 2;
	
	/* Add a point */
	if ((button==1) && (curve->active_knot==-1))
		rs_curve_widget_add_knot(curve, x, y);
	else if (button == 1 && (curve->active_knot >= 0))
		rs_spline_move(curve->spline, curve->active_knot, x, y);

	/* Delete a point if not first or last */
	else if (button == 2
		&& (curve->active_knot>0)
		&& (curve->active_knot<(rs_spline_length(curve->spline)-1)))
	{
		rs_spline_delete(curve->spline, curve->active_knot);
		curve->active_knot = -1;
	}
	else if (button == 3)
		g_signal_emit (G_OBJECT (curve), 
			signals[RIGHTCLICK_SIGNAL], 0);

	gtk_widget_queue_draw(widget);

	return(TRUE);
}
/*
 * Update when button is released
 */
static gboolean
rs_curve_widget_button_release(GtkWidget *widget, GdkEventButton *event)
{
	rs_curve_changed(RS_CURVE_WIDGET(widget));
	return(TRUE);
}

/*
 * Handle motion
 */
static gboolean
rs_curve_widget_motion_notify(GtkWidget *widget, GdkEventMotion *event)
{
	gint w, h;
	gfloat x,y;
	guint i, n = 0;
	gfloat *knots;
	RSCurveWidget *curve;
	gint old_active_knot;

	g_return_val_if_fail(widget != NULL, FALSE);
	g_return_val_if_fail(RS_IS_CURVE_WIDGET (widget), FALSE);
	g_return_val_if_fail(event != NULL, FALSE);

	/* Get back our curve widget */
	curve = RS_CURVE_WIDGET(widget);

	/* Remember the last active knot */
	old_active_knot = curve->active_knot;

	GdkWindow *window = gtk_widget_get_window(widget);
	w = gdk_window_get_width(window);
	h = gdk_window_get_height(window);

	/* Get a working copy of current knots */
	rs_spline_get_knots(curve->spline, &knots, &n);

	/* Calculate pixel coordinates for X-axis */
	for(i=0;i<n;i++)
		knots[i*2+0] = (float) w * knots[i*2+0];

	/* Moving a knot? */
	if ((event->state&GDK_BUTTON1_MASK) && (curve->active_knot>=0))
	{
		x = event->x/w;
		y = 1.0f - event->y/h;

		/* Clamp Y value */
		if (y<0.0f) y = 0.0f;
		if (y>1.0f) y = 1.0f;

		/* Clamp X value */
		if (x<0.0f) x = 0.0f;
		if (x>1.0f) x = 1.0f;

		/* Restrict X-axis for first and last knot */
		if (curve->active_knot == 0) /* first */
			rs_spline_move(curve->spline, curve->active_knot, x, y);
		else if (curve->active_knot == rs_spline_length(curve->spline)-1) /* last */
			rs_spline_move(curve->spline, curve->active_knot, x, y);
		else
		{
			/* Delete knot if we collide with neighbour */
			if (event->x <= knots[(curve->active_knot-1)*2+0])
			{
				rs_spline_delete(curve->spline, curve->active_knot);
				curve->active_knot--;
			}
			else if (event->x >= knots[(curve->active_knot+1)*2+0])
				rs_spline_delete(curve->spline, curve->active_knot);

			/* Move the knot */
			rs_spline_move(curve->spline, curve->active_knot, x, y);
		}
		if (curve->delay_update > 0)
			g_source_remove(curve->delay_update);
		curve->delay_update = g_timeout_add(50, delayed_update, curve);

		gtk_widget_queue_draw(GTK_WIDGET(curve));
		
	}
	else /* Only reset active_knot if we're not moving anything */
	{

		/* Find knot below cursor if any  */
		curve->active_knot = -1;
		/* This also indicates the closeness the cursor must be to 'pick up' a point */
		float closest_distance = 16.0f;
		for(i=0;i<n;i++)
		{
			float dist = fabsf(event->x-knots[i*2+0]);
			if (dist < closest_distance)
			{
				closest_distance = dist;
				curve->active_knot = i;
			}
		}
	}

	/* Update knots if needed */
	if (old_active_knot != curve->active_knot)
		gtk_widget_queue_draw(widget);
	g_free(knots);

	return(TRUE);
}

static void
rs_curve_widget_size_allocate(GtkWidget *widget, GtkAllocation *allocation, gpointer user_data)
{
	/* Get back our curve widget */
	RSCurveWidget *curve = RS_CURVE_WIDGET(widget);

	/* Free our bg_buffer, since it must be useless by now */
	if (curve->bg_buffer)
		g_object_unref(curve->bg_buffer);

	/* Mark it as not existing */
	curve->bg_buffer = NULL;
}


/* Added by Anders Kvist */
void
rs_curve_auto_adjust_ends(GtkWidget *widget) {

  g_return_if_fail(RS_IS_CURVE_WIDGET(widget));

  RSCurveWidget *curve = RS_CURVE_WIDGET(widget);

  gint i = 0;
  gdouble black_threshold = 0.2; // Percent underexposed pixels
  gdouble white_threshold = 0.05; // Percent overexposed pixels
  gdouble blackpoint;
  gdouble whitepoint;
  guint total = 0;
  guint total_pixels = 0;

  guint *hist;
  hist = curve->histogram_data;

  while(i < 256) {
    total_pixels += hist[i];
    i++;
  }

  // calculate black point
  i = 0;
  while(i < 256) {
    total += hist[i];
    if ((total) > ((total_pixels)/100*black_threshold))
      break;
    i++;
  }
  blackpoint = (gdouble) i / (gdouble) 255;
		
  // calculate white point
  total = 0;
  i = 255;
  while(i) {
    total += hist[i];
    if ((total) > ((total_pixels)/100*white_threshold))
      break;
    i--;
  }
  whitepoint = (gdouble) i / (gdouble) 255;

  rs_curve_widget_move_knot(RS_CURVE_WIDGET(widget),0,blackpoint,0.0);
  rs_curve_widget_move_knot(RS_CURVE_WIDGET(widget),-1,whitepoint,1.0);
}

#ifdef RS_CURVE_TEST

void
changed(GtkWidget *widget, gpointer user_data)
{
	gfloat *s;
	gint i;
	s = rs_curve_widget_sample(RS_CURVE_WIDGET(widget), 20);
	for(i=0;i<20;i++)
	{
		printf("%.05f\n", s[i]);
	}
	g_free(s);
}

int
main(int argc, char **argv)
{
	/* Iterator */
	gint i;

        /* A window */
	GtkWidget *window;

	/* The curve */
        GtkWidget *curve;

	/* A simple S-curve */
	const gfloat scurve_knots[] = {
		0.625f, 0.75f,
		0.125f, 0.25f,
		0.5f, 0.5f,
		0.0f, 0.0f,
		1.0f, 1.0f
	};

	gtk_init (&argc, &argv);

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

	/* Build a nice curve */
	curve = rs_curve_widget_new();
	for (i=0; i<sizeof(scurve_knots)/(2*sizeof(scurve_knots[0])); i++)
	{
		/* Add knots to the curve */
		rs_curve_widget_add_knot(RS_CURVE_WIDGET(curve), scurve_knots[2*i], scurve_knots[2*i+1]);
	}

	gtk_container_add(GTK_CONTAINER(window), curve);
	g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	g_signal_connect(curve, "changed", G_CALLBACK(changed), NULL);

	gtk_widget_show_all(window);

	gtk_main();

	return 0;
}
#endif
