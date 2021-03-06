/*
 * * Copyright (C) 2006-2011 Anders Brander <anders@brander.dk>, 
 * * Anders Kvist <akv@lnxbx.dk> and Klaus Post <klauspost@gmail.com>
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
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "application.h"
#include "filename.h"
#include "conf_interface.h"
#include "gettext.h"
#include "gtk-helper.h"
#include "rs-metadata.h"

static void filename_entry_changed_writeback(GtkEntry *entry, gpointer user_data);
static void filename_entry_changed_writeconf(GtkEntry *entry, gpointer user_data);
static void filename_add_clicked(GtkButton *button, gpointer user_data);

gchar *
filename_parse(const gchar *in, const gchar *filename, const gint snapshot, gboolean load_metadata)
{
	/*
	 * %f = filename
	 * %c = incremental counter
	 * %s = setting (A,B or C)
	 * %d = date (will have to wait until read from exif)
	 * %t = time (will have to wait until read from exif)
	 * %p = path of raw file
	 */

	gchar temp[1024];
	gchar tempc[32];
	gchar *output = NULL;
	gint n=0, m=0;
	gint c=0;
	gboolean counter = FALSE;
	gboolean file_exists = FALSE;
	gint i = 1;
	gchar *basename;
	gchar *path;
	RSMetadata *metadata = NULL;

	if (load_metadata)
		metadata = rs_metadata_new_from_file(filename);
	else
		metadata = rs_metadata_new();

	if (filename == NULL) return NULL;
	if (in == NULL) return NULL;

	basename = g_path_get_basename(filename);
	path = g_path_get_dirname(filename);
	output = g_strrstr(basename, ".");

	/* Prepare time/date */
	struct tm *tm = g_new0(struct tm, 1);
	time_t tt;
	tt = (time_t) metadata->timestamp;
	gmtime_r(&tt, tm);

	if (output != NULL) {
		*output = '\0';
	}
	output = NULL;
	do {
		
		while (in[n])
		{
  			switch(in[n])
			{
				case '%':
					switch (in[n+1])
					{
						case '1':
						case '2':
						case '3':
						case '4':
						case '5':
						case '6':
						case '7':
						case '8':
						case '9':
							c = (gint) in[n+1];
							switch (in[n+2])
							{
								case 'c':
									counter = TRUE;
									if (c == 49)
										sprintf(tempc, "%01d",i);
									else if (c == 50)
										sprintf(tempc, "%02d",i);
									else if (c == 51)
										sprintf(tempc, "%03d",i);
									else if (c == 52)
										sprintf(tempc, "%04d",i);
									else if (c == 53)
										sprintf(tempc, "%05d",i);
									else if (c == 54)
										sprintf(tempc, "%06d",i);
									else if (c == 55)
										sprintf(tempc, "%07d",i);
									else if (c == 56)
										sprintf(tempc, "%08d",i);
									else if (c == 57)
										sprintf(tempc, "%09d",i);
									n += 3;
									strcpy(&temp[m], tempc);
									m += strlen(tempc);
									break;
								default:
									temp[m++] = in[n];
									temp[m++] = in[n+1];
									temp[m++] = in[n+2];
									n += 3;
									break;
								}
							break;
						case 'f':
							strcpy(&temp[m], basename);
							m += strlen(basename);
							n += 2;
							break;
						case 'c':
							counter = TRUE;
							g_sprintf(tempc, "%d", i);
							strcpy(&temp[m], tempc);
							m += strlen(tempc);
						    n += 2;
							break;
						case 's':
							switch (snapshot)
							{
								case 0:
									strcpy(&temp[m], "A");
									break;
								case 1:
									strcpy(&temp[m], "B");
									break;
								case 2:
									strcpy(&temp[m], "C");
									break;
							}
							m += 1;
							n += 2;
							break;
						case 'd':
						{
							gchar *result = g_new0(gchar, 11);
							strftime(result, 11, "%Y-%m-%d", tm);
							strcpy(&temp[m], result);

							n += 2;
							m += strlen(result);

							g_free(result);
							break;
						}
						case 'w':
						{
							gchar *result = g_new0(gchar, 3);
							strftime(result, 3, "%U", tm);
							strcpy(&temp[m], result);

							n += 2;
							m += strlen(result);

							g_free(result);
							break;
						}
						case 'W':
						{
							gchar *result = g_new0(gchar, 3);
							strftime(result, 3, "%W", tm);
							strcpy(&temp[m], result);

							n += 2;
							m += strlen(result);

							g_free(result);
							break;
						}
						case 'D':
						{
							gchar *result = g_new0(gchar, 500);
							switch (in[n+2])
							{
								case 'y':
									strftime(result, 500, "%y", tm);
									break;
								case 'Y':
									strftime(result, 500, "%Y", tm);
									break;
								case 'm':
									strftime(result, 500, "%m", tm);
									break;
								case 'M':
									strftime(result, 500, "%B", tm);
									break;
								case 'd':
									strftime(result, 500, "%d", tm);
									break;
								case 'D':
									strftime(result, 500, "%A", tm);
									break;
								default:
									*result = 0;
								  
							}
							strcpy(&temp[m], result);

							n += 3;
							m += strlen(result);

							g_free(result);
							break;
						}
						case 'M':
						{
							gchar *result = g_new0(gchar, 50);
							switch (in[n+2])
							{
								case 'i':
									sprintf(result, "%d", metadata->iso);
									break;
								case 'S':
									sprintf(result, "%d", (int)(0.4999f + metadata->shutterspeed));
									break;
								case 's':
									if (metadata->shutterspeed <= 1000)
										sprintf(result, "%0.3f", 1.0f / metadata->shutterspeed);
									else
										sprintf(result, "%0.5f", 1.0f / metadata->shutterspeed);
									break;
								case 'a':
									sprintf(result, "%0.1f", metadata->aperture);
									break;
								case 'f':
									sprintf(result, "%d", metadata->focallength);
									break;
								default:
									*result = 0;
							}
							strcpy(&temp[m], result);

							n += 3;
							m += strlen(result);

							g_free(result);
							break;
						}

						case 't':
						{
							gchar *result = g_new0(gchar, 9);
							strftime(result, 9, "%H:%M:%S", tm);
							strcpy(&temp[m], result);

							n += 2;
							m += strlen(result);

							g_free(result);
							break;
						}
						case 'p':
						{
							strcpy(&temp[m], path);
							m += strlen(path);
							n += 2;
							break;
						}
						default:
							temp[m++] = in[n];
							temp[m++] = in[n+1];
							n += 2;
						break;
					}
					break;
				default:
					temp[m++] = in[n++];
					break;
				}
			}

			temp[m] = '\0';

			if (output)
				g_free(output);
			
			output = g_strdup(temp);

			file_exists = g_file_test(output, G_FILE_TEST_EXISTS);

			if (counter == FALSE)
				file_exists = FALSE;

			// resets for next run	
			i++;
			n = 0;
			m = 0;

	} while (file_exists == TRUE);
	
	g_free(basename);
	g_free(tm);
	g_free(path);
	g_object_unref(metadata);
	
	return output;
}

static void
filename_entry_changed_writeback(GtkEntry *entry, gpointer user_data)
{
	gchar **filename = (gchar **) user_data;

	if (filename)
	{
		if (*filename) g_free(*filename);
		*filename = g_strdup(gtk_entry_get_text(entry));
	}
	return;
}

static void
filename_entry_changed_writeconf(GtkEntry *entry, gpointer user_data)
{
	gchar *conf_key = (gchar *) user_data;

	if (conf_key)
		rs_conf_set_string(conf_key, gtk_entry_get_text(entry));
	return;
}

#define TEXT_FUNCTION(A,B) static void A(GtkMenuItem *menuitem, GtkBin *combo) {\
	gint position = -1;\
	GtkWidget *entry = gtk_bin_get_child(combo);\
	gtk_editable_insert_text(GTK_EDITABLE(entry), B, -1, &position); }

TEXT_FUNCTION(add_f,"%f");
TEXT_FUNCTION(add_c,"%2c");
TEXT_FUNCTION(add_s,"%s");
TEXT_FUNCTION(add_d,"%d");
TEXT_FUNCTION(add_t,"%t");
TEXT_FUNCTION(add_p,"%p");
TEXT_FUNCTION(add_DY,"%DY");
TEXT_FUNCTION(add_Dy,"%Dy");
TEXT_FUNCTION(add_Dm,"%Dm");
TEXT_FUNCTION(add_DM,"%DM");
TEXT_FUNCTION(add_DD,"%DD");
TEXT_FUNCTION(add_Dd,"%Dd");
TEXT_FUNCTION(add_w,"%w");
TEXT_FUNCTION(add_W,"%W");
TEXT_FUNCTION(add_Mi,"%Mi");
TEXT_FUNCTION(add_Ma,"%Ma");
TEXT_FUNCTION(add_Ms,"%Ms");
TEXT_FUNCTION(add_MS,"%MS");
TEXT_FUNCTION(add_Mf,"%Mf");

#undef TEXT_FUNCTION

static void
filename_add_clicked(GtkButton *button, gpointer user_data)
{
	gui_menu_popup(GTK_WIDGET(button), user_data,
		_("%f - Original Filename"), add_f,
		_("%p - Path of Original File"), add_p,
		_("%2c - Incremental Counter"), add_c,
		_("%s - Setting ID (A, B or C)"), add_s,
		_("%d - Date from Exif (YYYY-MM-DD)"), add_d,
		_("%DY - Year from Exif (YYYY)"), add_DY,
		_("%Dy - Year from Exif (YY)"), add_Dy,
		_("%Dm - Month from Exif (MM)"), add_Dm,
		_("%DM - Month from Exif (Text)"), add_DM,
		_("%Dd - Date from Exif (DD)"), add_Dd,
		_("%DD - Day of Week from Exif"), add_DD,
		_("%t - Time from EXIF (HH:MM:SS)"), add_t,
		_("%w - Week Number from Exif (Start Sunday)"), add_w,
		_("%W - Week Number from Exif (Start Monday)"), add_W,
		_("%Mi - ISO Value of Image"), add_Mi,
		_("%Ma - Aperture (2.8)"), add_Ma,
		_("%Ms - Shutter Time in Seconds (0.008)"), add_Ms,
		_("%MS - Inverted Shutter Time (125)"), add_MS,
		_("%Mf - Focal Length in mm (70)"), add_Mf,
		-1
	);
}

GtkWidget *
rs_filename_chooser_button_new(gchar **filename, const gchar *conf_key)
{
	gint i;
	const static gchar *filenames[] = {
		DEFAULT_CONF_EXPORT_FILENAME,
		"%f",
		"%f_%c",
		"%f_output_%4c",
		NULL };
	GtkWidget *addbutton;
	GtkWidget *hbox = gtk_hbox_new(FALSE, 2);
	GtkWidget *combo;
	GtkWidget *entry;

	combo = gtk_combo_box_text_new_with_entry();
	entry = gtk_bin_get_child(GTK_BIN(combo));
	addbutton = gtk_button_new_with_label("+");

	g_object_set_data(G_OBJECT(hbox), "entry", entry);

	for(i=0; filenames[i] != NULL; i++)
		gtk_combo_box_text_insert_text(GTK_COMBO_BOX_TEXT(combo), i, filenames[i]);
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);

	if (filename)
	{
		gtk_entry_set_text(GTK_ENTRY(entry), *filename);
		g_signal_connect (G_OBJECT(entry), "changed",
			G_CALLBACK(filename_entry_changed_writeback), (gpointer) filename);
	}
	if (conf_key)
		g_signal_connect (G_OBJECT(entry), "changed",
			G_CALLBACK(filename_entry_changed_writeconf), (gpointer) conf_key);

	/* If we have a conf_key but no supplied filename, we try to load saved
	 * filename from conf system */
	if (conf_key && !filename)
	{
		gchar *saved_filename;
		saved_filename = rs_conf_get_string(conf_key);
		if (saved_filename)
			gtk_entry_set_text(GTK_ENTRY(entry), saved_filename);
	}
	g_signal_connect (G_OBJECT(addbutton), "clicked",
		G_CALLBACK(filename_add_clicked), combo);

	gtk_box_pack_start (GTK_BOX (hbox), combo, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), addbutton, FALSE, FALSE, 0);

	return(hbox);
}
