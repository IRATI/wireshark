/* packet-wccp.c
 * Routines for Web Cache Communication Protocol dissection
 * Jerry Talkington <jtalkington@users.sourceforge.net>
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

#include <glib.h>
#include <epan/packet.h>
#include <epan/strutil.h>
#include <epan/emem.h>
#include <epan/expert.h>
#include "packet-wccp.h"

static int proto_wccp = -1;
static int hf_wccp_message_type = -1;	/* the message type */
static int hf_wccp_version = -1;	/* protocol version */
static int hf_wccp2_version = -1;
static int hf_hash_revision = -1;	/* the version of the hash */
static int hf_change_num = -1;		/* change number */
static int hf_hash_flag = -1;
static int hf_hash_flag_u = -1;
static int hf_recvd_id = -1;
static int hf_num_web_caches = -1;
static int hf_cache_ip = -1;
static int hf_item_header_length = -1;
static int hf_item_type = -1;
static int hf_item_length = -1;
static int hf_item_data = -1;
static int hf_security_option = -1;
static int hf_security_md5_checksum = -1;
static int hf_service_type = -1;
static int hf_service_id = -1;
static int hf_service_priority = -1;
static int hf_service_protocol = -1;
static int hf_service_flags = -1;
static int hf_service_flags_src_ip_hash = -1;
static int hf_service_flags_dest_ip_hash = -1;
static int hf_service_flags_src_port_hash = -1;
static int hf_service_flags_dest_port_hash = -1;
static int hf_service_flags_ports_defined = -1;
static int hf_service_flags_ports_source = -1;
static int hf_service_flags_src_ip_alt_hash = -1;
static int hf_service_flags_dest_ip_alt_hash = -1;
static int hf_service_flags_src_port_alt_hash = -1;
static int hf_service_flags_dest_port_alt_hash = -1;
static int hf_service_port = -1;
static int hf_router_identity_ip = -1;
static int hf_router_identity_receive_id = -1;
static int hf_router_send_to_ip = -1;
static int hf_router_num_recv_ip = -1;
static int hf_router_recv_ip = -1;
static int hf_web_cache_identity_ip = -1;
static int hf_web_cache_identity_hash_rev = -1;
static int hf_web_cache_identity_flags = -1;
static int hf_web_cache_identity_flag_hash_info = -1;
static int hf_web_cache_identity_flag_assign_type = -1;
static int hf_web_cache_identity_num_mask = -1;
static int hf_web_cache_identity_assign_mask = -1;
static int hf_web_cache_identity_assign_weight = -1;
static int hf_web_cache_identity_status = -1;
static int hf_assignment_key_ip = -1;
static int hf_assignment_key_change_num = -1;
static int hf_router_view_member_change_num = -1;
static int hf_router_router_num = -1;
static int hf_router_router_ip = -1;
static int hf_wc_view_info_change_num = -1;
static int hf_wc_view_router_num = -1;
static int hf_wc_view_ip = -1;
static int hf_router_assignment_element_ip = -1;
static int hf_router_assignment_element_receive_id = -1;
static int hf_router_assignment_element_change_num = -1;
static int hf_wccp2_assignment_info_router_num = -1;
static int hf_wccp2_assignment_info_ip = -1;
static int hf_router_query_info_ip = -1;
static int hf_router_query_info_receive_id = -1;
static int hf_router_query_info_send_to_ip = -1;
static int hf_router_query_info_target_ip = -1;
static int hf_capability_info_type = -1;
static int hf_capability_info_length = -1;
static int hf_capability_info_bytes = -1;
static int hf_capability_info_value = -1;
static int hf_capability_forwarding_method_flag_gre = -1;
static int hf_capability_forwarding_method_flag_l2 = -1;
static int hf_capability_assignment_method_flag_hash = -1;
static int hf_capability_assignment_method_flag_mask = -1;
static int hf_capability_return_method_flag_gre = -1;
static int hf_capability_return_method_flag_l2 = -1;
static int hf_wccp2_value_element_src_ip = -1;
static int hf_wccp2_value_element_dest_ip = -1;
static int hf_wccp2_value_element_src_port = -1;
static int hf_wccp2_value_element_dest_port = -1;
static int hf_wccp2_value_element_web_cache_ip = -1;
static int hf_wccp2_mask_value_set_element_src_ip = -1;
static int hf_wccp2_mask_value_set_element_dest_ip = -1;
static int hf_wccp2_mask_value_set_element_src_port = -1;
static int hf_wccp2_mask_value_set_element_dest_port = -1;
static int hf_wccp2_mask_value_set_element_num = -1;
static int hf_alt_assignment_info_assignment_type = -1;
static int hf_alt_assignment_info_assignment_length = -1;
static int hf_alt_assignment_info_num_routers = -1;
static int hf_alt_assignment_info_num_elements = -1;

static gint ett_wccp = -1;
static gint ett_cache_count = -1;
static gint ett_buckets = -1;
static gint ett_flags = -1;
static gint ett_cache_info = -1;
static gint ett_security_info = -1;
static gint ett_service_info = -1;
static gint ett_service_flags = -1;
static gint ett_router_identity_element = -1;
static gint ett_router_identity_info = -1;
static gint ett_wc_identity_element = -1;
static gint ett_wc_identity_info = -1;
static gint ett_router_view_info = -1;
static gint ett_wc_view_info = -1;
static gint ett_router_assignment_element = -1;
static gint ett_router_assignment_info = -1;
static gint ett_query_info = -1;
static gint ett_capabilities_info = -1;
static gint ett_capability_element = -1;
static gint ett_capability_forwarding_method = -1;
static gint ett_capability_assignment_method = -1;
static gint ett_capability_return_method = -1;
static gint ett_alt_assignment_info = -1;
static gint ett_mv_set_element = -1;
static gint ett_value_element = -1;
static gint ett_unknown_info = -1;

/*
 * At
 *
 *	http://tools.ietf.org/html/draft-forster-wrec-wccp-v1-00
 *
 * is a copy of the Internet-Draft for WCCP 1.0.
 *
 * At
 *
 *	http://tools.ietf.org/id/draft-wilson-wrec-wccp-v2-01.txt
 *
 * is an Internet-Draft for WCCP 2.0.
 *
 * At   http://tools.ietf.org/id/draft-mclaggan-wccp-v2rev1-00.txt
 *
 * is an Internet-Draft for WCCP 2 revision 1
 */

/* This is NOT IANA assigned */
#define UDP_PORT_WCCP	2048

#define WCCPv1			4
#define WCCPv2			0x0200
#define WCCP_HERE_I_AM		7
#define WCCP_I_SEE_YOU		8
#define WCCP_ASSIGN_BUCKET	9
#define WCCP2_HERE_I_AM		10
#define WCCP2_I_SEE_YOU		11
#define WCCP2_REDIRECT_ASSIGN	12
#define WCCP2_REMOVAL_QUERY	13

static const value_string wccp_type_vals[] = {
    { WCCP_HERE_I_AM,        "1.0 Here I am" },
    { WCCP_I_SEE_YOU,        "1.0 I see you" },
    { WCCP_ASSIGN_BUCKET,    "1.0 Assign bucket" },
    { WCCP2_HERE_I_AM,       "2.0 Here I am" },
    { WCCP2_I_SEE_YOU,       "2.0 I see you" },
    { WCCP2_REDIRECT_ASSIGN, "2.0 Redirect assign" },
    { WCCP2_REMOVAL_QUERY,   "2.0 Removal query" },
    { 0,                     NULL }
};

static const value_string wccp_version_val[] = {
	{ WCCPv1, "1"},
	{ WCCPv2, "2"},
	{ 0, NULL}
};

const true_false_string tfs_defined_not_defined = { "Defined", "Not defined" };
const true_false_string tfs_src_dest_port = { "Source port", "Destination port" };
const true_false_string tfs_historical_current = { "Historical", "Current" };
const true_false_string tfs_mask_hash = { "Mask", "Hash" };

#define HASH_INFO_SIZE	(4*(1+8+1))

#define	WCCP_U_FLAG	0x80000000
#define	WCCP_T_FLAG	0x4000

#define WCCP2_SECURITY_INFO		0
#define WCCP2_SERVICE_INFO		1
#define WCCP2_ROUTER_ID_INFO		2
#define WCCP2_WC_ID_INFO		3
#define WCCP2_RTR_VIEW_INFO		4
#define WCCP2_WC_VIEW_INFO		5
#define WCCP2_REDIRECT_ASSIGNMENT	6
#define WCCP2_QUERY_INFO		7
#define WCCP2_CAPABILITIES_INFO		8
#define WCCP2_ALT_ASSIGNMENT		13
#define WCCP2_ASSIGN_MAP		14
#define WCCP2_COMMAND_EXTENSION		15

static const value_string info_type_vals[] = {
	{ WCCP2_SECURITY_INFO,       "Security Info" },
	{ WCCP2_SERVICE_INFO,        "Service Info" },
	{ WCCP2_ROUTER_ID_INFO,      "Router Identity Info" },
	{ WCCP2_WC_ID_INFO,          "Web-Cache Identity Info" },
	{ WCCP2_RTR_VIEW_INFO,       "Router View Info" },
	{ WCCP2_WC_VIEW_INFO,        "Web-Cache View Info" },
	{ WCCP2_REDIRECT_ASSIGNMENT, "Assignment Info" },
	{ WCCP2_QUERY_INFO,          "Query Info" },
	{ WCCP2_CAPABILITIES_INFO,   "Capabilities Info" },
	{ WCCP2_ALT_ASSIGNMENT,      "Alternate Assignment" },
	{ WCCP2_ASSIGN_MAP,          "Assignment Map" },
	{ WCCP2_COMMAND_EXTENSION,   "Command Extension" },
	{ 0,                         NULL }
};

const value_string service_id_vals[] = {
    { 0x00, "HTTP" },
    { 0,    NULL }
};

typedef struct capability_flag {
	guint32 value;
	const char *short_name;
	int* phf;
} capability_flag;

static void dissect_hash_data(tvbuff_t *tvb, int offset,
    proto_tree *wccp_tree);
static void dissect_web_cache_list_entry(tvbuff_t *tvb, int offset,
    int idx, proto_tree *wccp_tree);
static guint32 wccp_bucket_info(guint8 bucket_info, proto_tree *bucket_tree,
    guint32 start, tvbuff_t *tvb, int offset);
static const gchar *bucket_name(guint8 bucket);
static guint16 dissect_wccp2_header(tvbuff_t *tvb, int offset,
    proto_tree *wccp_tree);
static void dissect_wccp2_info(tvbuff_t *tvb, int offset, guint16 length,
    packet_info *pinfo, proto_tree *wccp_tree);
static gboolean dissect_wccp2_security_info(tvbuff_t *tvb, int offset,
    int length, packet_info *pinfo, proto_tree *info_tree, proto_item *info_item);
static gboolean dissect_wccp2_service_info(tvbuff_t *tvb, int offset,
    int length, packet_info *pinfo, proto_tree *info_tree, proto_item *info_item);
static gboolean dissect_wccp2_router_identity_info(tvbuff_t *tvb, int offset,
    int length, packet_info *pinfo, proto_tree *info_tree, proto_item *info_item);
static gboolean dissect_wccp2_wc_identity_info(tvbuff_t *tvb, int offset,
    int length, packet_info *pinfo, proto_tree *info_tree, proto_item *info_item);
static gboolean dissect_wccp2_router_view_info(tvbuff_t *tvb, int offset,
    int length, packet_info *pinfo, proto_tree *info_tree, proto_item *info_item);
static gboolean dissect_wccp2_wc_view_info(tvbuff_t *tvb, int offset,
    int length, packet_info *pinfo, proto_tree *info_tree, proto_item *info_item);
static gboolean dissect_wccp2_assignment_info(tvbuff_t *tvb, int offset,
    int length, packet_info *pinfo, proto_tree *info_tree, proto_item *info_item);
static gboolean dissect_wccp2_router_query_info(tvbuff_t *tvb, int offset,
    int length, packet_info *pinfo, proto_tree *info_tree, proto_item *info_item);
static gboolean dissect_wccp2_capability_info(tvbuff_t *tvb, int offset,
    int length, packet_info *pinfo, proto_tree *info_tree, proto_item *info_item);
static void dissect_32_bit_capability_flags(tvbuff_t *tvb, int curr_offset,
    guint16 capability_val_len, packet_info *pinfo, gint ett, const capability_flag *flags,
    proto_tree *element_tree, proto_item *element_item);
static gboolean dissect_wccp2_alt_assignment_info(tvbuff_t *tvb, int offset,
    int length, packet_info *pinfo, proto_tree *info_tree, proto_item *info_item);

static int
dissect_wccp(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree, void *data _U_)
{
	int offset = 0;
	proto_tree *wccp_tree = NULL;
	proto_item *wccp_tree_item;
	guint32 wccp_message_type;
	guint16 length;
	guint32 cache_count;
	guint32 ipaddr;
	guint i;

	wccp_message_type = tvb_get_ntohl(tvb, offset);

	/* Check if this is really a WCCP message */
	if (try_val_to_str(wccp_message_type, wccp_type_vals) == NULL)
		return 0;

	col_set_str(pinfo->cinfo, COL_PROTOCOL, "WCCP");
	col_clear(pinfo->cinfo, COL_INFO);


	if(check_col(pinfo->cinfo, COL_INFO)) {
		col_add_str(pinfo->cinfo, COL_INFO, val_to_str(wccp_message_type,
		    wccp_type_vals, "Unknown WCCP message (%u)"));
	}

	if(tree != NULL) {
		wccp_tree_item = proto_tree_add_item(tree, proto_wccp, tvb, offset,
		    -1, ENC_NA);
		wccp_tree = proto_item_add_subtree(wccp_tree_item, ett_wccp);

		proto_tree_add_uint(wccp_tree, hf_wccp_message_type, tvb, offset,
		    4, wccp_message_type);
		offset += 4;

		switch (wccp_message_type) {

		case WCCP_HERE_I_AM:
			proto_tree_add_item(wccp_tree, hf_wccp_version, tvb, offset, 4, ENC_BIG_ENDIAN);
			offset += 4;
			dissect_hash_data(tvb, offset, wccp_tree);
			offset += HASH_INFO_SIZE;
			proto_tree_add_item(wccp_tree, hf_recvd_id, tvb, offset,
			    4, ENC_BIG_ENDIAN);
			/*offset += 4;*/
			break;

		case WCCP_I_SEE_YOU:
			proto_tree_add_item(wccp_tree, hf_wccp_version, tvb, offset, 4, ENC_BIG_ENDIAN);
			offset += 4;
			proto_tree_add_item(wccp_tree, hf_change_num, tvb, offset, 4, ENC_BIG_ENDIAN);
			offset += 4;
			proto_tree_add_item(wccp_tree, hf_recvd_id, tvb, offset, 4, ENC_BIG_ENDIAN);
			offset += 4;
			cache_count = tvb_get_ntohl(tvb, offset);
			proto_tree_add_uint(wccp_tree, hf_num_web_caches, tvb, offset, 4, cache_count);
			offset += 4;
			for (i = 0; i < cache_count; i++) {
				dissect_web_cache_list_entry(tvb, offset, i, wccp_tree);
				offset += 4 + HASH_INFO_SIZE;
			}
			break;

		case WCCP_ASSIGN_BUCKET:
			/*
			 * This hasn't been tested, since I don't have any
			 * traces with this in it.
			 *
			 * The V1 spec claims that this does, indeed,
			 * have a Received ID field after the type,
			 * rather than a Version field.
			 */
			proto_tree_add_item(wccp_tree, hf_recvd_id, tvb, offset, 4, ENC_BIG_ENDIAN);
			offset += 4;
			cache_count = tvb_get_ntohl(tvb, offset);
			proto_tree_add_uint(wccp_tree, hf_num_web_caches, tvb, offset, 4, cache_count);
			offset += 4;
			for (i = 0; i < cache_count; i++) {
				ipaddr = tvb_get_ipv4(tvb, offset);
				proto_tree_add_ipv4_format(wccp_tree,
				    hf_cache_ip, tvb, offset, 4,
				    ipaddr,
				    "Web Cache %d IP Address: %s", i,
				    ip_to_str((guint8 *)&ipaddr));
				offset += 4;
			}
			for (i = 0; i < 256; i += 4) {
				proto_tree_add_text(wccp_tree, tvb, offset, 4,
				    "Buckets %d - %d: %10s %10s %10s %10s",
				    i, i + 3,
				    bucket_name(tvb_get_guint8(tvb, offset)),
				    bucket_name(tvb_get_guint8(tvb, offset+1)),
				    bucket_name(tvb_get_guint8(tvb, offset+2)),
				    bucket_name(tvb_get_guint8(tvb, offset+3)));
				offset += 4;
			}
			break;

		case WCCP2_HERE_I_AM:
		case WCCP2_I_SEE_YOU:
		case WCCP2_REMOVAL_QUERY:
		case WCCP2_REDIRECT_ASSIGN:
		default:	/* assume unknown packets are v2 */
			length = dissect_wccp2_header(tvb, offset, wccp_tree);
			offset += 4;
			dissect_wccp2_info(tvb, offset, length, pinfo, wccp_tree);
			break;
		}
	}

	return(tvb_length(tvb));
}

static void
dissect_hash_data(tvbuff_t *tvb, int offset, proto_tree *wccp_tree)
{
	proto_item *bucket_item;
	proto_tree *bucket_tree;
	proto_item *tf;
	proto_tree *field_tree;
	int i, n;
	guint8 bucket_info;

	proto_tree_add_item(wccp_tree, hf_hash_revision, tvb, offset, 4,
	    ENC_BIG_ENDIAN);
	offset += 4;

	bucket_item = proto_tree_add_text(wccp_tree, tvb, offset, 32,
	    "Hash information");
	bucket_tree = proto_item_add_subtree(bucket_item, ett_buckets);

	for (i = 0, n = 0; i < 32; i++) {
		bucket_info = tvb_get_guint8(tvb, offset);
		n = wccp_bucket_info(bucket_info, bucket_tree, n, tvb, offset);
		offset += 1;
	}

	tf = proto_tree_add_item(wccp_tree, hf_hash_flag, tvb, offset, 4, ENC_BIG_ENDIAN);
	field_tree = proto_item_add_subtree(tf, ett_flags);
	proto_tree_add_item(field_tree, hf_hash_flag_u, tvb, offset, 4, ENC_BIG_ENDIAN);
}

static void
dissect_web_cache_list_entry(tvbuff_t *tvb, int offset, int idx,
    proto_tree *wccp_tree)
{
	proto_item *tl;
	proto_tree *list_entry_tree;

	tl = proto_tree_add_text(wccp_tree, tvb, offset, 4 + HASH_INFO_SIZE,
	    "Web-Cache List Entry(%d)", idx);
	list_entry_tree = proto_item_add_subtree(tl, ett_cache_info);
	proto_tree_add_item(list_entry_tree, hf_cache_ip, tvb, offset, 4, ENC_BIG_ENDIAN);
	dissect_hash_data(tvb, offset + 4, list_entry_tree);
}

/*
 * wccp_bucket_info()
 * takes an integer representing a "Hash Information" bitmap, and spits out
 * the corresponding proto_tree entries, returning the next bucket number.
 */
static guint32
wccp_bucket_info(guint8 bucket_info, proto_tree *bucket_tree, guint32 start,
    tvbuff_t *tvb, int offset)
{
	guint32 i;

	for(i = 0; i < 8; i++) {
		proto_tree_add_text(bucket_tree, tvb, offset, sizeof(bucket_info), "Bucket %3d: %s", start, (bucket_info & 1<<i ? "Assigned" : "Not Assigned") );
		start++;
	}
	return(start);
}

static const gchar *
bucket_name(guint8 bucket)
{
	const gchar *cur;

	if (bucket == 0xff) {
		cur="Unassigned";
	} else {
		cur=ep_strdup_printf("%u", bucket);
	}
	return cur;
}

static guint16
dissect_wccp2_header(tvbuff_t *tvb, int offset, proto_tree *wccp_tree)
{
	guint16 length;

	proto_tree_add_item(wccp_tree, hf_wccp2_version, tvb, offset, 2, ENC_BIG_ENDIAN);
	offset += 2;
	length = tvb_get_ntohs(tvb, offset);
	proto_tree_add_uint(wccp_tree, hf_item_header_length, tvb, offset, 2, length);
	return length;
}

static void
dissect_wccp2_info(tvbuff_t *tvb, int offset, guint16 length,
    packet_info *pinfo, proto_tree *wccp_tree)
{
	guint16 type;
	guint16 item_length;
	proto_item *ti;
	proto_tree *info_tree;
	gint ett;
	gboolean (*dissector)(tvbuff_t *, int, int, packet_info *, proto_tree *, proto_item *);

	while (length != 0) {
		type = tvb_get_ntohs(tvb, offset);
		item_length = tvb_get_ntohs(tvb, offset+2);

		switch (type) {

		case WCCP2_SECURITY_INFO:
			ett = ett_security_info;
			dissector = dissect_wccp2_security_info;
			break;

		case WCCP2_SERVICE_INFO:
			ett = ett_service_info;
			dissector = dissect_wccp2_service_info;
			break;

		case WCCP2_ROUTER_ID_INFO:
			ett = ett_router_identity_info;
			dissector = dissect_wccp2_router_identity_info;
			break;

		case WCCP2_WC_ID_INFO:
			ett = ett_wc_identity_info;
			dissector = dissect_wccp2_wc_identity_info;
			break;

		case WCCP2_RTR_VIEW_INFO:
			ett = ett_router_view_info;
			dissector = dissect_wccp2_router_view_info;
			break;

		case WCCP2_WC_VIEW_INFO:
			ett = ett_wc_view_info;
			dissector = dissect_wccp2_wc_view_info;
			break;

		case WCCP2_REDIRECT_ASSIGNMENT:
			ett = ett_router_assignment_info;
			dissector = dissect_wccp2_assignment_info;
			break;

		case WCCP2_QUERY_INFO:
			ett = ett_query_info;
			dissector = dissect_wccp2_router_query_info;
			break;

		case WCCP2_CAPABILITIES_INFO:
			ett = ett_capabilities_info;
			dissector = dissect_wccp2_capability_info;
			break;

		case WCCP2_ALT_ASSIGNMENT:
			ett = ett_alt_assignment_info;
			dissector = dissect_wccp2_alt_assignment_info;
			break;

		case WCCP2_ASSIGN_MAP:
		case WCCP2_COMMAND_EXTENSION:
		default:
			ett = ett_unknown_info;
			dissector = NULL;
			break;
		}

		ti = proto_tree_add_text(wccp_tree, tvb, offset, item_length + 4, "Message Component (%s)",
		    val_to_str(type, info_type_vals, "Unknown info type (%u)"));
		info_tree = proto_item_add_subtree(ti, ett);
		proto_tree_add_item(info_tree, hf_item_type, tvb, offset, 2, ENC_BIG_ENDIAN);
		proto_tree_add_item(info_tree, hf_item_length, tvb, offset+2, 2, ENC_BIG_ENDIAN);

		offset += 4;
		length -= 4;

		/*
		 * XXX - pass in "length" and check for that as well.
		 */
		if (dissector != NULL) {
			if (!(*dissector)(tvb, offset, item_length, pinfo, info_tree, ti))
				return;	/* ran out of data */
		} else {
			proto_tree_add_item(info_tree, hf_item_data, tvb, offset, item_length, ENC_NA);
		}
		offset += item_length;
		length -= item_length;
	}
}

#define SECURITY_INFO_LEN		4

#define WCCP2_NO_SECURITY		0
#define WCCP2_MD5_SECURITY		1

const value_string security_option_vals[] = {
	{ WCCP2_NO_SECURITY, "None" },
	{ WCCP2_MD5_SECURITY, "MD5" },
	{ 0,    NULL }
};

static gboolean
dissect_wccp2_security_info(tvbuff_t *tvb, int offset, int length,
    packet_info *pinfo, proto_tree *info_tree, proto_item *info_item)
{
	guint32 security_option;

	if (length < SECURITY_INFO_LEN) {
		expert_add_info_format(pinfo, info_item, PI_PROTOCOL, PI_WARN,
			    "Item length is %u, should be >= %u", length, SECURITY_INFO_LEN);
		return TRUE;
	}

	security_option = tvb_get_ntohl(tvb, offset);
	proto_tree_add_item(info_tree, hf_security_option, tvb, offset, 4, ENC_BIG_ENDIAN);
	if (security_option == WCCP2_MD5_SECURITY) {
		proto_tree_add_item(info_tree, hf_security_md5_checksum, tvb, offset+4, length-4, ENC_NA);
	}

	return TRUE;
}

#define SERVICE_INFO_LEN		(4+4+8*2)

#define	WCCP2_SERVICE_STANDARD		0
#define	WCCP2_SERVICE_DYNAMIC		1

const value_string service_type_vals[] = {
	{ WCCP2_SERVICE_STANDARD, "Well-known service" },
	{ WCCP2_SERVICE_DYNAMIC, "Dynamic service" },
	{ 0,    NULL }
};

/*
 * Service flags.
 */
#define	WCCP2_SI_SRC_IP_HASH		0x0001
#define	WCCP2_SI_DST_IP_HASH		0x0002
#define	WCCP2_SI_SRC_PORT_HASH		0x0004
#define	WCCP2_SI_DST_PORT_HASH		0x0008
#define	WCCP2_SI_PORTS_DEFINED		0x0010
#define	WCCP2_SI_PORTS_SOURCE		0x0020
#define	WCCP2_SI_SRC_IP_ALT_HASH	0x0100
#define	WCCP2_SI_DST_IP_ALT_HASH	0x0200
#define	WCCP2_SI_SRC_PORT_ALT_HASH	0x0400
#define	WCCP2_SI_DST_PORT_ALT_HASH	0x0800

static gboolean
dissect_wccp2_service_info(tvbuff_t *tvb, int offset, int length,
    packet_info *pinfo, proto_tree *info_tree, proto_item *info_item)
{
	guint8 service_type;
	guint8 priority;
	guint8 protocol;
	guint32 flags;
	proto_item *tf;
	proto_tree *field_tree;
	int i;

	if (length != SERVICE_INFO_LEN) {
		expert_add_info_format(pinfo, info_item, PI_PROTOCOL, PI_WARN,
				"Item length is %u, should be %u", length, SERVICE_INFO_LEN);
		return TRUE;
	}

	service_type = tvb_get_guint8(tvb, offset);
    proto_tree_add_item(info_tree, hf_service_type, tvb, offset, 1, ENC_BIG_ENDIAN);
	switch (service_type) {

	case WCCP2_SERVICE_STANDARD:
		proto_tree_add_item(info_tree, hf_service_id, tvb, offset+1, 1, ENC_BIG_ENDIAN);
		tf = proto_tree_add_item(info_tree, hf_service_priority, tvb, offset+2, 1, ENC_BIG_ENDIAN);
		priority = tvb_get_guint8(tvb, offset+2);
		if (priority != 0)
			expert_add_info_format(pinfo, tf, PI_PROTOCOL, PI_WARN,
				"The priority must be zero for well-known services.");
		tf = proto_tree_add_item(info_tree, hf_service_protocol, tvb, offset+3, 1, ENC_BIG_ENDIAN);
		protocol = tvb_get_guint8(tvb, offset+3);
		if (protocol != 0)
			expert_add_info_format(pinfo, tf, PI_PROTOCOL, PI_WARN,
				"The protocol must be zero for well-known services.");
		break;

	case WCCP2_SERVICE_DYNAMIC:
		proto_tree_add_item(info_tree, hf_service_id, tvb, offset+1, 1, ENC_BIG_ENDIAN);
		proto_tree_add_item(info_tree, hf_service_priority, tvb, offset+2, 1, ENC_BIG_ENDIAN);
		/*
		 * XXX - does "IP protocol identifier" mean this is a
		 * protocol type of the sort you get in IP headers?
		 * If so, we should get a table of those from the
		 * IP dissector, and use that.
		 */
		proto_tree_add_item(info_tree, hf_service_protocol, tvb, offset+3, 1, ENC_BIG_ENDIAN);
		break;
	}
	offset += 4;

	flags = tvb_get_ntohl(tvb, offset);
	tf = proto_tree_add_item(info_tree, hf_service_flags, tvb, offset, 4, ENC_BIG_ENDIAN);
	field_tree = proto_item_add_subtree(tf, ett_service_flags);

	proto_tree_add_item(field_tree, hf_service_flags_src_ip_hash, tvb, offset, 4, ENC_BIG_ENDIAN);
	proto_tree_add_item(field_tree, hf_service_flags_dest_ip_hash, tvb, offset, 4, ENC_BIG_ENDIAN);
	proto_tree_add_item(field_tree, hf_service_flags_src_port_hash, tvb, offset, 4, ENC_BIG_ENDIAN);
	proto_tree_add_item(field_tree, hf_service_flags_dest_port_hash, tvb, offset, 4, ENC_BIG_ENDIAN);
	proto_tree_add_item(field_tree, hf_service_flags_ports_defined, tvb, offset, 4, ENC_BIG_ENDIAN);
	if (flags & WCCP2_SI_PORTS_DEFINED)
		proto_tree_add_item(field_tree, hf_service_flags_ports_source, tvb, offset, 4, ENC_BIG_ENDIAN);
	proto_tree_add_item(field_tree, hf_service_flags_src_ip_alt_hash, tvb, offset, 4, ENC_BIG_ENDIAN);
	proto_tree_add_item(field_tree, hf_service_flags_dest_ip_alt_hash, tvb, offset, 4, ENC_BIG_ENDIAN);
	proto_tree_add_item(field_tree, hf_service_flags_src_port_alt_hash, tvb, offset, 4, ENC_BIG_ENDIAN);
	proto_tree_add_item(field_tree, hf_service_flags_dest_port_alt_hash, tvb, offset, 4, ENC_BIG_ENDIAN);
	offset += 4;

	if (flags & WCCP2_SI_PORTS_DEFINED) {
		for (i = 0; i < 8; i++) {
			proto_tree_add_item(info_tree, hf_service_port, tvb, offset, 2, ENC_BIG_ENDIAN);
			offset += 2;
		}
	}

	return TRUE;
}

#define	ROUTER_ID_INFO_MIN_LEN		(8+4+4)

static void
dissect_wccp2_router_identity_element(tvbuff_t *tvb, int offset,
    proto_tree *tree)
{
	proto_tree_add_item(tree, hf_router_identity_ip, tvb, offset, 4, ENC_BIG_ENDIAN);
	proto_tree_add_item(tree, hf_router_identity_receive_id, tvb, offset+4, 4, ENC_BIG_ENDIAN);
}

static gboolean
dissect_wccp2_router_identity_info(tvbuff_t *tvb, int offset, int length,
    packet_info *pinfo, proto_tree *info_tree, proto_item *info_item)
{
	guint32 n_received_from;
	guint i;
	proto_item *te;
	proto_tree *element_tree;

	if (length < ROUTER_ID_INFO_MIN_LEN) {
		expert_add_info_format(pinfo, info_item, PI_PROTOCOL, PI_WARN,
				"Item length is %u, should be >= %u", length, ROUTER_ID_INFO_MIN_LEN);
		return TRUE;
	}

	te = proto_tree_add_text(info_tree, tvb, offset, 8,
	    "Router Identity Element: IP address %s",
	    tvb_ip_to_str(tvb, offset));
	element_tree = proto_item_add_subtree(te, ett_router_identity_element);
	dissect_wccp2_router_identity_element(tvb, offset, element_tree);
	offset += 8;

	proto_tree_add_item(info_tree, hf_router_send_to_ip, tvb, offset, 4, ENC_BIG_ENDIAN);
	offset += 4;

	n_received_from = tvb_get_ntohl(tvb, offset);
	proto_tree_add_item(info_tree, hf_router_num_recv_ip, tvb, offset, 4, ENC_BIG_ENDIAN);
	offset += 4;
	for (i = 0; i < n_received_from; i++) {
		proto_tree_add_ipv4_format_value(info_tree, hf_router_recv_ip, tvb, offset, 4,
			tvb_get_ipv4(tvb, offset), "Received From IP Address %d: %s", i,
			tvb_ip_to_str(tvb, offset));
		offset += 4;
	}

	return TRUE;
}

#define	WC_ID_INFO_LEN			(4+4+8*4+4)

static gboolean
dissect_wccp2_web_cache_identity_element(tvbuff_t *tvb, int offset, packet_info *pinfo, proto_tree *tree, int length)
{
	proto_item *bucket_item;
	proto_tree *bucket_tree;
	proto_item *tf;
	proto_tree *field_tree;
    guint16 flags;
	guint32 i, n, num_masks;
	guint8 bucket_info;

	proto_tree_add_item(tree, hf_web_cache_identity_ip, tvb, offset, 4, ENC_BIG_ENDIAN);
	offset += 4;
	proto_tree_add_item(tree, hf_web_cache_identity_hash_rev, tvb, offset, 2, ENC_BIG_ENDIAN);
	offset += 2;

    flags = tvb_get_ntohs(tvb, offset);
	tf = proto_tree_add_uint(tree, hf_web_cache_identity_flags, tvb, offset, 2, flags);
	field_tree = proto_item_add_subtree(tf, ett_flags);
	proto_tree_add_item(field_tree, hf_web_cache_identity_flag_hash_info, tvb, offset, 2, ENC_BIG_ENDIAN);
	proto_tree_add_item(field_tree, hf_web_cache_identity_flag_assign_type, tvb, offset, 2, ENC_BIG_ENDIAN);
	offset += 2;

    if (flags & WCCP_T_FLAG) {
	    bucket_item = proto_tree_add_text(tree, tvb, offset, 8*4, "Mask Assignment information");
	    bucket_tree = proto_item_add_subtree(bucket_item, ett_buckets);

        num_masks = tvb_get_ntohl(tvb, offset);
	    proto_tree_add_uint(bucket_tree, hf_web_cache_identity_num_mask, tvb, offset, 4, num_masks);
        offset += 4;
	    for (i = 0; i < num_masks; i++) {
        	proto_tree_add_item(bucket_tree, hf_web_cache_identity_assign_mask, tvb, offset, 4, ENC_BIG_ENDIAN);
		    offset += 4;
        }
    }
    else {
	    if (length != WC_ID_INFO_LEN) {
		    expert_add_info_format(pinfo, tf, PI_PROTOCOL, PI_WARN,
				    "Item length is %u, should be %u", length, WC_ID_INFO_LEN);
		    return TRUE;
	    }

	    bucket_item = proto_tree_add_text(tree, tvb, offset, 8*4, "Hash information");
	    bucket_tree = proto_item_add_subtree(bucket_item, ett_buckets);
	    for (i = 0, n = 0; i < 32; i++) {
		    bucket_info = tvb_get_guint8(tvb, offset);
		    n = wccp_bucket_info(bucket_info, bucket_tree, n, tvb, offset);
		    offset += 1;
        }
    }

	proto_tree_add_item(tree, hf_web_cache_identity_assign_weight, tvb, offset, 2, ENC_BIG_ENDIAN);
	offset += 2;
	proto_tree_add_item(tree, hf_web_cache_identity_status, tvb, offset, 2, ENC_BIG_ENDIAN);
	return TRUE;
}

static gboolean
dissect_wccp2_wc_identity_info(tvbuff_t *tvb, int offset, int length,
    packet_info *pinfo, proto_tree *info_tree, proto_item *info_item _U_)
{
	proto_item *te;
	proto_tree *element_tree;

	te = proto_tree_add_text(info_tree, tvb, offset, length,
	    "Web-Cache Identity Element: IP address %s",
	    tvb_ip_to_str(tvb, offset));
	element_tree = proto_item_add_subtree(te, ett_wc_identity_element);
	if (!dissect_wccp2_web_cache_identity_element(tvb, offset, pinfo, element_tree, length))
		return FALSE;	/* ran out of data */

	return TRUE;
}

#define	ROUTER_VIEW_INFO_MIN_LEN	(4+8+4)

static void
dissect_wccp2_assignment_key(tvbuff_t *tvb, int offset,
    proto_tree *info_tree)
{
	proto_tree_add_item(info_tree, hf_assignment_key_ip, tvb, offset, 4, ENC_BIG_ENDIAN);
	proto_tree_add_item(info_tree, hf_assignment_key_change_num, tvb, offset+4, 4, ENC_BIG_ENDIAN);
}

static gboolean
dissect_wccp2_router_view_info(tvbuff_t *tvb, int offset, int length,
    packet_info *pinfo, proto_tree *info_tree, proto_item *info_item)
{
	guint32 n_routers;
	guint32 n_web_caches;
	guint i;
	proto_item *te;
	proto_tree *element_tree;

	if (length < ROUTER_VIEW_INFO_MIN_LEN) {
		expert_add_info_format(pinfo, info_item, PI_PROTOCOL, PI_WARN,
				"Item length is %u, should be >= %u", length, ROUTER_VIEW_INFO_MIN_LEN);
		return TRUE;
	}

	proto_tree_add_item(info_tree, hf_router_view_member_change_num, tvb, offset, 4, ENC_BIG_ENDIAN);
	offset += 4;

	dissect_wccp2_assignment_key(tvb, offset, info_tree);
	offset += 8;

	n_routers = tvb_get_ntohl(tvb, offset);
	proto_tree_add_uint(info_tree, hf_router_router_num, tvb, offset, 4, n_routers);
	offset += 4;

	for (i = 0; i < n_routers; i++) {
		proto_tree_add_ipv4_format_value(info_tree, hf_router_router_ip, tvb, offset, 4,
			tvb_get_ipv4(tvb, offset), "Router %d IP Address: %s", i,
			tvb_ip_to_str(tvb, offset));
		offset += 4;
	}

	n_web_caches = tvb_get_ntohl(tvb, offset);
	proto_tree_add_uint(info_tree, hf_num_web_caches, tvb, offset, 4, n_web_caches);
	offset += 4;

	for (i = 0; i < n_web_caches; i++) {
		te = proto_tree_add_text(info_tree, tvb, offset, WC_ID_INFO_LEN,
		    "Web-Cache Identity Element %d: IP address %s", i,
		    tvb_ip_to_str(tvb, offset));
		element_tree = proto_item_add_subtree(te, ett_wc_identity_element);
		if (!dissect_wccp2_web_cache_identity_element(tvb, offset, pinfo, element_tree, WC_ID_INFO_LEN))
			return FALSE;	/* ran out of data */
		offset += WC_ID_INFO_LEN;
	}

	return TRUE;
}

#define	WC_VIEW_INFO_MIN_LEN		(4+4)

static gboolean
dissect_wccp2_wc_view_info(tvbuff_t *tvb, int offset, int length,
    packet_info *pinfo, proto_tree *info_tree, proto_item *info_item)
{
	guint32 n_routers;
	guint32 n_web_caches;
	guint i;
	proto_item *te;
	proto_tree *element_tree;

	if (length < WC_VIEW_INFO_MIN_LEN) {
		expert_add_info_format(pinfo, info_item, PI_PROTOCOL, PI_WARN,
			    "Item length is %u, should be >= %u", length, WC_VIEW_INFO_MIN_LEN);
		return TRUE;
	}

	proto_tree_add_item(info_tree, hf_wc_view_info_change_num, tvb, offset, 4, ENC_BIG_ENDIAN);
	offset += 4;

	n_routers = tvb_get_ntohl(tvb, offset);
	proto_tree_add_uint(info_tree, hf_wc_view_router_num, tvb, offset, 4, n_routers);
	offset += 4;

	for (i = 0; i < n_routers; i++) {
		te = proto_tree_add_text(info_tree, tvb, offset, 8,
		    "Router %d Identity Element: IP address %s", i,
		    tvb_ip_to_str(tvb, offset));
		element_tree = proto_item_add_subtree(te, ett_router_identity_element);
		dissect_wccp2_router_identity_element(tvb, offset, element_tree);
		offset += 8;
	}

	n_web_caches = tvb_get_ntohl(tvb, offset);
	proto_tree_add_uint(info_tree, hf_num_web_caches, tvb, offset, 4, n_web_caches);
	offset += 4;

	for (i = 0; i < n_web_caches; i++) {
		proto_tree_add_ipv4_format_value(info_tree, hf_wc_view_ip, tvb, offset, 4,
			tvb_get_ipv4(tvb, offset), "Web-Cache %d IP Address: %s", i,
			tvb_ip_to_str(tvb, offset));
		offset += 4;
	}

	return TRUE;
}

#define	ASSIGNMENT_INFO_MIN_LEN		(8+4)

static void
dissect_wccp2_router_assignment_element(tvbuff_t *tvb, int offset,
    proto_tree *tree)
{
	proto_tree_add_item(tree, hf_router_assignment_element_ip, tvb, offset, 4, ENC_BIG_ENDIAN);
	proto_tree_add_item(tree, hf_router_assignment_element_receive_id, tvb, offset+4, 4, ENC_BIG_ENDIAN);
	proto_tree_add_item(tree, hf_router_assignment_element_change_num, tvb, offset+8, 4, ENC_BIG_ENDIAN);
}

static const gchar *
assignment_bucket_name(guint8 bucket)
{
	const gchar *cur;

	if (bucket == 0xff) {
		cur="Unassigned";
	} else {
		cur=ep_strdup_printf("%u%s", bucket & 0x7F,
		    (bucket & 0x80) ? " (Alt)" : "");
	}
	return cur;
}

static gboolean
dissect_wccp2_assignment_info(tvbuff_t *tvb, int offset, int length,
    packet_info *pinfo, proto_tree *info_tree, proto_item *info_item)
{
	guint32 n_routers;
	guint32 n_web_caches;
	guint i;
	proto_item *te;
	proto_tree *element_tree;

	if (length < ASSIGNMENT_INFO_MIN_LEN) {
		expert_add_info_format(pinfo, info_item, PI_PROTOCOL, PI_WARN,
			    "Item length is %u, should be >= %u", length, ASSIGNMENT_INFO_MIN_LEN);
		return TRUE;
	}

	dissect_wccp2_assignment_key(tvb, offset, info_tree);
	offset += 8;

	n_routers = tvb_get_ntohl(tvb, offset);
	proto_tree_add_uint(info_tree, hf_wccp2_assignment_info_router_num, tvb, offset, 4, n_routers);
	offset += 4;

	for (i = 0; i < n_routers; i++) {
		te = proto_tree_add_text(info_tree, tvb, offset, 4,
		    "Router %d Assignment Element: IP address %s", i,
		    tvb_ip_to_str(tvb, offset));
		element_tree = proto_item_add_subtree(te, ett_router_assignment_element);
		dissect_wccp2_router_assignment_element(tvb, offset, element_tree);
		offset += 12;
	}

	n_web_caches = tvb_get_ntohl(tvb, offset);
	proto_tree_add_uint(info_tree, hf_num_web_caches, tvb, offset, 4, n_web_caches);
	offset += 4;

	for (i = 0; i < n_web_caches; i++) {
		proto_tree_add_ipv4_format_value(info_tree, hf_wccp2_assignment_info_ip, tvb, offset, 4,
			tvb_get_ipv4(tvb, offset), "Web-Cache %d IP Address: %s", i,
		    tvb_ip_to_str(tvb, offset));
		offset += 4;
	}

	for (i = 0; i < 256; i += 4) {
		proto_tree_add_text(info_tree, tvb, offset, 4,
		    "Buckets %3d - %3d: %10s %10s %10s %10s",
		    i, i + 3,
		    assignment_bucket_name(tvb_get_guint8(tvb, offset)),
		    assignment_bucket_name(tvb_get_guint8(tvb, offset+1)),
		    assignment_bucket_name(tvb_get_guint8(tvb, offset+2)),
		    assignment_bucket_name(tvb_get_guint8(tvb, offset+3)));
		offset += 4;
	}

	return TRUE;
}

#define	QUERY_INFO_LEN			(4+4+4+4)

static gboolean
dissect_wccp2_router_query_info(tvbuff_t *tvb, int offset, int length,
    packet_info *pinfo, proto_tree *info_tree, proto_item *info_item)
{
	if (length != QUERY_INFO_LEN) {
		expert_add_info_format(pinfo, info_item, PI_PROTOCOL, PI_WARN,
				"Item length is %u, should be %u", length, QUERY_INFO_LEN);
		return TRUE;
	}

	proto_tree_add_item(info_tree, hf_router_query_info_ip, tvb, offset, 4, ENC_BIG_ENDIAN);
	proto_tree_add_item(info_tree, hf_router_query_info_receive_id, tvb, offset+4, 4, ENC_BIG_ENDIAN);
	proto_tree_add_item(info_tree, hf_router_query_info_send_to_ip, tvb, offset+8, 4, ENC_BIG_ENDIAN);
	proto_tree_add_item(info_tree, hf_router_query_info_target_ip, tvb, offset+12, 4, ENC_BIG_ENDIAN);

	return TRUE;
}

#define WCCP2_FORWARDING_METHOD         0x01
#define WCCP2_ASSIGNMENT_METHOD         0x02
#define WCCP2_PACKET_RETURN_METHOD      0x03

static const value_string capability_type_vals[] = {
	{ WCCP2_FORWARDING_METHOD,    "Forwarding Method" },
	{ WCCP2_ASSIGNMENT_METHOD,    "Assignment Method" },
	{ WCCP2_PACKET_RETURN_METHOD, "Return Method" },
	{ 0,                          NULL }
};

#define WCCP2_FORWARDING_METHOD_GRE	0x00000001
#define WCCP2_FORWARDING_METHOD_L2	0x00000002

static const capability_flag forwarding_method_flags[] = {
	{ WCCP2_FORWARDING_METHOD_GRE, "IP-GRE", &hf_capability_forwarding_method_flag_gre },
	{ WCCP2_FORWARDING_METHOD_L2, "L2", &hf_capability_forwarding_method_flag_l2 },
	{ 0, NULL, NULL }
};

#define WCCP2_ASSIGNMENT_METHOD_HASH    0x00000001
#define WCCP2_ASSIGNMENT_METHOD_MASK    0x00000002

static const capability_flag assignment_method_flags[] = {
	{ WCCP2_ASSIGNMENT_METHOD_HASH, "Hash", &hf_capability_assignment_method_flag_hash },
	{ WCCP2_ASSIGNMENT_METHOD_MASK, "Mask", &hf_capability_assignment_method_flag_mask },
	{ 0,                            NULL,   NULL }
};

#define WCCP2_PACKET_RETURN_METHOD_GRE  0x00000001
#define WCCP2_PACKET_RETURN_METHOD_L2   0x00000002

static const capability_flag packet_return_method_flags[] = {
	{ WCCP2_PACKET_RETURN_METHOD_GRE, "IP-GRE", &hf_capability_return_method_flag_gre },
	{ WCCP2_PACKET_RETURN_METHOD_L2, "L2", &hf_capability_return_method_flag_l2 },
	{ 0, NULL, NULL }
};

static gboolean
dissect_wccp2_capability_info(tvbuff_t *tvb, int offset, int length,
    packet_info *pinfo, proto_tree *info_tree, proto_item *info_item _U_)
{
	guint16 capability_type;
	guint16 capability_val_len;
	int curr_offset;
	proto_item *te;
	proto_tree *element_tree;

	for (curr_offset = offset; curr_offset < (length + offset);
		curr_offset += (capability_val_len + 4)) {
		capability_type = tvb_get_ntohs(tvb, curr_offset);
		capability_val_len = tvb_get_ntohs(tvb, curr_offset + 2);

		te = proto_tree_add_item(info_tree, hf_capability_info_type, tvb, curr_offset, 2, ENC_BIG_ENDIAN);
		proto_item_set_len(te, capability_val_len + 4);
		element_tree = proto_item_add_subtree(te, ett_capability_element);
		if (capability_val_len < 4) {
			expert_add_info_format(pinfo, te, PI_PROTOCOL, PI_WARN,
					"Value Length: %u (illegal, must be >= 4)", capability_val_len);
			break;
		}

		proto_tree_add_uint(element_tree, hf_capability_info_length, tvb, curr_offset+2, 2, capability_val_len);
		switch (capability_type) {

		case WCCP2_FORWARDING_METHOD:
			dissect_32_bit_capability_flags(tvb, curr_offset,
			    capability_val_len, pinfo,
			    ett_capability_forwarding_method,
			    forwarding_method_flags, element_tree, te);
			break;

		case WCCP2_ASSIGNMENT_METHOD:
			dissect_32_bit_capability_flags(tvb, curr_offset,
			    capability_val_len, pinfo,
			    ett_capability_assignment_method,
			    assignment_method_flags, element_tree, te);
			break;

		case WCCP2_PACKET_RETURN_METHOD:
			dissect_32_bit_capability_flags(tvb, curr_offset,
			    capability_val_len, pinfo,
			    ett_capability_return_method,
			    packet_return_method_flags, element_tree, te);
			break;

		default:
			proto_tree_add_item(element_tree, hf_capability_info_bytes, tvb, curr_offset+4, capability_val_len, ENC_NA);
			break;
		}

	}
	return TRUE;
}

static void
dissect_32_bit_capability_flags(tvbuff_t *tvb, int curr_offset,
    guint16 capability_val_len, packet_info *pinfo, gint ett, const capability_flag *flags,
    proto_tree *element_tree, proto_item *element_item)
{
	guint32 capability_val;
	proto_item *tm;
	proto_tree *method_tree;
	int i;
	gboolean first = TRUE;

	if (capability_val_len != 4) {
		expert_add_info_format(pinfo, element_item, PI_PROTOCOL, PI_WARN,
				"Value Length: %u (illegal, must be >= 4)", capability_val_len);
		return;
	}

	capability_val = tvb_get_ntohl(tvb, curr_offset + 4);
	tm = proto_tree_add_uint(element_tree, hf_capability_info_value, tvb, curr_offset + 4, 4, capability_val);

	for (i = 0; flags[i].short_name != NULL; i++) {
		if (capability_val & flags[i].value) {
			if (first) {
				proto_item_append_text( tm, " (%s", flags[i].short_name);
				first = FALSE;
			} else {
				proto_item_append_text( tm, ", %s", flags[i].short_name);
			}
		}
	}

	if (first == FALSE)
		proto_item_append_text( tm, ")");

	method_tree = proto_item_add_subtree(tm, ett);
	for (i = 0; flags[i].phf != NULL; i++)
		proto_tree_add_item(method_tree, *(flags[i].phf), tvb, curr_offset+4, 4, ENC_BIG_ENDIAN);

}

#define ALT_ASSIGNMENT_INFO_MIN_LEN    (4+4)

#define WCCP2_HASH_ASSIGNMENT_TYPE     0x0000
#define WCCP2_MASK_ASSIGNMENT_TYPE     0x0001

static const value_string assignment_type_vals[] = {
	{ WCCP2_HASH_ASSIGNMENT_TYPE, "Hash" },
	{ WCCP2_MASK_ASSIGNMENT_TYPE, "Mask" },
	{ 0,                          NULL }
};

static void
dissect_wccp2_value_element(tvbuff_t *tvb, int offset, int idx, proto_tree *info_tree)
{
	proto_item *tl;
	proto_tree *element_tree;

	tl = proto_tree_add_text(info_tree, tvb, offset, 16, "Value Element(%u)", idx);
	element_tree = proto_item_add_subtree(tl, ett_value_element);

	proto_tree_add_item(element_tree, hf_wccp2_value_element_src_ip, tvb, offset, 4, ENC_BIG_ENDIAN);
	proto_tree_add_item(element_tree, hf_wccp2_value_element_dest_ip, tvb, offset+4, 4, ENC_BIG_ENDIAN);
	proto_tree_add_item(element_tree, hf_wccp2_value_element_src_port, tvb, offset+8, 2, ENC_BIG_ENDIAN);
	proto_tree_add_item(element_tree, hf_wccp2_value_element_dest_port, tvb, offset+10, 2, ENC_BIG_ENDIAN);
	proto_tree_add_item(element_tree, hf_wccp2_value_element_web_cache_ip, tvb, offset+12, 4, ENC_BIG_ENDIAN);
}

static guint
dissect_wccp2_mask_value_set_element(tvbuff_t *tvb, int offset, int idx, proto_tree *info_tree)
{
	proto_item *tl;
	proto_tree *element_tree;
	guint num_of_val_elements;
	guint i;

	tl = proto_tree_add_text(info_tree, tvb, offset, 0, "Mask/Value Set Element(%d)", idx);
	element_tree = proto_item_add_subtree(tl, ett_mv_set_element);

	proto_tree_add_item(element_tree, hf_wccp2_mask_value_set_element_src_ip, tvb, offset, 4, ENC_BIG_ENDIAN);
	proto_tree_add_item(element_tree, hf_wccp2_mask_value_set_element_dest_ip, tvb, offset+4, 4, ENC_BIG_ENDIAN);
	proto_tree_add_item(element_tree, hf_wccp2_mask_value_set_element_src_port, tvb, offset+8, 2, ENC_BIG_ENDIAN);
	proto_tree_add_item(element_tree, hf_wccp2_mask_value_set_element_dest_port, tvb, offset+10, 2, ENC_BIG_ENDIAN);
	proto_tree_add_item(element_tree, hf_wccp2_mask_value_set_element_num, tvb, offset+12, 4, ENC_BIG_ENDIAN);
	num_of_val_elements = tvb_get_ntohl(tvb, offset+12);
	offset += 16;

	for (i = 0; i < num_of_val_elements; i++)
	{
		dissect_wccp2_value_element(tvb, offset, i, element_tree);
		offset += 16;
	}

	proto_item_set_len(tl, 16+i*16);

	return 16+idx*16;
}

static gboolean
dissect_wccp2_alt_assignment_info(tvbuff_t *tvb, int offset, int length,
    packet_info *pinfo, proto_tree *info_tree, proto_item *info_item)
{
	guint16 assignment_type;
	guint16 assignment_length;

	if (length < ALT_ASSIGNMENT_INFO_MIN_LEN) {
		expert_add_info_format(pinfo, info_item, PI_PROTOCOL, PI_WARN,
				"Item length is %u, should be >= %u", length, ALT_ASSIGNMENT_INFO_MIN_LEN);
		return TRUE;
	}

	assignment_type = tvb_get_ntohs(tvb, offset);
	proto_tree_add_item(info_tree, hf_alt_assignment_info_assignment_type, tvb, offset, 2, ENC_BIG_ENDIAN);
	offset += 2;

	assignment_length = tvb_get_ntohs(tvb, offset);
	proto_tree_add_item(info_tree, hf_alt_assignment_info_assignment_length, tvb, offset, 2, ENC_BIG_ENDIAN);
	offset += 2;

	switch (assignment_type) {
	case WCCP2_HASH_ASSIGNMENT_TYPE:
		dissect_wccp2_assignment_info(tvb, offset, assignment_length, pinfo, info_tree, info_item);
		break;

	case WCCP2_MASK_ASSIGNMENT_TYPE:
	{
		guint num_of_rtr;
		guint num_of_elem;
		guint i;

		dissect_wccp2_assignment_key(tvb, offset, info_tree);
		offset += 8;

		num_of_rtr = tvb_get_ntohl(tvb, offset);
		proto_tree_add_uint(info_tree, hf_alt_assignment_info_num_routers, tvb, offset, 4, num_of_rtr);
		offset += 4;
		for (i = 0; i < num_of_rtr; i++)
		{
			dissect_wccp2_router_assignment_element(tvb, offset, info_tree);
			offset += 12;
		}

		num_of_elem = tvb_get_ntohl(tvb, offset);
		proto_tree_add_uint(info_tree, hf_alt_assignment_info_num_elements, tvb, offset, 4, num_of_elem);
		offset += 4;
		for (i = 0; i < num_of_elem; i++)
		{
			offset += dissect_wccp2_mask_value_set_element(tvb, offset, i, info_tree);
		}
		break;
	}

	default:
		break;
	}

	return TRUE;
}

void
proto_register_wccp(void)
{
	static hf_register_info hf[] = {
		{ &hf_wccp_message_type,
			{ "WCCP Message Type", "wccp.message", FT_UINT32, BASE_DEC, VALS(wccp_type_vals), 0x0,
				"The WCCP message that was sent", HFILL }
		},
		{ &hf_wccp_version,
			{ "WCCP Version", "wccp.version", FT_UINT32, BASE_HEX, VALS(wccp_version_val), 0x0,
				"The WCCP version", HFILL }
		},
		{ &hf_wccp2_version,
			{ "WCCP Version", "wccp.version", FT_UINT16, BASE_HEX, NULL, 0x0,
				"The WCCP version", HFILL }
		},
		{ &hf_hash_revision,
			{ "Hash Revision", "wccp.hash_revision", FT_UINT32, BASE_DEC, 0x0, 0x0,
				"The cache hash revision", HFILL }
		},
		{ &hf_change_num,
			{ "Change Number", "wccp.change_num", FT_UINT32, BASE_DEC, 0x0, 0x0,
				"The Web-Cache list entry change number", HFILL }
		},
		{ &hf_hash_flag,
			{ "Flags", "wccp.hash_flag", FT_UINT32, BASE_HEX, 0x0, 0x0,
				NULL, HFILL }
		},
		{ &hf_hash_flag_u,
			{ "Hash information", "wccp.hash_flag.u", FT_BOOLEAN, 32, TFS(&tfs_historical_current), WCCP_U_FLAG,
				NULL, HFILL }
		},
		{ &hf_recvd_id,
			{ "Received ID", "wccp.recvd_id", FT_UINT32, BASE_DEC, 0x0, 0x0,
				"The number of I_SEE_YOU's that have been sent", HFILL }
		},
		{ &hf_num_web_caches,
			{ "Number of Web Caches", "wccp.num_web_caches", FT_UINT32, BASE_DEC, 0x0, 0x0,
				NULL, HFILL }
		},
		{ &hf_cache_ip,
			{ "Web Cache IP address", "wccp.cache_ip", FT_IPv4, BASE_NONE, NULL, 0x0,
				"The IP address of a Web cache", HFILL }
		},
		{ &hf_item_header_length,
			{ "Length", "wccp.item_header_length", FT_UINT16, BASE_DEC, 0x0, 0x0,
				NULL, HFILL }
		},
		{ &hf_item_length,
			{ "Length", "wccp.item_length", FT_UINT16, BASE_DEC, 0x0, 0x0,
				NULL, HFILL }
		},
		{ &hf_item_type,
			{ "Type", "wccp.item_type", FT_UINT16, BASE_DEC, VALS(info_type_vals), 0x0,
				NULL, HFILL }
		},
		{ &hf_item_data,
			{ "Data", "wccp.item_data", FT_BYTES, BASE_NONE, 0x0, 0x0,
				NULL, HFILL }
		},
		{ &hf_security_option,
			{ "Security Option", "wccp.security_option", FT_UINT16, BASE_DEC, VALS(security_option_vals), 0x0,
				NULL, HFILL }
		},
		{ &hf_security_md5_checksum,
			{ "MD5 checksum", "wccp.security_md5_checksum", FT_BYTES, BASE_NONE, 0x0, 0x0,
				NULL, HFILL }
		},
		{ &hf_service_type,
			{ "Service Type", "wccp.service_type", FT_UINT8, BASE_DEC, VALS(service_type_vals), 0x0,
				NULL, HFILL }
		},
		{ &hf_service_id,
			{ "Service ID", "wccp.service_id", FT_UINT8, BASE_DEC, VALS(service_id_vals), 0x0,
				NULL, HFILL }
		},
		{ &hf_service_priority,
			{ "Priority", "wccp.service_priority", FT_UINT8, BASE_DEC, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_service_protocol,
			{ "Protocol", "wccp.service_protocol", FT_UINT8, BASE_DEC, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_service_flags,
			{ "Flags", "wccp.service_flags", FT_UINT32, BASE_HEX, 0x0, 0x0,
				NULL, HFILL }
		},
		{ &hf_service_flags_src_ip_hash,
			{ "Source IP address in primary hash", "wccp.service_flag.src_ip_hash", FT_BOOLEAN, 32, TFS(&tfs_used_notused), WCCP2_SI_SRC_IP_HASH,
				NULL, HFILL }
		},
		{ &hf_service_flags_dest_ip_hash,
			{ "Destination IP address in primary hash", "wccp.service_flag.dest_ip_hash", FT_BOOLEAN, 32, TFS(&tfs_used_notused), WCCP2_SI_DST_IP_HASH,
				NULL, HFILL }
		},
		{ &hf_service_flags_src_port_hash,
			{ "Source port in primary hash", "wccp.service_flag.src_port_hash", FT_BOOLEAN, 32, TFS(&tfs_used_notused), WCCP2_SI_SRC_PORT_HASH,
				NULL, HFILL }
		},
		{ &hf_service_flags_dest_port_hash,
			{ "Destination port in primary hash", "wccp.service_flag.dest_port_hash", FT_BOOLEAN, 32, TFS(&tfs_used_notused), WCCP2_SI_DST_PORT_HASH,
				NULL, HFILL }
		},
		{ &hf_service_flags_ports_defined,
			{ "Ports", "wccp.service_flag.ports_defined", FT_BOOLEAN, 32, TFS(&tfs_defined_not_defined), WCCP2_SI_PORTS_DEFINED,
				NULL, HFILL }
		},
		{ &hf_service_flags_ports_source,
			{ "Ports refer to", "wccp.service_flag.ports_source", FT_BOOLEAN, 32, TFS(&tfs_src_dest_port), WCCP2_SI_PORTS_SOURCE,
				NULL, HFILL }
		},
		{ &hf_service_flags_src_ip_alt_hash,
			{ "Source IP address in secondary hash", "wccp.service_flag.src_ip_alt_hash", FT_BOOLEAN, 32, TFS(&tfs_used_notused), WCCP2_SI_SRC_IP_ALT_HASH,
				NULL, HFILL }
		},
		{ &hf_service_flags_dest_ip_alt_hash,
			{ "Destination IP address in secondary hash", "wccp.service_flag.dest_ip_alt_hash", FT_BOOLEAN, 32, TFS(&tfs_used_notused), WCCP2_SI_DST_IP_ALT_HASH,
				NULL, HFILL }
		},
		{ &hf_service_flags_src_port_alt_hash,
			{ "Source port in secondary hash", "wccp.service_flag.src_port_alt_hash", FT_BOOLEAN, 32, TFS(&tfs_used_notused), WCCP2_SI_SRC_PORT_ALT_HASH,
				NULL, HFILL }
		},
		{ &hf_service_flags_dest_port_alt_hash,
			{ "Destination port in secondary hash", "wccp.service_flag.dest_port_alt_hash", FT_BOOLEAN, 32, TFS(&tfs_used_notused), WCCP2_SI_DST_PORT_ALT_HASH,
				NULL, HFILL }
		},
		{ &hf_service_port,
			{ "Port", "wccp.service_port", FT_UINT16, BASE_DEC, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_router_identity_ip,
			{ "IP Address", "wccp.router_identity.ip", FT_IPv4, BASE_NONE, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_router_identity_receive_id,
			{ "Received ID", "wccp.router_identity.receive_id", FT_UINT32, BASE_DEC, 0x0, 0x0,
				NULL, HFILL }
		},
		{ &hf_router_send_to_ip,
			{ "Sent To IP Address", "wccp.router.send_to_ip", FT_IPv4, BASE_NONE, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_router_num_recv_ip,
			{ "Number of Received From IP addresses", "wccp.router.num_recv_ip", FT_UINT32, BASE_DEC, 0x0, 0x0,
				NULL, HFILL }
		},
		{ &hf_router_recv_ip,
			{ "Received From IP Address", "wccp.router.recv_ip", FT_IPv4, BASE_NONE, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_web_cache_identity_ip,
			{ "Web-Cache IP Address", "wccp.web_cache_identity.ip", FT_IPv4, BASE_NONE, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_web_cache_identity_hash_rev,
			{ "Hash Revision", "wccp.web_cache_identity.hash_rev", FT_UINT16, BASE_DEC, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_web_cache_identity_flags,
			{ "Flags", "wccp.web_cache_identity.flags", FT_UINT16, BASE_HEX, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_web_cache_identity_flag_hash_info,
			{ "Hash information", "wccp.web_cache_identity.flags.hash_info", FT_BOOLEAN, 16, TFS(&tfs_historical_current), 0x8000,
				NULL, HFILL }
		},
		{ &hf_web_cache_identity_flag_assign_type,
			{ "Assignment Type", "wccp.web_cache_identity.flags.assign_type", FT_BOOLEAN, 16, TFS(&tfs_mask_hash), 0x4000,
				NULL, HFILL }
		},
		{ &hf_web_cache_identity_num_mask,
			{ "Number of Masks", "wccp.web_cache_identity.num_mask", FT_UINT32, BASE_DEC, 0x0, 0x0,
				NULL, HFILL }
		},
		{ &hf_web_cache_identity_assign_mask,
			{ "Mask Element", "wccp.web_cache_identity.assign_mask", FT_UINT32, BASE_HEX, 0x0, 0x0,
				NULL, HFILL }
		},
		{ &hf_web_cache_identity_assign_weight,
			{ "Assignment Weight", "wccp.web_cache_identity.assignment_weight", FT_UINT16, BASE_DEC, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_web_cache_identity_status,
			{ "Status", "wccp.web_cache_identity.status", FT_UINT16, BASE_HEX, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_assignment_key_ip,
			{ "Assignment Key IP Address", "wccp.assignment_key.ip", FT_IPv4, BASE_NONE, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_assignment_key_change_num,
			{ "Assignment Key Change Number", "wccp.assignment_key.change_num", FT_UINT32, BASE_DEC, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_router_view_member_change_num,
			{ "Member Change Number", "wccp.router_view.member_change_num", FT_UINT32, BASE_DEC, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_router_router_num,
			{ "Number of Routers", "wccp.router_view.router_num", FT_UINT32, BASE_DEC, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_router_router_ip,
			{ "Router IP Address", "wccp.router_view.router_ip", FT_IPv4, BASE_NONE, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_wc_view_info_change_num,
			{ "Change Number", "wccp.wc_view.change_num", FT_UINT32, BASE_DEC, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_wc_view_router_num,
			{ "Number of Routers", "wccp.wc_view.router_num", FT_UINT32, BASE_DEC, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_wc_view_ip,
			{ "Router IP Address", "wccp.wc_view.ip", FT_IPv4, BASE_NONE, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_router_assignment_element_ip,
			{ "IP Address", "wccp.router_assignment_element.ip", FT_IPv4, BASE_NONE, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_router_assignment_element_receive_id,
			{ "Receive ID", "wccp.router_assignment_element.receive_id", FT_UINT32, BASE_DEC, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_router_assignment_element_change_num,
			{ "Change Number", "wccp.router_assignment_element.change_num", FT_UINT32, BASE_DEC, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_wccp2_assignment_info_router_num,
			{ "Number of Routers", "wccp.assignment_info.router_num", FT_UINT32, BASE_DEC, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_wccp2_assignment_info_ip,
			{ "Router IP Address", "wccp.assignment_info.ip", FT_IPv4, BASE_NONE, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_router_query_info_ip,
			{ "Router IP Address", "wccp.router_query_info.ip", FT_IPv4, BASE_NONE, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_router_query_info_receive_id,
			{ "Receive ID", "wccp.router_query_info.receive_id", FT_UINT32, BASE_DEC, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_router_query_info_send_to_ip,
			{ "Sent To IP Address", "wccp.router_query_info.send_to_ip", FT_IPv4, BASE_NONE, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_router_query_info_target_ip,
			{ "Target IP Address", "wccp.router_query_info.target_ip", FT_IPv4, BASE_NONE, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_capability_info_type,
			{ "Type", "wccp.capability_info.type", FT_UINT16, BASE_DEC, VALS(capability_type_vals), 0x0,
				NULL, HFILL }
		},
		{ &hf_capability_info_length,
			{ "Value Length", "wccp.capability_info.length", FT_UINT16, BASE_DEC, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_capability_info_bytes,
			{ "Value", "wccp.capability_info.bytes", FT_BYTES, BASE_NONE, 0x0, 0x0,
				NULL, HFILL }
		},
		{ &hf_capability_info_value,
			{ "Value", "wccp.capability_info.value", FT_UINT32, BASE_HEX, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_capability_forwarding_method_flag_gre,
			{ "GRE-encapsulated", "wccp.capability_info.forwarding_method_flag.gre", FT_BOOLEAN, 32, TFS(&tfs_supported_not_supported), WCCP2_FORWARDING_METHOD_GRE,
				NULL, HFILL }
		},
		{ &hf_capability_forwarding_method_flag_l2,
			{ "L2 rewrite", "wccp.capability_info.forwarding_method_flag.l2", FT_BOOLEAN, 32, TFS(&tfs_supported_not_supported), WCCP2_FORWARDING_METHOD_L2,
				NULL, HFILL }
		},
		{ &hf_capability_assignment_method_flag_hash,
			{ "Hash", "wccp.capability_info.assignment_method_flag.hash", FT_BOOLEAN, 32, TFS(&tfs_supported_not_supported), WCCP2_ASSIGNMENT_METHOD_HASH,
				NULL, HFILL }
		},
		{ &hf_capability_assignment_method_flag_mask,
			{ "Mask", "wccp.capability_info.assignment_method_flag.mask", FT_BOOLEAN, 32, TFS(&tfs_supported_not_supported), WCCP2_ASSIGNMENT_METHOD_MASK,
				NULL, HFILL }
		},
		{ &hf_capability_return_method_flag_gre,
			{ "GRE-encapsulated", "wccp.capability_info.return_method_flag.gre", FT_BOOLEAN, 32, TFS(&tfs_supported_not_supported), WCCP2_PACKET_RETURN_METHOD_GRE,
				NULL, HFILL }
		},
		{ &hf_capability_return_method_flag_l2,
			{ "L2 rewrite", "wccp.capability_info.return_method_flag.l2", FT_BOOLEAN, 32, TFS(&tfs_supported_not_supported), WCCP2_PACKET_RETURN_METHOD_L2,
				NULL, HFILL }
		},
		{ &hf_wccp2_value_element_src_ip,
			{ "Source Address", "wccp.value_element.src_ip", FT_IPv4, BASE_NONE, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_wccp2_value_element_dest_ip,
			{ "Destination Address", "wccp.value_element.dest_ip", FT_IPv4, BASE_NONE, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_wccp2_value_element_src_port,
			{ "Source Port", "wccp.value_element.src_port", FT_UINT16, BASE_DEC, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_wccp2_value_element_dest_port,
			{ "Destination Port", "wccp.value_element.dest_port", FT_UINT16, BASE_DEC, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_wccp2_value_element_web_cache_ip,
			{ "Web Cache Address", "wccp.value_element.web_cache_ip", FT_IPv4, BASE_NONE, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_wccp2_mask_value_set_element_src_ip,
			{ "Source Address Mask", "wccp.mask_value_set_element.src_ip", FT_IPv4, BASE_NONE, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_wccp2_mask_value_set_element_dest_ip,
			{ "Destination Address Mask", "wccp.mask_value_set_element.dest_ip", FT_IPv4, BASE_NONE, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_wccp2_mask_value_set_element_src_port,
			{ "Source Port Mask", "wccp.mask_value_set_element.src_port", FT_UINT16, BASE_DEC, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_wccp2_mask_value_set_element_dest_port,
			{ "Destination Port Mask", "wccp.mask_value_set_element.dest_port", FT_UINT16, BASE_DEC, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_wccp2_mask_value_set_element_num,
			{ "Number of Value Elements", "wccp.mask_value_set_element.num", FT_UINT32, BASE_DEC, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_alt_assignment_info_assignment_type,
			{ "Assignment type", "wccp.alt_assignment_info.assignment_type", FT_UINT16, BASE_DEC, VALS(assignment_type_vals), 0x0,
				NULL, HFILL }
		},
		{ &hf_alt_assignment_info_assignment_length,
			{ "Assignment length", "wccp.alt_assignment_info.assignment_length", FT_UINT16, BASE_DEC, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_alt_assignment_info_num_routers,
			{ "Number of routers", "wccp.alt_assignment_info.num_routers", FT_UINT32, BASE_DEC, NULL, 0x0,
				NULL, HFILL }
		},
		{ &hf_alt_assignment_info_num_elements,
			{ "Number of elements", "wccp.alt_assignment_info.num_elements", FT_UINT32, BASE_DEC, NULL, 0x0,
				NULL, HFILL }
		},
	};

	static gint *ett[] = {
		&ett_wccp,
		&ett_cache_count,
		&ett_buckets,
		&ett_flags,
		&ett_cache_info,
		&ett_security_info,
		&ett_service_info,
		&ett_service_flags,
		&ett_router_identity_element,
		&ett_router_identity_info,
		&ett_wc_identity_element,
		&ett_wc_identity_info,
		&ett_router_view_info,
		&ett_wc_view_info,
		&ett_query_info,
		&ett_router_assignment_element,
		&ett_router_assignment_info,
		&ett_capabilities_info,
		&ett_capability_element,
		&ett_capability_forwarding_method,
		&ett_capability_assignment_method,
		&ett_capability_return_method,
		&ett_mv_set_element,
		&ett_value_element,
		&ett_alt_assignment_info,
		&ett_unknown_info,
	};

	proto_wccp = proto_register_protocol("Web Cache Communication Protocol",
	    "WCCP", "wccp");
	proto_register_field_array(proto_wccp, hf, array_length(hf));
	proto_register_subtree_array(ett, array_length(ett));
}

void
proto_reg_handoff_wccp(void)
{
	dissector_handle_t wccp_handle;

	wccp_handle = new_create_dissector_handle(dissect_wccp, proto_wccp);
	dissector_add_uint("udp.port", UDP_PORT_WCCP, wccp_handle);
}
