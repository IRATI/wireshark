/* packet-ipv6.c
 * Routines for IPv6 packet disassembly
 *
 * $Id$
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * SHIM6 support added by Matthijs Mekking <matthijs@NLnetLabs.nl>
 *
 * MobileIPv6 support added by Tomislav Borosa <tomislav.borosa@siemens.hr>
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

#include <math.h>
#include <glib.h>
#include <epan/packet.h>
#include <epan/ip_opts.h>
#include <epan/addr_resolv.h>
#include <epan/prefs.h>
#include <epan/reassemble.h>
#include <epan/ipproto.h>
#include <epan/ipv6-utils.h>
#include <epan/etypes.h>
#include <epan/ppptypes.h>
#include <epan/aftypes.h>
#include <epan/nlpid.h>
#include <epan/arcnet_pids.h>
#include <epan/in_cksum.h>
#include <epan/expert.h>
#include <epan/emem.h>
#include <epan/tap.h>
#include "packet-ipsec.h"
#include "packet-ipv6.h"
#include "packet-ip.h"

#ifdef HAVE_GEOIP_V6
#include <GeoIP.h>
#include <epan/geoip_db.h>
#endif /* HAVE_GEOIP_V6 */

/* Differentiated Services Field. See RFCs 2474, 2597 and 2598. */
#define IPDSFIELD_DSCP_MASK     0xFC
#define IPDSFIELD_ECN_MASK      0x03
#define IPDSFIELD_DSCP_SHIFT    2
#define IPDSFIELD_DSCP(dsfield) (((dsfield)&IPDSFIELD_DSCP_MASK)>>IPDSFIELD_DSCP_SHIFT)
#define IPDSFIELD_ECN(dsfield)  ((dsfield)&IPDSFIELD_ECN_MASK)
#define IPDSFIELD_DSCP_DEFAULT  0x00
#define IPDSFIELD_DSCP_CS1      0x08
#define IPDSFIELD_DSCP_CS2      0x10
#define IPDSFIELD_DSCP_CS3      0x18
#define IPDSFIELD_DSCP_CS4      0x20
#define IPDSFIELD_DSCP_CS5      0x28
#define IPDSFIELD_DSCP_CS6      0x30
#define IPDSFIELD_DSCP_CS7      0x38
#define IPDSFIELD_DSCP_AF11     0x0A
#define IPDSFIELD_DSCP_AF12     0x0C
#define IPDSFIELD_DSCP_AF13     0x0E
#define IPDSFIELD_DSCP_AF21     0x12
#define IPDSFIELD_DSCP_AF22     0x14
#define IPDSFIELD_DSCP_AF23     0x16
#define IPDSFIELD_DSCP_AF31     0x1A
#define IPDSFIELD_DSCP_AF32     0x1C
#define IPDSFIELD_DSCP_AF33     0x1E
#define IPDSFIELD_DSCP_AF41     0x22
#define IPDSFIELD_DSCP_AF42     0x24
#define IPDSFIELD_DSCP_AF43     0x26
#define IPDSFIELD_DSCP_EF       0x2E
#define IPDSFIELD_ECT_MASK      0x02
#define IPDSFIELD_CE_MASK       0x01

/* RPL Routing header */
#define IP6RRPL_BITMASK_CMPRI     0xF0000000
#define IP6RRPL_BITMASK_CMPRE     0x0F000000
#define IP6RRPL_BITMASK_PAD       0x00F00000
#define IP6RRPL_BITMASK_RESERVED  0x000FFFFF

static int ipv6_tap = -1;

static int proto_ipv6                           = -1;
static int hf_ipv6_version                      = -1;
static int hf_ip_version                        = -1;
static int hf_ipv6_class                        = -1;
static int hf_ipv6_flow                         = -1;
static int hf_ipv6_plen                         = -1;
static int hf_ipv6_nxt                          = -1;
static int hf_ipv6_hlim                         = -1;
static int hf_ipv6_src                          = -1;
static int hf_ipv6_src_host                     = -1;
static int hf_ipv6_src_sa_mac                   = -1;
static int hf_ipv6_src_isatap_ipv4              = -1;
static int hf_ipv6_src_6to4_gateway_ipv4        = -1;
static int hf_ipv6_src_6to4_sla_id              = -1;
static int hf_ipv6_src_teredo_server_ipv4       = -1;
static int hf_ipv6_src_teredo_port              = -1;
static int hf_ipv6_src_teredo_client_ipv4       = -1;
static int hf_ipv6_dst                          = -1;
static int hf_ipv6_dst_host                     = -1;
static int hf_ipv6_dst_sa_mac                   = -1;
static int hf_ipv6_dst_isatap_ipv4              = -1;
static int hf_ipv6_dst_6to4_gateway_ipv4        = -1;
static int hf_ipv6_dst_6to4_sla_id              = -1;
static int hf_ipv6_dst_teredo_server_ipv4       = -1;
static int hf_ipv6_dst_teredo_port              = -1;
static int hf_ipv6_dst_teredo_client_ipv4       = -1;
static int hf_ipv6_addr                         = -1;
static int hf_ipv6_host                         = -1;
static int hf_ipv6_sa_mac                       = -1;
static int hf_ipv6_isatap_ipv4                  = -1;
static int hf_ipv6_6to4_gateway_ipv4            = -1;
static int hf_ipv6_6to4_sla_id                  = -1;
static int hf_ipv6_teredo_server_ipv4           = -1;
static int hf_ipv6_teredo_port                  = -1;
static int hf_ipv6_teredo_client_ipv4           = -1;
static int hf_ipv6_opt                          = -1;
static int hf_ipv6_opt_type                     = -1;
static int hf_ipv6_opt_length                   = -1;
static int hf_ipv6_opt_pad1                     = -1;
static int hf_ipv6_opt_padn                     = -1;
static int hf_ipv6_opt_tel                      = -1;
static int hf_ipv6_opt_rtalert                  = -1;
static int hf_ipv6_opt_jumbo                    = -1;
static int hf_ipv6_opt_calipso_doi              = -1;
static int hf_ipv6_opt_calipso_cmpt_length      = -1;
static int hf_ipv6_opt_calipso_sens_level       = -1;
static int hf_ipv6_opt_calipso_checksum         = -1;
static int hf_ipv6_opt_calipso_cmpt_bitmap      = -1;
static int hf_ipv6_opt_qs_func                  = -1;
static int hf_ipv6_opt_qs_rate                  = -1;
static int hf_ipv6_opt_qs_ttl                   = -1;
static int hf_ipv6_opt_qs_ttl_diff              = -1;
static int hf_ipv6_opt_qs_unused                = -1;
static int hf_ipv6_opt_qs_nonce                 = -1;
static int hf_ipv6_opt_qs_reserved              = -1;
static int hf_ipv6_opt_rpl_flag                 = -1;
static int hf_ipv6_opt_rpl_flag_o               = -1;
static int hf_ipv6_opt_rpl_flag_r               = -1;
static int hf_ipv6_opt_rpl_flag_f               = -1;
static int hf_ipv6_opt_rpl_flag_rsv             = -1;
static int hf_ipv6_opt_rpl_instance_id          = -1;
static int hf_ipv6_opt_rpl_senderrank           = -1;
static int hf_ipv6_opt_experimental             = -1;
static int hf_ipv6_opt_unknown                  = -1;
static int hf_ipv6_dst_opt                      = -1;
static int hf_ipv6_hop_opt                      = -1;
static int hf_ipv6_unk_hdr                      = -1;
static int hf_ipv6_routing_hdr_opt              = -1;
static int hf_ipv6_routing_hdr_type             = -1;
static int hf_ipv6_routing_hdr_left             = -1;
static int hf_ipv6_routing_hdr_addr             = -1;
static int hf_ipv6_frag_nxt                     = -1;
static int hf_ipv6_frag_reserved                = -1;
static int hf_ipv6_frag_offset                  = -1;
static int hf_ipv6_frag_reserved_bits           = -1;
static int hf_ipv6_frag_more                    = -1;
static int hf_ipv6_frag_id                      = -1;
static int hf_ipv6_fragments                    = -1;
static int hf_ipv6_fragment                     = -1;
static int hf_ipv6_fragment_overlap             = -1;
static int hf_ipv6_fragment_overlap_conflict    = -1;
static int hf_ipv6_fragment_multiple_tails      = -1;
static int hf_ipv6_fragment_too_long_fragment   = -1;
static int hf_ipv6_fragment_error               = -1;
static int hf_ipv6_fragment_count               = -1;
static int hf_ipv6_reassembled_in               = -1;
static int hf_ipv6_reassembled_length           = -1;
static int hf_ipv6_reassembled_data             = -1;

static int hf_ipv6_mipv6_home_address           = -1;

static int hf_ipv6_routing_hdr_rpl_cmprI  = -1;
static int hf_ipv6_routing_hdr_rpl_cmprE  = -1;
static int hf_ipv6_routing_hdr_rpl_pad    = -1;
static int hf_ipv6_routing_hdr_rpl_reserved = -1;
static int hf_ipv6_routing_hdr_rpl_segments = -1;
static int hf_ipv6_routing_hdr_rpl_addr = -1;
static int hf_ipv6_routing_hdr_rpl_fulladdr = -1;

static int hf_ipv6_shim6                = -1;
static int hf_ipv6_shim6_nxt            = -1;
static int hf_ipv6_shim6_len            = -1;
static int hf_ipv6_shim6_p              = -1;
/* context tag is 49 bits, cannot be used for filter yet */
static int hf_ipv6_shim6_ct             = -1;
static int hf_ipv6_shim6_type           = -1;
static int hf_ipv6_shim6_proto          = -1;
static int hf_ipv6_shim6_checksum       = -1;
static int hf_ipv6_shim6_checksum_bad   = -1;
static int hf_ipv6_shim6_checksum_good  = -1;
static int hf_ipv6_shim6_inonce         = -1; /* also for request nonce */
static int hf_ipv6_shim6_rnonce         = -1;
static int hf_ipv6_shim6_precvd         = -1;
static int hf_ipv6_shim6_psent          = -1;
static int hf_ipv6_shim6_psrc           = -1;
static int hf_ipv6_shim6_pdst           = -1;
static int hf_ipv6_shim6_pnonce         = -1;
static int hf_ipv6_shim6_pdata          = -1;
static int hf_ipv6_shim6_sulid          = -1;
static int hf_ipv6_shim6_rulid          = -1;
static int hf_ipv6_shim6_reap           = -1;
static int hf_ipv6_shim6_opt_type       = -1;
static int hf_ipv6_shim6_opt_len        = -1;
static int hf_ipv6_shim6_opt_total_len  = -1;
static int hf_ipv6_shim6_opt_loc_verif_methods = -1;
static int hf_ipv6_shim6_opt_critical   = -1;
static int hf_ipv6_shim6_opt_loclist    = -1;
static int hf_ipv6_shim6_locator        = -1;
static int hf_ipv6_shim6_loc_flag       = -1;
static int hf_ipv6_shim6_loc_prio       = -1;
static int hf_ipv6_shim6_loc_weight     = -1;
static int hf_ipv6_shim6_opt_locnum     = -1;
static int hf_ipv6_shim6_opt_elemlen    = -1;
static int hf_ipv6_shim6_opt_fii        = -1;
static int hf_ipv6_traffic_class_dscp   = -1;
static int hf_ipv6_traffic_class_ect    = -1;
static int hf_ipv6_traffic_class_ce     = -1;

#ifdef HAVE_GEOIP_V6
static int hf_geoip_country             = -1;
static int hf_geoip_city                = -1;
static int hf_geoip_org                 = -1;
static int hf_geoip_isp                 = -1;
static int hf_geoip_asnum               = -1;
static int hf_geoip_lat                 = -1;
static int hf_geoip_lon                 = -1;
static int hf_geoip_src_country         = -1;
static int hf_geoip_src_city            = -1;
static int hf_geoip_src_org             = -1;
static int hf_geoip_src_isp             = -1;
static int hf_geoip_src_asnum           = -1;
static int hf_geoip_src_lat             = -1;
static int hf_geoip_src_lon             = -1;
static int hf_geoip_dst_country         = -1;
static int hf_geoip_dst_city            = -1;
static int hf_geoip_dst_org             = -1;
static int hf_geoip_dst_isp             = -1;
static int hf_geoip_dst_asnum           = -1;
static int hf_geoip_dst_lat             = -1;
static int hf_geoip_dst_lon             = -1;
#endif /* HAVE_GEOIP_V6 */

static gint ett_ipv6                    = -1;
static gint ett_ipv6_opt                = -1;
static gint ett_ipv6_opt_flag           = -1;
static gint ett_ipv6_version            = -1;
static gint ett_ipv6_shim6              = -1;
static gint ett_ipv6_shim6_option       = -1;
static gint ett_ipv6_shim6_locators     = -1;
static gint ett_ipv6_shim6_verif_methods = -1;
static gint ett_ipv6_shim6_loc_pref     = -1;
static gint ett_ipv6_shim6_probes_sent  = -1;
static gint ett_ipv6_shim6_probe_sent   = -1;
static gint ett_ipv6_shim6_probes_rcvd  = -1;
static gint ett_ipv6_shim6_probe_rcvd   = -1;
static gint ett_ipv6_shim6_cksum        = -1;
static gint ett_ipv6_fragments          = -1;
static gint ett_ipv6_fragment           = -1;
static gint ett_ipv6_traffic_class      = -1;

#ifdef HAVE_GEOIP_V6
static gint ett_geoip_info              = -1;
#endif /* HAVE_GEOIP_V6 */


static const fragment_items ipv6_frag_items = {
        &ett_ipv6_fragment,
        &ett_ipv6_fragments,
        &hf_ipv6_fragments,
        &hf_ipv6_fragment,
        &hf_ipv6_fragment_overlap,
        &hf_ipv6_fragment_overlap_conflict,
        &hf_ipv6_fragment_multiple_tails,
        &hf_ipv6_fragment_too_long_fragment,
        &hf_ipv6_fragment_error,
        &hf_ipv6_fragment_count,
        &hf_ipv6_reassembled_in,
        &hf_ipv6_reassembled_length,
        &hf_ipv6_reassembled_data,
        "IPv6 fragments"
};

static dissector_handle_t data_handle;

static dissector_table_t ip_dissector_table;

/* Reassemble fragmented datagrams */
static gboolean ipv6_reassemble = TRUE;

/* Place IPv6 summary in proto tree */
static gboolean ipv6_summary_in_tree = TRUE;

#ifdef HAVE_GEOIP_V6
/* Look up addresses in GeoIP */
static gboolean ipv6_use_geoip = TRUE;
#endif /* HAVE_GEOIP_V6 */

/* Perform strict RFC adherence checking */
static gboolean g_ipv6_rpl_srh_strict_rfc_checking = FALSE;

#ifndef offsetof
#define offsetof(type, member)  ((size_t)(&((type *)0)->member))
#endif

/*
 * defragmentation of IPv6
 */
static reassembly_table ipv6_reassembly_table;

/* http://www.iana.org/assignments/icmpv6-parameters (last updated 2012-12-22) */
static const value_string ipv6_opt_vals[] = {
    { IP6OPT_PAD1, "Pad1" },
    { IP6OPT_PADN, "PadN" },
    { IP6OPT_TEL, "Tunnel Encapsulation Limit" },
    { IP6OPT_RTALERT, "Router Alert" },
    { IP6OPT_CALIPSO, "Calipso" },
    { IP6OPT_QUICKSTART, "Quick Start" },
    { IP6OPT_ENDI, "Endpoint Identification" },
    { IP6OPT_EXP_1E, "Experimental (0x1E)" },
    { IP6OPT_EXP_3E, "Experimental (0x3E)" },
    { IP6OPT_EXP_5E, "Experimental (0x5E)" },
    { IP6OPT_RPL, "RPL Option" },
    { IP6OPT_EXP_7E, "Experimental (0x7E)" },
    { IP6OPT_EXP_9E, "Experimental (0x9E)" },
    { IP6OPT_EXP_BE, "Experimental (0xBE)" },
    { IP6OPT_JUMBO, "Jumbo" },
    { IP6OPT_HOME_ADDRESS, "Home Address" },
    { IP6OPT_EXP_DE, "Experimental (0xDE)" },
    { IP6OPT_EXP_FE, "Experimental (0xFE)" },
    { 0, NULL }
};


void
capture_ipv6(const guchar *pd, int offset, int len, packet_counts *ld)
{
  guint8 nxt;
  int advance;

  if (!BYTES_ARE_IN_FRAME(offset, len, 4+4+16+16)) {
    ld->other++;
    return;
  }
  nxt = pd[offset+6];           /* get the "next header" value */
  offset += 4+4+16+16;          /* skip past the IPv6 header */

again:
   switch (nxt) {
   case IP_PROTO_HOPOPTS:
   case IP_PROTO_ROUTING:
   case IP_PROTO_DSTOPTS:
     if (!BYTES_ARE_IN_FRAME(offset, len, 2)) {
       ld->other++;
       return;
     }
     nxt = pd[offset];
     advance = (pd[offset+1] + 1) << 3;
     if (!BYTES_ARE_IN_FRAME(offset, len, advance)) {
       ld->other++;
       return;
     }
     offset += advance;
     goto again;
   case IP_PROTO_FRAGMENT:
     if (!BYTES_ARE_IN_FRAME(offset, len, 2)) {
       ld->other++;
       return;
     }
     nxt = pd[offset];
     advance = 8;
     if (!BYTES_ARE_IN_FRAME(offset, len, advance)) {
       ld->other++;
       return;
     }
     offset += advance;
     goto again;
   case IP_PROTO_AH:
     if (!BYTES_ARE_IN_FRAME(offset, len, 2)) {
       ld->other++;
       return;
     }
     nxt = pd[offset];
     advance = 8 + ((pd[offset+1] - 1) << 2);
     if (!BYTES_ARE_IN_FRAME(offset, len, advance)) {
       ld->other++;
       return;
     }
     offset += advance;
     goto again;
   case IP_PROTO_SHIM6:
   case IP_PROTO_SHIM6_OLD:
     if (!BYTES_ARE_IN_FRAME(offset, len, 2)) {
       ld->other++;
       return;
     }
     nxt = pd[offset];
     advance = (pd[offset+1] + 1) << 3;
     if (!BYTES_ARE_IN_FRAME(offset, len, advance)) {
       ld->other++;
       return;
     }
     offset += advance;
     goto again;
   }

  switch(nxt) {
    case IP_PROTO_SCTP:
      ld->sctp++;
      break;
    case IP_PROTO_TCP:
      ld->tcp++;
      break;
    case IP_PROTO_UDP:
    case IP_PROTO_UDPLITE:
      ld->udp++;
      break;
    case IP_PROTO_ICMP:
    case IP_PROTO_ICMPV6:       /* XXX - separate counters? */
      ld->icmp++;
      break;
    case IP_PROTO_OSPF:
      ld->ospf++;
      break;
    case IP_PROTO_GRE:
      ld->gre++;
      break;
    case IP_PROTO_VINES:
      ld->vines++;
      break;
    default:
      ld->other++;
  }
}

#ifdef HAVE_GEOIP_V6
static void
add_geoip_info_entry(proto_tree *geoip_info_item, tvbuff_t *tvb, gint offset, const struct e_in6_addr *ip, int isdst)
{
  proto_tree *geoip_info_tree;

  guint num_dbs = geoip_db_num_dbs();
  guint item_cnt = 0;
  guint dbnum;

  geoip_info_tree = proto_item_add_subtree(geoip_info_item, ett_geoip_info);

  for (dbnum = 0; dbnum < num_dbs; dbnum++) {
    const char *geoip_str = geoip_db_lookup_ipv6(dbnum, *ip, NULL);
    int db_type = geoip_db_type(dbnum);

    int geoip_hf, geoip_local_hf;

    switch (db_type) {
      case GEOIP_COUNTRY_EDITION_V6:
        geoip_hf = hf_geoip_country;
        geoip_local_hf = (isdst) ? hf_geoip_dst_country : hf_geoip_src_country;
        break;
#if NUM_DB_TYPES > 31
      case GEOIP_CITY_EDITION_REV0_V6:
      case GEOIP_CITY_EDITION_REV1_V6:
        geoip_hf = hf_geoip_city;
        geoip_local_hf = (isdst) ? hf_geoip_dst_city : hf_geoip_src_city;
        break;
      case GEOIP_ORG_EDITION_V6:
        geoip_hf = hf_geoip_org;
        geoip_local_hf = (isdst) ? hf_geoip_dst_org : hf_geoip_src_org;
        break;
      case GEOIP_ISP_EDITION_V6:
        geoip_hf = hf_geoip_isp;
        geoip_local_hf = (isdst) ? hf_geoip_dst_isp : hf_geoip_src_isp;
        break;
      case GEOIP_ASNUM_EDITION_V6:
        geoip_hf = hf_geoip_asnum;
        geoip_local_hf = (isdst) ? hf_geoip_dst_asnum : hf_geoip_src_asnum;
        break;
#endif /* DB_NUM_TYPES */
      case WS_LAT_FAKE_EDITION:
        geoip_hf = hf_geoip_lat;
        geoip_local_hf = (isdst) ? hf_geoip_dst_lat : hf_geoip_src_lat;
        break;
      case WS_LON_FAKE_EDITION:
        geoip_hf = hf_geoip_lon;
        geoip_local_hf = (isdst) ? hf_geoip_dst_lon : hf_geoip_src_lon;
        break;
      default:
        continue;
        break;
    }

    if (geoip_str) {
      proto_item *item;
      if (db_type == WS_LAT_FAKE_EDITION || db_type == WS_LON_FAKE_EDITION) {
        /* Convert latitude, longitude to double. Fix bug #5077 */
        item = proto_tree_add_double_format_value(geoip_info_tree, geoip_local_hf, tvb,
          offset, 16, g_ascii_strtod(geoip_str, NULL), "%s", geoip_str);
        PROTO_ITEM_SET_GENERATED(item);
        item  = proto_tree_add_double_format_value(geoip_info_tree, geoip_hf, tvb,
          offset, 16, g_ascii_strtod(geoip_str, NULL), "%s", geoip_str);
        PROTO_ITEM_SET_GENERATED(item);
        PROTO_ITEM_SET_HIDDEN(item);
      } else {
        item = proto_tree_add_unicode_string(geoip_info_tree, geoip_local_hf, tvb,
          offset, 16, geoip_str);
        PROTO_ITEM_SET_GENERATED(item);
        item  = proto_tree_add_unicode_string(geoip_info_tree, geoip_hf, tvb,
          offset, 16, geoip_str);
        PROTO_ITEM_SET_GENERATED(item);
        PROTO_ITEM_SET_HIDDEN(item);
      }

      item_cnt++;
      proto_item_append_text(geoip_info_item, "%s%s", plurality(item_cnt, "", ", "), geoip_str);
    }
  }

  if (item_cnt == 0)
    proto_item_append_text(geoip_info_item, "Unknown");
}

static void
add_geoip_info(proto_tree *tree, tvbuff_t *tvb, gint offset, const struct e_in6_addr *src, const struct e_in6_addr *dst)
{
  guint num_dbs;
  proto_item *geoip_info_item;

  num_dbs = geoip_db_num_dbs();
  if (num_dbs < 1)
        return;

  geoip_info_item = proto_tree_add_text(tree, tvb, offset + IP6H_SRC, 16, "Source GeoIP: ");
  PROTO_ITEM_SET_GENERATED(geoip_info_item);
  add_geoip_info_entry(geoip_info_item, tvb, offset + IP6H_SRC, src, 0);

  geoip_info_item = proto_tree_add_text(tree, tvb, offset + IP6H_DST, 16, "Destination GeoIP: ");
  PROTO_ITEM_SET_GENERATED(geoip_info_item);
  add_geoip_info_entry(geoip_info_item, tvb, offset + IP6H_DST, dst, 1);
}
#endif /* HAVE_GEOIP_V6 */

static void
ipv6_reassemble_init(void)
{
  reassembly_table_init(&ipv6_reassembly_table,
                        &addresses_reassembly_table_functions);
}

enum {
  IPv6_RT_HEADER_SOURCE_ROUTING=0,
  IPv6_RT_HEADER_NIMROD,
  IPv6_RT_HEADER_MobileIP,
  IPv6_RT_HEADER_RPL
};

/* Routing Header Types */
static const value_string routing_header_type[] = {
  { IPv6_RT_HEADER_SOURCE_ROUTING, "IPv6 Source Routing" },
  { IPv6_RT_HEADER_NIMROD, "Nimrod" },
  { IPv6_RT_HEADER_MobileIP, "Mobile IP" },
  { IPv6_RT_HEADER_RPL, "RPL" },
  { 0, NULL }
};

static int
dissect_routing6(tvbuff_t *tvb, int offset, proto_tree *tree, packet_info *pinfo) {
    struct ip6_rthdr rt;
    guint len, seg_left;
    proto_tree *rthdr_tree;
    proto_item *ti;
    guint8 buf[sizeof(struct ip6_rthdr0) + sizeof(struct e_in6_addr) * 23];

    tvb_memcpy(tvb, (guint8 *)&rt, offset, sizeof(rt));
    len = (rt.ip6r_len + 1) << 3;

    /* Assigning seg_left and the if (seg_left) {} blocks of code that follow,
     * along with any expert_add_info_format() calls, all need to execute when
     * appropriate, regardless of whether the tree is NULL or not. */
    if (1) {
        /* !!! specify length */
        ti = proto_tree_add_uint_format(tree, hf_ipv6_routing_hdr_opt, tvb,
                      offset, len, rt.ip6r_type,
                      "Routing Header, Type : %s (%u)",
                      val_to_str_const(rt.ip6r_type, routing_header_type, "Unknown"),
                      rt.ip6r_type);
        rthdr_tree = proto_item_add_subtree(ti, ett_ipv6);

        proto_tree_add_text(rthdr_tree, tvb,
            offset + (int)offsetof(struct ip6_rthdr, ip6r_nxt), 1,
            "Next header: %s (%u)", ipprotostr(rt.ip6r_nxt), rt.ip6r_nxt);

        proto_tree_add_text(rthdr_tree, tvb,
            offset + (int)offsetof(struct ip6_rthdr, ip6r_len), 1,
            "Length: %u (%d bytes)", rt.ip6r_len, len);

        proto_tree_add_item(rthdr_tree, hf_ipv6_routing_hdr_type, tvb,
                  offset + (int)offsetof(struct ip6_rthdr, ip6r_type), 1, ENC_BIG_ENDIAN);

        proto_tree_add_item(rthdr_tree, hf_ipv6_routing_hdr_left, tvb,
                  offset + (int)offsetof(struct ip6_rthdr, ip6r_segleft), 1, ENC_BIG_ENDIAN);

        seg_left = tvb_get_guint8(tvb, offset + (int)offsetof(struct ip6_rthdr, ip6r_segleft));

        if (rt.ip6r_type == IPv6_RT_HEADER_SOURCE_ROUTING && len <= sizeof(buf)) {
            struct e_in6_addr *a;
            int n;
            struct ip6_rthdr0 *rt0;

            tvb_memcpy(tvb, buf, offset, len);
            rt0 = (struct ip6_rthdr0 *)buf;

            for (a = rt0->ip6r0_addr, n = 0;
                    a < (struct e_in6_addr *)(buf + len); a++, n++) {

              proto_tree_add_item(rthdr_tree, hf_ipv6_routing_hdr_addr, tvb,
                              offset + (int)(offsetof(struct ip6_rthdr0, ip6r0_addr)
                                     + n * sizeof(struct e_in6_addr)),
                              (int)sizeof(struct e_in6_addr), ENC_NA);
              if (seg_left)
                  TVB_SET_ADDRESS(&pinfo->dst, AT_IPv6, tvb,
                                  offset + (int)offsetof(struct ip6_rthdr0, ip6r0_addr) + n * (int)sizeof(struct e_in6_addr), 16);
            }
        }
        if (rt.ip6r_type == IPv6_RT_HEADER_MobileIP) {
            proto_tree_add_item(rthdr_tree, hf_ipv6_mipv6_home_address, tvb,
                              offset + 8, 16, ENC_NA);
            if (seg_left)
                TVB_SET_ADDRESS(&pinfo->dst, AT_IPv6, tvb, offset + 8, 16);
        }
        if (rt.ip6r_type == IPv6_RT_HEADER_RPL) {
            guint8 cmprI;
            guint8 cmprE;
            guint8 pad;
            guint32 reserved;
            gint segments;

            /* IPv6 destination address used for elided bytes */
            struct e_in6_addr dstAddr;
            /* IPv6 source address used for strict checking */
            struct e_in6_addr srcAddr;
            offset += 4;
            memcpy((guint8 *)&dstAddr, (guint8 *)pinfo->dst.data, pinfo->dst.len);
            memcpy((guint8 *)&srcAddr, (guint8 *)pinfo->src.data, pinfo->src.len);

            /* from RFC6554: Multicast addresses MUST NOT appear in the IPv6 Destination Address field */
            if(g_ipv6_rpl_srh_strict_rfc_checking && E_IN6_IS_ADDR_MULTICAST(&dstAddr)){
                expert_add_info_format(pinfo, ti, PI_PROTOCOL, PI_WARN, "Destination address must not be a multicast address ");
            }

            proto_tree_add_item(rthdr_tree, hf_ipv6_routing_hdr_rpl_cmprI, tvb, offset, 4, ENC_BIG_ENDIAN);
            proto_tree_add_item(rthdr_tree, hf_ipv6_routing_hdr_rpl_cmprE, tvb, offset, 4, ENC_BIG_ENDIAN);
            proto_tree_add_item(rthdr_tree, hf_ipv6_routing_hdr_rpl_pad, tvb, offset, 4, ENC_BIG_ENDIAN);

            cmprI = tvb_get_guint8(tvb, offset) & 0xF0;
            cmprE = tvb_get_guint8(tvb, offset) & 0x0F;
            pad   = tvb_get_guint8(tvb, offset + 1) & 0xF0;

            /* Shift bytes over */
            cmprI >>= 4;
            pad >>= 4;

            /* from RFC6554: when CmprI and CmprE are both 0, Pad MUST carry a value of 0 */
            if(g_ipv6_rpl_srh_strict_rfc_checking && (cmprI == 0 && cmprE == 0 && pad != 0)){
                expert_add_info_format(pinfo, ti, PI_PROTOCOL, PI_WARN, "When cmprI equals 0 and cmprE equals 0, pad MUST equal 0 but instead was %d", pad);
            }

            proto_tree_add_item(rthdr_tree, hf_ipv6_routing_hdr_rpl_reserved, tvb, offset, 4, ENC_BIG_ENDIAN);
            reserved = tvb_get_bits32(tvb, ((offset + 1) * 8) + 4, 20, ENC_BIG_ENDIAN);

            if(g_ipv6_rpl_srh_strict_rfc_checking && reserved != 0){
                expert_add_info_format(pinfo, ti, PI_PROTOCOL, PI_WARN, "Reserved field must equal 0 but instead was %d", reserved);
            }

            /* from RFC6554:
            n = (((Hdr Ext Len * 8) - Pad - (16 - CmprE)) / (16 - CmprI)) + 1 */
            segments = (((rt.ip6r_len * 8) - pad - (16 - cmprE)) / (16 - cmprI)) + 1;
            ti = proto_tree_add_int(rthdr_tree, hf_ipv6_routing_hdr_rpl_segments, tvb, offset, 2, segments);
            PROTO_ITEM_SET_GENERATED(ti);

            if (segments < 0) {
                /* This error should always be reported */
                expert_add_info_format(pinfo, ti, PI_MALFORMED, PI_ERROR, "Calculated total segments must be greater than or equal to 0, instead was %d", segments);
            } else {

                offset += 4;

                /* We use cmprI for internal (e.g.: not last) address for how many bytes to elide, so actual bytes present = 16-CmprI */
                while(segments > 1) {
                    struct e_in6_addr addr;

                    proto_tree_add_item(rthdr_tree, hf_ipv6_routing_hdr_rpl_addr, tvb, offset, (16-cmprI), ENC_NA);
                    /* Display Full Address */
                    memcpy((guint8 *)&addr, (guint8 *)&dstAddr, sizeof(dstAddr));
                    tvb_memcpy(tvb, (guint8 *)&addr + cmprI, offset, (16-cmprI));
                    ti = proto_tree_add_ipv6(rthdr_tree, hf_ipv6_routing_hdr_rpl_fulladdr, tvb, offset, (16-cmprI), (guint8 *)&addr);
                    PROTO_ITEM_SET_GENERATED(ti);
                    offset += (16-cmprI);
                    segments--;

                    if(g_ipv6_rpl_srh_strict_rfc_checking){
                        /* from RFC6554: */
                        /* The SRH MUST NOT specify a path that visits a node more than once. */
                        /* To do this, we will just check the current 'addr' against the next addresses */
                        gint tempSegments;
                        gint tempOffset;
                        tempSegments = segments; /* Has already been decremented above */
                        tempOffset = offset; /* Has already been moved */
                        while(tempSegments > 1) {
                            struct e_in6_addr tempAddr;
                            memcpy((guint8 *)&tempAddr, (guint8 *)&dstAddr, sizeof(dstAddr));
                            tvb_memcpy(tvb, (guint8 *)&tempAddr + cmprI, tempOffset, (16-cmprI));
                            /* Compare the addresses */
                            if (memcmp(addr.bytes, tempAddr.bytes, 16) == 0) {
                                /* Found a later address that is the same */
                                expert_add_info_format(pinfo, ti, PI_PROTOCOL, PI_ERROR, "Multiple instances of the same address must not appear in the source route list");
                                break;
                            }
                            tempOffset += (16-cmprI);
                            tempSegments--;
                        }
                        if (tempSegments == 1) {
                            struct e_in6_addr tempAddr;

                            memcpy((guint8 *)&tempAddr, (guint8 *)&dstAddr, sizeof(dstAddr));
                            tvb_memcpy(tvb, (guint8 *)&tempAddr + cmprE, tempOffset, (16-cmprE));
                            /* Compare the addresses */
                            if (memcmp(addr.bytes, tempAddr.bytes, 16) == 0) {
                                /* Found a later address that is the same */
                                expert_add_info_format(pinfo, ti, PI_PROTOCOL, PI_ERROR, "Multiple instances of the same address must not appear in the source route list");
                            }
                        }
                        /* IPv6 Source and Destination addresses of the encapsulating datagram (MUST) not appear in the SRH*/
                        if (memcmp(addr.bytes, srcAddr.bytes, 16) == 0) {
                            expert_add_info_format(pinfo, ti, PI_PROTOCOL, PI_ERROR, "Source address must not appear in the source route list");
                        }

                        if (memcmp(addr.bytes, dstAddr.bytes, 16) == 0) {
                            expert_add_info_format(pinfo, ti, PI_PROTOCOL, PI_ERROR, "Destination address must not appear in the source route list");
                        }

                        /* Multicast addresses MUST NOT appear in the in SRH */
                        if(E_IN6_IS_ADDR_MULTICAST(&addr)){
                            expert_add_info_format(pinfo, ti, PI_PROTOCOL, PI_ERROR, "Multicast addresses must not appear in the source route list");
                        }
                    }
                }

                /* We use cmprE for last address for how many bytes to elide, so actual bytes present = 16-CmprE */
                if (segments == 1) {
                    struct e_in6_addr addr;

                    proto_tree_add_item(rthdr_tree, hf_ipv6_routing_hdr_rpl_addr, tvb, offset, (16-cmprI), ENC_NA);
                    /* Display Full Address */
                    memcpy((guint8 *)&addr, (guint8 *)&dstAddr, sizeof(dstAddr));
                    tvb_memcpy(tvb, (guint8 *)&addr + cmprE, offset, (16-cmprE));
                    ti = proto_tree_add_ipv6(rthdr_tree, hf_ipv6_routing_hdr_rpl_fulladdr, tvb, offset, (16-cmprE), (guint8 *)&addr);
                    PROTO_ITEM_SET_GENERATED(ti);
                    /* offset += (16-cmprE); */

                    if(g_ipv6_rpl_srh_strict_rfc_checking){
                        /* IPv6 Source and Destination addresses of the encapsulating datagram (MUST) not appear in the SRH*/
                        if (memcmp(addr.bytes, srcAddr.bytes, 16) == 0) {
                            expert_add_info_format(pinfo, ti, PI_PROTOCOL, PI_ERROR, "Source address must not appear in the source route list");
                        }

                        if (memcmp(addr.bytes, dstAddr.bytes, 16) == 0) {
                            expert_add_info_format(pinfo, ti, PI_PROTOCOL, PI_ERROR, "Destination address must not appear in the source route list");
                        }

                        /* Multicast addresses MUST NOT appear in the in SRH */
                        if(E_IN6_IS_ADDR_MULTICAST(&addr)){
                            expert_add_info_format(pinfo, ti, PI_PROTOCOL, PI_ERROR, "Multicast addresses must not appear in the source route list");
                        }
                    }
                }
            }
        }
    }

    return len;
}

static int
dissect_frag6(tvbuff_t *tvb, int offset, packet_info *pinfo, proto_tree *tree,
    guint16 *offlg, guint32 *ident) {
    struct ip6_frag frag;
    int len;
    proto_item *ti;
    proto_tree *rthdr_tree;

    tvb_memcpy(tvb, (guint8 *)&frag, offset, sizeof(frag));
    len = sizeof(frag);
    frag.ip6f_offlg = g_ntohs(frag.ip6f_offlg);
    frag.ip6f_ident = g_ntohl(frag.ip6f_ident);
    *offlg = frag.ip6f_offlg;
    *ident = frag.ip6f_ident;
    if (check_col(pinfo->cinfo, COL_INFO)) {
        col_add_fstr(pinfo->cinfo, COL_INFO,
            "IPv6 fragment (nxt=%s (%u) off=%u id=0x%x)",
            ipprotostr(frag.ip6f_nxt), frag.ip6f_nxt,
            (frag.ip6f_offlg & IP6F_OFF_MASK) >> IP6F_OFF_SHIFT, frag.ip6f_ident);
    }
    if (tree) {
           ti = proto_tree_add_text(tree, tvb, offset, len,
                           "Fragmentation Header");
           rthdr_tree = proto_item_add_subtree(ti, ett_ipv6);

           proto_tree_add_item(rthdr_tree, hf_ipv6_frag_nxt, tvb,
                         offset + (int)offsetof(struct ip6_frag, ip6f_nxt), 1,
                         ENC_BIG_ENDIAN);

           proto_tree_add_item(rthdr_tree, hf_ipv6_frag_reserved, tvb,
                         offset + (int)offsetof(struct ip6_frag, ip6f_reserved), 1,
                         ENC_BIG_ENDIAN);

           proto_tree_add_item(rthdr_tree, hf_ipv6_frag_offset, tvb,
                    offset + (int)offsetof(struct ip6_frag, ip6f_offlg), 2, ENC_BIG_ENDIAN);
           proto_tree_add_item(rthdr_tree, hf_ipv6_frag_reserved_bits, tvb,
                    offset + (int)offsetof(struct ip6_frag, ip6f_offlg), 2, ENC_BIG_ENDIAN);
           proto_tree_add_item(rthdr_tree, hf_ipv6_frag_more, tvb,
                    offset + (int)offsetof(struct ip6_frag, ip6f_offlg), 2, ENC_BIG_ENDIAN);
           proto_tree_add_item(rthdr_tree, hf_ipv6_frag_id, tvb,
                    offset + (int)offsetof(struct ip6_frag, ip6f_ident), 4, ENC_BIG_ENDIAN);
    }
    return len;
}

static const value_string rtalertvals[] = {
    { IP6OPT_RTALERT_MLD, "MLD" },
    { IP6OPT_RTALERT_RSVP, "RSVP" },
    { IP6OPT_RTALERT_ACTNET, "Active Network" },
    { 0, NULL }
};

static int
dissect_unknown_option(tvbuff_t *tvb, int offset, proto_tree *tree)
{
    int len;
    proto_tree *unkopt_tree;
    proto_item *ti, *ti_len;

    len = (tvb_get_guint8(tvb, offset + 1) + 1) << 3;

    if (tree) {
        /* !!! specify length */
        ti = proto_tree_add_item(tree, hf_ipv6_unk_hdr, tvb, offset, len, ENC_NA);

        unkopt_tree = proto_item_add_subtree(ti, ett_ipv6);

        proto_tree_add_item(unkopt_tree, hf_ipv6_nxt, tvb, offset, 1, ENC_NA);
        offset += 1;

        ti_len = proto_tree_add_item(unkopt_tree, hf_ipv6_opt_length, tvb, offset, 1, ENC_NA);
        proto_item_append_text(ti_len, " (%d byte%s)", len, plurality(len, "", "s"));
        /* offset += 1; */
    }
    return len;
}

static int
dissect_opts(tvbuff_t *tvb, int offset, proto_tree *tree, packet_info * pinfo, const int hf_option_item)
{
    int len;
    int offset_end;
    proto_tree *dstopt_tree, *opt_tree;
    proto_item *ti, *ti_len, *ti_opt, *ti_opt_len;
    guint8 opt_len, opt_type;

    len = (tvb_get_guint8(tvb, offset + 1) + 1) << 3;
    offset_end = offset + len;

    if (tree) {
        /* !!! specify length */
        ti = proto_tree_add_item(tree, hf_option_item, tvb, offset, len, ENC_NA);

        dstopt_tree = proto_item_add_subtree(ti, ett_ipv6);

        proto_tree_add_item(dstopt_tree, hf_ipv6_nxt, tvb, offset, 1, ENC_NA);
        offset += 1;

        ti_len = proto_tree_add_item(dstopt_tree, hf_ipv6_opt_length, tvb, offset, 1, ENC_NA);
        proto_item_append_text(ti_len, " (%d byte%s)", len, plurality(len, "", "s"));
        offset += 1;

        while (offset_end > offset) {
            /* there are more options */

            /* IPv6 Option */
            ti_opt = proto_tree_add_item(dstopt_tree, hf_ipv6_opt, tvb, offset, 1, ENC_NA);
            opt_tree = proto_item_add_subtree(ti_opt, ett_ipv6_opt);

            /* Option type */
            proto_tree_add_item(opt_tree, hf_ipv6_opt_type, tvb, offset, 1, ENC_BIG_ENDIAN);
            opt_type = tvb_get_guint8(tvb, offset);

            /* Add option name to option root label */
            proto_item_append_text(ti_opt, " (%s", val_to_str(opt_type, ipv6_opt_vals, "Unknown %d"));

            /* The Pad1 option is a special case, and contains no data. */
            if (opt_type == IP6OPT_PAD1) {
                proto_tree_add_item(opt_tree, hf_ipv6_opt_pad1, tvb, offset, 1, ENC_NA);
                offset += 1;
                proto_item_append_text(ti_opt, ")");
                continue;
            }
            offset += 1;

            /* Option length */
            ti_opt_len = proto_tree_add_item(opt_tree, hf_ipv6_opt_length, tvb, offset, 1, ENC_BIG_ENDIAN);
            opt_len = tvb_get_guint8(tvb, offset);
            proto_item_set_len(ti_opt, opt_len + 2);
            offset += 1;

            switch (opt_type) {
            case IP6OPT_PADN:
                /* RFC 2460 states :
                 * "The PadN option is used to insert two or more octets of
                 * padding into the Options area of a header.  For N octets of
                 * padding, the Opt Data Len field contains the value N-2, and
                 * the Option Data consists of N-2 zero-valued octets."
                 */
                proto_tree_add_item(opt_tree, hf_ipv6_opt_padn, tvb,
                                            offset, opt_len, ENC_NA);
                offset += opt_len;
                break;
            case IP6OPT_TEL:
                if (opt_len != 1) {
                    expert_add_info_format(pinfo, ti_opt_len, PI_MALFORMED, PI_ERROR,
                        "Tunnel Encapsulation Limit: Invalid length (%u bytes)", opt_len);
                }
                proto_tree_add_item(opt_tree, hf_ipv6_opt_tel, tvb,
                                            offset, 1, ENC_BIG_ENDIAN);
                offset += 1;
                break;
            case IP6OPT_JUMBO:
                if (opt_len != 4) {
                    expert_add_info_format(pinfo, ti_opt_len, PI_MALFORMED, PI_ERROR,
                        "Jumbo payload: Invalid length (%u bytes)", opt_len);
                }
                proto_tree_add_item(opt_tree, hf_ipv6_opt_jumbo, tvb,
                                            offset, 4, ENC_BIG_ENDIAN);
                offset += 4;
                break;
            case IP6OPT_RTALERT:
              {
                if (opt_len != 2) {
                    expert_add_info_format(pinfo, ti_opt_len, PI_MALFORMED, PI_ERROR,
                            "Router alert: Invalid Length (%u bytes)",
                            opt_len + 2);
                }
                proto_tree_add_item(opt_tree, hf_ipv6_opt_rtalert, tvb,
                                            offset, 2, ENC_BIG_ENDIAN);
                offset += 2;
                break;
              }
            case IP6OPT_HOME_ADDRESS:
                if (opt_len != 16) {
                    expert_add_info_format(pinfo, ti_opt_len, PI_MALFORMED, PI_ERROR,
                        "Home Address: Invalid length (%u bytes)", opt_len);
                }
                proto_tree_add_item(opt_tree, hf_ipv6_mipv6_home_address, tvb,
                                    offset, 16, ENC_NA);
                TVB_SET_ADDRESS(&pinfo->src, AT_IPv6, tvb, offset, 16);
                offset += 16;
                break;
            case IP6OPT_CALIPSO:
              {
                guint8 cmpt_length;
                proto_tree_add_item(opt_tree, hf_ipv6_opt_calipso_doi, tvb,
                                            offset, 4, ENC_BIG_ENDIAN);
                offset += 4;
                proto_tree_add_item(opt_tree, hf_ipv6_opt_calipso_cmpt_length, tvb,
                                            offset, 1, ENC_BIG_ENDIAN);
                cmpt_length = tvb_get_guint8(tvb, offset);
                offset += 1;
                proto_tree_add_item(opt_tree, hf_ipv6_opt_calipso_sens_level, tvb,
                                            offset, 1, ENC_BIG_ENDIAN);
                offset += 1;
                /* Need to add Check Checksum..*/
                proto_tree_add_item(opt_tree, hf_ipv6_opt_calipso_checksum, tvb,
                                            offset, 2, ENC_BIG_ENDIAN);
                offset += 2;
                proto_tree_add_item(opt_tree, hf_ipv6_opt_calipso_cmpt_bitmap, tvb,
                                              offset, cmpt_length, ENC_NA);
                offset += cmpt_length;
                break;
              }
            case IP6OPT_QUICKSTART:
              {

                guint8 command = tvb_get_guint8(tvb, offset);
                guint8 function = command >> 4;
                guint8 rate = command & QS_RATE_MASK;
                guint8 ttl_diff;

                proto_tree_add_item(opt_tree, hf_ipv6_opt_qs_func, tvb, offset, 1, ENC_NA);

                if (function == QS_RATE_REQUEST) {
                  proto_tree_add_item(opt_tree, hf_ipv6_opt_qs_rate, tvb, offset, 1, ENC_NA);
                  offset += 1;
                  proto_tree_add_item(opt_tree, hf_ipv6_opt_qs_ttl, tvb, offset, 1, ENC_NA);
                  ttl_diff = (pinfo->ip_ttl - tvb_get_guint8(tvb, offset) % 256);
                  offset += 1;
                  ti = proto_tree_add_uint_format_value(opt_tree, hf_ipv6_opt_qs_ttl_diff,
                                                        tvb, offset, 1, ttl_diff,
                                                        "%u", ttl_diff);
                  PROTO_ITEM_SET_GENERATED(ti);
                  proto_item_append_text(ti_opt, ", %s, QS TTL %u, QS TTL diff %u",
                                         val_to_str_ext(rate, &qs_rate_vals_ext, "Unknown (%u)"),
                                         tvb_get_guint8(tvb, offset), ttl_diff);
                  offset += 1;
                  proto_tree_add_item(opt_tree, hf_ipv6_opt_qs_nonce, tvb, offset, 4, ENC_NA);
                  proto_tree_add_item(opt_tree, hf_ipv6_opt_qs_reserved, tvb, offset, 4, ENC_NA);
                  offset += 4;
                } else if (function == QS_RATE_REPORT) {
                  proto_tree_add_item(opt_tree, hf_ipv6_opt_qs_rate, tvb, offset, 1, ENC_NA);
                  offset += 1;
                  proto_item_append_text(ti_opt, ", %s",
                                         val_to_str_ext(rate, &qs_rate_vals_ext, "Unknown (%u)"));
                  proto_tree_add_item(opt_tree, hf_ipv6_opt_qs_unused, tvb, offset, 1, ENC_NA);
                  offset += 1;
                  proto_tree_add_item(opt_tree, hf_ipv6_opt_qs_nonce, tvb, offset, 4, ENC_NA);
                  proto_tree_add_item(opt_tree, hf_ipv6_opt_qs_reserved, tvb, offset, 4, ENC_NA);
                  offset += 4;
                }

              }
              break;
            case IP6OPT_RPL:
              {
                proto_tree *flag_tree;
                proto_item *ti_flag;

                ti_flag = proto_tree_add_item(opt_tree, hf_ipv6_opt_rpl_flag, tvb, offset, 1, ENC_BIG_ENDIAN);
                flag_tree = proto_item_add_subtree(ti_flag, ett_ipv6_opt_flag);
                proto_tree_add_item(flag_tree, hf_ipv6_opt_rpl_flag_o, tvb, offset, 1, ENC_BIG_ENDIAN);
                proto_tree_add_item(flag_tree, hf_ipv6_opt_rpl_flag_r, tvb, offset, 1, ENC_BIG_ENDIAN);
                proto_tree_add_item(flag_tree, hf_ipv6_opt_rpl_flag_f, tvb, offset, 1, ENC_BIG_ENDIAN);
                proto_tree_add_item(flag_tree, hf_ipv6_opt_rpl_flag_rsv, tvb, offset, 1, ENC_BIG_ENDIAN);
                offset +=1;

                proto_tree_add_item(flag_tree, hf_ipv6_opt_rpl_instance_id, tvb, offset, 1, ENC_BIG_ENDIAN);
                offset +=1;

                proto_tree_add_item(flag_tree, hf_ipv6_opt_rpl_senderrank, tvb, offset, 2, ENC_BIG_ENDIAN);
                offset +=2;

                /* TODO: Add dissector of sub TLV */
              }
              break;
            case IP6OPT_EXP_1E:
            case IP6OPT_EXP_3E:
            case IP6OPT_EXP_5E:
            case IP6OPT_EXP_7E:
            case IP6OPT_EXP_9E:
            case IP6OPT_EXP_BE:
            case IP6OPT_EXP_DE:
            case IP6OPT_EXP_FE:
                proto_tree_add_item(opt_tree, hf_ipv6_opt_experimental, tvb,
                                    offset, opt_len, ENC_NA);
                offset += opt_len;
              break;
            default:
                proto_tree_add_item(opt_tree, hf_ipv6_opt_unknown, tvb,
                                    offset, opt_len, ENC_NA);
                offset += opt_len;
                break;
            }
        /* Close the ) to option root label */
        proto_item_append_text(ti_opt, ")");
        }

    }
    return len;
}

static int
dissect_hopopts(tvbuff_t *tvb, int offset, proto_tree *tree, packet_info * pinfo)
{
    return dissect_opts(tvb, offset, tree, pinfo, hf_ipv6_hop_opt);
}

static int
dissect_dstopts(tvbuff_t *tvb, int offset, proto_tree *tree, packet_info * pinfo)
{
    return dissect_opts(tvb, offset, tree, pinfo, hf_ipv6_dst_opt);
}

/* START SHIM6 PART */
static guint16 shim_checksum(const guint8 *ptr, int len)
{
        vec_t cksum_vec[1];

        cksum_vec[0].ptr = ptr;
        cksum_vec[0].len = len;
        return in_cksum(&cksum_vec[0], 1);
}

static int
dissect_shim_hex(tvbuff_t *tvb, int offset, int len, const char *itemname, guint8 bitmask, proto_tree *tree)
{
    proto_item *ti;
    int count;
    gint p;

    p = offset;

    ti = proto_tree_add_text(tree, tvb, offset, len, "%s", itemname);

    proto_item_append_text(ti, " 0x%02x", tvb_get_guint8(tvb, p) & bitmask);
    for (count=1; count<len; count++)
      proto_item_append_text(ti, "%02x", tvb_get_guint8(tvb, p+count));

    return len;
}

static const value_string shimoptvals[] = {
    { SHIM6_OPT_RESPVAL,  "Responder Validator Option" },
    { SHIM6_OPT_LOCLIST,  "Locator List Option" },
    { SHIM6_OPT_LOCPREF,  "Locator Preferences Option" },
    { SHIM6_OPT_CGAPDM,   "CGA Parameter Data Structure Option" },
    { SHIM6_OPT_CGASIG,   "CGA Signature Option" },
    { SHIM6_OPT_ULIDPAIR, "ULID Pair Option" },
    { SHIM6_OPT_FII,      "Forked Instance Identifier Option" },
    { 0, NULL }
};

static const value_string shimverifmethods[] = {
    { SHIM6_VERIF_HBA, "HBA" },
    { SHIM6_VERIF_CGA, "CGA" },
    { 0, NULL }
};

static const value_string shimflags[] _U_ = {
    { SHIM6_FLAG_BROKEN,    "BROKEN" },
    { SHIM6_FLAG_TEMPORARY, "TEMPORARY" },
    { 0, NULL }
};

static const value_string shimreapstates[] = {
    { SHIM6_REAP_OPERATIONAL, "Operational" },
    { SHIM6_REAP_EXPLORING,   "Exploring" },
    { SHIM6_REAP_INBOUNDOK,   "InboundOK" },
    { 0, NULL }
};

static const value_string shim6_protocol[] = {
  { 0, "SHIM6" },
  { 1, "HIP" },
  { 0, NULL }
};


static void
dissect_shim6_opt_loclist(proto_tree * opt_tree, tvbuff_t * tvb, gint *offset)
{
  proto_item * it;
  proto_tree * subtree;
  guint count;
  guint optlen;
  int p = *offset;

  proto_tree_add_item(opt_tree, hf_ipv6_shim6_opt_loclist, tvb, p, 4, ENC_BIG_ENDIAN);
  p += 4;

  optlen = tvb_get_guint8(tvb, p);
  proto_tree_add_item(opt_tree, hf_ipv6_shim6_opt_locnum, tvb, p, 1, ENC_BIG_ENDIAN);
  p++;

  /* Verification Methods */
  it = proto_tree_add_text(opt_tree, tvb, p, optlen,
                            "Locator Verification Methods");
  subtree = proto_item_add_subtree(it, ett_ipv6_shim6_verif_methods);

  for (count=0; count < optlen; count++)
    proto_tree_add_item(subtree, hf_ipv6_shim6_opt_loc_verif_methods, tvb,
                            p+count, 1, ENC_BIG_ENDIAN);
  p += optlen;

  /* Padding, included in length field */
  if ((7 - optlen % 8) > 0) {
      proto_tree_add_text(opt_tree, tvb, p, (7 - optlen % 8), "Padding");
      p += (7 - optlen % 8);
  }

  /* Locators */
  it = proto_tree_add_text(opt_tree, tvb, p, 16 * optlen, "Locators");
  subtree = proto_item_add_subtree(it, ett_ipv6_shim6_locators);

  for (count=0; count < optlen; count++) {
      proto_tree_add_item(subtree, hf_ipv6_shim6_locator, tvb, p, 16, ENC_NA);
      p += 16;
  }
  *offset = p;
}

static void
dissect_shim6_opt_loc_pref(proto_tree * opt_tree, tvbuff_t * tvb, gint *offset, gint len, packet_info *pinfo)
{
  proto_tree * subtree;
  proto_item * it;

  gint p;
  gint optlen;
  gint count;

  p = *offset;

  proto_tree_add_item(opt_tree, hf_ipv6_shim6_opt_loclist, tvb, p, 4, ENC_BIG_ENDIAN);
  p += 4;

  optlen = tvb_get_guint8(tvb, p);
  proto_tree_add_item(opt_tree, hf_ipv6_shim6_opt_elemlen, tvb, p, 1, ENC_BIG_ENDIAN);

  if (optlen < 1 || optlen > 3) {
    it = proto_tree_add_text(opt_tree, tvb, p, 1,
      "Invalid element length: %u",  optlen);
    expert_add_info_format(pinfo, it, PI_MALFORMED, PI_ERROR,
      "Invalid element length: %u", optlen);
    return;
  }

  p++;

  /* Locator Preferences */
  count = 1;
  while (p < len) {
    it = proto_tree_add_text(opt_tree, tvb, p, optlen, "Locator Preferences %u", count);
    subtree = proto_item_add_subtree(it, ett_ipv6_shim6_loc_pref);

    /* Flags */
    if (optlen >= 1)
      proto_tree_add_item(subtree, hf_ipv6_shim6_loc_flag, tvb, p, 1, ENC_BIG_ENDIAN);
    /* Priority */
    if (optlen >= 2)
      proto_tree_add_item(subtree, hf_ipv6_shim6_loc_prio, tvb, p+1, 1, ENC_BIG_ENDIAN);
    /* Weight */
    if (optlen >= 3)
      proto_tree_add_item(subtree, hf_ipv6_shim6_loc_weight, tvb, p+2, 1, ENC_BIG_ENDIAN);
    /*
     * Shim6 Draft 08 doesn't specify the format when the Element length is
     * more than three, except that any such formats MUST be defined so that
     * the first three octets are the same as in the above case, that is, a
     * of a 1 octet flags field followed by a 1 octet priority field, and a
     * 1 octet weight field.
     */
    p += optlen;
    count++;
  }
  *offset = p;
}


static int
dissect_shimopts(tvbuff_t *tvb, int offset, proto_tree *tree, packet_info *pinfo)
{
    int len, total_len;
    gint p;
    gint padding;
    proto_tree *opt_tree;
    proto_item *ti;
    const gchar *ctype;


    p = offset;

    p += 4;

    len = tvb_get_ntohs(tvb, offset+2);
    padding = 7 - ((len + 3) % 8);
    total_len = 4 + len + padding;

    if (tree)
    {
        /* Option Type */
        ctype = val_to_str_const( (tvb_get_ntohs(tvb, offset) & SHIM6_BITMASK_OPT_TYPE) >> 1, shimoptvals, "Unknown Option Type");
        ti = proto_tree_add_text(tree, tvb, offset, total_len, "%s", ctype);
        opt_tree = proto_item_add_subtree(ti, ett_ipv6_shim6_option);

        proto_tree_add_item(opt_tree, hf_ipv6_shim6_opt_type, tvb, offset, 2, ENC_BIG_ENDIAN);

        /* Critical */
        proto_tree_add_item(opt_tree, hf_ipv6_shim6_opt_critical, tvb, offset+1, 1, ENC_BIG_ENDIAN);

        /* Content Length */
        proto_tree_add_item(opt_tree, hf_ipv6_shim6_opt_len, tvb, offset + 2, 2, ENC_BIG_ENDIAN);
        ti = proto_tree_add_uint_format(opt_tree, hf_ipv6_shim6_opt_total_len, tvb, offset+2, 2,
            total_len, "Total Length: %u", total_len);
        PROTO_ITEM_SET_GENERATED(ti);

        /* Option Type Specific */
        switch (tvb_get_ntohs(tvb, offset) >> 1)
        {
            case SHIM6_OPT_RESPVAL:
                p += dissect_shim_hex(tvb, p, len, "Validator:", 0xff, opt_tree);
                if (total_len-(len+4) > 0)
                    proto_tree_add_text(opt_tree, tvb, p, total_len-(len+4), "Padding");
                break;
            case SHIM6_OPT_LOCLIST:
                dissect_shim6_opt_loclist(opt_tree, tvb, &p);
                break;
            case SHIM6_OPT_LOCPREF:
                dissect_shim6_opt_loc_pref(opt_tree, tvb, &p, offset+len+4, pinfo);
                if (total_len-(len+4) > 0)
                  proto_tree_add_text(opt_tree, tvb, p, total_len-(len+4), "Padding");
                break;
            case SHIM6_OPT_CGAPDM:
                p += dissect_shim_hex(tvb, p, len, "CGA Parameter Data Structure:", 0xff, opt_tree);
                if (total_len-(len+4) > 0)
                    proto_tree_add_text(opt_tree, tvb, p, total_len-(len+4), "Padding");
                break;
            case SHIM6_OPT_CGASIG:
                p += dissect_shim_hex(tvb, p, len, "CGA Signature:", 0xff, opt_tree);
                if (total_len-(len+4) > 0)
                    proto_tree_add_text(opt_tree, tvb, p, total_len-(len+4), "Padding");
                break;
            case SHIM6_OPT_ULIDPAIR:
                proto_tree_add_text(opt_tree, tvb, p, 4, "Reserved");
                p += 4;
                proto_tree_add_item(opt_tree, hf_ipv6_shim6_sulid, tvb, p, 16, ENC_NA);
                p += 16;
                proto_tree_add_item(opt_tree, hf_ipv6_shim6_rulid, tvb, p, 16, ENC_NA);
                p += 16;
                break;
            case SHIM6_OPT_FII:
                proto_tree_add_item(opt_tree, hf_ipv6_shim6_opt_fii, tvb, p, 4, ENC_BIG_ENDIAN);
                p += 4;
                break;
            default:
                break;
        }
    }
    return total_len;
}

static void
dissect_shim6_ct(proto_tree * shim_tree, gint hf_item, tvbuff_t * tvb, gint offset, const guchar * label)
{
  guint8 tmp[6];
  guchar * ct_str;

  tmp[0] = tvb_get_guint8(tvb, offset++);
  tmp[1] = tvb_get_guint8(tvb, offset++);
  tmp[2] = tvb_get_guint8(tvb, offset++);
  tmp[3] = tvb_get_guint8(tvb, offset++);
  tmp[4] = tvb_get_guint8(tvb, offset++);
  tmp[5] = tvb_get_guint8(tvb, offset++);

  ct_str = ep_strdup_printf("%s: %02X %02X %02X %02X %02X %02X", label,
                              tmp[0] & SHIM6_BITMASK_CT, tmp[1], tmp[2],
                              tmp[3], tmp[4], tmp[5]
                            );
  proto_tree_add_none_format(shim_tree, hf_item, tvb, offset - 6, 6, "%s", ct_str);
}

static void
dissect_shim6_probes(proto_tree * shim_tree, tvbuff_t * tvb, gint offset,
                     const guchar * label, guint nbr_probe,
                     gboolean probes_rcvd)
{
  proto_tree * probes_tree;
  proto_tree * probe_tree;
  proto_item * it;
  gint ett_probes;
  gint ett_probe;
  guint count;

  if (probes_rcvd) {
    ett_probes = ett_ipv6_shim6_probes_rcvd;
    ett_probe = ett_ipv6_shim6_probe_rcvd;
  } else {
    ett_probes = ett_ipv6_shim6_probes_sent;
    ett_probe = ett_ipv6_shim6_probe_sent;
  }
  it = proto_tree_add_text(shim_tree, tvb, offset, 40 * nbr_probe, "%s", label);
  probes_tree = proto_item_add_subtree(it, ett_probes);

  for (count=0; count < nbr_probe; count++) {
    it = proto_tree_add_text(probes_tree, tvb, offset, 40, "Probe %u", count+1);
    probe_tree = proto_item_add_subtree(it, ett_probe);

    proto_tree_add_item(probe_tree, hf_ipv6_shim6_psrc, tvb, offset, 16, ENC_NA);
    offset += 16;
    proto_tree_add_item(probe_tree, hf_ipv6_shim6_pdst, tvb, offset, 16, ENC_NA);
    offset += 16;

    proto_tree_add_item(probe_tree, hf_ipv6_shim6_pnonce, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    proto_tree_add_item(probe_tree, hf_ipv6_shim6_pdata, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;
  }
}

/* Dissect SHIM6 data: control messages */
static int
dissect_shimctrl(tvbuff_t *tvb, gint offset, guint type, proto_tree *shim_tree)
{
  gint p;
  guint8 tmp;
  const gchar *sta;
  guint probes_sent;
  guint probes_rcvd;

    p = offset;

    switch (type)
    {
        case SHIM6_TYPE_I1:
            dissect_shim6_ct(shim_tree, hf_ipv6_shim6_ct, tvb, p, "Initiator Context Tag");
            p += 6;
            proto_tree_add_item(shim_tree, hf_ipv6_shim6_inonce, tvb, p, 4, ENC_BIG_ENDIAN);
            p += 4;
            break;
        case SHIM6_TYPE_R1:
            proto_tree_add_text(shim_tree, tvb, p, 2, "Reserved2");
            p += 2;
            proto_tree_add_item(shim_tree, hf_ipv6_shim6_inonce, tvb, p, 4, ENC_BIG_ENDIAN);
            p += 4;
            proto_tree_add_item(shim_tree, hf_ipv6_shim6_rnonce, tvb, p, 4, ENC_BIG_ENDIAN);
            p += 4;
            break;
        case SHIM6_TYPE_I2:
            dissect_shim6_ct(shim_tree, hf_ipv6_shim6_ct, tvb, p, "Initiator Context Tag");
            p += 6;
            proto_tree_add_item(shim_tree, hf_ipv6_shim6_inonce, tvb, p, 4, ENC_BIG_ENDIAN);
            p += 4;
            proto_tree_add_item(shim_tree, hf_ipv6_shim6_rnonce, tvb, p, 4, ENC_BIG_ENDIAN);
            p += 4;
            proto_tree_add_text(shim_tree, tvb, p, 4, "Reserved2");
            p += 4;
            break;
        case SHIM6_TYPE_R2:
            dissect_shim6_ct(shim_tree, hf_ipv6_shim6_ct, tvb, p, "Responder Context Tag");
            p += 6;
            proto_tree_add_item(shim_tree, hf_ipv6_shim6_inonce, tvb, p, 4, ENC_BIG_ENDIAN);
            p += 4;
            break;
        case SHIM6_TYPE_R1BIS:
            dissect_shim6_ct(shim_tree, hf_ipv6_shim6_ct, tvb, p, "Packet Context Tag");
            p += 6;
            proto_tree_add_item(shim_tree, hf_ipv6_shim6_rnonce, tvb, p, 4, ENC_BIG_ENDIAN);
            p += 4;
            break;
        case SHIM6_TYPE_I2BIS:
            dissect_shim6_ct(shim_tree, hf_ipv6_shim6_ct, tvb, p, "Initiator Context Tag");
            p += 6;
            proto_tree_add_item(shim_tree, hf_ipv6_shim6_inonce, tvb, p, 4, ENC_BIG_ENDIAN);
            p += 4;
            proto_tree_add_item(shim_tree, hf_ipv6_shim6_rnonce, tvb, p, 4, ENC_BIG_ENDIAN);
            p += 4;
            proto_tree_add_text(shim_tree, tvb, p, 6, "Reserved2");
            p += 6;
            dissect_shim6_ct(shim_tree, hf_ipv6_shim6_ct, tvb, p, "Initiator Context Tag");
            p += 6;
            break;
        case SHIM6_TYPE_UPD_REQ:
        case SHIM6_TYPE_UPD_ACK:
            dissect_shim6_ct(shim_tree, hf_ipv6_shim6_ct, tvb, p, "Receiver Context Tag");
            p += 6;
            proto_tree_add_item(shim_tree, hf_ipv6_shim6_rnonce, tvb, p, 4, ENC_BIG_ENDIAN);
            p += 4;
            break;
        case SHIM6_TYPE_KEEPALIVE:
            dissect_shim6_ct(shim_tree, hf_ipv6_shim6_ct, tvb, p, "Receiver Context Tag");
            p += 6;
            proto_tree_add_text(shim_tree, tvb, p, 4, "Reserved2");
            p += 4;
            break;
        case SHIM6_TYPE_PROBE:
            dissect_shim6_ct(shim_tree, hf_ipv6_shim6_ct, tvb, p, "Receiver Context Tag");
            p += 6;

            tmp = tvb_get_guint8(tvb, p);
            probes_sent = tmp & SHIM6_BITMASK_PSENT;
            probes_rcvd = (tmp & SHIM6_BITMASK_PRECVD) >> 4;

            proto_tree_add_uint_format(shim_tree, hf_ipv6_shim6_psent, tvb,
                                        p, 1, probes_sent,
                                        "Probes Sent: %u", probes_sent);
            proto_tree_add_uint_format(shim_tree, hf_ipv6_shim6_precvd, tvb,
                                        p, 1, probes_rcvd,
                                        "Probes Received: %u", probes_rcvd);
            p++;

            sta = val_to_str_const((tvb_get_guint8(tvb, p) & SHIM6_BITMASK_STA) >> 6,
                                        shimreapstates, "Unknown REAP State");
            proto_tree_add_uint_format(shim_tree, hf_ipv6_shim6_reap, tvb,
                p, 1, (tvb_get_guint8(tvb, p) & SHIM6_BITMASK_STA) >> 6,
                "REAP State: %s", sta);

            proto_tree_add_text(shim_tree, tvb, p, 3, "Reserved2");
            p += 3;

            /* Probes Sent */
            if (probes_sent) {
              dissect_shim6_probes(shim_tree, tvb, p, "Probes Sent",
                                                          probes_sent, FALSE);
              p += 40 * probes_sent;
            }

           /* Probes Received */
            if (probes_rcvd) {
              dissect_shim6_probes(shim_tree, tvb, p, "Probes Received",
                                                          probes_rcvd, TRUE);
              p += 40 * probes_rcvd;
            }
           break;
        default:
           break;
    }
    return p-offset;
}

/* Dissect SHIM6 data: payload, common part, options */
static const value_string shimctrlvals[] = {
    { SHIM6_TYPE_I1,        "I1" },
    { SHIM6_TYPE_R1,        "R1" },
    { SHIM6_TYPE_I2,        "I2" },
    { SHIM6_TYPE_R2,        "R2" },
    { SHIM6_TYPE_R1BIS,     "R1bis" },
    { SHIM6_TYPE_I2BIS,     "I2bis" },
    { SHIM6_TYPE_UPD_REQ,   "Update Request" },
    { SHIM6_TYPE_UPD_ACK,   "Update Acknowledgment" },
    { SHIM6_TYPE_KEEPALIVE, "Keepalive" },
    { SHIM6_TYPE_PROBE,     "Probe" },
    { 0, NULL }
};

static void ipv6_shim6_checkum_additional_info(tvbuff_t * tvb, packet_info * pinfo,
    proto_item * it_cksum, int offset, gboolean is_cksum_correct)
{
        proto_tree * checksum_tree;
        proto_item * item;

        checksum_tree = proto_item_add_subtree(it_cksum, ett_ipv6_shim6_cksum);
        item = proto_tree_add_boolean(checksum_tree, hf_ipv6_shim6_checksum_good, tvb,
           offset, 2, is_cksum_correct);
        PROTO_ITEM_SET_GENERATED(item);
        item = proto_tree_add_boolean(checksum_tree, hf_ipv6_shim6_checksum_bad, tvb,
           offset, 2, !is_cksum_correct);
        PROTO_ITEM_SET_GENERATED(item);
        if (!is_cksum_correct) {
          expert_add_info_format(pinfo, item, PI_CHECKSUM, PI_ERROR, "Bad checksum");
          col_append_str(pinfo->cinfo, COL_INFO, " [Shim6 CHECKSUM INCORRECT]");
        }
}

static int
dissect_shim6(tvbuff_t *tvb, int offset, proto_tree *tree, packet_info * pinfo)
{
    struct ip6_shim shim;
    int len;
    gint p;
    proto_tree *shim_tree;
    proto_item *ti;
    guint8 tmp[5];

    tvb_memcpy(tvb, (guint8 *)&shim, offset, sizeof(shim));
    len = (shim.ip6s_len + 1) << 3;

    if (tree)
    {
        ti = proto_tree_add_item(tree, hf_ipv6_shim6, tvb, offset, len, ENC_NA);
        shim_tree = proto_item_add_subtree(ti, ett_ipv6_shim6);

        /* Next Header */
        proto_tree_add_uint_format(shim_tree, hf_ipv6_shim6_nxt, tvb,
            offset + (int)offsetof(struct ip6_shim, ip6s_nxt), 1, shim.ip6s_nxt,
            "Next header: %s (%u)", ipprotostr(shim.ip6s_nxt), shim.ip6s_nxt);

        /* Header Extension Length */
        proto_tree_add_uint_format(shim_tree, hf_ipv6_shim6_len, tvb,
            offset + (int)offsetof(struct ip6_shim, ip6s_len), 1, shim.ip6s_len,
            "Header Ext Length: %u (%d bytes)", shim.ip6s_len, len);

        /* P Field */
        proto_tree_add_item(shim_tree, hf_ipv6_shim6_p, tvb,
                              offset + (int)offsetof(struct ip6_shim, ip6s_p), 1, ENC_BIG_ENDIAN);

        /* skip the first 2 bytes (nxt hdr, hdr ext len, p+7bits) */
        p = offset + 3;

        if (shim.ip6s_p & SHIM6_BITMASK_P)
        {
            tmp[0] = tvb_get_guint8(tvb, p++);
            tmp[1] = tvb_get_guint8(tvb, p++);
            tmp[2] = tvb_get_guint8(tvb, p++);
            tmp[3] = tvb_get_guint8(tvb, p++);
            tmp[4] = tvb_get_guint8(tvb, p++);

            /* Payload Extension Header */
            proto_tree_add_none_format(shim_tree, hf_ipv6_shim6_ct, tvb,
                offset + (int)offsetof(struct ip6_shim, ip6s_p), 6,
                "Receiver Context Tag: %02x %02x %02x %02x %02x %02x",
                shim.ip6s_p & SHIM6_BITMASK_CT, tmp[0], tmp[1], tmp[2], tmp[3], tmp[4]);
        }
        else
        {
            /* Control Message */
            guint16 csum;
            int advance;

            /* Message Type */
            proto_tree_add_item(shim_tree, hf_ipv6_shim6_type, tvb,
                                offset + (int)offsetof(struct ip6_shim, ip6s_p), 1,
                                ENC_BIG_ENDIAN
                                );

            /* Protocol bit (Must be zero for SHIM6) */
            proto_tree_add_item(shim_tree, hf_ipv6_shim6_proto, tvb, p, 1, ENC_BIG_ENDIAN);
            p++;

            /* Checksum */
            csum = shim_checksum(tvb_get_ptr(tvb, offset, len), len);

            if (csum == 0) {
                ti = proto_tree_add_uint_format(shim_tree, hf_ipv6_shim6_checksum, tvb, p, 2,
                    tvb_get_ntohs(tvb, p), "Checksum: 0x%04x [correct]", tvb_get_ntohs(tvb, p));
                ipv6_shim6_checkum_additional_info(tvb, pinfo, ti, p, TRUE);
            } else {
                ti = proto_tree_add_uint_format(shim_tree, hf_ipv6_shim6_checksum, tvb, p, 2,
                    tvb_get_ntohs(tvb, p), "Checksum: 0x%04x [incorrect: should be 0x%04x]",
                    tvb_get_ntohs(tvb, p), in_cksum_shouldbe(tvb_get_ntohs(tvb, p), csum));
                ipv6_shim6_checkum_additional_info(tvb, pinfo, ti, p, FALSE);
            }
            p += 2;

            /* Type specific data */
            advance = dissect_shimctrl(tvb, p, shim.ip6s_p & SHIM6_BITMASK_TYPE, shim_tree);
            p += advance;

            /* Options */
            while (p < offset+len) {
              p += dissect_shimopts(tvb, p, shim_tree, pinfo);
            }
        }
    }
    return len;
}

/* END SHIM6 PART */

static void
dissect_ipv6(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree)
{
  proto_tree *ipv6_tree = NULL;
  proto_item *ipv6_item = NULL, *ti;
  guint8 nxt;
  guint8 stype=0;
  int advance;
  guint16 plen;
  gboolean hopopts, routing, frag, ah, shim6, dstopts;
  guint16 offlg;
  guint32 ident;
  int offset;
  fragment_data *ipfd_head;
  tvbuff_t   *next_tvb;
  gboolean update_col_info = TRUE;
  gboolean save_fragmented = FALSE;
  const char *sep = "IPv6 ";
  guint8 *mac_addr;

  struct ip6_hdr ipv6;

  col_set_str(pinfo->cinfo, COL_PROTOCOL, "IPv6");
  col_clear(pinfo->cinfo, COL_INFO);

  offset = 0;
  tvb_memcpy(tvb, (guint8 *)&ipv6, offset, sizeof(ipv6));

  /* Get extension header and payload length */
  plen = g_ntohs(ipv6.ip6_plen);

  /* Adjust the length of this tvbuff to include only the IPv6 datagram. */
  set_actual_length(tvb, plen + (guint)sizeof (struct ip6_hdr));

  TVB_SET_ADDRESS(&pinfo->net_src, AT_IPv6, tvb, offset + IP6H_SRC, 16);
  TVB_SET_ADDRESS(&pinfo->src, AT_IPv6,     tvb, offset + IP6H_SRC, 16);
  TVB_SET_ADDRESS(&pinfo->net_dst, AT_IPv6, tvb, offset + IP6H_DST, 16);
  TVB_SET_ADDRESS(&pinfo->dst, AT_IPv6,     tvb, offset + IP6H_DST, 16);

  if (tree) {
    proto_tree* pt;
    proto_item* pi;
    proto_tree *ipv6_tc_tree;
    proto_item *ipv6_tc;
    const char *name;

    ipv6_item = proto_tree_add_item(tree, proto_ipv6, tvb, offset, -1, ENC_NA);
    ipv6_tree = proto_item_add_subtree(ipv6_item, ett_ipv6);

    /* !!! warning: (4-bit) version, (6-bit) DSCP, (1-bit) ECN-ECT, (1-bit) ECN-CE and (20-bit) Flow */
    pi = proto_tree_add_item(ipv6_tree, hf_ipv6_version, tvb,
                        offset + (int)offsetof(struct ip6_hdr, ip6_vfc), 1, ENC_BIG_ENDIAN);
    pt = proto_item_add_subtree(pi,ett_ipv6_version);
    pi = proto_tree_add_item(pt, hf_ip_version, tvb,
                        offset + (int)offsetof(struct ip6_hdr, ip6_vfc), 1, ENC_BIG_ENDIAN);
    PROTO_ITEM_SET_GENERATED(pi);

    ipv6_tc = proto_tree_add_item(ipv6_tree, hf_ipv6_class, tvb,
                        offset + (int)offsetof(struct ip6_hdr, ip6_flow), 4, ENC_BIG_ENDIAN);

    ipv6_tc_tree = proto_item_add_subtree(ipv6_tc, ett_ipv6_traffic_class);

    proto_tree_add_item(ipv6_tc_tree, hf_ipv6_traffic_class_dscp, tvb,
                        offset + (int)offsetof(struct ip6_hdr, ip6_flow), 4, ENC_BIG_ENDIAN);
    proto_tree_add_item(ipv6_tc_tree, hf_ipv6_traffic_class_ect, tvb,
                        offset + (int)offsetof(struct ip6_hdr, ip6_flow), 4, ENC_BIG_ENDIAN);

    proto_tree_add_item(ipv6_tc_tree, hf_ipv6_traffic_class_ce, tvb,
                        offset + (int)offsetof(struct ip6_hdr, ip6_flow), 4, ENC_BIG_ENDIAN);

    proto_tree_add_item(ipv6_tree, hf_ipv6_flow, tvb,
                        offset + (int)offsetof(struct ip6_hdr, ip6_flow), 4, ENC_BIG_ENDIAN);

    proto_tree_add_item(ipv6_tree, hf_ipv6_plen, tvb,
                        offset + (int)offsetof(struct ip6_hdr, ip6_plen), 2, ENC_BIG_ENDIAN);

    proto_tree_add_uint_format(ipv6_tree, hf_ipv6_nxt, tvb,
                offset + (int)offsetof(struct ip6_hdr, ip6_nxt), 1,
                ipv6.ip6_nxt,
                "Next header: %s (%u)",
                ipprotostr(ipv6.ip6_nxt), ipv6.ip6_nxt);

    proto_tree_add_item(ipv6_tree, hf_ipv6_hlim, tvb,
                        offset + (int)offsetof(struct ip6_hdr, ip6_hlim), 1, ENC_BIG_ENDIAN);
    /* Yes, there is not TTL in IPv6 Header... but it is the same of Hop Limit...*/
    pinfo->ip_ttl = tvb_get_guint8(tvb, offset + (int)offsetof(struct ip6_hdr, ip6_hlim));

    /* Add the different items for the source address */
    proto_tree_add_item(ipv6_tree, hf_ipv6_src, tvb,
                        offset + (int)offsetof(struct ip6_hdr, ip6_src), 16, ENC_NA);
    ti = proto_tree_add_ipv6(ipv6_tree, hf_ipv6_addr, tvb,
                              offset + (int)offsetof(struct ip6_hdr, ip6_src),
                              16, (guint8 *)&ipv6.ip6_src);
    PROTO_ITEM_SET_HIDDEN(ti);
    name = get_addr_name(&pinfo->src);
    if (ipv6_summary_in_tree) {
      proto_item_append_text(ipv6_item, ", Src: %s (%s)", name, ip6_to_str(&ipv6.ip6_src));
    }
    ti = proto_tree_add_string(ipv6_tree, hf_ipv6_src_host, tvb,
                              offset + (int)offsetof(struct ip6_hdr, ip6_src),
                              16, name);
    PROTO_ITEM_SET_GENERATED(ti);
    PROTO_ITEM_SET_HIDDEN(ti);
    ti = proto_tree_add_string(ipv6_tree, hf_ipv6_host, tvb,
                              offset + (int)offsetof(struct ip6_hdr, ip6_src),
                              16, name);
    PROTO_ITEM_SET_GENERATED(ti);
    PROTO_ITEM_SET_HIDDEN(ti);

    /* Extract embedded (IPv6 and MAC) address information */
    if (tvb_get_ntohs(tvb, offset + IP6H_SRC) == 0x2002) { /* RFC 3056 section 2 */
      ti = proto_tree_add_item(ipv6_tree, hf_ipv6_src_6to4_gateway_ipv4, tvb,
                                offset + IP6H_SRC + 2, 4, ENC_BIG_ENDIAN);
      PROTO_ITEM_SET_GENERATED(ti);
      ti = proto_tree_add_item(ipv6_tree, hf_ipv6_src_6to4_sla_id, tvb,
                                offset + IP6H_SRC + 6, 2, ENC_BIG_ENDIAN);
      PROTO_ITEM_SET_GENERATED(ti);
      ti = proto_tree_add_item(ipv6_tree, hf_ipv6_6to4_gateway_ipv4, tvb,
                                offset + IP6H_SRC + 2, 4, ENC_BIG_ENDIAN);
      PROTO_ITEM_SET_GENERATED(ti);
      PROTO_ITEM_SET_HIDDEN(ti);
      ti = proto_tree_add_item(ipv6_tree, hf_ipv6_6to4_sla_id, tvb,
                                offset + IP6H_SRC + 6, 2, ENC_BIG_ENDIAN);
      PROTO_ITEM_SET_GENERATED(ti);
      PROTO_ITEM_SET_HIDDEN(ti);
    } else if (tvb_get_ntohl(tvb, offset + IP6H_SRC) == 0x20010000) { /* RFC 4380 section 4 */
      guint16 mapped_port = tvb_get_ntohs(tvb, offset + IP6H_SRC + 10) ^ 0xffff;
      guint32 client_v4 = tvb_get_ipv4(tvb, offset + IP6H_SRC + 12) ^ 0xffffffff;

      ti = proto_tree_add_item(ipv6_tree, hf_ipv6_src_teredo_server_ipv4, tvb,
                                offset + IP6H_SRC + 4, 4, ENC_BIG_ENDIAN);
      PROTO_ITEM_SET_GENERATED(ti);
      ti = proto_tree_add_uint(ipv6_tree, hf_ipv6_src_teredo_port, tvb,
                                offset + IP6H_SRC + 10, 2, mapped_port);
      PROTO_ITEM_SET_GENERATED(ti);
      ti = proto_tree_add_ipv4(ipv6_tree, hf_ipv6_src_teredo_client_ipv4, tvb,
                                offset + IP6H_SRC + 12, 4, client_v4);
      PROTO_ITEM_SET_GENERATED(ti);
      ti = proto_tree_add_item(ipv6_tree, hf_ipv6_teredo_server_ipv4, tvb,
                                offset + IP6H_SRC + 4, 4, ENC_BIG_ENDIAN);
      PROTO_ITEM_SET_GENERATED(ti);
      PROTO_ITEM_SET_HIDDEN(ti);
      ti = proto_tree_add_uint(ipv6_tree, hf_ipv6_teredo_port, tvb,
                                offset + IP6H_SRC + 10, 2, mapped_port);
      PROTO_ITEM_SET_GENERATED(ti);
      PROTO_ITEM_SET_HIDDEN(ti);
      ti = proto_tree_add_ipv4(ipv6_tree, hf_ipv6_teredo_client_ipv4, tvb,
                                offset + IP6H_SRC + 12, 4, client_v4);
      PROTO_ITEM_SET_GENERATED(ti);
      PROTO_ITEM_SET_HIDDEN(ti);
    }

    if (tvb_get_guint8(tvb, offset + IP6H_SRC + 8) & 0x02 && tvb_get_ntohs(tvb, offset + IP6H_SRC + 11) == 0xfffe) {  /* RFC 4291 appendix A */
      mac_addr = (guint8 *)ep_alloc(6);
      tvb_memcpy(tvb, mac_addr, offset + IP6H_SRC + 8, 3);
      tvb_memcpy(tvb, mac_addr+3, offset+ IP6H_SRC + 13, 3);
      mac_addr[0] &= ~0x02;
      ti = proto_tree_add_ether(ipv6_tree, hf_ipv6_src_sa_mac, tvb,
                                offset + IP6H_SRC + 8, 6, mac_addr);
      PROTO_ITEM_SET_GENERATED(ti);
      ti = proto_tree_add_ether(ipv6_tree, hf_ipv6_sa_mac, tvb,
                                offset + IP6H_SRC + 8, 6, mac_addr);
      PROTO_ITEM_SET_GENERATED(ti);
      PROTO_ITEM_SET_HIDDEN(ti);
    } else if ((tvb_get_ntohl(tvb, offset + IP6H_SRC + 8) & 0xfcffffff) == 0x00005efe) { /* RFC 5214 section 6.1 */
      ti = proto_tree_add_item(ipv6_tree, hf_ipv6_src_isatap_ipv4, tvb,
                                offset + IP6H_SRC + 12, 4, ENC_BIG_ENDIAN);
      PROTO_ITEM_SET_GENERATED(ti);
      ti = proto_tree_add_item(ipv6_tree, hf_ipv6_isatap_ipv4, tvb,
                                offset + IP6H_SRC + 12, 4, ENC_BIG_ENDIAN);
      PROTO_ITEM_SET_GENERATED(ti);
      PROTO_ITEM_SET_HIDDEN(ti);
    }

    /* Add different items for the destination address */
    proto_tree_add_item(ipv6_tree, hf_ipv6_dst, tvb,
                        offset + (int)offsetof(struct ip6_hdr, ip6_dst), 16, ENC_NA);
    ti = proto_tree_add_ipv6(ipv6_tree, hf_ipv6_addr, tvb,
                              offset + (int)offsetof(struct ip6_hdr, ip6_dst),
                              16, (guint8 *)&ipv6.ip6_dst);
    PROTO_ITEM_SET_HIDDEN(ti);
    name = get_addr_name(&pinfo->dst);
    if (ipv6_summary_in_tree) {
      proto_item_append_text(ipv6_item, ", Dst: %s (%s)", name, ip6_to_str(&ipv6.ip6_dst));
    }
    ti = proto_tree_add_string(ipv6_tree, hf_ipv6_dst_host, tvb,
                              offset + (int)offsetof(struct ip6_hdr, ip6_dst),
                              16, name);
    PROTO_ITEM_SET_GENERATED(ti);
    PROTO_ITEM_SET_HIDDEN(ti);
    ti = proto_tree_add_string(ipv6_tree, hf_ipv6_host, tvb,
                              offset + (int)offsetof(struct ip6_hdr, ip6_dst),
                              16, name);
    PROTO_ITEM_SET_GENERATED(ti);
    PROTO_ITEM_SET_HIDDEN(ti);

    /* Extract embedded (IPv6 and MAC) address information */
    if (tvb_get_ntohs(tvb, offset + IP6H_DST) == 0x2002) { /* RFC 3056 section 2 */
      ti = proto_tree_add_item(ipv6_tree, hf_ipv6_dst_6to4_gateway_ipv4, tvb,
                                offset + IP6H_DST + 2, 4, ENC_BIG_ENDIAN);
      PROTO_ITEM_SET_GENERATED(ti);
      ti = proto_tree_add_item(ipv6_tree, hf_ipv6_dst_6to4_sla_id, tvb,
                                offset + IP6H_DST + 6, 2, ENC_BIG_ENDIAN);
      PROTO_ITEM_SET_GENERATED(ti);
      ti = proto_tree_add_item(ipv6_tree, hf_ipv6_6to4_gateway_ipv4, tvb,
                                offset + IP6H_DST + 2, 4, ENC_BIG_ENDIAN);
      PROTO_ITEM_SET_GENERATED(ti);
      PROTO_ITEM_SET_HIDDEN(ti);
      ti = proto_tree_add_item(ipv6_tree, hf_ipv6_6to4_sla_id, tvb,
                                offset + IP6H_DST + 6, 2, ENC_BIG_ENDIAN);
      PROTO_ITEM_SET_GENERATED(ti);
      PROTO_ITEM_SET_HIDDEN(ti);
    } else if (tvb_get_ntohl(tvb, offset + IP6H_DST) == 0x20010000) { /* RFC 4380 section 4 */
      guint16 mapped_port = tvb_get_ntohs(tvb, offset + IP6H_DST + 10) ^ 0xffff;
      guint32 client_v4 = tvb_get_ipv4(tvb, offset + IP6H_DST + 12) ^ 0xffffffff;

      ti = proto_tree_add_item(ipv6_tree, hf_ipv6_dst_teredo_server_ipv4, tvb,
                                offset + IP6H_DST + 4, 4, ENC_BIG_ENDIAN);
      PROTO_ITEM_SET_GENERATED(ti);
      ti = proto_tree_add_uint(ipv6_tree, hf_ipv6_dst_teredo_port, tvb,
                                offset + IP6H_DST + 10, 2, mapped_port);
      PROTO_ITEM_SET_GENERATED(ti);
      ti = proto_tree_add_ipv4(ipv6_tree, hf_ipv6_dst_teredo_client_ipv4, tvb,
                                offset + IP6H_DST + 12, 4, client_v4);
      PROTO_ITEM_SET_GENERATED(ti);
      ti = proto_tree_add_item(ipv6_tree, hf_ipv6_teredo_server_ipv4, tvb,
                                offset + IP6H_DST + 4, 4, ENC_BIG_ENDIAN);
      PROTO_ITEM_SET_GENERATED(ti);
      PROTO_ITEM_SET_HIDDEN(ti);
      ti = proto_tree_add_uint(ipv6_tree, hf_ipv6_teredo_port, tvb,
                                offset + IP6H_DST + 10, 2, mapped_port);
      PROTO_ITEM_SET_GENERATED(ti);
      PROTO_ITEM_SET_HIDDEN(ti);
      ti = proto_tree_add_ipv4(ipv6_tree, hf_ipv6_teredo_client_ipv4, tvb,
                                offset + IP6H_DST + 12, 4, client_v4);
      PROTO_ITEM_SET_GENERATED(ti);
      PROTO_ITEM_SET_HIDDEN(ti);
    }

    if (tvb_get_guint8(tvb, offset + IP6H_DST + 8) & 0x02 && tvb_get_ntohs(tvb, offset + IP6H_DST + 11) == 0xfffe) { /* RFC 4291 appendix A */
      mac_addr = (guint8 *)ep_alloc(6);
      tvb_memcpy(tvb, mac_addr, offset + IP6H_DST + 8, 3);
      tvb_memcpy(tvb, mac_addr+3, offset+ IP6H_DST + 13, 3);
      mac_addr[0] &= ~0x02;
      ti = proto_tree_add_ether(ipv6_tree, hf_ipv6_dst_sa_mac, tvb,
                                offset + IP6H_DST + 8, 6, mac_addr);
      PROTO_ITEM_SET_GENERATED(ti);
      ti = proto_tree_add_ether(ipv6_tree, hf_ipv6_sa_mac, tvb,
                                offset + IP6H_DST + 8, 6, mac_addr);
      PROTO_ITEM_SET_GENERATED(ti);
      PROTO_ITEM_SET_HIDDEN(ti);
    } else if ((tvb_get_ntohl(tvb, offset + IP6H_DST + 8) & 0xfcffffff) == 0x00005efe) { /* RFC 5214 section 6.1 */
      ti = proto_tree_add_item(ipv6_tree, hf_ipv6_dst_isatap_ipv4, tvb,
                                offset + IP6H_DST + 12, 4, ENC_BIG_ENDIAN);
      PROTO_ITEM_SET_GENERATED(ti);
      ti = proto_tree_add_item(ipv6_tree, hf_ipv6_isatap_ipv4, tvb,
                                offset + IP6H_DST + 12, 4, ENC_BIG_ENDIAN);
      PROTO_ITEM_SET_GENERATED(ti);
      PROTO_ITEM_SET_HIDDEN(ti);
    }
  }

#ifdef HAVE_GEOIP_V6
  if (tree && ipv6_use_geoip) {
    add_geoip_info(ipv6_tree, tvb, offset, &ipv6.ip6_src, &ipv6.ip6_dst);
  }
#endif

  /* start of the new header (could be a extension header) */
  nxt = tvb_get_guint8(tvb, offset + 6);
  offset += (int)sizeof(struct ip6_hdr);
  offlg = 0;
  ident = 0;

/* start out assuming this isn't fragmented, and has none of the other
   non-final headers */
  hopopts = FALSE;
  routing = FALSE;
  ah = FALSE;
  shim6 = FALSE;
  dstopts = FALSE;

again:
   switch (nxt) {

   case IP_PROTO_HOPOPTS:
      hopopts = TRUE;
      advance = dissect_hopopts(tvb, offset, ipv6_tree, pinfo);
      nxt = tvb_get_guint8(tvb, offset);
      offset += advance;
      plen -= advance;
      goto again;

    case IP_PROTO_ROUTING:
      routing = TRUE;
      advance = dissect_routing6(tvb, offset, ipv6_tree, pinfo);
      nxt = tvb_get_guint8(tvb, offset);
      offset += advance;
      plen -= advance;
      goto again;

    case IP_PROTO_FRAGMENT:
      advance = dissect_frag6(tvb, offset, pinfo, ipv6_tree,
          &offlg, &ident);
      nxt = tvb_get_guint8(tvb, offset);
      offset += advance;
      plen -= advance;
      frag = offlg & (IP6F_OFF_MASK | IP6F_MORE_FRAG);
      save_fragmented |= frag;
      if (ipv6_reassemble && frag && tvb_bytes_exist(tvb, offset, plen)) {
        ipfd_head = fragment_add_check(&ipv6_reassembly_table,
          tvb, offset, pinfo, ident, NULL,
          offlg & IP6F_OFF_MASK,
          plen,
          offlg & IP6F_MORE_FRAG);
        next_tvb = process_reassembled_data(tvb, offset, pinfo, "Reassembled IPv6",
        ipfd_head, &ipv6_frag_items, &update_col_info, ipv6_tree);
        if (next_tvb) {  /* Process post-fragment headers after reassembly... */
          offset= 0;
          offlg = 0;
          tvb = next_tvb;
          goto again;
        }
      }
      if (!(offlg & IP6F_OFF_MASK)) /*...or in the first fragment */
        goto again;
      break;

    case IP_PROTO_AH:
      ah = TRUE;
      advance = dissect_ah_header(tvb_new_subset_remaining(tvb, offset),
                                  pinfo, ipv6_tree, NULL, NULL);
      nxt = tvb_get_guint8(tvb, offset);
      offset += advance;
      plen -= advance;
      goto again;

    case IP_PROTO_SHIM6:
    case IP_PROTO_SHIM6_OLD:
      shim6 = TRUE;
      advance = dissect_shim6(tvb, offset, ipv6_tree, pinfo);
      nxt = tvb_get_guint8(tvb, offset);
      stype = tvb_get_guint8(tvb, offset+2);
      offset += advance;
      plen -= advance;
      goto again;

    case IP_PROTO_DSTOPTS:
      dstopts = TRUE;
      advance = dissect_dstopts(tvb, offset, ipv6_tree, pinfo);
      nxt = tvb_get_guint8(tvb, offset);
      offset += advance;
      plen -= advance;
      goto again;

    case IP_PROTO_NONE:
      break;

    default:
      /* Since we did not recognize this IPv6 option, check
       * whether it is a known protocol. If not, then it
       * is an unknown IPv6 option
       */
      if (!dissector_get_uint_handle(ip_dissector_table, nxt)) {
        advance = dissect_unknown_option(tvb, offset, ipv6_tree);
        nxt = tvb_get_guint8(tvb, offset);
        offset += advance;
        plen -= advance;
        goto again;
      }
    }

  proto_item_set_len (ipv6_item, offset);

  /* collect packet info */
  pinfo->ipproto = nxt;
  pinfo->iplen = (int)sizeof(ipv6) + plen + offset;
  pinfo->iphdrlen = offset;
  tap_queue_packet(ipv6_tap, pinfo, &ipv6);

  if (offlg & IP6F_OFF_MASK || (ipv6_reassemble && offlg & IP6F_MORE_FRAG)) {
    /* Not the first fragment, or the first when we are reassembling and there are more. */
    /* Don't dissect it; just show this as a fragment. */
    /* COL_INFO was filled in by "dissect_frag6()" */
    call_dissector(data_handle, tvb_new_subset_remaining(tvb, offset), pinfo, tree);
    return;
  } else {
    /* First fragment, not fragmented, or already reassembled.  Dissect what we have here. */

    /* Get a tvbuff for the payload. */
    next_tvb = tvb_new_subset_remaining(tvb, offset);

    /*
     * If this is the first fragment, but not the only fragment,
     * tell the next protocol that.
     */
    if (offlg & IP6F_MORE_FRAG)
      pinfo->fragmented = TRUE;
    else
      pinfo->fragmented = FALSE;
  }


  /* do lookup with the subdissector table */
  if (!dissector_try_uint(ip_dissector_table, nxt, next_tvb, pinfo, tree)) {
    /* Unknown protocol.
       Handle "no next header" specially. */
    if (nxt == IP_PROTO_NONE) {
      if (check_col(pinfo->cinfo, COL_INFO)) {
        /* If we had an Authentication Header, the AH dissector already
           put something in the Info column; leave it there. */
        if (!ah) {
          if (hopopts || routing || dstopts || shim6) {
            if (hopopts) {
              col_append_fstr(pinfo->cinfo, COL_INFO, "%shop-by-hop options",
                             sep);
              sep = ", ";
            }
            if (routing) {
              col_append_fstr(pinfo->cinfo, COL_INFO, "%srouting", sep);
              sep = ", ";
            }
            if (dstopts) {
              col_append_fstr(pinfo->cinfo, COL_INFO, "%sdestination options",
                              sep);
            }
            if (shim6) {
              if (stype & SHIM6_BITMASK_P) {
                col_append_str(pinfo->cinfo, COL_INFO, "Shim6 (Payload)");
              }
              else {
                col_append_fstr(pinfo->cinfo, COL_INFO, "Shim6 (%s)",
                   val_to_str_const(stype & SHIM6_BITMASK_TYPE, shimctrlvals, "Unknown"));
              }
            }
          } else
            col_set_str(pinfo->cinfo, COL_INFO, "IPv6 no next header");
        }
      }
    } else {
      if (check_col(pinfo->cinfo, COL_INFO))
        col_add_fstr(pinfo->cinfo, COL_INFO, "%s (%u)", ipprotostr(nxt), nxt);
    }
    call_dissector(data_handle, next_tvb, pinfo, tree);
  }
  pinfo->fragmented = save_fragmented;
}

void
proto_register_ipv6(void)
{
  static hf_register_info hf[] = {
    { &hf_ipv6_version,
      { "Version",              "ipv6.version",
                                FT_UINT8, BASE_DEC, NULL, 0xF0, NULL, HFILL }},
    { &hf_ip_version,
      { "This field makes the filter \"ip.version == 6\" possible",             "ip.version",
                                FT_UINT8, BASE_DEC, NULL, 0xF0, NULL, HFILL }},
    { &hf_ipv6_class,
      { "Traffic class",        "ipv6.class",
                                FT_UINT32, BASE_HEX, NULL, 0x0FF00000, NULL, HFILL }},
    { &hf_ipv6_flow,
      { "Flowlabel",            "ipv6.flow",
                                FT_UINT32, BASE_HEX, NULL, 0x000FFFFF, NULL, HFILL }},
    { &hf_ipv6_plen,
      { "Payload length",       "ipv6.plen",
                                FT_UINT16, BASE_DEC, NULL, 0x0, NULL, HFILL }},
    { &hf_ipv6_nxt,
      { "Next header",          "ipv6.nxt",
                                FT_UINT8, BASE_DEC | BASE_EXT_STRING, &ipproto_val_ext, 0x0, NULL, HFILL }},
    { &hf_ipv6_hlim,
      { "Hop limit",            "ipv6.hlim",
                                FT_UINT8, BASE_DEC, NULL, 0x0, NULL, HFILL }},
    { &hf_ipv6_src,
      { "Source",               "ipv6.src",
                                FT_IPv6, BASE_NONE, NULL, 0x0,
                                "Source IPv6 Address", HFILL }},
    { &hf_ipv6_src_host,
      { "Source Host",          "ipv6.src_host",
                                FT_STRING, BASE_NONE, NULL, 0x0,
                                "Source IPv6 Host", HFILL }},
    { &hf_ipv6_src_sa_mac,
      { "Source SA MAC",                "ipv6.src_sa_mac",
                                FT_ETHER, BASE_NONE, NULL, 0x0,
                                "Source IPv6 Stateless Autoconfiguration MAC Address", HFILL }},
    { &hf_ipv6_src_isatap_ipv4,
      { "Source ISATAP IPv4",           "ipv6.src_isatap_ipv4",
                                FT_IPv4, BASE_NONE, NULL, 0x0,
                                "Source IPv6 ISATAP Encapsulated IPv4 Address", HFILL }},
    { &hf_ipv6_src_6to4_gateway_ipv4,
      { "Source 6to4 Gateway IPv4",     "ipv6.src_6to4_gw_ipv4",
                                FT_IPv4, BASE_NONE, NULL, 0x0,
                                "Source IPv6 6to4 Gateway IPv4 Address", HFILL }},
    { &hf_ipv6_src_6to4_sla_id,
      { "Source 6to4 SLA ID",           "ipv6.src_6to4_sla_id",
                                FT_UINT16, BASE_DEC, NULL, 0x0,
                                "Source IPv6 6to4 SLA ID", HFILL }},
    { &hf_ipv6_src_teredo_server_ipv4,
      { "Source Teredo Server IPv4",    "ipv6.src_ts_ipv4",
                                FT_IPv4, BASE_NONE, NULL, 0x0,
                                "Source IPv6 Teredo Server Encapsulated IPv4 Address", HFILL }},
    { &hf_ipv6_src_teredo_port,
      { "Source Teredo Port",           "ipv6.src_tc_port",
                                FT_UINT16, BASE_DEC, NULL, 0x0,
                                "Source IPv6 Teredo Client Mapped Port", HFILL }},
    { &hf_ipv6_src_teredo_client_ipv4,
      { "Source Teredo Client IPv4",    "ipv6.src_tc_ipv4",
                                FT_IPv4, BASE_NONE, NULL, 0x0,
                                "Source IPv6 Teredo Client Encapsulated IPv4 Address", HFILL }},
    { &hf_ipv6_dst,
      { "Destination",          "ipv6.dst",
                                FT_IPv6, BASE_NONE, NULL, 0x0,
                                "Destination IPv6 Address", HFILL }},
    { &hf_ipv6_dst_host,
      { "Destination Host",     "ipv6.dst_host",
                                FT_STRING, BASE_NONE, NULL, 0x0,
                                "Destination IPv6 Host", HFILL }},
    { &hf_ipv6_dst_sa_mac,
      { "Destination SA MAC",                   "ipv6.dst_sa_mac",
                                FT_ETHER, BASE_NONE, NULL, 0x0,
                                "Destination IPv6 Stateless Autoconfiguration MAC Address", HFILL }},
    { &hf_ipv6_dst_isatap_ipv4,
      { "Destination ISATAP IPv4",              "ipv6.dst_isatap_ipv4",
                                FT_IPv4, BASE_NONE, NULL, 0x0,
                                "Destination IPv6 ISATAP Encapsulated IPv4 Address", HFILL }},
    { &hf_ipv6_dst_6to4_gateway_ipv4,
      { "Destination 6to4 Gateway IPv4",        "ipv6.dst_6to4_gw_ipv4",
                                FT_IPv4, BASE_NONE, NULL, 0x0,
                                "Destination IPv6 6to4 Gateway IPv4 Address", HFILL }},
    { &hf_ipv6_dst_6to4_sla_id,
      { "Destination 6to4 SLA ID",               "ipv6.dst_6to4_sla_id",
                                FT_UINT16, BASE_DEC, NULL, 0x0,
                                "Destination IPv6 6to4 SLA ID", HFILL }},
    { &hf_ipv6_dst_teredo_server_ipv4,
      { "Destination Teredo Server IPv4",         "ipv6.dst_ts_ipv4",
                                FT_IPv4, BASE_NONE, NULL, 0x0,
                                "Destination IPv6 Teredo Server Encapsulated IPv4 Address", HFILL }},
    { &hf_ipv6_dst_teredo_port,
      { "Destination Teredo Port",                "ipv6.dst_tc_port",
                                FT_UINT16, BASE_DEC, NULL, 0x0,
                                "Destination IPv6 Teredo Client Mapped Port", HFILL }},
    { &hf_ipv6_dst_teredo_client_ipv4,
      { "Destination Teredo Client IPv4",         "ipv6.dst_tc_ipv4",
                                FT_IPv4, BASE_NONE, NULL, 0x0,
                                "Destination IPv6 Teredo Client Encapsulated IPv4 Address", HFILL }},
    { &hf_ipv6_addr,
      { "Source or Destination Address",          "ipv6.addr",
                                FT_IPv6, BASE_NONE, NULL, 0x0,
                                NULL, HFILL }},
    { &hf_ipv6_host,
      { "Source or Destination Host",             "ipv6.host",
                                FT_STRING, BASE_NONE, NULL, 0x0,
                                NULL, HFILL }},

    { &hf_ipv6_sa_mac,
      { "SA MAC",               "ipv6.sa_mac",
                                FT_ETHER, BASE_NONE, NULL, 0x0,
                                "IPv6 Stateless Autoconfiguration MAC Address", HFILL }},
    { &hf_ipv6_isatap_ipv4,
      { "ISATAP IPv4",          "ipv6.isatap_ipv4",
                                FT_IPv4, BASE_NONE, NULL, 0x0,
                                "IPv6 ISATAP Encapsulated IPv4 Address", HFILL }},
    { &hf_ipv6_6to4_gateway_ipv4,
      { "6to4 Gateway IPv4",    "ipv6.6to4_gw_ipv4",
                                FT_IPv4, BASE_NONE, NULL, 0x0,
                                "IPv6 6to4 Gateway IPv4 Address", HFILL }},
    { &hf_ipv6_6to4_sla_id,
      { "6to4 SLA ID",          "ipv6.6to4_sla_id",
                                FT_UINT16, BASE_DEC, NULL, 0x0,
                                "IPv6 6to4 SLA ID", HFILL }},
    { &hf_ipv6_teredo_server_ipv4,
      { "Teredo Server IPv4",   "ipv6.ts_ipv4",
                                FT_IPv4, BASE_NONE, NULL, 0x0,
                                "IPv6 Teredo Server Encapsulated IPv4 Address", HFILL }},
    { &hf_ipv6_teredo_port,
      { "Teredo Port",          "ipv6.tc_port",
                                FT_UINT16, BASE_DEC, NULL, 0x0,
                                "IPv6 Teredo Client Mapped Port", HFILL }},
    { &hf_ipv6_teredo_client_ipv4,
      { "Teredo Client IPv4",   "ipv6.tc_ipv4",
                                FT_IPv4, BASE_NONE, NULL, 0x0,
                                "IPv6 Teredo Client Encapsulated IPv4 Address", HFILL }},
#ifdef HAVE_GEOIP_V6
    { &hf_geoip_country,
      { "Source or Destination GeoIP Country", "ipv6.geoip.country",
                                FT_STRING, BASE_NONE, NULL, 0x0,
                                NULL, HFILL }},
    { &hf_geoip_city,
      { "Source or Destination GeoIP City", "ipv6.geoip.city",
                                FT_STRING, BASE_NONE, NULL, 0x0,
                                NULL, HFILL }},
    { &hf_geoip_org,
      { "Source or Destination GeoIP Organization", "ipv6.geoip.org",
                                FT_STRING, BASE_NONE, NULL, 0x0,
                                NULL, HFILL }},
    { &hf_geoip_isp,
      { "Source or Destination GeoIP ISP", "ipv6.geoip.isp",
                                FT_STRING, BASE_NONE, NULL, 0x0,
                                NULL, HFILL }},
    { &hf_geoip_asnum,
      { "Source or Destination GeoIP AS Number", "ipv6.geoip.asnum",
                                FT_STRING, BASE_NONE, NULL, 0x0,
                                NULL, HFILL }},
    { &hf_geoip_lat,
      { "Source or Destination GeoIP Latitude", "ipv6.geoip.lat",
                                FT_DOUBLE, BASE_NONE, NULL, 0x0,
                                NULL, HFILL }},
    { &hf_geoip_lon,
      { "Source or Destination GeoIP Longitude", "ipv6.geoip.lon",
                                FT_DOUBLE, BASE_NONE, NULL, 0x0,
                                NULL, HFILL }},
    { &hf_geoip_src_country,
      { "Source GeoIP Country", "ipv6.geoip.src_country",
                                FT_STRING, BASE_NONE, NULL, 0x0,
                                NULL, HFILL }},
    { &hf_geoip_src_city,
      { "Source GeoIP City", "ipv6.geoip.src_city",
                                FT_STRING, BASE_NONE, NULL, 0x0,
                                NULL, HFILL }},
    { &hf_geoip_src_org,
      { "Source GeoIP Organization", "ipv6.geoip.src_org",
                                FT_STRING, BASE_NONE, NULL, 0x0,
                                NULL, HFILL }},
    { &hf_geoip_src_isp,
      { "Source GeoIP ISP", "ipv6.geoip.src_isp",
                                FT_STRING, BASE_NONE, NULL, 0x0,
                                NULL, HFILL }},
    { &hf_geoip_src_asnum,
      { "Source GeoIP AS Number", "ipv6.geoip.src_asnum",
                                FT_STRING, BASE_NONE, NULL, 0x0,
                                NULL, HFILL }},
    { &hf_geoip_src_lat,
      { "Source GeoIP Latitude", "ipv6.geoip.src_lat",
                                FT_DOUBLE, BASE_NONE, NULL, 0x0,
                                NULL, HFILL }},
    { &hf_geoip_src_lon,
      { "Source GeoIP Longitude", "ipv6.geoip.src_lon",
                                FT_DOUBLE, BASE_NONE, NULL, 0x0,
                                NULL, HFILL }},
    { &hf_geoip_dst_country,
      { "Destination GeoIP Country", "ipv6.geoip.dst_country",
                                FT_STRING, BASE_NONE, NULL, 0x0,
                                NULL, HFILL }},
    { &hf_geoip_dst_city,
      { "Destination GeoIP City", "ipv6.geoip.dst_city",
                                FT_STRING, BASE_NONE, NULL, 0x0,
                                NULL, HFILL }},
    { &hf_geoip_dst_org,
      { "Destination GeoIP Organization", "ipv6.geoip.dst_org",
                                FT_STRING, BASE_NONE, NULL, 0x0,
                                NULL, HFILL }},
    { &hf_geoip_dst_isp,
      { "Destination GeoIP ISP", "ipv6.geoip.dst_isp",
                                FT_STRING, BASE_NONE, NULL, 0x0,
                                NULL, HFILL }},
    { &hf_geoip_dst_asnum,
      { "Destination GeoIP AS Number", "ipv6.geoip.dst_asnum",
                                FT_STRING, BASE_NONE, NULL, 0x0,
                                NULL, HFILL }},
    { &hf_geoip_dst_lat,
      { "Destination GeoIP Latitude", "ipv6.geoip.dst_lat",
                                FT_DOUBLE, BASE_NONE, NULL, 0x0,
                                NULL, HFILL }},
    { &hf_geoip_dst_lon,
      { "Destination GeoIP Longitude", "ipv6.geoip.dst_lon",
                                FT_DOUBLE, BASE_NONE, NULL, 0x0,
                                NULL, HFILL }},
#endif /* HAVE_GEOIP_V6 */


    { &hf_ipv6_dst_opt,
      { "Destination Option",   "ipv6.dst_opt",
                                FT_NONE, BASE_NONE, NULL, 0x0,
                                NULL, HFILL }},
    { &hf_ipv6_hop_opt,
      { "Hop-by-Hop Option",    "ipv6.hop_opt",
                                FT_NONE, BASE_NONE, NULL, 0x0,
                                NULL, HFILL }},
    { &hf_ipv6_unk_hdr,
      { "Unknown Extension Header",     "ipv6.unknown_hdr",
                                FT_NONE, BASE_NONE, NULL, 0x0,
                                NULL, HFILL }},
    { &hf_ipv6_opt,
      { "IPv6 Option",          "ipv6.opt",
                                  FT_NONE, BASE_NONE, NULL, 0x0,
                                  "Option", HFILL }},
    { &hf_ipv6_opt_type,
      { "Type",                   "ipv6.opt.type",
                                  FT_UINT8, BASE_DEC, VALS(ipv6_opt_vals), 0x0,
                                  "Options type", HFILL }},
    { &hf_ipv6_opt_length,
      { "Length",                 "ipv6.opt.length",
                                FT_UINT8, BASE_DEC, NULL, 0x0,
                                "Length in units of 8 octets", HFILL }},
    { &hf_ipv6_opt_pad1,
      { "Pad1",                 "ipv6.opt.pad1",
                                FT_NONE, BASE_NONE, NULL, 0x0,
                                "Pad1 Option", HFILL }},
    { &hf_ipv6_opt_padn,
      { "PadN",                 "ipv6.opt.padn",
                                FT_BYTES, BASE_NONE, NULL, 0x0,
                                "PadN Option", HFILL }},
    { &hf_ipv6_opt_rtalert,
      { "Router Alert",         "ipv6.opt.router_alert",
                                FT_UINT16, BASE_DEC, VALS(rtalertvals), 0x0,
                                NULL, HFILL }},
    { &hf_ipv6_opt_tel,
      { "Tunnel Encapsulation Limit", "ipv6.opt.tel",
                                FT_UINT8, BASE_DEC, NULL, 0x0,
                                "How many further levels of encapsulation are permitted", HFILL }},
    { &hf_ipv6_opt_jumbo,
      { "Jumbo",                "ipv6.opt.jumbo",
                                FT_UINT32, BASE_DEC, NULL, 0x0,
                                "Length of the IPv6 packet in octets", HFILL }},
    { &hf_ipv6_opt_calipso_doi,
      { "CALIPSO Domain of Interpretation","ipv6.opt.calipso.doi",
                                FT_UINT8, BASE_DEC, NULL, 0x0,
                                NULL, HFILL }},
    { &hf_ipv6_opt_calipso_cmpt_length,
      { "Compartment Length","ipv6.opt.calipso.cmpt.length",
                                FT_UINT8, BASE_DEC, NULL, 0x0,
                                NULL, HFILL }},
    { &hf_ipv6_opt_calipso_sens_level,
      { "Sensitivity Level","ipv6.opt.calipso.sens_level",
                                FT_UINT8, BASE_DEC, NULL, 0x0,
                                NULL, HFILL }},
    { &hf_ipv6_opt_calipso_checksum,
      { "Checksum","ipv6.opt.calipso.checksum",
                                FT_UINT16, BASE_HEX, NULL, 0x0,
                                NULL, HFILL }},
    { &hf_ipv6_opt_calipso_cmpt_bitmap,
      { "Compartment Bitmap","ipv6.opt.calipso.cmpt_bitmap",
                                FT_BYTES, BASE_NONE, NULL, 0x0,
                                NULL, HFILL }},
    { &hf_ipv6_opt_qs_func,
      { "Function",             "ipv6.opt.qs_func",
                                FT_UINT8, BASE_DEC, VALS(qs_func_vals), QS_FUNC_MASK,
                                NULL, HFILL }},
    { &hf_ipv6_opt_qs_rate,
      { "Rate",                 "ipv6.opt.qs_rate",
                                FT_UINT8, BASE_DEC | BASE_EXT_STRING, &qs_rate_vals_ext, QS_RATE_MASK,
                                NULL, HFILL }},
    { &hf_ipv6_opt_qs_ttl,
      { "QS TTL",               "ipv6.opt.qs_ttl",
                                FT_UINT8, BASE_DEC, NULL, 0x0,
                                NULL, HFILL }},
    { &hf_ipv6_opt_qs_ttl_diff,
      { "TTL Diff",             "ipv6.opt.qs_ttl_diff",
                                FT_UINT8, BASE_DEC, NULL, 0x0,
                                NULL, HFILL }},
    { &hf_ipv6_opt_qs_unused,
      { "Not Used",             "ipv6.opt.qs_unused",
                                FT_UINT8, BASE_DEC, NULL, 0x0,
                                NULL, HFILL }},
    { &hf_ipv6_opt_qs_nonce,
      { "QS Nonce",             "ipv6.opt.qs_nonce",
                                FT_UINT32, BASE_HEX, NULL, 0xFFFFFFFC,
                                NULL, HFILL }},
    { &hf_ipv6_opt_qs_reserved,
      { "Reserved",             "ipv6.opt.qs_reserved",
                                FT_UINT32, BASE_HEX, NULL, 0x0003,
                                NULL, HFILL }},
    { &hf_ipv6_opt_rpl_flag,
      { "Flag",                 "ipv6.opt.rpl.flag",
                                FT_UINT8, BASE_DEC, NULL, 0x0,
                                NULL, HFILL }},
    { &hf_ipv6_opt_rpl_flag_o,
      { "Down",                 "ipv6.opt.rpl.flag.o",
                                FT_BOOLEAN, 8, NULL, 0x80,
                                "The packet is expected to progress Up or Down", HFILL }},
    { &hf_ipv6_opt_rpl_flag_r,
      { "Rank Error",           "ipv6.opt.rpl.flag.r",
                                FT_BOOLEAN, 8, NULL, 0x40,
                                "Indicating whether a rank error was detected", HFILL }},
    { &hf_ipv6_opt_rpl_flag_f,
      { "Forwarding Error",     "ipv6.opt.rpl.flag.f",
                                FT_BOOLEAN, 8, NULL, 0x20,
                                "Indicating that this node can not forward the packet further towards the destination", HFILL }},
    { &hf_ipv6_opt_rpl_flag_rsv,
      { "Reserved",     "ipv6.opt.rpl.flag.rsv",
                                FT_UINT8, BASE_HEX, NULL, 0x1F,
                                "Reserved (Must Be Zero)", HFILL }},
    { &hf_ipv6_opt_rpl_instance_id,
      { "RPLInstanceID",        "ipv6.opt.rpl.instance_id",
                                FT_UINT8, BASE_HEX, NULL, 0x0,
                                "Indicating the DODAG instance along which the packet is sent", HFILL }},
    { &hf_ipv6_opt_rpl_senderrank,
      { "Sender Rank",          "ipv6.opt.rpl.sender_rank",
                                FT_UINT16, BASE_HEX, NULL, 0x0,
                                "Set to zero by the source and to DAGRank(rank) by a router that forwards inside the RPL network", HFILL }},
    { &hf_ipv6_opt_experimental,
      { "Experimental Option","ipv6.opt.experimental",
                                FT_BYTES, BASE_NONE, NULL, 0x0,
                                NULL, HFILL }},
    { &hf_ipv6_opt_unknown,
      { "Unknown Option Payload","ipv6.opt.unknown",
                                FT_BYTES, BASE_NONE, NULL, 0x0,
                                NULL, HFILL }},
    { &hf_ipv6_routing_hdr_opt,
      { "Routing Header, Type","ipv6.routing_hdr",
                                FT_UINT8, BASE_DEC, NULL, 0x0,
                                "Routing Header Option", HFILL }},
    { &hf_ipv6_routing_hdr_type,
      { "Type",                 "ipv6.routing_hdr.type",
                                FT_UINT8, BASE_DEC, VALS(routing_header_type), 0x0,
                                "Routing Header Type", HFILL }},
    { &hf_ipv6_routing_hdr_left,
      { "Segments Left",        "ipv6.routing_hdr.left",
                                FT_UINT8, BASE_DEC, NULL, 0x0,
                                "Routing Header Segments Left", HFILL }},
    { &hf_ipv6_routing_hdr_addr,
      { "Address",              "ipv6.routing_hdr.addr",
                                FT_IPv6, BASE_NONE, NULL, 0x0,
                                "Routing Header Address", HFILL }},
    { &hf_ipv6_frag_nxt,
      { "Next header", "ipv6.fragment.nxt",
                                FT_UINT16, BASE_DEC | BASE_EXT_STRING, &ipproto_val_ext, 0x0,
                                "Fragment next header", HFILL }},
    { &hf_ipv6_frag_reserved,
      { "Reserved octet", "ipv6.fragment.reserved_octet",
                                FT_UINT16, BASE_HEX, NULL, 0x0,
                                "Should always be 0", HFILL }},
    { &hf_ipv6_frag_offset,
      { "Offset",               "ipv6.fragment.offset",
                                FT_UINT16, BASE_DEC_HEX, NULL, IP6F_OFF_MASK,
                                "Fragment Offset", HFILL }},
    { &hf_ipv6_frag_reserved_bits,
      { "Reserved bits",        "ipv6.fragment.reserved_bits",
                                FT_UINT16, BASE_DEC_HEX, NULL, IP6F_RESERVED_MASK,
                                NULL, HFILL }},
    { &hf_ipv6_frag_more,
      { "More Fragment",        "ipv6.fragment.more",
                                FT_BOOLEAN, 16, TFS(&tfs_yes_no), IP6F_MORE_FRAG,
                                "More Fragments", HFILL }},
    { &hf_ipv6_frag_id,
      { "Identification",        "ipv6.fragment.id",
                                FT_UINT32, BASE_HEX, NULL, 0x0,
                                "Fragment Identification", HFILL }},
    { &hf_ipv6_fragment_overlap,
      { "Fragment overlap",     "ipv6.fragment.overlap",
                                FT_BOOLEAN, BASE_NONE, NULL, 0x0,
                                "Fragment overlaps with other fragments", HFILL }},

    { &hf_ipv6_fragment_overlap_conflict,
      { "Conflicting data in fragment overlap", "ipv6.fragment.overlap.conflict",
                                FT_BOOLEAN, BASE_NONE, NULL, 0x0,
                                "Overlapping fragments contained conflicting data", HFILL }},

    { &hf_ipv6_fragment_multiple_tails,
      { "Multiple tail fragments found", "ipv6.fragment.multipletails",
                                FT_BOOLEAN, BASE_NONE, NULL, 0x0,
                                "Several tails were found when defragmenting the packet", HFILL }},

    { &hf_ipv6_fragment_too_long_fragment,
      { "Fragment too long",    "ipv6.fragment.toolongfragment",
                                FT_BOOLEAN, BASE_NONE, NULL, 0x0,
                                "Fragment contained data past end of packet", HFILL }},

    { &hf_ipv6_fragment_error,
      { "Defragmentation error", "ipv6.fragment.error",
                                FT_FRAMENUM, BASE_NONE, NULL, 0x0,
                                "Defragmentation error due to illegal fragments", HFILL }},

    { &hf_ipv6_fragment_count,
      { "Fragment count",       "ipv6.fragment.count",
                                FT_UINT32, BASE_DEC, NULL, 0x0,
                                NULL, HFILL }},

    { &hf_ipv6_fragment,
      { "IPv6 Fragment",        "ipv6.fragment",
                                FT_FRAMENUM, BASE_NONE, NULL, 0x0,
                                NULL, HFILL }},

    { &hf_ipv6_fragments,
      { "IPv6 Fragments",       "ipv6.fragments",
                                FT_NONE, BASE_NONE, NULL, 0x0,
                                NULL, HFILL }},

    { &hf_ipv6_reassembled_in,
      { "Reassembled IPv6 in frame", "ipv6.reassembled_in",
                                FT_FRAMENUM, BASE_NONE, NULL, 0x0,
                                "This IPv6 packet is reassembled in this frame", HFILL }},

    { &hf_ipv6_reassembled_length,
      { "Reassembled IPv6 length", "ipv6.reassembled.length",
                                FT_UINT32, BASE_DEC, NULL, 0x0,
                                "The total length of the reassembled payload", HFILL }},

    { &hf_ipv6_reassembled_data,
      { "Reassembled IPv6 data", "ipv6.reassembled.data",
                                FT_BYTES, BASE_NONE, NULL, 0x0,
                                "The reassembled payload", HFILL }},
    /* RPL Routing Header */
    { &hf_ipv6_routing_hdr_rpl_cmprI,
      { "Compressed Internal Octets (CmprI)", "ipv6.routing_hdr.rpl.cmprI",
        FT_UINT32, BASE_DEC, NULL, IP6RRPL_BITMASK_CMPRI,
        "Elided octets from all but last segment", HFILL }},

    { &hf_ipv6_routing_hdr_rpl_cmprE,
      { "Compressed Final Octets (CmprE)", "ipv6.routing_hdr.rpl.cmprE",
        FT_UINT32, BASE_DEC, NULL, IP6RRPL_BITMASK_CMPRE,
        "Elided octets from last segment address", HFILL }},

    { &hf_ipv6_routing_hdr_rpl_pad,
      { "Padding Bytes", "ipv6.routing_hdr.rpl.pad",
        FT_UINT32, BASE_DEC, NULL, IP6RRPL_BITMASK_PAD,
        NULL, HFILL }},

    { &hf_ipv6_routing_hdr_rpl_reserved,
      { "Reserved", "ipv6.routing_hdr.rpl.reserved",
        FT_UINT32, BASE_DEC, NULL, IP6RRPL_BITMASK_RESERVED,
        "Must be Zero", HFILL }},

    { &hf_ipv6_routing_hdr_rpl_segments,
      { "Total Segments", "ipv6.routing_hdr.rpl.segments",
        FT_INT32, BASE_DEC, NULL, 0,
        NULL, HFILL }},

    { &hf_ipv6_routing_hdr_rpl_addr,
      { "Address", "ipv6.routing_hdr.rpl.address",
        FT_BYTES, BASE_NONE, NULL, 0,
        NULL, HFILL }},

    { &hf_ipv6_routing_hdr_rpl_fulladdr,
      { "Full Address", "ipv6.routing_hdr.rpl.full_address",
        FT_IPv6, BASE_NONE, NULL, 0,
        "Uncompressed IPv6 Address", HFILL }},

    /* Mobile IPv6 */
    { &hf_ipv6_mipv6_home_address,
      { "Home Address", "ipv6.mipv6_home_address",
                                FT_IPv6, BASE_NONE, NULL, 0x0,
                                NULL, HFILL }},

    /* SHIM6 */
    { &hf_ipv6_shim6,
      { "SHIM6",                "ipv6.shim6",
                                FT_NONE, BASE_NONE, NULL, 0x0,
                                NULL, HFILL }},

    { &hf_ipv6_shim6_nxt,
      { "Next Header",          "ipv6.shim6.nxt",
                                FT_UINT8, BASE_DEC, NULL, 0x0,
                                NULL, HFILL }},

    { &hf_ipv6_shim6_len,
      { "Header Ext Length",    "ipv6.shim6.len",
                                FT_UINT8, BASE_DEC, NULL, 0x0,
                                NULL, HFILL }},

    { &hf_ipv6_shim6_p,
      { "P Bit",                "ipv6.shim6.p",
                                FT_BOOLEAN, 8, NULL, SHIM6_BITMASK_P,
                                NULL, HFILL }},

    { &hf_ipv6_shim6_ct,
      { "Context Tag",          "ipv6.shim6.ct",
                                FT_NONE, BASE_NONE, NULL, 0x0,
                                NULL, HFILL }},

    { &hf_ipv6_shim6_type,
      { "Message Type",         "ipv6.shim6.type",
                                FT_UINT8, BASE_DEC,
                                VALS(shimctrlvals), SHIM6_BITMASK_TYPE,
                                NULL, HFILL }},

    { &hf_ipv6_shim6_proto,
      { "Protocol",             "ipv6.shim6.proto",
                                FT_UINT8, BASE_DEC,
                                VALS(shim6_protocol), SHIM6_BITMASK_PROTOCOL,
                                NULL, HFILL }},

    { &hf_ipv6_shim6_checksum,
      { "Checksum",             "ipv6.shim6.checksum",
                                FT_UINT16, BASE_HEX, NULL, 0x0,
                                "Shim6 Checksum", HFILL }},
    { &hf_ipv6_shim6_checksum_bad,
      { "Bad Checksum",         "ipv6.shim6.checksum_bad",
                                FT_BOOLEAN, BASE_NONE, NULL, 0x0,
                                "Shim6 Bad Checksum", HFILL }},

    { &hf_ipv6_shim6_checksum_good,
      { "Good Checksum",        "ipv6.shim6.checksum_good",
                                FT_BOOLEAN, BASE_NONE, NULL, 0x0,
                                NULL, HFILL }},

    { &hf_ipv6_shim6_inonce,
      { "Initiator Nonce",      "ipv6.shim6.inonce",
                                FT_UINT32, BASE_DEC_HEX, NULL, 0x0,
                                NULL, HFILL }},

    { &hf_ipv6_shim6_rnonce,
      { "Responder Nonce",      "ipv6.shim6.rnonce",
                                FT_UINT32, BASE_DEC_HEX, NULL, 0x0,
                                NULL, HFILL }},

    { &hf_ipv6_shim6_precvd,
      { "Probes Received",      "ipv6.shim6.precvd",
                                FT_UINT8, BASE_DEC, NULL, 0x0,
                                NULL, HFILL }},

    { &hf_ipv6_shim6_psent,
      { "Probes Sent",          "ipv6.shim6.psent",
                                FT_UINT8, BASE_DEC, NULL, 0x0,
                                NULL, HFILL }},

    { &hf_ipv6_shim6_psrc,
      { "Source Address",       "ipv6.shim6.psrc",
                                FT_IPv6, BASE_NONE, NULL, 0x0,
                                "Shim6 Probe Source Address", HFILL }},

    { &hf_ipv6_shim6_pdst,
      { "Destination Address",  "ipv6.shim6.pdst",
                                FT_IPv6, BASE_NONE, NULL, 0x0,
                                "Shim6 Probe Destination Address", HFILL }},

    { &hf_ipv6_shim6_pnonce,
      { "Nonce",                "ipv6.shim6.pnonce",
                                FT_UINT32, BASE_DEC_HEX, NULL, 0x0,
                                "Shim6 Probe Nonce", HFILL }},

    { &hf_ipv6_shim6_pdata,
      { "Data",                 "ipv6.shim6.pdata",
                                FT_UINT32, BASE_HEX, NULL, 0x0,
                                "Shim6 Probe Data", HFILL }},

    { &hf_ipv6_shim6_sulid,
      { "Sender ULID",          "ipv6.shim6.sulid",
                                FT_IPv6, BASE_NONE, NULL, 0x0,
                                "Shim6 Sender ULID", HFILL }},

    { &hf_ipv6_shim6_rulid,
      { "Receiver ULID",        "ipv6.shim6.rulid",
                                FT_IPv6, BASE_NONE, NULL, 0x0,
                                "Shim6 Receiver ULID", HFILL }},

    { &hf_ipv6_shim6_reap,
      { "REAP State",           "ipv6.shim6.reap",
                                FT_UINT8, BASE_DEC, NULL, 0x0,
                                NULL, HFILL }},

    { &hf_ipv6_shim6_opt_type,
      { "Option Type",          "ipv6.shim6.opt.type",
                                FT_UINT16, BASE_DEC,
                                VALS(shimoptvals), SHIM6_BITMASK_OPT_TYPE,
                                "Shim6 Option Type", HFILL }},

    { &hf_ipv6_shim6_opt_critical,
      { "Option Critical Bit",  "ipv6.shim6.opt.critical",
                                FT_BOOLEAN, 8,
                                TFS(&tfs_yes_no),
                                SHIM6_BITMASK_CRITICAL,
                                "TRUE : option is critical, FALSE: option is not critical",
                                HFILL }},

    { &hf_ipv6_shim6_opt_len,
      { "Content Length",       "ipv6.shim6.opt.len",
                                FT_UINT16, BASE_DEC, NULL, 0x0,
                                "Content Length Option", HFILL }},

    { &hf_ipv6_shim6_opt_total_len,
      { "Total Length",         "ipv6.shim6.opt.total_len",
                                FT_UINT16, BASE_DEC, NULL, 0x0,
                                "Total Option Length", HFILL }},

    { &hf_ipv6_shim6_opt_loc_verif_methods,
      { "Verification Method",  "ipv6.shim6.opt.verif_method",
                                FT_UINT8, BASE_DEC,
                                VALS(shimverifmethods), 0x0,
                                "Locator Verification Method", HFILL }},

    { &hf_ipv6_shim6_opt_loclist,
      { "Locator List Generation", "ipv6.shim6.opt.loclist",
                                FT_UINT32, BASE_DEC_HEX, NULL, 0x0,
                                NULL, HFILL }},

    { &hf_ipv6_shim6_locator,
      { "Locator",              "ipv6.shim6.locator",
                                FT_IPv6, BASE_NONE, NULL, 0x0,
                                "Shim6 Locator", HFILL }},

    { &hf_ipv6_shim6_opt_locnum,
      { "Num Locators",         "ipv6.shim6.opt.locnum",
                                FT_UINT8, BASE_DEC, NULL, 0x0,
                                "Number of Locators in Locator List", HFILL }},

    { &hf_ipv6_shim6_opt_elemlen,
      { "Element Length",       "ipv6.shim6.opt.elemlen",
                                FT_UINT8, BASE_DEC, NULL, 0x0,
                                "Length of Elements in Locator Preferences Option", HFILL }},
    { &hf_ipv6_shim6_loc_flag,
      { "Flags",                "ipv6.shim6.loc.flags",
                                FT_UINT8, BASE_DEC, NULL, 0x0,
                                "Locator Preferences Flags", HFILL }},

    { &hf_ipv6_shim6_loc_prio,
      { "Priority",             "ipv6.shim6.loc.prio",
                                FT_UINT8, BASE_DEC, NULL, 0x0,
                                "Locator Preferences Priority", HFILL }},

    { &hf_ipv6_shim6_loc_weight,
      { "Weight",               "ipv6.shim6.loc.weight",
                                FT_UINT8, BASE_DEC, NULL, 0x0,
                                "Locator Preferences Weight", HFILL }},

    { &hf_ipv6_shim6_opt_fii,
      { "Forked Instance Identifier", "ipv6.shim6.opt.fii",
                                FT_UINT32, BASE_DEC_HEX, NULL, 0x0,
                                NULL, HFILL }},

    { &hf_ipv6_traffic_class_dscp,
      { "Differentiated Services Field", "ipv6.traffic_class.dscp",
                                FT_UINT32, BASE_HEX | BASE_EXT_STRING, &dscp_vals_ext, 0x0FC00000, NULL, HFILL }},

    { &hf_ipv6_traffic_class_ect,
      { "ECN-Capable Transport (ECT)", "ipv6.traffic_class.ect",
                                FT_BOOLEAN, 32, TFS(&tfs_set_notset), 0x0200000, NULL, HFILL }},

    { &hf_ipv6_traffic_class_ce,
      { "ECN-CE",               "ipv6.traffic_class.ce",
                                FT_BOOLEAN, 32, TFS(&tfs_set_notset), 0x0100000, NULL, HFILL }},
  };
  static gint *ett[] = {
    &ett_ipv6,
    &ett_ipv6_opt,
    &ett_ipv6_opt_flag,
    &ett_ipv6_version,
    &ett_ipv6_shim6,
    &ett_ipv6_shim6_option,
    &ett_ipv6_shim6_locators,
    &ett_ipv6_shim6_verif_methods,
    &ett_ipv6_shim6_loc_pref,
    &ett_ipv6_shim6_probes_sent,
    &ett_ipv6_shim6_probes_rcvd,
    &ett_ipv6_shim6_probe_sent,
    &ett_ipv6_shim6_probe_rcvd,
    &ett_ipv6_shim6_cksum,
    &ett_ipv6_fragments,
    &ett_ipv6_fragment,
    &ett_ipv6_traffic_class,
#ifdef HAVE_GEOIP_V6
    &ett_geoip_info
#endif /* HAVE_GEOIP_V6 */
  };
  module_t *ipv6_module;

  proto_ipv6 = proto_register_protocol("Internet Protocol Version 6", "IPv6", "ipv6");
  proto_register_field_array(proto_ipv6, hf, array_length(hf));
  proto_register_subtree_array(ett, array_length(ett));

  /* Register configuration options */
  ipv6_module = prefs_register_protocol(proto_ipv6, NULL);
  prefs_register_bool_preference(ipv6_module, "defragment",
        "Reassemble fragmented IPv6 datagrams",
        "Whether fragmented IPv6 datagrams should be reassembled",
        &ipv6_reassemble);
  prefs_register_bool_preference(ipv6_module, "summary_in_tree",
        "Show IPv6 summary in protocol tree",
        "Whether the IPv6 summary line should be shown in the protocol tree",
        &ipv6_summary_in_tree);
#ifdef HAVE_GEOIP_V6
        prefs_register_bool_preference(ipv6_module, "use_geoip" ,
                  "Enable GeoIP lookups",
                  "Whether to look up IPv6 addresses in each GeoIP database we have loaded",
                  &ipv6_use_geoip);
#endif /* HAVE_GEOIP_V6 */

  /* RPL Strict Header Checking */
  prefs_register_bool_preference(ipv6_module, "perform_strict_rpl_srh_rfc_checking",
        "Perform strict checking for adherence to the RFC for RPL Source Routing Headers (RFC 6554)",
        "Whether to check that all RPL Source Routing Headers adhere to RFC 6554",
        &g_ipv6_rpl_srh_strict_rfc_checking);

  register_dissector("ipv6", dissect_ipv6, proto_ipv6);
  register_init_routine(ipv6_reassemble_init);
  ipv6_tap = register_tap("ipv6");
}

void
proto_reg_handoff_ipv6(void)
{
  dissector_handle_t ipv6_handle;

  data_handle = find_dissector("data");
  ipv6_handle = find_dissector("ipv6");
  dissector_add_uint("ethertype", ETHERTYPE_IPv6, ipv6_handle);
  dissector_add_uint("ppp.protocol", PPP_IPV6, ipv6_handle);
  dissector_add_uint("ppp.protocol", ETHERTYPE_IPv6, ipv6_handle);
  dissector_add_uint("gre.proto", ETHERTYPE_IPv6, ipv6_handle);
  dissector_add_uint("ip.proto", IP_PROTO_IPV6, ipv6_handle);
  dissector_add_uint("null.type", BSD_AF_INET6_BSD, ipv6_handle);
  dissector_add_uint("null.type", BSD_AF_INET6_FREEBSD, ipv6_handle);
  dissector_add_uint("null.type", BSD_AF_INET6_DARWIN, ipv6_handle);
  dissector_add_uint("chdlctype", ETHERTYPE_IPv6, ipv6_handle);
  dissector_add_uint("fr.ietf", NLPID_IP6, ipv6_handle);
  dissector_add_uint("osinl.excl", NLPID_IP6, ipv6_handle);
  dissector_add_uint("x.25.spi", NLPID_IP6, ipv6_handle);
  dissector_add_uint("arcnet.protocol_id", ARCNET_PROTO_IPv6, ipv6_handle);

  ip_dissector_table = find_dissector_table("ip.proto");
}

/*
 * Editor modelines
 *
 * Local Variables:
 * c-basic-offset: 2
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * ex: set shiftwidth=2 tabstop=8 expandtab:
 * :indentSize=2:tabSize=8:noTabs=true:
 */
