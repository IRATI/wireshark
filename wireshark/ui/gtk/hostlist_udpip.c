/* hostlist_udpip.c   2004 Ian Schorr
 * modified from endpoint_talkers_udpip.c   2003 Ronnie Sahlberg
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

#include "config.h"

#include <string.h>

#include <gtk/gtk.h>

#include "epan/packet.h"
#include <epan/stat_cmd_args.h>
#include <epan/tap.h>
#include <epan/dissectors/packet-udp.h>

#include "../stat_menu.h"

#include "ui/gtk/gui_stat_menu.h"
#include "ui/gtk/hostlist_table.h"

static int
udpip_hostlist_packet(void *pit, packet_info *pinfo, epan_dissect_t *edt _U_, const void *vip)
{
	hostlist_table *hosts=(hostlist_table *)pit;
	const e_udphdr *udphdr=(e_udphdr *)vip;

	/* Take two "add" passes per packet, adding for each direction, ensures that all
	packets are counted properly (even if address is sending to itself)
	XXX - this could probably be done more efficiently inside hostlist_table */
	add_hostlist_table_data(hosts, &udphdr->ip_src, udphdr->uh_sport, TRUE, 1, pinfo->fd->pkt_len, SAT_NONE, PT_UDP);
	add_hostlist_table_data(hosts, &udphdr->ip_dst, udphdr->uh_dport, FALSE, 1, pinfo->fd->pkt_len, SAT_NONE, PT_UDP);

	return 1;
}



static void
gtk_udpip_hostlist_init(const char *opt_arg, void* userdata _U_)
{
	const char *filter=NULL;

	if(!strncmp(opt_arg,"endpoints,udp,",14)){
		filter=opt_arg+14;
	} else {
		filter=NULL;
	}

	init_hostlist_table(FALSE, "UDP", "udp", filter, udpip_hostlist_packet);

}

void
gtk_udpip_hostlist_cb(GtkAction *action _U_, gpointer user_data _U_)
{
	gtk_udpip_hostlist_init("endpoints,udp",NULL);
}

void
register_tap_listener_udpip_hostlist(void)
{
	register_stat_cmd_arg("endpoints,udp", gtk_udpip_hostlist_init,NULL);
	register_hostlist_table(FALSE, "UDP", "udp", NULL /*filter*/, udpip_hostlist_packet);
}
