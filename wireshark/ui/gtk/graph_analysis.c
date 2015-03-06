/* graph_analysis.c
 * Graphic Analysis addition for Wireshark
 *
 * $Id$
 *
 * Copyright 2004, Verso Technologies Inc.
 * By Alejandro Vaquero <alejandrovaquero@yahoo.com>
 *
 * based on rtp_analysis.c and io_stat
 *
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
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
 * Foundation,  Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <stdio.h>
#include <string.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#if GTK_CHECK_VERSION(3,0,0)
# include <gdk/gdkkeysyms-compat.h>
#endif

#include <epan/epan_dissect.h>
#include <epan/tap.h>
#include <epan/dissectors/packet-rtp.h>
#include <epan/addr_resolv.h>
#include <epan/filesystem.h>

#include "ui/util.h"

#include "ui/alert_box.h"
#include "ui/last_open_dir.h"
#include "ui/recent.h"
#include "ui/simple_dialog.h"

#include <wsutil/file_util.h>

#include "ui/gtk/gtkglobals.h"
#include "ui/gtk/file_dlg.h"
#include "ui/gtk/gui_utils.h"
#include "ui/gtk/dlg_utils.h"
#include "ui/gtk/main.h"
#include "ui/gtk/graph_analysis.h"

#include "ui/gtk/old-gtk-compat.h"

#include "../../image/voip_bg.xpm"

/****************************************************************************/

#define MAX_LABEL 50
#define MAX_COMMENT 100
#define ITEM_HEIGHT 20
#define NODE_WIDTH 100
#define TOP_Y_BORDER 40
#define BOTTOM_Y_BORDER 2
#define COMMENT_WIDTH 400
#define TIME_WIDTH 150

#define NODE_CHARS_WIDTH 20
#define CONV_TIME_HEADER       "Conv.| Time    "
#define TIME_HEADER "|Time     "
#define CONV_TIME_EMPTY_HEADER "     |         "
#define TIME_EMPTY_HEADER      "|         "
#define CONV_TIME_HEADER_LENGTH 16
#define TIME_HEADER_LENGTH 10

/****************************************************************************/
/* Reset the user_data structure */
static void graph_analysis_reset(graph_analysis_data_t *user_data)
{
	int i;

	user_data->num_nodes = 0;
	user_data->num_items = 0;
	for (i=0; i<MAX_NUM_NODES; i++) {
		user_data->nodes[i].type = AT_NONE;
		user_data->nodes[i].len = 0;
		g_free((void *)user_data->nodes[i].data);
		user_data->nodes[i].data = NULL;
	}

	user_data->dlg.first_node = 0;
	user_data->dlg.first_item = 0;
	user_data->dlg.left_x_border = 0;
	user_data->dlg.selected_item = 0xFFFFFFFF;    /*not item selected */
}

/****************************************************************************/
/* Init the user_data structure */
static void graph_analysis_init_dlg(graph_analysis_data_t *user_data)
{
	int i;

	user_data->num_nodes = 0;
	user_data->num_items = 0;
	user_data->on_destroy_user_data = NULL;
	user_data->data = NULL;
	for (i=0; i<MAX_NUM_NODES; i++) {
		user_data->nodes[i].type = AT_NONE;
		user_data->nodes[i].len = 0;
		user_data->nodes[i].data = NULL;
	}

	user_data->dlg.first_node = 0;
	user_data->dlg.first_item = 0;
	user_data->dlg.left_x_border = 0;
	user_data->dlg.selected_item = 0xFFFFFFFF;    /*not item selected */
	/* init dialog_graph */
	user_data->dlg.needs_redraw = TRUE;
	user_data->dlg.draw_area_time = NULL;
	user_data->dlg.draw_area = NULL;
	user_data->dlg.draw_area_comments = NULL;
#if GTK_CHECK_VERSION(2,22,0)
	user_data->dlg.surface_main = NULL;
	user_data->dlg.surface_time = NULL;
	user_data->dlg.surface_comments = NULL;
#else
	user_data->dlg.pixmap_main = NULL;
	user_data->dlg.pixmap_time = NULL;
	user_data->dlg.pixmap_comments = NULL;
#endif
	user_data->dlg.v_scrollbar = NULL;
	user_data->dlg.v_scrollbar_adjustment = NULL;
	user_data->dlg.hpane = NULL;
	user_data->dlg.surface_width = 350;
	user_data->dlg.surface_height = 400;
	user_data->dlg.first_node = 0;
	user_data->dlg.first_item = 0;
	user_data->dlg.left_x_border = 0;
	user_data->dlg.selected_item = 0xFFFFFFFF;    /*not item selected */
	user_data->dlg.window = NULL;
	user_data->dlg.parent_w = NULL;
	user_data->dlg.inverse = FALSE;
	user_data->dlg.title = NULL;
}

/****************************************************************************/
/* CALLBACKS */

/****************************************************************************/
/* close the dialog window */
static void on_destroy(GtkWidget *win _U_, graph_analysis_data_t *user_data)
{
	int i;

	for (i=0; i<MAX_NUM_NODES; i++) {
		user_data->nodes[i].type = AT_NONE;
		user_data->nodes[i].len = 0;
		g_free((void *)user_data->nodes[i].data);
		user_data->nodes[i].data = NULL;
	}
	user_data->dlg.window = NULL;
	g_free(user_data->dlg.title);
	user_data->dlg.title = NULL;

	if(user_data->on_destroy_user_data) {
		user_data->on_destroy_user_data(user_data->data);
	}
}

#define RIGHT_ARROW 1
#define LEFT_ARROW 0
#define WIDTH_ARROW 8
#define HEIGHT_ARROW 6

/****************************************************************************/
#if GTK_CHECK_VERSION(2,22,0)
static void draw_arrow(cairo_surface_t *surface, GdkRGBA *color, gint x, gint y, gboolean arrow_type)
{
	cairo_t *cr;

	cr = cairo_create (surface);
	gdk_cairo_set_source_rgba (cr, color);
	if (arrow_type == LEFT_ARROW)
	{
		cairo_move_to (cr, x + WIDTH_ARROW,      y);
		cairo_line_to (cr, x + WIDTH_ARROW,      y + HEIGHT_ARROW);
		cairo_line_to (cr, x,                    y + HEIGHT_ARROW / 2.);
	}
	else if (arrow_type == RIGHT_ARROW)
	{
		cairo_move_to (cr, x,                    y);
		cairo_line_to (cr, x + WIDTH_ARROW,      y + HEIGHT_ARROW / 2.);
		cairo_line_to (cr, x,                    y + HEIGHT_ARROW);
	}
	cairo_close_path (cr);
	cairo_fill (cr);

	cairo_destroy (cr);
}
#else
static void draw_arrow(GdkDrawable *pixmap, GdkRGBA *color, gint x, gint y, gboolean arrow_type)
{
	cairo_t *cr;

	if (GDK_IS_DRAWABLE(pixmap)) {
		cr = gdk_cairo_create (pixmap);
		gdk_cairo_set_source_rgba (cr, color);
		if (arrow_type == LEFT_ARROW)
		{
			cairo_move_to (cr, x + WIDTH_ARROW,      y);
			cairo_line_to (cr, x + WIDTH_ARROW,      y + HEIGHT_ARROW);
			cairo_line_to (cr, x,                    y + HEIGHT_ARROW / 2.);
		}
		else if (arrow_type == RIGHT_ARROW)
		{
			cairo_move_to (cr, x,                    y);
			cairo_line_to (cr, x + WIDTH_ARROW,      y + HEIGHT_ARROW / 2.);
			cairo_line_to (cr, x,                    y + HEIGHT_ARROW);
		}
		cairo_close_path (cr);
		cairo_fill (cr);

		cairo_destroy (cr);
	}
}
#endif

/****************************************************************************/
/* Adds trailing characters to complete the requested length.               */
/****************************************************************************/

static void enlarge_string(GString *gstr, guint32 length, char pad) {

	gsize i;

	for (i = gstr->len; i < length; i++) {
		g_string_append_c(gstr, pad);
	}
}

/****************************************************************************/
/* overwrites the characters in a string, between positions p1 and p2, with */
/*   the characters of text_to_insert                                       */
/*   NB: it does not check that p1 and p2 fit into string                   */
/****************************************************************************/

static void overwrite (GString *gstr, char *text_to_insert, guint32 p1, guint32 p2) {

	gsize len;
	gsize pos;

	if (p1 == p2)
		return;

	if (p1 > p2) {
		pos = p2;
		len = p1 - p2;
	}
	else{
		pos = p1;
		len = p2 - p1;
	}

	if (len > strlen(text_to_insert)) {
		len = strlen(text_to_insert);
	}

	if (pos > gstr->len)
		pos = gstr->len;

	/* ouch this is ugly but gtk1 needs it */
	if ((pos + len) > gstr->len)
		g_string_truncate(gstr, pos);
	else
		g_string_erase(gstr, pos, len);

	g_string_insert(gstr, pos, text_to_insert);
}

/****************************************************************************/
static gboolean
dialog_graph_dump_to_file(char *pathname, graph_analysis_data_t *user_data)
{
	guint32  i, first_node, display_items, display_nodes;
	guint32  start_position, end_position, item_width, header_length;
	graph_analysis_item_t *gai;
	guint16  first_conv_num = 0;
	gboolean several_convs  = FALSE;
	gboolean first_packet   = TRUE;

	GString    *label_string, *empty_line, *separator_line, *tmp_str, *tmp_str2;
	const char *empty_header;
	char        src_port[8], dst_port[8];
	gchar      *time_str;
	GList      *list;

	FILE  *of;

	of = ws_fopen(pathname, "w");
	if (of==NULL) {
		open_failure_alert_box(pathname, errno, TRUE);
		return FALSE;
	}

	time_str       = (gchar *)g_malloc(COL_MAX_LEN);
	label_string   = g_string_new("");
	empty_line     = g_string_new("");
	separator_line = g_string_new("");
	tmp_str        = g_string_new("");
	tmp_str2       = g_string_new("");

	display_items = 0;
	list = g_list_first(user_data->graph_info->list);
	while (list)
	{
		gai = (graph_analysis_item_t *)list->data;
		list = g_list_next(list);

		if (!gai->display)
			continue;

		display_items += 1;
		if (first_packet) {
			first_conv_num = gai->conv_num;
			first_packet = FALSE;
		}
		else if (gai->conv_num != first_conv_num) {
			several_convs = TRUE;
		}
	}

	/* if not items to display */
	if (display_items == 0)
		goto exit;

	display_nodes = user_data->num_nodes;

	first_node = user_data->dlg.first_node;

	/* Write the conv. and time headers */
	if (several_convs) {
		fprintf(of, CONV_TIME_HEADER);
		empty_header = CONV_TIME_EMPTY_HEADER;
		header_length = CONV_TIME_HEADER_LENGTH;
	}
	else{
		fprintf(of, TIME_HEADER);
		empty_header = TIME_EMPTY_HEADER;
		header_length = TIME_HEADER_LENGTH;
	}

	/* Write the node names on top */
	for (i=0; i<display_nodes; i+=2) {
		/* print the node identifiers */
		g_string_printf(label_string, "| %s",
			get_addr_name(&(user_data->nodes[i+first_node])));
		enlarge_string(label_string, NODE_CHARS_WIDTH*2, ' ');
		fprintf(of, "%s", label_string->str);
		g_string_printf(label_string, "| ");
		enlarge_string(label_string, NODE_CHARS_WIDTH, ' ');
		g_string_append(empty_line, label_string->str);
	}

	fprintf(of, "|\n%s", empty_header);
	g_string_printf(label_string, "| ");
	enlarge_string(label_string, NODE_CHARS_WIDTH, ' ');
	fprintf(of, "%s", label_string->str);

	/* Write the node names on top */
	for (i=1; i<display_nodes; i+=2) {
		/* print the node identifiers */
		g_string_printf(label_string, "| %s",
			get_addr_name(&(user_data->nodes[i+first_node])));
		if (label_string->len < NODE_CHARS_WIDTH)
		{
			enlarge_string(label_string, NODE_CHARS_WIDTH, ' ');
			g_string_append(label_string, "| ");
		}
		enlarge_string(label_string, NODE_CHARS_WIDTH*2, ' ');
		fprintf(of, "%s", label_string->str);
		g_string_printf(label_string, "| ");
		enlarge_string(label_string, NODE_CHARS_WIDTH, ' ');
		g_string_append(empty_line, label_string->str);
	}

	fprintf(of, "\n");

	g_string_append_c(empty_line, '|');

	enlarge_string(separator_line, (guint32) empty_line->len + header_length, '-');

	/*
	 * Draw the items
	 */

	list = g_list_first(user_data->graph_info->list);
	while (list)
	{
		gai = (graph_analysis_item_t *)list->data;
		list = g_list_next(list);

		if (!gai->display)
			continue;

		start_position = (gai->src_node-first_node)*NODE_CHARS_WIDTH+NODE_CHARS_WIDTH/2;

		end_position = (gai->dst_node-first_node)*NODE_CHARS_WIDTH+NODE_CHARS_WIDTH/2;

		if (start_position > end_position) {
			item_width = start_position-end_position;
		}
		else if (start_position < end_position) {
			item_width = end_position-start_position;
		}
		else{ /* same origin and destination address */
			end_position = start_position+NODE_CHARS_WIDTH;
			item_width = NODE_CHARS_WIDTH;
		}

		/* separator between conversations */
		if (gai->conv_num != first_conv_num) {
			fprintf(of, "%s\n", separator_line->str);
			first_conv_num = gai->conv_num;
		}

		/* write the conversation number */
		if (several_convs) {
			g_string_printf(label_string, "%i", gai->conv_num);
			enlarge_string(label_string, 5, ' ');
			fprintf(of, "%s", label_string->str);
		}

#if 0
		/* write the time */
		g_string_printf(label_string, "|%.3f", nstime_to_sec(&gai->fd->rel_ts));
#endif
		/* Write the time, using the same format as in the time col */
		set_fd_time(gai->fd, time_str);
		g_string_printf(label_string, "|%s", time_str);
		enlarge_string(label_string, 10, ' ');
		fprintf(of, "%s", label_string->str);

		/* write the frame label */

		g_string_printf(tmp_str, "%s", empty_line->str);
		overwrite(tmp_str, gai->frame_label,
			start_position,
			end_position
			);
		fprintf(of, "%s", tmp_str->str);

		/* write the comments */
		fprintf(of, "%s\n", gai->comment);

		/* write the arrow and frame label*/
		fprintf(of, "%s", empty_header);

		g_string_printf(tmp_str, "%s", empty_line->str);

		g_string_truncate(tmp_str2, 0);

		if (start_position<end_position) {
			enlarge_string(tmp_str2, item_width-2, '-');
			g_string_append_c(tmp_str2, '>');
		}
		else{
			g_string_printf(tmp_str2, "<");
			enlarge_string(tmp_str2, item_width-1, '-');
		}

		overwrite(tmp_str, tmp_str2->str,
			start_position,
			end_position
			);

		g_snprintf(src_port, sizeof(src_port), "(%i)", gai->port_src);
		g_snprintf(dst_port, sizeof(dst_port), "(%i)", gai->port_dst);

		if (start_position<end_position) {
			overwrite(tmp_str, src_port, start_position-9, start_position-1);
			overwrite(tmp_str, dst_port, end_position+1, end_position+9);
		}
		else{
			overwrite(tmp_str, src_port, start_position+1, start_position+9);
			overwrite(tmp_str, dst_port, end_position-9, end_position+1);
		}

		fprintf(of, "%s\n", tmp_str->str);
	}

exit:
	g_string_free(label_string, TRUE);
	g_string_free(empty_line, TRUE);
	g_string_free(separator_line, TRUE);
	g_string_free(tmp_str, TRUE);
	g_string_free(tmp_str2, TRUE);
	g_free(time_str);
	fclose (of);
	return TRUE;

}

/****************************************************************************/
/* save in a file */

static char *
gtk_save_graph_as_plain_text_file(graph_analysis_data_t *user_data)
{
	GtkWidget *save_to_file_w;
	char *pathname;

	save_to_file_w = file_selection_new("Wireshark: Save graph to plain text file",
					    GTK_WINDOW(user_data->dlg.window),
					    FILE_SELECTION_SAVE);
        gtk_dialog_set_default_response(GTK_DIALOG(save_to_file_w),
       	                                GTK_RESPONSE_ACCEPT);

	pathname = file_selection_run(save_to_file_w);
	if (pathname == NULL) {
		/* User cancelled or closed the dialog. */
		return NULL;
	}

	/* We've crosed the Rubicon; get rid of the dialog box. */
	window_destroy(save_to_file_w);

	return pathname;
}

/****************************************************************************/
static void
on_save_bt_clicked                    (GtkWidget       *button _U_,
				       graph_analysis_data_t *user_data)
{
	char *pathname;

	/*
	 * Loop until the user either selects a file or gives up.
	 */
	for (;;) {
		pathname = gtk_save_graph_as_plain_text_file(user_data);
		if (pathname == NULL) {
			/* User gave up. */
			break;
		}
		if (dialog_graph_dump_to_file(pathname, user_data)) {
			/* We succeeded. */
			g_free(pathname);
			break;
		}
		/* Dump failed; let the user select another file
		   or give up. */
		g_free(pathname);
	}
}

/****************************************************************************/
static void dialog_graph_draw(graph_analysis_data_t *user_data)
{
	guint32 i, last_item, first_item, display_items;
	guint32 start_arrow, end_arrow, label_x, src_port_x, dst_port_x, arrow_width;
	guint32 current_item;
	guint32 left_x_border;
	guint32 right_x_border;
	guint32 top_y_border;
	guint32 bottom_y_border;
	graph_analysis_item_t *gai;

	PangoLayout *layout;
	PangoLayout *middle_layout;
	PangoLayout *small_layout;
	PangoFontDescription *middle_font_desc;
	gint middle_font_size;
	PangoFontDescription *small_font_desc;
	gint small_font_size;

	gint     label_width, label_height;
	guint32  draw_width, draw_height;
	char     label_string[MAX_COMMENT];
	GList   *list;
	cairo_t *cr;
	gchar	*time_str;

#if 0
	GdkColor *color_p, *bg_color_p;
	GdkColor black_color = {0, 0, 0, 0};
	GdkColor white_color = {0, 0xffff, 0xffff, 0xffff};
	/* gray and soft gray colors */
	GdkColor grey_color0 = {0, 0x64ff, 0x64ff, 0x64ff};
	GdkColor grey_color1 = {0, 0x25ff, 0x25ff, 0x25ff};

	/* the first color is blue to highlight the selected item */
	static GdkColor background_color[MAX_NUM_COL_CONV+1] = {
		{0,     0x00FF, 0x00FF, 0xFFFF},
		{0,     0x90FF, 0xEEFF, 0x90FF},
		{0,     0xFFFF, 0xA0FF, 0x7AFF},
		{0,     0xFFFF, 0xB6FF, 0xC1FF},
		{0,     0xFAFF, 0xFAFF, 0xD2FF},
		{0,     0xFFFF, 0xFFFF, 0x33FF},
		{0,     0x66FF, 0xCDFF, 0xAAFF},
		{0,     0xE0FF, 0xFFFF, 0xFFFF},
		{0,     0xB0FF, 0xC4FF, 0xDEFF},
		{0,     0x87FF, 0xCEFF, 0xFAFF},
		{0,     0xD3FF, 0xD3FF, 0xD3FF}
	};

#endif
	GdkRGBA *color_p, *bg_color_p;

	GdkRGBA black_color =  {0.0, 0.0, 0.0, 1.0}; /* Black */
	GdkRGBA white_color =  {1.0, 1.0, 1.0, 1.0 };
	/* gray and soft gray colors */
	GdkRGBA grey_color0 = {0.3945, 0.3945, 0.3945, 1.0};
	GdkRGBA grey_color1 = {0.1484, 0.1484, 0.1484, 1.0};

	static GdkRGBA background_color[MAX_NUM_COL_CONV+1] = {
		/* Red, Green, Blue Alpha */
		{0.0039, 0.0039, 1.0000, 1.0},
		{0.5664, 0.9336, 0.5664, 1.0},
		{1.0000, 0.6289, 0.4805, 1.0},
		{1.0000, 0.7148, 0.7578, 1.0},
		{0.9805, 0.9805, 0.8242, 1.0},
		{1.0000, 1.0000, 0.2031, 1.0},
		{0.4023, 0.8046, 0.6680, 1.0},
		{0.8789, 1.0000, 1.0000, 1.0},
		{0.6914, 0.7695, 0.8710, 1.0},
		{0.5312, 0.8086, 0.9957, 1.0},
		{0.8281, 0.8281, 0.8281, 1.0}
	};

	/* XXX can't we just set the background color ? */
	GdkPixbuf *bg_pixbuf;

	/* Dashed line pattern */
	static const double dashed1[] = {5.0, 4.0};
	static int len1  = sizeof(dashed1) / sizeof(dashed1[0]);

	GtkAllocation draw_area_time_alloc, draw_area_alloc, draw_area_comments_alloc;

	if(!user_data->dlg.needs_redraw) {
		return;
	}

	bg_pixbuf =  gdk_pixbuf_new_from_xpm_data(voip_bg_xpm);
	time_str = (gchar *)g_malloc(COL_MAX_LEN);
	user_data->dlg.needs_redraw = FALSE;

	gtk_widget_get_allocation(user_data->dlg.draw_area_time, &draw_area_time_alloc);
	gtk_widget_get_allocation(user_data->dlg.draw_area, &draw_area_alloc);
	gtk_widget_get_allocation(user_data->dlg.draw_area_comments, &draw_area_comments_alloc);

	/* Clear out old plot */
#if GTK_CHECK_VERSION(2,22,0)
	cr = cairo_create (user_data->dlg.surface_time);
	cairo_set_source_rgb (cr, 1, 1, 1);
	cairo_rectangle (cr, 0, 0, draw_area_time_alloc.width, draw_area_time_alloc.height);
	cairo_fill (cr);
	cairo_destroy (cr);

	cr = cairo_create (user_data->dlg.surface_main);
	cairo_set_source_rgb (cr, 1, 1, 1);
	cairo_rectangle (cr, 0, 0, draw_area_alloc.width, draw_area_alloc.height);
	cairo_fill (cr);
	cairo_destroy (cr);

	cr = cairo_create (user_data->dlg.surface_comments);
	cairo_set_source_rgb (cr, 1, 1, 1);
	cairo_rectangle (cr, 0, 0, draw_area_comments_alloc.width, draw_area_comments_alloc.height);
	cairo_fill (cr);
	cairo_destroy (cr);

#else
	if ( GDK_IS_DRAWABLE(user_data->dlg.pixmap_time) ) {
		cr = gdk_cairo_create (user_data->dlg.pixmap_time);
		cairo_set_source_rgb (cr, 1, 1, 1);
		cairo_rectangle (cr, 0, 0, draw_area_time_alloc.width, draw_area_time_alloc.height);
		cairo_fill (cr);
		cairo_destroy (cr);
	}

	if ( GDK_IS_DRAWABLE(user_data->dlg.pixmap_main) ) {
		cr = gdk_cairo_create (user_data->dlg.pixmap_main);
		cairo_set_source_rgb (cr, 1, 1, 1);
		cairo_rectangle (cr, 0, 0, draw_area_alloc.width, draw_area_alloc.height);
		cairo_fill (cr);
		cairo_destroy (cr);
	}

	if ( GDK_IS_DRAWABLE(user_data->dlg.pixmap_comments) ) {
		cr = gdk_cairo_create (user_data->dlg.pixmap_comments);
		cairo_set_source_rgb (cr, 1, 1, 1);
		cairo_rectangle (cr, 0, 0, draw_area_comments_alloc.width, draw_area_comments_alloc.height);
		cairo_fill (cr);
		cairo_destroy (cr);
	}
#endif
	/* Calculate the y border */
	top_y_border = TOP_Y_BORDER;	/* to display the node address */
	bottom_y_border = BOTTOM_Y_BORDER;

	draw_height = draw_area_alloc.height-top_y_border-bottom_y_border;

	first_item = user_data->dlg.first_item;
	display_items = draw_height/ITEM_HEIGHT;

	/* get the items to display and fill the matrix array */
	list = g_list_first(user_data->graph_info->list);
	current_item = 0;
	i = 0;
	while (list)
	{
		gai = (graph_analysis_item_t *)list->data;
		if (gai->display) {
			if (current_item>=display_items) break;		/* the item is outside the display */
			if (i>=first_item) {
				user_data->dlg.items[current_item].fd = gai->fd;
				user_data->dlg.items[current_item].port_src = gai->port_src;
				user_data->dlg.items[current_item].port_dst = gai->port_dst;
				/* Add "..." if the length is 50 characters */
				if (strlen(gai->frame_label) > 48) {
					gai->frame_label[48] = '.';
					gai->frame_label[47] = '.';
					gai->frame_label[46] = '.';
				}
				user_data->dlg.items[current_item].frame_label = gai->frame_label;
				user_data->dlg.items[current_item].comment = gai->comment;
				user_data->dlg.items[current_item].conv_num = gai->conv_num;

				user_data->dlg.items[current_item].src_node = gai->src_node;
				user_data->dlg.items[current_item].dst_node = gai->dst_node;
				user_data->dlg.items[current_item].line_style = gai->line_style;
				current_item++;
			}
			i++;
		}

		list = g_list_next(list);
	}
	/* in case the window is resized we might have to move the top item */
	if ((first_item + display_items) > user_data->num_items) {
		if (display_items>user_data->num_items)
			first_item = 0;
		else
			first_item = user_data->num_items - display_items;
	}

	/* in case there are less items than possible displayed */
	display_items = current_item;
	last_item = first_item+display_items-1;

	/* if no items to display */
	if (display_items == 0)	{
		g_free(time_str);
		return;
	}


	/* Calculate the x borders */
	/* We use time from the last display item to calcultate the x left border */
#if 0
	g_snprintf(label_string, MAX_LABEL, "%.3f", nstime_to_sec(&user_data->dlg.items[display_items-1].fd->rel_ts));
#endif
	/* Write the time, using the same format as in th etime col */
	set_fd_time(user_data->dlg.items[display_items-1].fd, time_str);
	g_snprintf(label_string, MAX_LABEL, "%s", time_str);
	layout = gtk_widget_create_pango_layout(user_data->dlg.draw_area_time, label_string);
	middle_layout = gtk_widget_create_pango_layout(user_data->dlg.draw_area_time, label_string);
	small_layout = gtk_widget_create_pango_layout(user_data->dlg.draw_area_time, label_string);

	middle_font_desc = pango_font_description_copy(pango_context_get_font_description(pango_layout_get_context(middle_layout)));
	middle_font_size = pango_font_description_get_size(middle_font_desc);
	pango_font_description_set_size(middle_font_desc, (gint)(middle_font_size*0.8));
	pango_layout_set_font_description(middle_layout, middle_font_desc);

	small_font_desc = pango_font_description_copy(pango_context_get_font_description(pango_layout_get_context(small_layout)));
	small_font_size = pango_font_description_get_size(small_font_desc);
	pango_font_description_set_size(small_font_desc, (gint)(small_font_size*0.7));
	pango_layout_set_font_description(small_layout, small_font_desc);

	pango_layout_get_pixel_size(layout, &label_width, &label_height);

	/* resize the "time" draw area */
	left_x_border = 0;
	user_data->dlg.left_x_border = left_x_border;

	right_x_border = 0;
	draw_width = user_data->dlg.surface_width-right_x_border-left_x_border;

#if GTK_CHECK_VERSION(2,22,0)
	/* Paint time title background */
	cr = cairo_create (user_data->dlg.surface_time);
	gdk_cairo_set_source_pixbuf (cr, bg_pixbuf, 0, 0);
	cairo_pattern_set_extend (cairo_get_source (cr), CAIRO_EXTEND_REPEAT);
	cairo_rectangle (cr, 0, 0, draw_area_time_alloc.width, top_y_border);
	cairo_fill (cr);
	cairo_destroy (cr);

	/* Paint main title background */
	cr = cairo_create (user_data->dlg.surface_main);
	gdk_cairo_set_source_pixbuf (cr, bg_pixbuf, 0, 0);
	cairo_pattern_set_extend (cairo_get_source (cr), CAIRO_EXTEND_REPEAT);
	cairo_rectangle (cr, 0, 0, draw_area_alloc.width, top_y_border);
	cairo_fill (cr);
	cairo_destroy (cr);

	/* Paint main comment background */
	cr = cairo_create (user_data->dlg.surface_comments);
	gdk_cairo_set_source_pixbuf (cr, bg_pixbuf, 0, 0);
	cairo_pattern_set_extend (cairo_get_source (cr), CAIRO_EXTEND_REPEAT);
	cairo_rectangle (cr, 0, 0, draw_area_comments_alloc.width, top_y_border);
	cairo_fill (cr);
	cairo_destroy (cr);
#else
	/* Paint time title background */
	if ( GDK_IS_DRAWABLE(user_data->dlg.pixmap_time) ) {
		cr = gdk_cairo_create (user_data->dlg.pixmap_time);
		gdk_cairo_set_source_pixbuf (cr, bg_pixbuf, 0, 0);
		cairo_pattern_set_extend (cairo_get_source (cr), CAIRO_EXTEND_REPEAT);
		cairo_rectangle (cr, 0, 0, draw_area_time_alloc.width, top_y_border);
		cairo_fill (cr);
		cairo_destroy (cr);

	}
	/* Paint main title background */
	if ( GDK_IS_DRAWABLE(user_data->dlg.pixmap_main) ) {
		cr = gdk_cairo_create (user_data->dlg.pixmap_main);
		gdk_cairo_set_source_pixbuf (cr, bg_pixbuf, 0, 0);
		cairo_pattern_set_extend (cairo_get_source (cr), CAIRO_EXTEND_REPEAT);
		cairo_rectangle (cr, 0, 0, draw_area_alloc.width, top_y_border);
		cairo_fill (cr);
		cairo_destroy (cr);
	}

	/* Paint main comment background */
	if ( GDK_IS_DRAWABLE(user_data->dlg.pixmap_comments) ) {
		cr = gdk_cairo_create (user_data->dlg.pixmap_comments);
		gdk_cairo_set_source_pixbuf (cr, bg_pixbuf, 0, 0);
		cairo_pattern_set_extend (cairo_get_source (cr), CAIRO_EXTEND_REPEAT);
		cairo_rectangle (cr, 0, 0, draw_area_comments_alloc.width, top_y_border);
		cairo_fill (cr);
		cairo_destroy (cr);
	}

#endif
	/* Draw the word "Time" on top of time column */
	g_snprintf(label_string, label_width, "%s", "  Time");
	pango_layout_set_text(layout, label_string, -1);
	pango_layout_get_pixel_size(layout, &label_width, &label_height);
#if GTK_CHECK_VERSION(2,22,0)
	cr = cairo_create (user_data->dlg.surface_time);
	cairo_move_to (cr, left_x_border, top_y_border/2-label_height/2);
	pango_cairo_show_layout (cr, layout);
	cairo_destroy (cr);
	cr = NULL;
#else
	if (GDK_IS_DRAWABLE(user_data->dlg.pixmap_time)) {
		cr = gdk_cairo_create (user_data->dlg.pixmap_time);
		cairo_move_to (cr, left_x_border, top_y_border/2-label_height/2);
		pango_cairo_show_layout (cr, layout);
		cairo_destroy (cr);
		cr = NULL;
	}
#endif
	/* Draw the word "Comment" on top of comment column */
	g_snprintf(label_string, label_width, "%s", "Comment");
	pango_layout_set_text(layout, label_string, -1);
	pango_layout_get_pixel_size(layout, &label_width, &label_height);
#if GTK_CHECK_VERSION(2,22,0)
	cr = cairo_create (user_data->dlg.surface_comments);
	cairo_move_to (cr, MAX_COMMENT/2-label_width/2, top_y_border/2-((i&1)?0:label_height));
	pango_cairo_show_layout (cr, layout);
	cairo_destroy (cr);
	cr = NULL;
#endif
	/* Paint the background items */
	for (current_item=0; current_item<display_items; current_item++) {
		/*select the color. if it is the selected item select blue color */
		if ( current_item+first_item == user_data->dlg.selected_item ) {
			bg_color_p = &background_color[0]; /* blue */
		} else {
			bg_color_p = &background_color[1+user_data->dlg.items[current_item].conv_num%MAX_NUM_COL_CONV];
		}
#if GTK_CHECK_VERSION(2,22,0)
		/* Paint background */
		cr = cairo_create (user_data->dlg.surface_main);
		gdk_cairo_set_source_rgba (cr, bg_color_p);
		cairo_rectangle (cr, left_x_border, top_y_border+current_item*ITEM_HEIGHT, draw_width, ITEM_HEIGHT);
		cairo_fill (cr);
		cairo_destroy (cr);
#else
		if (GDK_IS_DRAWABLE(user_data->dlg.pixmap_main)) {
			/* Paint background */
			cr = gdk_cairo_create (user_data->dlg.pixmap_main);
			gdk_cairo_set_source_rgba (cr, bg_color_p);
			cairo_rectangle (cr, left_x_border, top_y_border+current_item*ITEM_HEIGHT, draw_width, ITEM_HEIGHT);
			cairo_fill (cr);
			cairo_destroy (cr);
		}
#endif
	}
	/* Draw the node names on top and the division lines */
	for (i=0; i<user_data->num_nodes; i++) {
		/* print the node identifiers */
		/* XXX we assign 5 pixels per character in the node identity */
		g_strlcpy(label_string, get_addr_name(&(user_data->nodes[i])), NODE_WIDTH/5);
		pango_layout_set_text(layout, label_string, -1);
		pango_layout_get_pixel_size(layout, &label_width, &label_height);
#if GTK_CHECK_VERSION(2,22,0)
		cr = cairo_create (user_data->dlg.surface_main);
		cairo_move_to (cr, left_x_border+NODE_WIDTH/2-label_width/2+NODE_WIDTH*i, top_y_border/2-((i&1)?0:label_height));
		pango_cairo_show_layout (cr, layout);
		cairo_destroy (cr);
		cr = NULL;
#else
		if (GDK_IS_DRAWABLE(user_data->dlg.pixmap_main)) {
			cr = gdk_cairo_create (user_data->dlg.pixmap_main);
			cairo_move_to (cr, left_x_border+NODE_WIDTH/2-label_width/2+NODE_WIDTH*i, top_y_border/2-((i&1)?0:label_height));
			pango_cairo_show_layout (cr, layout);
			cairo_destroy (cr);
			cr = NULL;
		}
#endif
#if GTK_CHECK_VERSION(2,22,0)
		/* draw the node division lines */
		cr = cairo_create (user_data->dlg.surface_main);
		gdk_cairo_set_source_rgba (cr, &grey_color0);
		cairo_set_line_width (cr, 1.0);
		cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
		cairo_set_dash(cr, dashed1, len1, 0);
		cairo_move_to(cr, left_x_border+NODE_WIDTH/2+NODE_WIDTH*i, top_y_border);
		cairo_line_to(cr, (left_x_border+NODE_WIDTH/2+NODE_WIDTH*i), draw_area_alloc.height-bottom_y_border);
		cairo_stroke(cr);
		cairo_destroy(cr);
#else
		/* draw the node division lines */
		if (GDK_IS_DRAWABLE(user_data->dlg.pixmap_main) ) {
			cr = gdk_cairo_create (user_data->dlg.pixmap_main);
			gdk_cairo_set_source_rgba (cr, &grey_color0);
			cairo_set_line_width (cr, 1.0);
			cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
			cairo_set_dash(cr, dashed1, len1, 0);
			cairo_move_to(cr, left_x_border+NODE_WIDTH/2+NODE_WIDTH*i, top_y_border);
			cairo_line_to(cr, (left_x_border+NODE_WIDTH/2+NODE_WIDTH*i), draw_area_alloc.height-bottom_y_border);
			cairo_stroke(cr);
			cairo_destroy(cr);
		}
#endif
	}

	/* Draw the items */
	for (current_item=0; current_item<display_items; current_item++) {
#if 0
		/* Draw the time */
		g_snprintf(label_string, MAX_LABEL, "%.3f", nstime_to_sec(&user_data->dlg.items[current_item].fd->rel_ts));
#endif
		/* Draw the time */
		set_fd_time(user_data->dlg.items[current_item].fd, time_str);
		g_snprintf(label_string, MAX_LABEL, "%s", time_str);
		pango_layout_set_text(layout, label_string, -1);
		pango_layout_get_pixel_size(layout, &label_width, &label_height);
#if GTK_CHECK_VERSION(2,22,0)
		cr = cairo_create (user_data->dlg.surface_time);
		cairo_move_to (cr, 3, top_y_border+current_item*ITEM_HEIGHT+ITEM_HEIGHT/2-label_height/2);
		pango_cairo_show_layout (cr, layout);
		cairo_destroy (cr);
		cr = NULL;
#else
		if (GDK_IS_DRAWABLE(user_data->dlg.pixmap_time)) {
			cr = gdk_cairo_create (user_data->dlg.pixmap_time);
			cairo_move_to (cr, 3, top_y_border+current_item*ITEM_HEIGHT+ITEM_HEIGHT/2-label_height/2);
			pango_cairo_show_layout (cr, layout);
			cairo_destroy (cr);
			cr = NULL;
		}
#endif
		/*draw the comments */
		g_snprintf(label_string, MAX_COMMENT, "%s", user_data->dlg.items[current_item].comment);
		pango_layout_set_text(middle_layout, label_string, -1);
		pango_layout_get_pixel_size(middle_layout, &label_width, &label_height);
#if GTK_CHECK_VERSION(2,22,0)
		cr = cairo_create (user_data->dlg.surface_comments);
		cairo_move_to (cr, 2, top_y_border+current_item*ITEM_HEIGHT+ITEM_HEIGHT/2-label_height/2);
		pango_cairo_show_layout (cr, middle_layout);
		cairo_destroy (cr);
		cr = NULL;
#else
		if (GDK_IS_DRAWABLE(user_data->dlg.pixmap_comments)) {
			cr = gdk_cairo_create (user_data->dlg.pixmap_comments);
			cairo_move_to (cr, 2, top_y_border+current_item*ITEM_HEIGHT+ITEM_HEIGHT/2-label_height/2);
			pango_cairo_show_layout (cr, middle_layout);
			cairo_destroy (cr);
			cr = NULL;
		}
#endif
		/* draw the arrow line */
		start_arrow = left_x_border+(user_data->dlg.items[current_item].src_node)*NODE_WIDTH+NODE_WIDTH/2;
		end_arrow = left_x_border+(user_data->dlg.items[current_item].dst_node)*NODE_WIDTH+NODE_WIDTH/2;

#if GTK_CHECK_VERSION(2,22,0)
		cr = cairo_create (user_data->dlg.surface_main);
		if (user_data->dlg.items[current_item].line_style == 2) {
			/* draw a line thick */
			cairo_set_line_width (cr, 2.0);
		}else{
			cairo_set_line_width (cr, 1.0);
		}
		if ( current_item+first_item == user_data->dlg.selected_item ) {
			/* draw white line */
			cairo_set_source_rgb (cr, 1, 1, 1);
		}else{
			/* draw black line */
			cairo_set_source_rgb (cr, 0, 0, 0);
		}
		cairo_move_to(cr, start_arrow, (top_y_border+current_item*ITEM_HEIGHT+ITEM_HEIGHT-7)+0.5);
		cairo_line_to(cr, end_arrow, (top_y_border+current_item*ITEM_HEIGHT+ITEM_HEIGHT-7)+0.5);
		cairo_stroke(cr);
		cairo_destroy(cr);
#else
		if (GDK_IS_DRAWABLE(user_data->dlg.pixmap_main) ) {
			cr = gdk_cairo_create (user_data->dlg.pixmap_main);
			if (user_data->dlg.items[current_item].line_style == 2) {
				/* draw a line thick */
				cairo_set_line_width (cr, 2.0);
			}else{
				cairo_set_line_width (cr, 1.0);
			}
			if ( current_item+first_item == user_data->dlg.selected_item ) {
				/* draw white line */
				cairo_set_source_rgb (cr, 1, 1, 1);
			}else{
				/* draw black line */
				cairo_set_source_rgb (cr, 0, 0, 0);
			}
			cairo_move_to(cr, start_arrow, (top_y_border+current_item*ITEM_HEIGHT+ITEM_HEIGHT-7)+0.5);
			cairo_line_to(cr, end_arrow, (top_y_border+current_item*ITEM_HEIGHT+ITEM_HEIGHT-7)+0.5);
			cairo_stroke(cr);
			cairo_destroy(cr);
		}
#endif
		/* select colors */
		if ( current_item+first_item == user_data->dlg.selected_item ) {
			color_p = &white_color;
		} else {
			color_p = &black_color;
		}
		/* draw the arrow */
#if GTK_CHECK_VERSION(2,22,0)
		if (start_arrow<end_arrow)
			draw_arrow(user_data->dlg.surface_main, color_p, end_arrow-WIDTH_ARROW, (top_y_border+current_item*ITEM_HEIGHT+ITEM_HEIGHT-7)-(HEIGHT_ARROW/2), RIGHT_ARROW);
		else
			draw_arrow(user_data->dlg.surface_main, color_p, end_arrow, top_y_border+current_item*ITEM_HEIGHT+ITEM_HEIGHT-7-(HEIGHT_ARROW/2), LEFT_ARROW);
#else
		if (start_arrow<end_arrow)
			draw_arrow(user_data->dlg.pixmap_main, color_p, end_arrow-WIDTH_ARROW, (top_y_border+current_item*ITEM_HEIGHT+ITEM_HEIGHT-7)-(HEIGHT_ARROW/2), RIGHT_ARROW);
		else
			draw_arrow(user_data->dlg.pixmap_main, color_p, end_arrow, top_y_border+current_item*ITEM_HEIGHT+ITEM_HEIGHT-7-(HEIGHT_ARROW/2), LEFT_ARROW);
#endif
		/* draw the frame comment */
		g_snprintf(label_string, MAX_LABEL, "%s", user_data->dlg.items[current_item].frame_label);
		pango_layout_set_text(layout, label_string, -1);
		pango_layout_get_pixel_size(layout, &label_width, &label_height);
		if (start_arrow<end_arrow) {
			arrow_width = end_arrow-start_arrow;
			label_x = arrow_width/2+start_arrow;
		}
		else {
			arrow_width = start_arrow-end_arrow;
			label_x = arrow_width/2+end_arrow;
		}

		if ((int)left_x_border > ((int)label_x-(int)label_width/2))
			label_x = left_x_border + label_width/2;

#if GTK_CHECK_VERSION(2,22,0)
		cr = cairo_create (user_data->dlg.surface_main);
		gdk_cairo_set_source_rgba (cr, color_p);
		cairo_move_to (cr, label_x - label_width/2, top_y_border+current_item*ITEM_HEIGHT+ITEM_HEIGHT/2-label_height/2-3);
		pango_cairo_show_layout (cr, layout);
		cairo_destroy (cr);
		cr = NULL;
#else
		if (GDK_IS_DRAWABLE(user_data->dlg.pixmap_main)) {
			cr = gdk_cairo_create (user_data->dlg.pixmap_main);
			gdk_cairo_set_source_rgba (cr, color_p);
			cairo_move_to (cr, label_x - label_width/2, top_y_border+current_item*ITEM_HEIGHT+ITEM_HEIGHT/2-label_height/2-3);
			pango_cairo_show_layout (cr, layout);
			cairo_destroy (cr);
			cr = NULL;
		}
#endif
		/* draw the source port number */
		g_snprintf(label_string, MAX_LABEL, "(%i)", user_data->dlg.items[current_item].port_src);
		pango_layout_set_text(small_layout, label_string, -1);
		pango_layout_get_pixel_size(small_layout, &label_width, &label_height);
		if (start_arrow<end_arrow) {
			src_port_x = start_arrow - label_width - 2;
		}
		else {
			src_port_x = start_arrow + 2;
		}
#if GTK_CHECK_VERSION(2,22,0)
		cr = cairo_create (user_data->dlg.surface_main);
		/* select color */
		if ( current_item+first_item == user_data->dlg.selected_item ) {
			gdk_cairo_set_source_rgba (cr, &grey_color1);
		} else {
			gdk_cairo_set_source_rgba (cr, &grey_color0);
		}
		gdk_cairo_set_source_rgba (cr, &grey_color0);
		cairo_move_to (cr, src_port_x, top_y_border+current_item*ITEM_HEIGHT+ITEM_HEIGHT-2-label_height/2-2);
		pango_cairo_show_layout (cr, small_layout);
		cairo_destroy (cr);
#else
		if (GDK_IS_DRAWABLE(user_data->dlg.pixmap_main)) {
			cr = gdk_cairo_create (user_data->dlg.pixmap_main);
			/* select color */
			if ( current_item+first_item == user_data->dlg.selected_item ) {
				gdk_cairo_set_source_rgba (cr, &grey_color1);
			} else {
				gdk_cairo_set_source_rgba (cr, &grey_color0);
			}
			gdk_cairo_set_source_rgba (cr, &grey_color0);
			cairo_move_to (cr, src_port_x, top_y_border+current_item*ITEM_HEIGHT+ITEM_HEIGHT-2-label_height/2-2);
			pango_cairo_show_layout (cr, small_layout);
			cairo_destroy (cr);
		}
#endif
		/* draw the destination port number */
		g_snprintf(label_string, MAX_LABEL, "(%i)", user_data->dlg.items[current_item].port_dst);
		pango_layout_set_text(small_layout, label_string, -1);
		pango_layout_get_pixel_size(small_layout, &label_width, &label_height);
		if (start_arrow<end_arrow) {
			dst_port_x = end_arrow + 2;
		}
		else {
			dst_port_x = end_arrow - label_width - 2;
		}
#if GTK_CHECK_VERSION(2,22,0)
		cr = cairo_create (user_data->dlg.surface_main);
		/* select color */
		if ( current_item+first_item == user_data->dlg.selected_item ) {
			gdk_cairo_set_source_rgba (cr, &grey_color1);
		} else {
			gdk_cairo_set_source_rgba (cr, &grey_color0);
		}
		cairo_move_to (cr, dst_port_x, top_y_border+current_item*ITEM_HEIGHT+ITEM_HEIGHT-2-label_height/2-2);
		pango_cairo_show_layout (cr, small_layout);
		cairo_destroy (cr);
#else
		if (GDK_IS_DRAWABLE(user_data->dlg.pixmap_main)) {
			cr = gdk_cairo_create (user_data->dlg.pixmap_main);
			/* select color */
			if ( current_item+first_item == user_data->dlg.selected_item ) {
				gdk_cairo_set_source_rgba (cr, &grey_color1);
			} else {
				gdk_cairo_set_source_rgba (cr, &grey_color0);
			}
			cairo_move_to (cr, dst_port_x, top_y_border+current_item*ITEM_HEIGHT+ITEM_HEIGHT-2-label_height/2-2);
			pango_cairo_show_layout (cr, small_layout);
			cairo_destroy (cr);
		}
#endif
		/* draw the div line of the selected item with soft gray*/
		if ( current_item+first_item == user_data->dlg.selected_item )
			for (i=0; i<user_data->num_nodes; i++) {
#if GTK_CHECK_VERSION(2,22,0)
				cr = cairo_create (user_data->dlg.surface_main);
				gdk_cairo_set_source_rgba (cr, &grey_color1);
				cairo_set_line_width (cr, 1.0);
				cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
				cairo_set_dash(cr, dashed1, len1, 0);
				cairo_move_to(cr, left_x_border+NODE_WIDTH/2+NODE_WIDTH*i, (user_data->dlg.selected_item-first_item)*ITEM_HEIGHT+TOP_Y_BORDER);
				cairo_line_to(cr, (left_x_border+NODE_WIDTH/2+NODE_WIDTH*i), (user_data->dlg.selected_item-first_item)*ITEM_HEIGHT+TOP_Y_BORDER+ITEM_HEIGHT);
				cairo_stroke(cr);
				cairo_destroy(cr);
#else
				if (GDK_IS_DRAWABLE(user_data->dlg.pixmap_main) ) {
					cr = gdk_cairo_create (user_data->dlg.pixmap_main);
					gdk_cairo_set_source_rgba (cr, &grey_color1);
					cairo_set_line_width (cr, 1.0);
					cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
					cairo_set_dash(cr, dashed1, len1, 0);
					cairo_move_to(cr, left_x_border+NODE_WIDTH/2+NODE_WIDTH*i, (user_data->dlg.selected_item-first_item)*ITEM_HEIGHT+TOP_Y_BORDER);
					cairo_line_to(cr, (left_x_border+NODE_WIDTH/2+NODE_WIDTH*i), (user_data->dlg.selected_item-first_item)*ITEM_HEIGHT+TOP_Y_BORDER+ITEM_HEIGHT);
					cairo_stroke(cr);
					cairo_destroy(cr);
				}
#endif
			}
	}

	g_object_unref(G_OBJECT(layout));
	g_free(time_str);

	/* refresh the draw areas */
	if (gtk_widget_is_drawable(user_data->dlg.draw_area_time) ) {
		cr = gdk_cairo_create (gtk_widget_get_window(user_data->dlg.draw_area_time));
#if GTK_CHECK_VERSION(2,22,0)
		cairo_set_source_surface (cr, user_data->dlg.surface_time, 0, 0);
#else
		gdk_cairo_set_source_pixmap (cr, user_data->dlg.pixmap_time, 0, 0);
#endif
		cairo_rectangle (cr, 0, 0, draw_area_time_alloc.width, draw_area_time_alloc.height);
		cairo_fill (cr);
		cairo_destroy (cr);
		cr = NULL;
	}

	if (gtk_widget_is_drawable(user_data->dlg.draw_area) ) {
		cr = gdk_cairo_create (gtk_widget_get_window(user_data->dlg.draw_area));
#if GTK_CHECK_VERSION(2,22,0)
		cairo_set_source_surface (cr, user_data->dlg.surface_main, 0, 0);
#else
		gdk_cairo_set_source_pixmap (cr, user_data->dlg.pixmap_main, 0, 0);
#endif
		cairo_rectangle (cr, 0, 0, draw_area_alloc.width, draw_area_alloc.height);
		cairo_fill (cr);
		cairo_destroy (cr);
		cr = NULL;
	}

	if (gtk_widget_is_drawable(user_data->dlg.draw_area_comments) ) {
		cr = gdk_cairo_create (gtk_widget_get_window(user_data->dlg.draw_area_comments));
#if GTK_CHECK_VERSION(2,22,0)
		cairo_set_source_surface (cr, user_data->dlg.surface_comments, 0, 0);
#else
		gdk_cairo_set_source_pixmap (cr, user_data->dlg.pixmap_comments, 0, 0);
#endif
		cairo_rectangle (cr, 0, 0, draw_area_comments_alloc.width, draw_area_comments_alloc.height);
		cairo_fill (cr);
		cairo_destroy (cr);
		cr = NULL;
	}

	/* update the v_scrollbar */
	gtk_adjustment_set_upper(user_data->dlg.v_scrollbar_adjustment, (gdouble) user_data->num_items-1);
	gtk_adjustment_set_step_increment(user_data->dlg.v_scrollbar_adjustment, 1);
	gtk_adjustment_set_page_increment(user_data->dlg.v_scrollbar_adjustment, (gdouble) (last_item-first_item));
	gtk_adjustment_set_page_size(user_data->dlg.v_scrollbar_adjustment, (gdouble) (last_item-first_item));
	gtk_adjustment_set_value(user_data->dlg.v_scrollbar_adjustment, (gdouble) first_item);

	gtk_adjustment_changed(user_data->dlg.v_scrollbar_adjustment);
	gtk_adjustment_value_changed(user_data->dlg.v_scrollbar_adjustment);
}

/****************************************************************************/
static void dialog_graph_redraw(graph_analysis_data_t *user_data)
{
	user_data->dlg.needs_redraw = TRUE;
	dialog_graph_draw(user_data);
}

/****************************************************************************/
static gboolean button_press_event(GtkWidget *widget _U_, GdkEventButton *event, gpointer data)
{
	graph_analysis_data_t *user_data = (graph_analysis_data_t *)data;
	guint32 item;

	if (event->type != GDK_BUTTON_PRESS) return TRUE;

	if (event->y<TOP_Y_BORDER) return TRUE;

	/* get the item clicked */
	item = ((guint32)event->y - TOP_Y_BORDER) / ITEM_HEIGHT;
	if (item >= user_data->num_items) return TRUE;
	user_data->dlg.selected_item = item + user_data->dlg.first_item;

	user_data->dlg.needs_redraw = TRUE;
	dialog_graph_draw(user_data);

	cf_goto_frame(&cfile, user_data->dlg.items[item].fd->num);

	return TRUE;
}



/****************************************************************************/
static gboolean key_press_event(GtkWidget *widget _U_, GdkEventKey *event, gpointer data)
{
	graph_analysis_data_t *user_data = (graph_analysis_data_t *)data;

	/* if there is nothing selected, just return */
	if (user_data->dlg.selected_item == 0xFFFFFFFF) return TRUE;

	/* Up arrow */
	if (event->keyval == GDK_Up) {
		if (user_data->dlg.selected_item == 0) return TRUE;
		user_data->dlg.selected_item--;
		if ( (user_data->dlg.selected_item<user_data->dlg.first_item) || (user_data->dlg.selected_item>user_data->dlg.first_item+gtk_adjustment_get_page_size(user_data->dlg.v_scrollbar_adjustment)) )
			user_data->dlg.first_item = user_data->dlg.selected_item;
		/* Down arrow */
	} else if (event->keyval == GDK_Down) {
		if (user_data->dlg.selected_item == user_data->num_items-1) return TRUE;
		user_data->dlg.selected_item++;
		if ( (user_data->dlg.selected_item<user_data->dlg.first_item) || (user_data->dlg.selected_item>user_data->dlg.first_item+gtk_adjustment_get_page_size(user_data->dlg.v_scrollbar_adjustment)) )
			user_data->dlg.first_item = (guint32)user_data->dlg.selected_item-(guint32)gtk_adjustment_get_page_size(user_data->dlg.v_scrollbar_adjustment);
	} else if (event->keyval == GDK_Left) {
		if (user_data->dlg.first_node == 0) return TRUE;
		user_data->dlg.first_node--;
	} else return TRUE;

	user_data->dlg.needs_redraw = TRUE;
	dialog_graph_draw(user_data);

	cf_goto_frame(&cfile, user_data->dlg.items[user_data->dlg.selected_item-user_data->dlg.first_item].fd->num);

	return TRUE;
}



/****************************************************************************/
static gboolean draw_area_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
	graph_analysis_data_t *user_data = (graph_analysis_data_t *)data;
	GtkAllocation allocation;

	gtk_widget_get_allocation (widget, &allocation);
#if GTK_CHECK_VERSION(2,22,0)
	cairo_set_source_surface (cr, user_data->dlg.surface_main, 0, 0);
#else
	gdk_cairo_set_source_pixmap (cr, user_data->dlg.pixmap_main, 0, 0);
#endif
	cairo_rectangle (cr, 0, 0, allocation.width, allocation.height);
	cairo_fill (cr);


	return FALSE;
}
#if !GTK_CHECK_VERSION(3,0,0)
/****************************************************************************/
static gboolean expose_event(GtkWidget *widget, GdkEventExpose *event _U_, gpointer data)
{
	GtkAllocation allocation;
	graph_analysis_data_t *user_data = (graph_analysis_data_t *)data;
	cairo_t *cr = gdk_cairo_create (gtk_widget_get_window(widget));


	if (gtk_widget_is_drawable(widget)) {
		gtk_widget_get_allocation (widget, &allocation);
#if GTK_CHECK_VERSION(2,22,0)
		cairo_set_source_surface (cr, user_data->dlg.surface_main, 0, 0);
#else
		gdk_cairo_set_source_pixmap (cr, user_data->dlg.pixmap_main, 0, 0);
#endif
		cairo_rectangle (cr, 0, 0, allocation.width, allocation.height);
		cairo_fill (cr);

		cairo_destroy (cr);
	}

	return FALSE;
}
#endif

static gboolean
draw_area_scrolled(GtkAdjustment *adjustment _U_, gpointer data)
{
	graph_analysis_data_t *user_data = (graph_analysis_data_t *)data;
	cairo_t *cr = gdk_cairo_create (gtk_widget_get_window(user_data->dlg.draw_area));


	draw_area_draw(user_data->dlg.draw_area, cr, data);

	return TRUE;
}
/****************************************************************************/
static gboolean draw_comments(GtkWidget *widget, cairo_t *cr, gpointer data)
{
	graph_analysis_data_t *user_data = (graph_analysis_data_t *)data;
	GtkAllocation          allocation;

	gtk_widget_get_allocation (widget, &allocation);

#if GTK_CHECK_VERSION(2,22,0)
	cairo_set_source_surface (cr, user_data->dlg.surface_comments, 0, 0);
#else
	gdk_cairo_set_source_pixmap (cr, user_data->dlg.pixmap_comments, 0, 0);
#endif
	cairo_rectangle (cr, 0, 0, allocation.width, allocation.height);
	cairo_fill (cr);


	return FALSE;
}
#if !GTK_CHECK_VERSION(3,0,0)
/****************************************************************************/
static gboolean expose_event_comments(GtkWidget *widget, GdkEventExpose *event _U_, gpointer data)
{
	GtkAllocation allocation;
	graph_analysis_data_t *user_data = (graph_analysis_data_t *)data;
	cairo_t *cr = gdk_cairo_create (gtk_widget_get_window(widget));

	if (gtk_widget_is_drawable(widget)) {
		gtk_widget_get_allocation (widget, &allocation);
#if GTK_CHECK_VERSION(2,22,0)
		cairo_set_source_surface (cr, user_data->dlg.surface_comments, 0, 0);
#else
		gdk_cairo_set_source_pixmap (cr, user_data->dlg.pixmap_comments, 0, 0);
#endif
		cairo_rectangle (cr, 0, 0, allocation.width, allocation.height);
		cairo_fill (cr);

		cairo_destroy (cr);
	}

	return FALSE;
}
#endif

static gboolean
comments_area_scrolled(GtkAdjustment *adjustment _U_, gpointer data)
{
	graph_analysis_data_t *user_data = (graph_analysis_data_t *)data;
	cairo_t *cr = gdk_cairo_create (gtk_widget_get_window(user_data->dlg.draw_area_comments));


	draw_comments(user_data->dlg.draw_area_comments, cr, data);

	return TRUE;
}

#if GTK_CHECK_VERSION(3,0,0)
/****************************************************************************/
static gboolean draw_time(GtkWidget *widget, cairo_t *cr, gpointer data)
{
	graph_analysis_data_t *user_data = (graph_analysis_data_t *)data;
	GtkAllocation          allocation;

	gtk_widget_get_allocation (widget, &allocation);

	cairo_set_source_surface (cr, user_data->dlg.surface_time, 0, 0);
	cairo_rectangle (cr, 0, 0, allocation.width, allocation.height);
	cairo_fill (cr);

	return FALSE;
}
#else
/****************************************************************************/
static gboolean expose_event_time(GtkWidget *widget, GdkEventExpose *event _U_, gpointer data)
{
	GtkAllocation          allocation;
	graph_analysis_data_t *user_data = (graph_analysis_data_t *)data;
	cairo_t               *cr        = gdk_cairo_create (gtk_widget_get_window(widget));

	if (gtk_widget_is_drawable(widget) ) {
		gtk_widget_get_allocation (widget, &allocation);
#if GTK_CHECK_VERSION(2,22,0)
		cairo_set_source_surface (cr, user_data->dlg.surface_time, 0, 0);
#else
		gdk_cairo_set_source_pixmap (cr, user_data->dlg.pixmap_time, 0, 0);
#endif
		cairo_rectangle (cr, 0, 0, allocation.width, allocation.height);
		cairo_fill (cr);

		cairo_destroy (cr);
	}

	return FALSE;
}
#endif
/****************************************************************************/
static gboolean configure_event(GtkWidget *widget, GdkEventConfigure *event _U_, gpointer data)
{
	graph_analysis_data_t *user_data = (graph_analysis_data_t *)data;
	GtkAllocation          widget_alloc;
	cairo_t               *cr;

#if GTK_CHECK_VERSION(2,22,0)
	if(user_data->dlg.surface_main) {
		cairo_surface_destroy (user_data->dlg.surface_main);
		user_data->dlg.surface_main = NULL;
	}
#else
	if(user_data->dlg.pixmap_main) {
		g_object_unref(user_data->dlg.pixmap_main);
		user_data->dlg.pixmap_main = NULL;
	}
#endif
	gtk_widget_get_allocation(widget, &widget_alloc);

#if GTK_CHECK_VERSION(2,22,0)
	user_data->dlg.surface_main = gdk_window_create_similar_surface (gtk_widget_get_window(widget),
			CAIRO_CONTENT_COLOR,
			widget_alloc.width,
			widget_alloc.height);

	cr = cairo_create (user_data->dlg.surface_main);
	cairo_rectangle (cr, 0, 0, widget_alloc.width, widget_alloc.height);
	cairo_set_source_rgb (cr, 1, 1, 1);
	cairo_fill (cr);
	cairo_destroy (cr);
	cr = NULL;
#else
	user_data->dlg.pixmap_main = gdk_pixmap_new(gtk_widget_get_window(widget),
		widget_alloc.width,
		widget_alloc.height,
		-1);
	if ( GDK_IS_DRAWABLE(user_data->dlg.pixmap_main) ) {
		cr = gdk_cairo_create (user_data->dlg.pixmap_main);
		cairo_rectangle (cr, 0, 0, widget_alloc.width, widget_alloc.height);
		cairo_set_source_rgb (cr, 1, 1, 1);
		cairo_fill (cr);
		cairo_destroy (cr);
		cr = NULL;
	}
#endif

	dialog_graph_redraw(user_data);

	return TRUE;
}

/****************************************************************************/
static gboolean configure_event_comments(GtkWidget *widget, GdkEventConfigure *event _U_, gpointer data)
{
	graph_analysis_data_t *user_data = (graph_analysis_data_t *)data;
	GtkAllocation          widget_alloc;
	cairo_t               *cr;

	gtk_widget_get_allocation(widget, &widget_alloc);

#if GTK_CHECK_VERSION(2,22,0)
	if(user_data->dlg.surface_comments) {
		cairo_surface_destroy (user_data->dlg.surface_comments);
		user_data->dlg.surface_comments = NULL;
	}
#else
	if(user_data->dlg.pixmap_comments) {
		g_object_unref(user_data->dlg.pixmap_comments);
		user_data->dlg.pixmap_comments = NULL;
	}
#endif

#if GTK_CHECK_VERSION(2,22,0)
	user_data->dlg.surface_comments = gdk_window_create_similar_surface (gtk_widget_get_window(widget),
			CAIRO_CONTENT_COLOR,
			widget_alloc.width,
			widget_alloc.height);

	cr = cairo_create (user_data->dlg.surface_comments);
	cairo_rectangle (cr, 0, 0, widget_alloc.width, widget_alloc.height);
	cairo_set_source_rgb (cr, 1, 1, 1);
	cairo_fill (cr);
	cairo_destroy (cr);
	cr = NULL;
#else
	user_data->dlg.pixmap_comments = gdk_pixmap_new(gtk_widget_get_window(widget),
						widget_alloc.width,
						widget_alloc.height,
						-1);

	if ( GDK_IS_DRAWABLE(user_data->dlg.pixmap_main) ) {
		cr = gdk_cairo_create (user_data->dlg.pixmap_comments);
		cairo_rectangle (cr, 0, 0, widget_alloc.width, widget_alloc.height);
		cairo_set_source_rgb (cr, 1, 1, 1);
		cairo_fill (cr);
		cairo_destroy (cr);
		cr = NULL;
	}
#endif
	dialog_graph_redraw(user_data);
	return TRUE;
}

/****************************************************************************/
static gboolean configure_event_time(GtkWidget *widget, GdkEventConfigure *event _U_, gpointer data)
{
	graph_analysis_data_t *user_data = (graph_analysis_data_t *)data;
	GtkAllocation          widget_alloc;
	cairo_t               *cr;

	gtk_widget_get_allocation(widget, &widget_alloc);

#if GTK_CHECK_VERSION(2,22,0)
	if(user_data->dlg.surface_time) {
		cairo_surface_destroy (user_data->dlg.surface_time);
		user_data->dlg.surface_time = NULL;
	}
#else
	if(user_data->dlg.pixmap_time) {
		g_object_unref(user_data->dlg.pixmap_time);
		user_data->dlg.pixmap_time = NULL;
	}
#endif
#if GTK_CHECK_VERSION(2,22,0)
	user_data->dlg.surface_time = gdk_window_create_similar_surface(gtk_widget_get_window(widget),
			CAIRO_CONTENT_COLOR,
			widget_alloc.width,
			widget_alloc.height);

	cr = cairo_create (user_data->dlg.surface_time);
	cairo_rectangle (cr, 0, 0, widget_alloc.width, widget_alloc.height);
	cairo_set_source_rgb (cr, 1, 1, 1);
	cairo_fill (cr);
	cairo_destroy (cr);
	cr = NULL;
#else
	user_data->dlg.pixmap_time = gdk_pixmap_new(gtk_widget_get_window(widget),
						widget_alloc.width,
						widget_alloc.height,
						-1);

	if ( GDK_IS_DRAWABLE(user_data->dlg.pixmap_time) ) {
		cr = gdk_cairo_create (user_data->dlg.pixmap_time);
		cairo_rectangle (cr, 0, 0, widget_alloc.width, widget_alloc.height);
		cairo_set_source_rgb (cr, 1, 1, 1);
		cairo_fill (cr);
		cairo_destroy (cr);
		cr = NULL;
	}
#endif

	dialog_graph_redraw(user_data);

	return TRUE;
}

/****************************************************************************/
static gboolean pane_callback(GtkWidget *widget _U_, GParamSpec *pspec _U_, gpointer data)
{
	graph_analysis_data_t *user_data = (graph_analysis_data_t *)data;
	GtkAllocation          draw_area_comments_alloc, draw_area_alloc;
	cairo_t               *cr;

	/* If we want to limit minimum and maximum size of graph window, we can include this code. */
#if 0
	if (gtk_paned_get_position(GTK_PANED(user_data->dlg.hpane)) > user_data->dlg.surface_width)
		gtk_paned_set_position(GTK_PANED(user_data->dlg.hpane), user_data->dlg.surface_width);
	else if (gtk_paned_get_position(GTK_PANED(user_data->dlg.hpane)) < NODE_WIDTH*2)
		gtk_paned_set_position(GTK_PANED(user_data->dlg.hpane), NODE_WIDTH*2);
#endif

	/* repaint the comment area because when moving the pane position there are times that the expose_event_comments is not called */

	gtk_widget_get_allocation(user_data->dlg.draw_area_comments, &draw_area_comments_alloc);

	if (gtk_widget_is_drawable(user_data->dlg.draw_area_comments)) {
		cr = gdk_cairo_create (gtk_widget_get_window(user_data->dlg.draw_area_comments));
#if GTK_CHECK_VERSION(2,22,0)
		cairo_set_source_surface (cr, user_data->dlg.surface_comments, 0, 0);
#else
		gdk_cairo_set_source_pixmap (cr, user_data->dlg.pixmap_comments, 0, 0);
#endif
		cairo_rectangle (cr, 0, 0, draw_area_comments_alloc.width, draw_area_comments_alloc.height);
		cairo_fill (cr);
		cairo_destroy (cr);
	}
	/* repaint the draw area because when moving the pane position there are times that the
	   expose_event_comments is not called */
	gtk_widget_get_allocation(user_data->dlg.draw_area, &draw_area_alloc);

	if (gtk_widget_is_drawable(user_data->dlg.draw_area)) {
	 cr = gdk_cairo_create(gtk_widget_get_window(user_data->dlg.draw_area));
#if GTK_CHECK_VERSION(2,22,0)
	 cairo_set_source_surface (cr, user_data->dlg.surface_main, 0, 0);
#else
	 gdk_cairo_set_source_pixmap (cr, user_data->dlg.pixmap_main, 0, 0);
#endif
	 cairo_rectangle (cr, 0, 0, draw_area_alloc.width, draw_area_alloc.height);
	 cairo_fill (cr);
	 cairo_destroy (cr);
	}


	return TRUE;
}

/****************************************************************************/
static void v_scrollbar_changed(GtkWidget *widget _U_, gpointer data)
{
	graph_analysis_data_t *user_data = (graph_analysis_data_t *)data;

	if ((user_data->dlg.first_item+gtk_adjustment_get_page_size(user_data->dlg.v_scrollbar_adjustment)+1 == user_data->num_items)
	    && (gtk_adjustment_get_value(user_data->dlg.v_scrollbar_adjustment) >= user_data->dlg.first_item ))
		return;

	if (user_data->dlg.first_item == gtk_adjustment_get_value(user_data->dlg.v_scrollbar_adjustment))
		return;

	user_data->dlg.first_item = (guint32) gtk_adjustment_get_value(user_data->dlg.v_scrollbar_adjustment);

	dialog_graph_redraw(user_data);

	return;
}

/****************************************************************************/
static void create_draw_area(graph_analysis_data_t *user_data, GtkWidget *box)
{
	GtkWidget      *hbox;
	GtkWidget      *hpane_l;
	GtkWidget      *viewport;
	GtkWidget      *scroll_window_comments;
	GtkWidget      *viewport_comments;
	GtkWidget      *frame_time;
	GtkWidget      *scroll_vbox;
	GtkWidget      *frame_box;
	GtkRequisition  scroll_requisition;
	GtkWidget      *frame;

	hbox = ws_gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0, FALSE);
	gtk_widget_show(hbox);

	/* create "time" draw area */
	user_data->dlg.draw_area_time = gtk_drawing_area_new();
	gtk_widget_set_size_request(user_data->dlg.draw_area_time, TIME_WIDTH, user_data->dlg.surface_height);
	frame_time = gtk_frame_new(NULL);
	gtk_widget_show(frame_time);
	gtk_container_add(GTK_CONTAINER(frame_time), user_data->dlg.draw_area_time);

	/* create "comments" draw area */
	user_data->dlg.draw_area_comments = gtk_drawing_area_new();
	gtk_widget_set_size_request(user_data->dlg.draw_area_comments, COMMENT_WIDTH, user_data->dlg.surface_height);
	scroll_window_comments = gtk_scrolled_window_new(NULL, NULL);
	gtk_widget_set_size_request(scroll_window_comments, (gint)(COMMENT_WIDTH/1.5), user_data->dlg.surface_height);
	/*
	 * Set the scrollbar policy for the horizontal and vertical scrollbars
	 * The policy determines when the scrollbar should appear
	 */
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW (scroll_window_comments),
		GTK_POLICY_ALWAYS, /* Policy for horizontal bar. */
		GTK_POLICY_NEVER); /* Policy for vertical bar */

	/* Changes the type of shadow drawn around the contents of scrolled_window. */
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll_window_comments),
		GTK_SHADOW_ETCHED_IN);

	g_signal_connect(gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(scroll_window_comments)),
		"value-changed", G_CALLBACK(comments_area_scrolled), user_data);


	viewport_comments = gtk_viewport_new(gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(scroll_window_comments)),
					     gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroll_window_comments)));
	gtk_container_add(GTK_CONTAINER(viewport_comments), user_data->dlg.draw_area_comments);
	gtk_container_add(GTK_CONTAINER(scroll_window_comments), viewport_comments);
	gtk_viewport_set_shadow_type(GTK_VIEWPORT(viewport_comments), GTK_SHADOW_NONE);
	gtk_widget_add_events (user_data->dlg.draw_area_comments, GDK_BUTTON_PRESS_MASK);

	/* create main Graph draw area */
	user_data->dlg.draw_area = gtk_drawing_area_new();
	/* allow a little extra space (2*NODE_WIDTH) for wide labels */
	user_data->dlg.surface_width = user_data->num_nodes * NODE_WIDTH + 2*NODE_WIDTH; 
	gtk_widget_set_size_request(user_data->dlg.draw_area, user_data->dlg.surface_width, user_data->dlg.surface_height);
	user_data->dlg.scroll_window = gtk_scrolled_window_new(NULL, NULL);
	if ( user_data->num_nodes < 6)
		gtk_widget_set_size_request(user_data->dlg.scroll_window, NODE_WIDTH*user_data->num_nodes, user_data->dlg.surface_height);
	else
		gtk_widget_set_size_request(user_data->dlg.scroll_window, NODE_WIDTH*5, user_data->dlg.surface_height);

	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW(user_data->dlg.scroll_window),
		GTK_POLICY_ALWAYS,
		GTK_POLICY_NEVER);

	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(user_data->dlg.scroll_window),
		GTK_SHADOW_ETCHED_IN);

	g_signal_connect(gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(user_data->dlg.scroll_window)),
		"value-changed", G_CALLBACK(draw_area_scrolled), user_data);


	viewport = gtk_viewport_new(gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(user_data->dlg.scroll_window)),
				    gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(user_data->dlg.scroll_window)));
	gtk_container_add(GTK_CONTAINER(viewport), user_data->dlg.draw_area);
	gtk_container_add(GTK_CONTAINER(user_data->dlg.scroll_window), viewport);
	gtk_viewport_set_shadow_type(GTK_VIEWPORT(viewport), GTK_SHADOW_NONE);
	gtk_widget_set_can_focus(user_data->dlg.draw_area, TRUE);
	gtk_widget_grab_focus(user_data->dlg.draw_area);

	/* signals needed to handle backing pixmap */
#if GTK_CHECK_VERSION(3,0,0)
	g_signal_connect(user_data->dlg.draw_area, "draw", G_CALLBACK(draw_area_draw), user_data);
#else
	g_signal_connect(user_data->dlg.draw_area, "expose_event", G_CALLBACK(expose_event), user_data);
#endif
	g_signal_connect(user_data->dlg.draw_area, "configure_event", G_CALLBACK(configure_event), user_data);

	/* signals needed to handle backing pixmap comments */
#if GTK_CHECK_VERSION(3,0,0)
	g_signal_connect(user_data->dlg.draw_area_comments, "draw", G_CALLBACK(draw_comments), user_data);
#else
	g_signal_connect(user_data->dlg.draw_area_comments, "expose_event", G_CALLBACK(expose_event_comments), user_data);
#endif
	g_signal_connect(user_data->dlg.draw_area_comments, "configure_event", G_CALLBACK(configure_event_comments), user_data);

	/* signals needed to handle backing pixmap time */
#if GTK_CHECK_VERSION(3,0,0)
	g_signal_connect(user_data->dlg.draw_area_time, "draw", G_CALLBACK(draw_time), user_data);
#else
	g_signal_connect(user_data->dlg.draw_area_time, "expose_event", G_CALLBACK(expose_event_time), user_data);
#endif
	g_signal_connect(user_data->dlg.draw_area_time, "configure_event", G_CALLBACK(configure_event_time), user_data);

	gtk_widget_add_events (user_data->dlg.draw_area, GDK_BUTTON_PRESS_MASK);
	g_signal_connect(user_data->dlg.draw_area, "button_press_event", G_CALLBACK(button_press_event), user_data);
	g_signal_connect(user_data->dlg.draw_area, "key_press_event",  G_CALLBACK(key_press_event), user_data);

	gtk_widget_show(user_data->dlg.draw_area_time);
	gtk_widget_show(user_data->dlg.draw_area);
	gtk_widget_show(viewport);
	gtk_widget_show(user_data->dlg.draw_area_comments);
	gtk_widget_show(viewport_comments);

	gtk_widget_show(user_data->dlg.scroll_window);
	gtk_widget_show(scroll_window_comments);

	user_data->dlg.hpane = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_paned_pack1(GTK_PANED (user_data->dlg.hpane), user_data->dlg.scroll_window, FALSE, TRUE);
	gtk_paned_pack2(GTK_PANED (user_data->dlg.hpane), scroll_window_comments, TRUE, TRUE);
	g_signal_connect(user_data->dlg.hpane, "notify::position",  G_CALLBACK(pane_callback), user_data);
	gtk_widget_show(user_data->dlg.hpane);

	/* Allow the hbox with time to expand (TRUE, TRUE) */
	hpane_l = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_paned_pack1(GTK_PANED (hpane_l), frame_time, FALSE, TRUE);
	gtk_paned_pack2(GTK_PANED (hpane_l), user_data->dlg.hpane, TRUE, TRUE);
	g_signal_connect(hpane_l, "notify::position",  G_CALLBACK(pane_callback), user_data);
	gtk_widget_show(hpane_l);

	gtk_box_pack_start(GTK_BOX(hbox), hpane_l, TRUE, TRUE, 0);

	/* Create the scroll_vbox to include the vertical scroll and a box at the bottom */
	scroll_vbox = ws_gtk_box_new(GTK_ORIENTATION_VERTICAL, 0, FALSE);
	gtk_widget_show(scroll_vbox);

	/* create the associated v_scrollbar */
	user_data->dlg.v_scrollbar_adjustment = (GtkAdjustment *)gtk_adjustment_new(0, 0, 0, 0, 0, 0);
	user_data->dlg.v_scrollbar = gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, user_data->dlg.v_scrollbar_adjustment);
	gtk_widget_show(user_data->dlg.v_scrollbar);
	gtk_box_pack_start(GTK_BOX(scroll_vbox), user_data->dlg.v_scrollbar, TRUE, TRUE, 0);
	g_signal_connect(user_data->dlg.v_scrollbar_adjustment, "value_changed",
			 G_CALLBACK(v_scrollbar_changed), user_data);

	frame_box = gtk_frame_new(NULL);
#if GTK_CHECK_VERSION(3,0,0)
	gtk_widget_get_preferred_size(user_data->dlg.v_scrollbar, &scroll_requisition, NULL);
#else
	gtk_widget_size_request(user_data->dlg.v_scrollbar, &scroll_requisition);
#endif
	gtk_widget_set_size_request(frame_box, 1, scroll_requisition.width+2);
	gtk_frame_set_shadow_type(GTK_FRAME(frame_box), GTK_SHADOW_NONE);
	gtk_widget_show(frame_box);
	gtk_box_pack_end(GTK_BOX(scroll_vbox), frame_box, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(hbox), scroll_vbox, FALSE, FALSE, 3);

	/* Frame around the main area */
	frame = gtk_frame_new(NULL);
	gtk_widget_show(frame);
	gtk_container_add(GTK_CONTAINER(frame), hbox);
	gtk_container_set_border_width(GTK_CONTAINER(hbox), 3);

	/*gtk_box_pack_start(GTK_BOX(box), hbox, TRUE, TRUE, 15);*/
	/*gtk_box_pack_start(GTK_BOX(box), frame, TRUE, TRUE, 15);*/
	gtk_box_pack_start(GTK_BOX(box), frame, TRUE, TRUE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(box), 10);
}
/****************************************************************************/
/* PUBLIC */
/****************************************************************************/


/****************************************************************************/
static void dialog_graph_create_window(graph_analysis_data_t *user_data)
{
	GtkWidget *vbox;
	GtkWidget *hbuttonbox;
	GtkWidget *bt_close;
	GtkWidget *bt_save;
	gchar     *title_name_ptr;
	gchar     *win_name;

	title_name_ptr = cf_get_display_name(&cfile);
	win_name = g_strdup_printf("%s - Graph Analysis", title_name_ptr);
	g_free(title_name_ptr);

	/* create the main window */
	user_data->dlg.window = dlg_window_new((user_data->dlg.title)?user_data->dlg.title:win_name);
	gtk_window_set_destroy_with_parent(GTK_WINDOW(user_data->dlg.window), TRUE);

	vbox = ws_gtk_box_new(GTK_ORIENTATION_VERTICAL, 0, FALSE);
	gtk_container_add(GTK_CONTAINER(user_data->dlg.window), vbox);
	gtk_widget_show(vbox);

	create_draw_area(user_data, vbox);

	/* button row */
	hbuttonbox = gtk_button_box_new (GTK_ORIENTATION_HORIZONTAL);
	gtk_box_pack_start (GTK_BOX (vbox), hbuttonbox, FALSE, FALSE, 10);
	gtk_button_box_set_layout (GTK_BUTTON_BOX (hbuttonbox), GTK_BUTTONBOX_SPREAD);
	gtk_box_set_spacing (GTK_BOX (hbuttonbox), 30);
	gtk_widget_show(hbuttonbox);

	bt_save = gtk_button_new_from_stock(GTK_STOCK_SAVE_AS);
	gtk_box_pack_start(GTK_BOX(hbuttonbox), bt_save, TRUE, TRUE, 0);
	gtk_widget_show(bt_save);
	g_signal_connect(bt_save, "clicked", G_CALLBACK(on_save_bt_clicked), user_data);
	gtk_widget_set_tooltip_text(bt_save, "Save an ASCII representation of the graph to a file");

	bt_close = gtk_button_new_from_stock(GTK_STOCK_CLOSE);
	gtk_box_pack_start(GTK_BOX(hbuttonbox), bt_close, TRUE, TRUE, 0);
	gtk_widget_set_can_default(bt_close, TRUE);
	gtk_widget_show(bt_close);
	gtk_widget_set_tooltip_text(bt_close, "Close this dialog");
	window_set_cancel_button(user_data->dlg.window, bt_close, window_cancel_button_cb);

	g_signal_connect(user_data->dlg.window, "delete_event", G_CALLBACK(window_delete_event_cb), NULL);
	g_signal_connect(user_data->dlg.window, "destroy", G_CALLBACK(on_destroy), user_data);

	gtk_widget_show(user_data->dlg.window);
	window_present(user_data->dlg.window);

	/* Destroy our graph window with our parent if the caller specified the parent */
	if(user_data->dlg.parent_w) {
		gtk_window_set_transient_for(GTK_WINDOW(user_data->dlg.window),
					     GTK_WINDOW(user_data->dlg.parent_w));
		/* Destruction of this child window */
		gtk_window_set_destroy_with_parent(GTK_WINDOW(user_data->dlg.window), TRUE);
	}
	g_free(win_name);
}

/* Return the index array if the node is in the array. Return -1 if there is room in the array
 * and Return -2 if the array is full
 */
/****************************************************************************/
static gint add_or_get_node(graph_analysis_data_t *user_data, address *node) {
	guint i;

	if (node->type == AT_NONE) return NODE_OVERFLOW;

	for (i=0; i<MAX_NUM_NODES && i < user_data->num_nodes ; i++) {
		if ( CMP_ADDRESS(&(user_data->nodes[i]), node) == 0 ) return i;	/* it is in the array */
	}

	if (i == MAX_NUM_NODES) {
		return  NODE_OVERFLOW;
	} else {
		user_data->num_nodes++;
		COPY_ADDRESS(&(user_data->nodes[i]), node);
		return i;
	}
}

/* Get the nodes from the list */
/****************************************************************************/
static void get_nodes(graph_analysis_data_t *user_data)
{
	GList *list;
	graph_analysis_item_t *gai;

	/* fill the node array */
	list = g_list_first(user_data->graph_info->list);
	while (list)
	{
		gai = (graph_analysis_item_t *)list->data;
		if (gai->display) {
			user_data->num_items++;
			if (!user_data->dlg.inverse) {
				gai->src_node = (guint16)add_or_get_node(user_data, &(gai->src_addr));
				gai->dst_node = (guint16)add_or_get_node(user_data, &(gai->dst_addr));
			} else {
				gai->dst_node = (guint16)add_or_get_node(user_data, &(gai->src_addr));
				gai->src_node = (guint16)add_or_get_node(user_data, &(gai->dst_addr));
			}
		}
		list = g_list_next(list);
	}
}

/****************************************************************************/
graph_analysis_data_t *graph_analysis_init(void)
{
	graph_analysis_data_t *user_data;
	/* init */
	user_data = g_new(graph_analysis_data_t,1);

	/* init user_data */
	graph_analysis_init_dlg(user_data);

	return user_data;
}
/****************************************************************************/
/* PUBLIC */
/****************************************************************************/

/****************************************************************************/
void graph_analysis_create(graph_analysis_data_t *user_data)
{
	/* reset the data */
	graph_analysis_reset(user_data);

	/* get nodes (each node is an address) */
	get_nodes(user_data);

	/* create the graph windows */
	dialog_graph_create_window(user_data);

	/* redraw the graph */
	dialog_graph_redraw(user_data);

	return;
}

/****************************************************************************/
void graph_analysis_update(graph_analysis_data_t *user_data)
{
	/* reset the data */
	graph_analysis_reset(user_data);

	/* get nodes (each node is an address) */
	get_nodes(user_data);

	user_data->dlg.surface_width = user_data->num_nodes * NODE_WIDTH;
	gtk_widget_set_size_request(user_data->dlg.draw_area, user_data->dlg.surface_width, user_data->dlg.surface_height);
	if (user_data->num_nodes < 6)
		gtk_widget_set_size_request(user_data->dlg.scroll_window, NODE_WIDTH*user_data->num_nodes, user_data->dlg.surface_height);
	else
		gtk_widget_set_size_request(user_data->dlg.scroll_window, NODE_WIDTH*5, user_data->dlg.surface_height);

	/* redraw the graph */
	dialog_graph_redraw(user_data);

	window_present(user_data->dlg.window);
	return;
}


/****************************************************************************/
void graph_analysis_redraw(graph_analysis_data_t *user_data)
{
	/* get nodes (each node is an address) */
	get_nodes(user_data);

	user_data->dlg.surface_width = user_data->num_nodes * NODE_WIDTH;
	gtk_widget_set_size_request(user_data->dlg.draw_area, user_data->dlg.surface_width, user_data->dlg.surface_height);
	if (user_data->num_nodes < 6)
		gtk_widget_set_size_request(user_data->dlg.scroll_window, NODE_WIDTH*user_data->num_nodes, user_data->dlg.surface_height);
	else
		gtk_widget_set_size_request(user_data->dlg.scroll_window, NODE_WIDTH*5, user_data->dlg.surface_height);


	/* redraw the graph */
	dialog_graph_redraw(user_data);

	window_present(user_data->dlg.window);
	return;
}
