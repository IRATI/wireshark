/* rpc_progs.c
 * rpc_progs   2002 Ronnie Sahlberg
 *
 * $Id$
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/* This module provides rpc call/reply SRT statistics to Wireshark.
 * It is only used by Wireshark and not TShark
 *
 * It serves as an example on how to use the tap api.
 */

#include "config.h"

#include <stdio.h>

#include <gtk/gtk.h>

#include <epan/packet_info.h>
#include <epan/epan.h>
#include <epan/stat_cmd_args.h>
#include <epan/tap.h>
#include <epan/dissectors/packet-rpc.h>

#include "../stat_menu.h"
#include "../globals.h"

#include "ui/gtk/gui_stat_menu.h"
#include "ui/gtk/gui_utils.h"
#include "ui/gtk/dlg_utils.h"
#include "ui/gtk/main.h"

#include "ui/gtk/old-gtk-compat.h"

#define NANOSECS_PER_SEC 1000000000

static GtkWidget *win   = NULL;
static GtkWidget *grid  = NULL;
static int num_progs    = 0;

/* used to keep track of statistics for a specific program/version */
typedef struct _rpc_program_t {
	struct _rpc_program_t *next;

	guint32    program;
	GtkWidget *wprogram;
	gchar      sprogram[24];

	guint32    version;
	GtkWidget *wversion;
	gchar      sversion[16];

	int        num;
	GtkWidget *wnum;
	gchar      snum[16];

	nstime_t   min;
	GtkWidget *wmin;
	gchar      smin[16];

	nstime_t   max;
	GtkWidget *wmax;
	gchar      smax[16];

	nstime_t   tot;
	GtkWidget *wavg;
	gchar      savg[16];
} rpc_program_t;

static rpc_program_t *prog_list = NULL;


static char *
rpcprogs_gen_title(void)
{
	char *display_name;
	char *title;

	display_name = cf_get_display_name(&cfile);
	title = g_strdup_printf("ONC-RPC Program Statistics: %s",
	    display_name);
	g_free(display_name);
	return title;
}

static void
rpcprogs_init_grid(GtkWidget *grid_parent)
{
	GtkWidget *tmp;

	grid = ws_gtk_grid_new();
	ws_gtk_grid_set_homogeneous(GTK_GRID(grid), TRUE);

	gtk_container_add(GTK_CONTAINER(grid_parent), grid);

	tmp = gtk_label_new("Program");
	ws_gtk_grid_attach_defaults(GTK_GRID(grid), tmp, 0, 0, 1, 1);
	gtk_label_set_justify(GTK_LABEL(tmp), GTK_JUSTIFY_LEFT);

	tmp = gtk_label_new("Version");
	ws_gtk_grid_attach_defaults(GTK_GRID(grid), tmp, 1, 0, 1, 1);
	gtk_label_set_justify(GTK_LABEL(tmp), GTK_JUSTIFY_RIGHT);

	tmp = gtk_label_new("Calls");
	ws_gtk_grid_attach_defaults(GTK_GRID(grid), tmp, 2, 0, 1, 1);
	gtk_label_set_justify(GTK_LABEL(tmp), GTK_JUSTIFY_RIGHT);

	tmp = gtk_label_new("Min SRT");
	ws_gtk_grid_attach_defaults(GTK_GRID(grid), tmp, 3, 0, 1, 1);
	gtk_label_set_justify(GTK_LABEL(tmp), GTK_JUSTIFY_RIGHT);

	tmp = gtk_label_new("Max SRT");
	ws_gtk_grid_attach_defaults(GTK_GRID(grid), tmp, 4, 0, 1, 1);
	gtk_label_set_justify(GTK_LABEL(tmp), GTK_JUSTIFY_RIGHT);

	tmp = gtk_label_new("Avg SRT");
	ws_gtk_grid_attach_defaults(GTK_GRID(grid), tmp, 5, 0, 1, 1);
	gtk_label_set_justify(GTK_LABEL(tmp), GTK_JUSTIFY_RIGHT);
}



static void
rpcprogs_reset(void *dummy _U_)
{
	GtkWidget     *grid_parent;
	rpc_program_t *rp;

	while (prog_list) {
		rp = prog_list;
		prog_list = prog_list->next;
		g_free(rp);
	}
	num_progs = 0;

	grid_parent = gtk_widget_get_parent(grid);
	gtk_widget_destroy(grid); /* also destroys the widgets contained within the grid */
	grid = NULL;

	rpcprogs_init_grid(grid_parent);
	gtk_widget_show(grid);

}

static void
add_new_program(rpc_program_t *rp)
{
	num_progs++;

	rp->wprogram = gtk_label_new("0");
	ws_gtk_grid_attach_defaults(GTK_GRID(grid), rp->wprogram, 0, num_progs, 1, 1);
	gtk_widget_show(rp->wprogram);

	rp->wversion = gtk_label_new("0");
	ws_gtk_grid_attach_defaults(GTK_GRID(grid), rp->wversion, 1, num_progs, 1, 1);
	gtk_widget_show(rp->wversion);

	rp->wnum = gtk_label_new("0");
	ws_gtk_grid_attach_defaults(GTK_GRID(grid), rp->wnum,     2, num_progs, 1, 1);
	gtk_widget_show(rp->wnum);

	rp->wmin = gtk_label_new("0");
	ws_gtk_grid_attach_defaults(GTK_GRID(grid), rp->wmin,     3, num_progs, 1, 1);
	gtk_widget_show(rp->wmin);

	rp->wmax = gtk_label_new("0");
	ws_gtk_grid_attach_defaults(GTK_GRID(grid), rp->wmax,     4, num_progs, 1, 1);
	gtk_widget_show(rp->wmax);

	rp->wavg = gtk_label_new("0");
	ws_gtk_grid_attach_defaults(GTK_GRID(grid), rp->wavg,     5, num_progs, 1, 1);
	gtk_widget_show(rp->wavg);

	rp->num       = 0;
	rp->min.secs  = 0;
	rp->min.nsecs = 0;
	rp->max.secs  = 0;
	rp->max.nsecs = 0;
	rp->tot.secs  = 0;
	rp->tot.nsecs = 0;
}



static gboolean
rpcprogs_packet(void *dummy _U_, packet_info *pinfo, epan_dissect_t *edt _U_, const void *arg)
{
	const rpc_call_info_value *ri = (rpc_call_info_value *)arg;
	nstime_t       delta;
	rpc_program_t *rp;

	if (!prog_list) {
		/* the list was empty */
		rp = (rpc_program_t *)g_malloc(sizeof(rpc_program_t));
		add_new_program(rp);
		rp->next = NULL;
		rp->program = ri->prog;
		rp->version = ri->vers;
		prog_list = rp;
	} else if ((ri->prog == prog_list->program) && (ri->vers == prog_list->version)) {
		rp = prog_list;
	} else if ( (ri->prog < prog_list->program)
		||  ((ri->prog == prog_list->program) && (ri->vers < prog_list->version))) {
		/* we should be first entry in list */
		rp = (rpc_program_t *)g_malloc(sizeof(rpc_program_t));
		add_new_program(rp);
		rp->next = prog_list;
		rp->program = ri->prog;
		rp->version = ri->vers;
		prog_list = rp;
	} else {
		/* we go somewhere else in the list */
		for (rp = prog_list; rp; rp = rp->next) {
			if ((rp->next)
			&&  (rp->next->program == ri->prog)
			&&  (rp->next->version == ri->vers)) {
				rp = rp->next;
				break;
			}
			if ((!rp->next)
			|| (rp->next->program > ri->prog)
			|| (  (rp->next->program == ri->prog)
			   && (rp->next->version > ri->vers))) {
				rpc_program_t *trp;
				trp = (rpc_program_t *)g_malloc(sizeof(rpc_program_t));
				add_new_program(trp);
				trp->next    = rp->next;
				trp->program = ri->prog;
				trp->version = ri->vers;
				rp->next = trp;
				rp = trp;
				break;
			}
		}
	}


	/* we are only interested in reply packets */
	if (ri->request || !rp) {
		return FALSE;
	}

	/* calculate time delta between request and reply */
	nstime_delta(&delta, &pinfo->fd->abs_ts, &ri->req_time);

	if ((rp->max.secs  == 0)
	&&  (rp->max.nsecs == 0) ) {
		rp->max.secs  = delta.secs;
		rp->max.nsecs = delta.nsecs;
	}

	if ((rp->min.secs  == 0)
	&&  (rp->min.nsecs == 0) ) {
		rp->min.secs  = delta.secs;
		rp->min.nsecs = delta.nsecs;
	}

	if ( (delta.secs < rp->min.secs)
	||   (	(delta.secs == rp->min.secs)
	     && (delta.nsecs < rp->min.nsecs) ) ) {
		rp->min.secs  = delta.secs;
		rp->min.nsecs = delta.nsecs;
	}

	if ( (delta.secs > rp->max.secs)
	||   (	(delta.secs == rp->max.secs)
	     && (delta.nsecs > rp->max.nsecs) ) ) {
		rp->max.secs  = delta.secs;
		rp->max.nsecs = delta.nsecs;
	}

	rp->tot.secs  += delta.secs;
	rp->tot.nsecs += delta.nsecs;
	if (rp->tot.nsecs > NANOSECS_PER_SEC) {
		rp->tot.nsecs -= NANOSECS_PER_SEC;
		rp->tot.secs++;
	}
	rp->num++;

	return TRUE;
}


static void
rpcprogs_draw(void *dummy _U_)
{
	rpc_program_t *rp;
	int            i;
	guint64        td;

	for (rp=prog_list,i=1; rp; rp=rp->next,i++) {
		/* Ignore procedures with no calls */
		if (rp->num == 0) {
			continue;
		}
		/* Scale the average SRT in units of 1us and round to the nearest us.
		   tot.secs is a time_t which may be 32 or 64 bits (or even floating)
		   depending on the platform.  After casting tot.secs to a 64 bits int, it
		   would take a capture with a duration of over 136 *years* to
		   overflow the secs portion of td. */
		td = ((guint64)(rp->tot.secs))*NANOSECS_PER_SEC + rp->tot.nsecs;
		td = ((td / rp->num) + 500) / 1000;

		g_snprintf(rp->sprogram, sizeof(rp->sprogram), "%s",rpc_prog_name(rp->program));
		gtk_label_set_text(GTK_LABEL(rp->wprogram), rp->sprogram);

		g_snprintf(rp->sversion, sizeof(rp->sversion), "%d",rp->version);
		gtk_label_set_text(GTK_LABEL(rp->wversion), rp->sversion);

		g_snprintf(rp->snum, sizeof(rp->snum), "%d",rp->num);
		gtk_label_set_text(GTK_LABEL(rp->wnum), rp->snum);

		g_snprintf(rp->smin, sizeof(rp->smin), "%3d.%06d",(int)rp->min.secs, (rp->min.nsecs+500)/1000);
		gtk_label_set_text(GTK_LABEL(rp->wmin), rp->smin);

		g_snprintf(rp->smax, sizeof(rp->smax), "%3d.%06d",(int)rp->max.secs, (rp->max.nsecs+500)/1000);
		gtk_label_set_text(GTK_LABEL(rp->wmax), rp->smax);

		g_snprintf(rp->savg, sizeof(rp->savg), "%3d.%06d",(int)(td/1000000),(int)(td%1000000));
		gtk_label_set_text(GTK_LABEL(rp->wavg), rp->savg);

	}
}

static void
win_destroy_cb(void *dummy _U_, gpointer data _U_)
{
	rpc_program_t *rp, *rp2;

	remove_tap_listener(win);

	win = NULL;
	for (rp=prog_list; rp;) {
		rp2 = rp->next;
		g_free(rp);
		rp = rp2;
	}
	prog_list = NULL;
}


/* When called, this function will start rpcprogs
 */
static void
gtk_rpcprogs_init(const char *opt_arg _U_, void* userdata _U_)
{
	char      *title_string;
	GtkWidget *vbox;
	GtkWidget *stat_label;
	GtkWidget *grid_parent;
	GString   *error_string;
	GtkWidget *bt_close;
	GtkWidget *bbox;

	if (win) {
		gdk_window_raise(gtk_widget_get_window(win));
		return;
	}

	title_string = rpcprogs_gen_title();
	win = dlg_window_new(title_string);  /* transient_for top_level */
	gtk_window_set_destroy_with_parent(GTK_WINDOW(win), TRUE);

	vbox = ws_gtk_box_new(GTK_ORIENTATION_VERTICAL, 3, FALSE);
	gtk_container_add(GTK_CONTAINER(win), vbox);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);

	stat_label = gtk_label_new(title_string);
	g_free(title_string);
	gtk_box_pack_start(GTK_BOX(vbox), stat_label, FALSE, FALSE, 0);

	/* wrap grid in a dummy container so that the grid can be */
	/*  destroyed and re-created as needed.			    */
	/* XXX: This is a kludge; Better: Use a GtkTreeView & etc.  */
	grid_parent = ws_gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0, TRUE);
	gtk_box_pack_start(GTK_BOX(vbox), grid_parent, TRUE, TRUE, 0);
	rpcprogs_init_grid(grid_parent);

	error_string = register_tap_listener("rpc", win, NULL, 0, rpcprogs_reset, rpcprogs_packet, rpcprogs_draw);
	if (error_string) {
		fprintf(stderr, "wireshark: Couldn't register rpc,programs tap: %s\n",
		    error_string->str);
		g_string_free(error_string, TRUE);
		exit(1);
	}

	/* Button row. */
	bbox = dlg_button_row_new(GTK_STOCK_CLOSE, NULL);
	gtk_box_pack_start(GTK_BOX(vbox), bbox, FALSE, FALSE, 0);

	bt_close = (GtkWidget *)g_object_get_data(G_OBJECT(bbox), GTK_STOCK_CLOSE);
	window_set_cancel_button(win, bt_close, window_cancel_button_cb);

	g_signal_connect(win, "delete_event", G_CALLBACK(window_delete_event_cb), NULL);
	g_signal_connect(win, "destroy", G_CALLBACK(win_destroy_cb), NULL);

	gtk_widget_show_all(win);
	window_present(win);

	cf_retap_packets(&cfile);
	gdk_window_raise(gtk_widget_get_window(win));
}

void
gtk_rpcprogs_cb(GtkWidget *w _U_, gpointer data _U_)
{
	gtk_rpcprogs_init("", NULL);
}

void
register_tap_listener_gtkrpcprogs(void)
{
	register_stat_cmd_arg("rpc,programs", gtk_rpcprogs_init, NULL);
}
