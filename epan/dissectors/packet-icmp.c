/* packet-icmp.c
 * Routines for ICMP - Internet Control Message Protocol
 *
 * $Id$
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * Monday, June 27, 2005
 * Support for the ICMP extensions for MPLS
 * (http://www.ietf.org/proceedings/01aug/I-D/draft-ietf-mpls-icmp-02.txt
 *  which has been replaced by rfcs 4884 and 4950)
 * by   Maria-Luiza Crivat <luizacri@gmail.com>
 * &    Brice Augustin <bricecotte@gmail.com>
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
 *
 * Added support for ICMP extensions RFC 4884 and RFC 5837
 * (c) 2011 Gaurav Tungatkar <gstungat@ncsu.edu>
 */

#include "config.h"

#include <glib.h>
#include <time.h>

#include <epan/packet.h>
#include <epan/ipproto.h>
#include <epan/prefs.h>
#include <epan/in_cksum.h>

#include "packet-ip.h"
#include "packet-icmp.h"
#include <epan/conversation.h>
#include <epan/emem.h>
#include <epan/tap.h>

static int icmp_tap = -1;

/* Conversation related data */
static int hf_icmp_resp_in = -1;
static int hf_icmp_resp_to = -1;
static int hf_icmp_resptime = -1;
static int hf_icmp_data_time = -1;
static int hf_icmp_data_time_relative = -1;

typedef struct _icmp_conv_info_t {
	emem_tree_t *unmatched_pdus;
	emem_tree_t *matched_pdus;
} icmp_conv_info_t;

static icmp_transaction_t *transaction_start(packet_info * pinfo,
					     proto_tree * tree,
					     guint32 * key);
static icmp_transaction_t *transaction_end(packet_info * pinfo,
					   proto_tree * tree,
					   guint32 * key);

/* Decode the end of the ICMP payload as ICMP MPLS extensions
if the packet in the payload has more than 128 bytes */
static gboolean favor_icmp_mpls_ext = FALSE;

static int proto_icmp = -1;
static int hf_icmp_type = -1;
static int hf_icmp_code = -1;
static int hf_icmp_checksum = -1;
static int hf_icmp_checksum_bad = -1;
static int hf_icmp_ident = -1;
static int hf_icmp_ident_le = -1;
static int hf_icmp_seq_num = -1;
static int hf_icmp_seq_num_le = -1;
static int hf_icmp_mtu = -1;
static int hf_icmp_redir_gw = -1;
static int hf_icmp_length = -1;

/* Mobile ip */
static int hf_icmp_mip_type = -1;
static int hf_icmp_mip_length = -1;
static int hf_icmp_mip_prefix_length = -1;
static int hf_icmp_mip_seq = -1;
static int hf_icmp_mip_life = -1;
static int hf_icmp_mip_flags = -1;
static int hf_icmp_mip_r = -1;
static int hf_icmp_mip_b = -1;
static int hf_icmp_mip_h = -1;
static int hf_icmp_mip_f = -1;
static int hf_icmp_mip_m = -1;
static int hf_icmp_mip_g = -1;
static int hf_icmp_mip_v = -1;
static int hf_icmp_mip_rt = -1;
static int hf_icmp_mip_u = -1;
static int hf_icmp_mip_x = -1;
static int hf_icmp_mip_reserved = -1;
static int hf_icmp_mip_coa = -1;
static int hf_icmp_mip_challenge = -1;

/* extensions RFC 4884*/
static int hf_icmp_ext = -1;
static int hf_icmp_ext_version = -1;
static int hf_icmp_ext_reserved = -1;
static int hf_icmp_ext_checksum = -1;
static int hf_icmp_ext_checksum_bad = -1;
static int hf_icmp_ext_length = -1;
static int hf_icmp_ext_class = -1;
static int hf_icmp_ext_c_type = -1;

/* Interface information extension RFC 5837 */
static int hf_icmp_int_info_ifindex = -1;
static int hf_icmp_int_info_ipaddr = -1;
static int hf_icmp_int_info_name = -1;
static int hf_icmp_int_info_mtu = -1;
static int hf_icmp_int_info_afi = -1;
static int hf_icmp_int_info_ipv4 = -1;
static int hf_icmp_int_info_ipv6 = -1;
static int hf_icmp_int_info_role = -1;
static int hf_icmp_int_info_reserved = -1;
static gint ett_icmp_interface_info_object = -1;
static gint ett_icmp_interface_ipaddr = -1;
static gint ett_icmp_interface_name = -1;
/* MPLS extension object*/
static int hf_icmp_mpls_label = -1;
static int hf_icmp_mpls_exp = -1;
static int hf_icmp_mpls_s = -1;
static int hf_icmp_mpls_ttl = -1;

static gint ett_icmp = -1;
static gint ett_icmp_mip = -1;
static gint ett_icmp_mip_flags = -1;

/* extensions */
static gint ett_icmp_ext = -1;
static gint ett_icmp_ext_object = -1;

/* MPLS extensions */
static gint ett_icmp_mpls_stack_object = -1;

/* ICMP definitions */
#define ICMP_ECHOREPLY     0
#define ICMP_UNREACH       3
#define ICMP_SOURCEQUENCH  4
#define ICMP_REDIRECT      5
#define ICMP_ALTHOST       6
#define ICMP_ECHO          8
#define ICMP_RTRADVERT     9
#define ICMP_RTRSOLICIT   10
#define ICMP_TIMXCEED     11
#define ICMP_PARAMPROB    12
#define ICMP_TSTAMP       13
#define ICMP_TSTAMPREPLY  14
#define ICMP_IREQ         15
#define ICMP_IREQREPLY    16
#define ICMP_MASKREQ      17
#define ICMP_MASKREPLY    18
#define ICMP_PHOTURIS     40

/* ICMP UNREACHABLE */
#define ICMP_NET_UNREACH        0	/* Network Unreachable */
#define ICMP_HOST_UNREACH       1	/* Host Unreachable */
#define ICMP_PROT_UNREACH       2	/* Protocol Unreachable */
#define ICMP_PORT_UNREACH       3	/* Port Unreachable */
#define ICMP_FRAG_NEEDED        4	/* Fragmentation Needed/DF set */
#define ICMP_SR_FAILED          5	/* Source Route failed */
#define ICMP_NET_UNKNOWN        6
#define ICMP_HOST_UNKNOWN       7
#define ICMP_HOST_ISOLATED      8
#define ICMP_NET_ANO            9
#define ICMP_HOST_ANO           10
#define ICMP_NET_UNR_TOS        11
#define ICMP_HOST_UNR_TOS       12
#define ICMP_PKT_FILTERED       13	/* Packet filtered */
#define ICMP_PREC_VIOLATION     14	/* Precedence violation */
#define ICMP_PREC_CUTOFF        15	/* Precedence cut off */

#define ICMP_MIP_EXTENSION_PAD	0
#define ICMP_MIP_MOB_AGENT_ADV	16
#define ICMP_MIP_PREFIX_LENGTHS	19
#define ICMP_MIP_CHALLENGE	24

static dissector_handle_t ip_handle;
static dissector_handle_t data_handle;

static const value_string icmp_type_str[] = {
	{ICMP_ECHOREPLY, "Echo (ping) reply"},
	{1, "Reserved"},
	{2, "Reserved"},
	{ICMP_UNREACH, "Destination unreachable"},
	{ICMP_SOURCEQUENCH, "Source quench (flow control)"},
	{ICMP_REDIRECT, "Redirect"},
	{ICMP_ALTHOST, "Alternate host address"},
	{ICMP_ECHO, "Echo (ping) request"},
	{ICMP_RTRADVERT, "Router advertisement"},
	{ICMP_RTRSOLICIT, "Router solicitation"},
	{ICMP_TIMXCEED, "Time-to-live exceeded"},
	{ICMP_PARAMPROB, "Parameter problem"},
	{ICMP_TSTAMP, "Timestamp request"},
	{ICMP_TSTAMPREPLY, "Timestamp reply"},
	{ICMP_IREQ, "Information request"},
	{ICMP_IREQREPLY, "Information reply"},
	{ICMP_MASKREQ, "Address mask request"},
	{ICMP_MASKREPLY, "Address mask reply"},
	{19, "Reserved (for security)"},
	{30, "Traceroute"},
	{31, "Datagram Conversion Error"},
	{32, "Mobile Host Redirect"},
	{33, "IPv6 Where-Are-You"},
	{34, "IPv6 I-Am-Here"},
	{35, "Mobile Registration Request"},
	{36, "Mobile Registration Reply"},
	{37, "Domain Name Request"},
	{38, "Domain Name Reply"},
	{39, "SKIP"},
	{ICMP_PHOTURIS, "Photuris"},
	{41, "Experimental mobility protocols"},
	{0, NULL}
};

static const value_string unreach_code_str[] = {
	{ICMP_NET_UNREACH, "Network unreachable"},
	{ICMP_HOST_UNREACH, "Host unreachable"},
	{ICMP_PROT_UNREACH, "Protocol unreachable"},
	{ICMP_PORT_UNREACH, "Port unreachable"},
	{ICMP_FRAG_NEEDED, "Fragmentation needed"},
	{ICMP_SR_FAILED, "Source route failed"},
	{ICMP_NET_UNKNOWN, "Destination network unknown"},
	{ICMP_HOST_UNKNOWN, "Destination host unknown"},
	{ICMP_HOST_ISOLATED, "Source host isolated"},
	{ICMP_NET_ANO, "Network administratively prohibited"},
	{ICMP_HOST_ANO, "Host administratively prohibited"},
	{ICMP_NET_UNR_TOS, "Network unreachable for TOS"},
	{ICMP_HOST_UNR_TOS, "Host unreachable for TOS"},
	{ICMP_PKT_FILTERED, "Communication administratively filtered"},
	{ICMP_PREC_VIOLATION, "Host precedence violation"},
	{ICMP_PREC_CUTOFF, "Precedence cutoff in effect"},
	{0, NULL}
};

static const value_string redir_code_str[] = {
	{0, "Redirect for network"},
	{1, "Redirect for host"},
	{2, "Redirect for TOS and network"},
	{3, "Redirect for TOS and host"},
	{0, NULL}
};

static const value_string alt_host_code_str[] = {
	{0, "Alternate address for host"},
	{0, NULL}
};

static const value_string rtradvert_code_str[] = {
	{0, "Normal router advertisement"},
	{16, "Does not route common traffic"},
	{0, NULL}
};

static const value_string ttl_code_str[] = {
	{0, "Time to live exceeded in transit"},
	{1, "Fragment reassembly time exceeded"},
	{0, NULL}
};

static const value_string par_code_str[] = {
	{0, "Pointer indicates the error"},
	{1, "Required option missing"},
	{2, "Bad length"},
	{0, NULL}
};

static const value_string photuris_code_str[] = {
	{0, "Bad SPI"},
	{1, "Authentication Failed"},
	{2, "Decompression Failed"},
	{3, "Decryption Failed"},
	{4, "Need Authentication"},
	{5, "Need Authorization"},
	{0, NULL}
};

static const value_string mip_extensions[] = {
	{ICMP_MIP_EXTENSION_PAD, "One byte padding extension"},	/* RFC 2002 */
	{ICMP_MIP_MOB_AGENT_ADV, "Mobility Agent Advertisement Extension"},
	/* RFC 2002 */
	{ICMP_MIP_PREFIX_LENGTHS, "Prefix Lengths Extension"},	/* RFC 2002 */
	{ICMP_MIP_CHALLENGE, "Challenge Extension"},	/* RFC 3012 */
	{0, NULL}
};

/* RFC 5837 ICMP extension - Interface Information Object
 * Interface Role
 */
static const value_string interface_role_str[] = {
	{0, "IP interface upon which datagram arrived"},
	{1,
	 "sub-IP component of an IP interface upon which datagram arrived"},
	{2, "IP interface through which datagram would be forwarded"},
	{3, "IP next-hop to which datagram would be forwarded"},
	{0, NULL}
};

#define INT_INFO_INTERFACE_ROLE                 0xc0
#define INT_INFO_RESERVED                       0x30
#define INT_INFO_IFINDEX                        0x08
#define INT_INFO_IPADDR                         0x04
#define INT_INFO_NAME                           0x02
#define INT_INFO_MTU                            0x01

#define INTERFACE_INFORMATION_OBJECT_CLASS      2

#define MPLS_STACK_ENTRY_OBJECT_CLASS           1
#define MPLS_EXTENDED_PAYLOAD_OBJECT_CLASS      0

#define MPLS_STACK_ENTRY_C_TYPE                 1
#define MPLS_EXTENDED_PAYLOAD_C_TYPE            1

#define INET6_ADDRLEN                           16

static conversation_t *_find_or_create_conversation(packet_info * pinfo)
{
	conversation_t *conv = NULL;

	/* Have we seen this conversation before? */
	conv =
	    find_conversation(pinfo->fd->num, &pinfo->src, &pinfo->dst,
			      pinfo->ptype, 0, 0, 0);
	if (conv == NULL) {
		/* No, this is a new conversation. */
		conv =
		    conversation_new(pinfo->fd->num, &pinfo->src,
				     &pinfo->dst, pinfo->ptype, 0, 0, 0);
	}
	return conv;
}

/*
 * Dissect the mobile ip advertisement extensions.
 */
static void
dissect_mip_extensions(tvbuff_t * tvb, int offset, proto_tree * tree)
{
	guint8 type;
	guint8 length;
	guint16 flags;
	proto_item *ti;
	proto_tree *mip_tree = NULL;
	proto_tree *flags_tree = NULL;
	gint numCOAs;
	gint i;

	/* Not much to do if we're not parsing everything */
	if (!tree)
		return;

	while (tvb_reported_length_remaining(tvb, offset) > 0) {
		type = tvb_get_guint8(tvb, offset + 0);
		if (type) {
			length = tvb_get_guint8(tvb, offset + 1);
		} else {
			length = 0;
		}

		ti = proto_tree_add_text(tree, tvb, offset,
					 type ? (length + 2) : 1,
					 "Ext: %s", val_to_str(type,
							       mip_extensions,
							       "Unknown ext %u"));
		mip_tree = proto_item_add_subtree(ti, ett_icmp_mip);

		switch (type) {
		case ICMP_MIP_EXTENSION_PAD:
			/* One byte padding extension */
			/* Add our fields */
			/* type */
			proto_tree_add_item(mip_tree, hf_icmp_mip_type,
					    tvb, offset, 1,
					    ENC_BIG_ENDIAN);
			offset++;
			break;
		case ICMP_MIP_MOB_AGENT_ADV:
			/* Mobility Agent Advertisement Extension (RFC 2002) */
			/* Add our fields */
			/* type */
			proto_tree_add_item(mip_tree, hf_icmp_mip_type,
					    tvb, offset, 1,
					    ENC_BIG_ENDIAN);
			offset++;
			/* length */
			proto_tree_add_item(mip_tree, hf_icmp_mip_length,
					    tvb, offset, 1,
					    ENC_BIG_ENDIAN);
			offset++;
			/* sequence number */
			proto_tree_add_item(mip_tree, hf_icmp_mip_seq, tvb,
					    offset, 2, ENC_BIG_ENDIAN);
			offset += 2;
			/* Registration Lifetime */
			proto_tree_add_item(mip_tree, hf_icmp_mip_life,
					    tvb, offset, 2,
					    ENC_BIG_ENDIAN);
			offset += 2;
			/* flags */
			flags = tvb_get_ntohs(tvb, offset);
			ti = proto_tree_add_uint(mip_tree,
						 hf_icmp_mip_flags, tvb,
						 offset, 2, flags);
			flags_tree =
			    proto_item_add_subtree(ti, ett_icmp_mip_flags);
			proto_tree_add_boolean(flags_tree, hf_icmp_mip_r,
					       tvb, offset, 2, flags);
			proto_tree_add_boolean(flags_tree, hf_icmp_mip_b,
					       tvb, offset, 2, flags);
			proto_tree_add_boolean(flags_tree, hf_icmp_mip_h,
					       tvb, offset, 2, flags);
			proto_tree_add_boolean(flags_tree, hf_icmp_mip_f,
					       tvb, offset, 2, flags);
			proto_tree_add_boolean(flags_tree, hf_icmp_mip_m,
					       tvb, offset, 2, flags);
			proto_tree_add_boolean(flags_tree, hf_icmp_mip_g,
					       tvb, offset, 2, flags);
			proto_tree_add_boolean(flags_tree, hf_icmp_mip_v,
					       tvb, offset, 2, flags);
			proto_tree_add_boolean(flags_tree, hf_icmp_mip_rt,
					       tvb, offset, 2, flags);
			proto_tree_add_boolean(flags_tree, hf_icmp_mip_u,
					       tvb, offset, 2, flags);
			proto_tree_add_boolean(flags_tree, hf_icmp_mip_x,
					       tvb, offset, 2, flags);

			/* Reserved */
			proto_tree_add_uint(flags_tree,
					    hf_icmp_mip_reserved, tvb,
					    offset, 2, flags);
			offset += 2;

			/* COAs */
			numCOAs = (length - 6) / 4;
			for (i = 0; i < numCOAs; i++) {
				proto_tree_add_item(mip_tree,
						    hf_icmp_mip_coa, tvb,
						    offset, 4,
						    ENC_BIG_ENDIAN);
				offset += 4;
			}
			break;
		case ICMP_MIP_PREFIX_LENGTHS:
			/* Prefix-Lengths Extension  (RFC 2002) */
			/* Add our fields */
			/* type */
			proto_tree_add_item(mip_tree, hf_icmp_mip_type,
					    tvb, offset, 1,
					    ENC_BIG_ENDIAN);
			offset++;
			/* length */
			proto_tree_add_item(mip_tree, hf_icmp_mip_length,
					    tvb, offset, 1,
					    ENC_BIG_ENDIAN);
			offset++;

			/* prefix lengths */
			for (i = 0; i < length; i++) {
				proto_tree_add_item(mip_tree,
						    hf_icmp_mip_prefix_length,
						    tvb, offset, 1,
						    ENC_BIG_ENDIAN);
				offset++;
			}
			break;
		case ICMP_MIP_CHALLENGE:
			/* Challenge Extension  (RFC 3012) */
			/* type */
			proto_tree_add_item(mip_tree, hf_icmp_mip_type,
					    tvb, offset, 1,
					    ENC_BIG_ENDIAN);
			offset++;
			/* length */
			proto_tree_add_item(mip_tree, hf_icmp_mip_length,
					    tvb, offset, 1,
					    ENC_BIG_ENDIAN);
			offset++;
			/* challenge */
			proto_tree_add_item(mip_tree,
					    hf_icmp_mip_challenge, tvb,
					    offset, length, ENC_NA);
			offset += length;

			break;
		default:
			/* type */
			proto_tree_add_item(mip_tree, hf_icmp_mip_type,
					    tvb, offset, 1,
					    ENC_BIG_ENDIAN);
			offset++;
			/* length */
			proto_tree_add_item(mip_tree, hf_icmp_mip_length,
					    tvb, offset, 1,
					    ENC_BIG_ENDIAN);
			offset++;
			/* data, if any */
			if (length != 0) {
				proto_tree_add_text(mip_tree, tvb, offset,
						    length, "Contents");
				offset += length;
			}

			break;
		}
	}

}				/* dissect_mip_extensions */

static gboolean
dissect_mpls_extended_payload_object(tvbuff_t * tvb, gint offset,
				     proto_tree * ext_object_tree,
				     proto_item * tf_object)
{

	guint16 obj_length, obj_trunc_length;
	gboolean unknown_object;
	guint8 c_type;
	unknown_object = FALSE;
	/* Object length */
	obj_length = tvb_get_ntohs(tvb, offset);

	obj_trunc_length =
	    MIN(obj_length, tvb_reported_length_remaining(tvb, offset));

	/* C-Type */
	c_type = tvb_get_guint8(tvb, offset + 3);
	proto_tree_add_uint(ext_object_tree, hf_icmp_ext_c_type, tvb,
			    offset + 3, 1, c_type);

	/* skip the object header */
	offset += 4;

	switch (c_type) {
	case MPLS_EXTENDED_PAYLOAD_C_TYPE:
		proto_item_set_text(tf_object, "Extended Payload");

		/* This object contains some portion of the original packet
		   that could not fit in the 128 bytes of the ICMP payload */
		if (obj_trunc_length > 4) {
			proto_tree_add_text(ext_object_tree, tvb,
					    offset, obj_trunc_length - 4,
					    "Data (%d bytes)",
					    obj_trunc_length - 4);
		}
		break;
	default:
		unknown_object = TRUE;
	}			/* end switch c_type */
	return unknown_object;
}

static gboolean
dissect_mpls_stack_entry_object(tvbuff_t * tvb, gint offset,
				proto_tree * ext_object_tree,
				proto_item * tf_object)
{

	proto_item *tf_entry;
	proto_tree *mpls_stack_object_tree;
	guint16 obj_length, obj_trunc_length;
	gint obj_end_offset;
	guint label;
	guint8 ttl;
	guint8 tmp;
	gboolean unknown_object;
	guint8 c_type;
	unknown_object = FALSE;
	/* Object length */
	obj_length = tvb_get_ntohs(tvb, offset);

	obj_trunc_length =
	    MIN(obj_length, tvb_reported_length_remaining(tvb, offset));
	obj_end_offset = offset + obj_trunc_length;
	/* C-Type */
	c_type = tvb_get_guint8(tvb, offset + 3);
	proto_tree_add_uint(ext_object_tree, hf_icmp_ext_c_type, tvb,
			    offset + 3, 1, c_type);

	/* skip the object header */
	offset += 4;

	switch (c_type) {
	case MPLS_STACK_ENTRY_C_TYPE:
		proto_item_set_text(tf_object, "MPLS Stack Entry");
		/* For each entry */
		while (offset + 4 <= obj_end_offset) {
			if (tvb_reported_length_remaining(tvb, offset) < 4) {
				/* Not enough room in the packet ! */
				break;
			}
			/* Create a subtree for each entry (the text will be set later) */
			tf_entry = proto_tree_add_text(ext_object_tree,
						       tvb, offset, 4,
						       " ");
			mpls_stack_object_tree =
			    proto_item_add_subtree(tf_entry,
						   ett_icmp_mpls_stack_object);

			/* Label */
			label = (guint) tvb_get_ntohs(tvb, offset);
			tmp = tvb_get_guint8(tvb, offset + 2);
			label = (label << 4) + (tmp >> 4);

			proto_tree_add_uint(mpls_stack_object_tree,
					    hf_icmp_mpls_label, tvb,
					    offset, 3, label << 4);

			proto_item_set_text(tf_entry, "Label: %u", label);

			/* Experimental field (also called "CoS") */
			proto_tree_add_uint(mpls_stack_object_tree,
					    hf_icmp_mpls_exp, tvb,
					    offset + 2, 1, tmp);

			proto_item_append_text(tf_entry, ", Exp: %u",
					       (tmp >> 1) & 0x07);

			/* Stack bit */
			proto_tree_add_boolean(mpls_stack_object_tree,
					       hf_icmp_mpls_s, tvb,
					       offset + 2, 1, tmp);

			proto_item_append_text(tf_entry, ", S: %u",
					       tmp & 0x01);

			/* TTL */
			ttl = tvb_get_guint8(tvb, offset + 3);

			proto_tree_add_item(mpls_stack_object_tree,
					    hf_icmp_mpls_ttl, tvb,
					    offset + 3, 1, ENC_BIG_ENDIAN);

			proto_item_append_text(tf_entry, ", TTL: %u", ttl);

			/* Skip the entry */
			offset += 4;
		}

		if (offset < obj_end_offset) {
			proto_tree_add_text(ext_object_tree, tvb, offset,
					    obj_end_offset - offset,
					    "%d junk bytes",
					    obj_end_offset - offset);
		}
		break;

	default:

		unknown_object = TRUE;

		break;
	}			/* end switch c_type */
	return unknown_object;

}				/* end dissect_mpls_stack_entry_object */

/* Dissect Interface Information Object RFC 5837*/
static gboolean
dissect_interface_information_object(tvbuff_t * tvb, gint offset,
				     proto_tree * ext_object_tree,
				     proto_item * tf_object)
{
	proto_item *ti;
	proto_tree *int_name_object_tree = NULL;
	proto_tree *int_ipaddr_object_tree;
	guint16 obj_length, obj_trunc_length;
	gint obj_end_offset;
	guint8 c_type;
	gboolean unknown_object;
	guint8 if_index_flag;
	guint8 ipaddr_flag;
	guint8 name_flag;
	guint32 if_index;
	guint16 afi;
	struct e_in6_addr ipaddr_v6;
	guint8 int_name_length = 0;

	unknown_object = FALSE;
	/* Object length */
	obj_length = tvb_get_ntohs(tvb, offset);

	obj_trunc_length =
	    MIN(obj_length, tvb_reported_length_remaining(tvb, offset));
	obj_end_offset = offset + obj_trunc_length;

	/* C-Type */
	c_type = tvb_get_guint8(tvb, offset + 3);

	proto_item_set_text(tf_object, "Interface Information Object");
	if (tvb_reported_length_remaining(tvb, offset) < 4) {
		/* Not enough room in the packet ! return unknown_object = TRUE */
		return TRUE;
	}

	if_index_flag = (c_type & INT_INFO_IFINDEX) >> 3;
	ipaddr_flag = (c_type & INT_INFO_IPADDR) >> 2;
	name_flag = (c_type & INT_INFO_NAME) >> 1;

	{
		static const gint *c_type_fields[] = {
			&hf_icmp_int_info_role,
			&hf_icmp_int_info_reserved,
			&hf_icmp_int_info_ifindex,
			&hf_icmp_int_info_ipaddr,
			&hf_icmp_int_info_name,
			&hf_icmp_int_info_mtu,
			NULL
		};
		proto_tree_add_bitmask(ext_object_tree, tvb, offset + 3,
				       hf_icmp_ext_c_type,
				       ett_icmp_interface_info_object,
				       c_type_fields, ENC_BIG_ENDIAN);
	}

	/* skip header */
	offset += 4;

	/*if ifIndex is set, next 32 bits are ifIndex */
	if (if_index_flag) {
		if (obj_end_offset >= offset + 4) {
			if_index = tvb_get_ntohl(tvb, offset);
			proto_tree_add_text(ext_object_tree,
					    tvb, offset, 4,
					    "Interface Index: %u",
					    if_index);
			offset += 4;
		} else {
			proto_tree_add_text(ext_object_tree,
					    tvb, offset, 4,
					    "Interface Index:(truncated)");
			return FALSE;
		}
	}

	/* IP Address Sub Object */
	if (ipaddr_flag && (obj_end_offset >= offset + 2)) {
		/* Address Family Identifier */
		afi = tvb_get_ntohs(tvb, offset);

		/*
		 * if afi = 1, IPv4 address, 2 bytes afi, 2 bytes rsvd, 4 bytes IP addr
		 * if afi = 2, IPv6 address, 2 bytes afi, 2 bytes rsvd, 6 bytes IP addr
		 */
		ti = proto_tree_add_text(ext_object_tree, tvb, offset,
					 afi == 1 ? 8 : 10,
					 "IP Address Sub-Object");

		int_ipaddr_object_tree =
		    proto_item_add_subtree(ti, ett_icmp_interface_ipaddr);

		proto_tree_add_uint(int_ipaddr_object_tree,
				    hf_icmp_int_info_afi, tvb, offset, 2,
				    afi);

		/* skip reserved */
		offset += 4;
		if (afi == 1 && (obj_end_offset >= offset + 4)) {
			proto_tree_add_ipv4(int_ipaddr_object_tree,
					    hf_icmp_int_info_ipv4, tvb,
					    offset, 4, tvb_get_ntohl(tvb,
								     offset));
			offset += 4;
		} else if (afi == 2
			   && (obj_end_offset >= offset + INET6_ADDRLEN)) {
			tvb_get_ipv6(tvb, offset, &ipaddr_v6);
			proto_tree_add_ipv6(int_ipaddr_object_tree,
					    hf_icmp_int_info_ipv6, tvb,
					    offset, INET6_ADDRLEN,
					    (guint8 *) & ipaddr_v6);
			offset += INET6_ADDRLEN;
		} else {
			proto_tree_add_text(int_ipaddr_object_tree, tvb,
					    offset,
					    offset - obj_end_offset,
					    "Bad IP Address");
			return FALSE;
		}
	}

	/* Interface Name Sub Object */
	if (name_flag) {
		if (obj_end_offset >= offset + 1) {
			int_name_length = tvb_get_guint8(tvb, offset);
			ti = proto_tree_add_text(ext_object_tree, tvb,
						 offset, int_name_length,
						 "Interface Name Sub-Object");

			int_name_object_tree =
			    proto_item_add_subtree(ti,
						   ett_icmp_interface_name);
			proto_tree_add_text(int_name_object_tree, tvb,
					    offset, 1, "Length: %u",
					    int_name_length);
		}
		if (obj_end_offset >= offset + 1 + int_name_length) {

			proto_tree_add_text(int_name_object_tree, tvb,
					    offset + 1, int_name_length,
					    "Interface Name: %s",
					    tvb_format_text(tvb, offset + 1, int_name_length));
		}
	}


	return unknown_object;

}				/*end dissect_interface_information_object */

static void
dissect_extensions(tvbuff_t * tvb, gint offset, proto_tree * tree)
{
	guint8 version;
	guint8 class_num;
	guint8 c_type;
	guint16 reserved;
	guint16 cksum, computed_cksum;
	guint16 obj_length, obj_trunc_length;
	proto_item *ti, *tf_object, *hidden_item;
	proto_tree *ext_tree, *ext_object_tree;
	gint obj_end_offset;
	guint reported_length;
	gboolean unknown_object;
	guint8 int_info_obj_count;

	if (!tree)
		return;

	ext_tree = NULL;
	int_info_obj_count = 0;

	reported_length = tvb_reported_length_remaining(tvb, offset);

	if (reported_length < 4 /* Common header */ ) {
		proto_tree_add_text(tree, tvb, offset,
				    reported_length,
				    "ICMP Multi-Part Extensions (truncated)");
		return;
	}

	/* Add a tree for multi-part extensions RFC 4884 */
	ti = proto_tree_add_none_format(tree, hf_icmp_ext, tvb,
					offset, reported_length,
					"ICMP Multi-Part Extensions");

	ext_tree = proto_item_add_subtree(ti, ett_icmp_ext);

	/* Version */
	version = hi_nibble(tvb_get_guint8(tvb, offset));
	proto_tree_add_uint(ext_tree, hf_icmp_ext_version, tvb, offset, 1,
			    version);

	/* Reserved */
	reserved = tvb_get_ntohs(tvb, offset) & 0x0fff;
	proto_tree_add_uint_format(ext_tree, hf_icmp_ext_reserved,
				   tvb, offset, 2, reserved,
				   "Reserved: 0x%03x", reserved);

	/* Checksum */
	cksum = tvb_get_ntohs(tvb, offset + 2);

	computed_cksum =
	    ip_checksum(tvb_get_ptr(tvb, offset, reported_length),
			reported_length);

	if (computed_cksum == 0) {
		proto_tree_add_uint_format(ext_tree, hf_icmp_ext_checksum,
					   tvb, offset + 2, 2, cksum,
					   "Checksum: 0x%04x [correct]",
					   cksum);
		hidden_item =
		    proto_tree_add_boolean(ext_tree,
					   hf_icmp_ext_checksum_bad, tvb,
					   offset + 2, 2, FALSE);
	} else {
		proto_tree_add_uint_format(ext_tree, hf_icmp_ext_checksum,
					   tvb, offset + 2, 2, cksum,
					   "Checksum: 0x%04x [incorrect, should be 0x%04x]",
					   cksum, in_cksum_shouldbe(cksum,
								    computed_cksum));
		hidden_item =
		    proto_tree_add_boolean(ext_tree,
					   hf_icmp_ext_checksum_bad, tvb,
					   offset + 2, 2, TRUE);
	}
	PROTO_ITEM_SET_HIDDEN(hidden_item);

	if (version != 1 && version != 2) {
		/* Unsupported version */
		proto_item_append_text(ti, " (unsupported version)");
		return;
	}

	/* Skip the common header */
	offset += 4;

	/* While there is enough room to read an object */
	while (tvb_reported_length_remaining(tvb, offset) >=
	       4 /* Object header */ ) {
		/* Object length */
		obj_length = tvb_get_ntohs(tvb, offset);

		obj_trunc_length =
		    MIN(obj_length,
			tvb_reported_length_remaining(tvb, offset));

		obj_end_offset = offset + obj_trunc_length;

		/* Add a subtree for this object (the text will be reset later) */
		tf_object = proto_tree_add_text(ext_tree, tvb, offset,
						MAX(obj_trunc_length, 4),
						"Unknown object");

		ext_object_tree =
		    proto_item_add_subtree(tf_object, ett_icmp_ext_object);

		proto_tree_add_uint(ext_object_tree, hf_icmp_ext_length,
				    tvb, offset, 2, obj_length);

		/* Class */
		class_num = tvb_get_guint8(tvb, offset + 2);
		proto_tree_add_uint(ext_object_tree, hf_icmp_ext_class,
				    tvb, offset + 2, 1, class_num);

		/* C-Type */
		c_type = tvb_get_guint8(tvb, offset + 3);

		if (obj_length < 4 /* Object header */ ) {
			/* Thanks doc/README.developer :)) */
			proto_item_set_text(tf_object,
					    "Object with bad length");
			break;
		}


		switch (class_num) {
		case MPLS_STACK_ENTRY_OBJECT_CLASS:
			unknown_object =
			    dissect_mpls_stack_entry_object(tvb, offset,
							    ext_object_tree,
							    tf_object);
			break;
		case INTERFACE_INFORMATION_OBJECT_CLASS:
			unknown_object =
			    dissect_interface_information_object(tvb,
								 offset,
								 ext_object_tree,
								 tf_object);
			int_info_obj_count++;
			if (int_info_obj_count > 4) {
				proto_item_set_text(tf_object,
						    "More than 4 Interface Information Objects");
			}
			break;
		case MPLS_EXTENDED_PAYLOAD_OBJECT_CLASS:
			unknown_object =
			    dissect_mpls_extended_payload_object(tvb,
								 offset,
								 ext_object_tree,
								 tf_object);
			break;
		default:

			unknown_object = TRUE;

			break;
		}		/* end switch class_num */

		/* Skip the object header */
		offset += 4;

		/* The switches couldn't decode the object */
		if (unknown_object == TRUE) {
			proto_item_set_text(tf_object,
					    "Unknown object (%d/%d)",
					    class_num, c_type);

			if (obj_trunc_length > 4) {
				proto_tree_add_text(ext_object_tree, tvb,
						    offset,
						    obj_trunc_length - 4,
						    "Data (%d bytes)",
						    obj_trunc_length - 4);
			}
		}

		/* */
		if (obj_trunc_length < obj_length) {
			proto_item_append_text(tf_object, " (truncated)");
		}

		/* Go to the end of the object */
		offset = obj_end_offset;

	}
}

#include <stdio.h>
/* ======================================================================= */
static icmp_transaction_t *transaction_start(packet_info * pinfo,
					     proto_tree * tree,
					     guint32 * key)
{
	conversation_t *conversation;
	icmp_conv_info_t *icmp_info;
	icmp_transaction_t *icmp_trans;
	emem_tree_key_t icmp_key[3];
	proto_item *it;

	/* Handle the conversation tracking */
	conversation = _find_or_create_conversation(pinfo);
	icmp_info = (icmp_conv_info_t *)conversation_get_proto_data(conversation, proto_icmp);
	if (icmp_info == NULL) {
		icmp_info = se_new(icmp_conv_info_t);
		icmp_info->unmatched_pdus =
		    se_tree_create_non_persistent(EMEM_TREE_TYPE_RED_BLACK,
						  "icmp_unmatched_pdus");
		icmp_info->matched_pdus =
		    se_tree_create_non_persistent(EMEM_TREE_TYPE_RED_BLACK,
						  "icmp_matched_pdus");
		conversation_add_proto_data(conversation, proto_icmp,
					    icmp_info);
	}

	if (!PINFO_FD_VISITED(pinfo)) {
		/* this is a new request, create a new transaction structure and map it to the
		   unmatched table
		 */
		icmp_key[0].length = 2;
		icmp_key[0].key = key;
		icmp_key[1].length = 0;
		icmp_key[1].key = NULL;

		icmp_trans = se_new(icmp_transaction_t);
		icmp_trans->rqst_frame = PINFO_FD_NUM(pinfo);
		icmp_trans->resp_frame = 0;
		icmp_trans->rqst_time = pinfo->fd->abs_ts;
		nstime_set_zero(&icmp_trans->resp_time);
		se_tree_insert32_array(icmp_info->unmatched_pdus, icmp_key,
				       (void *) icmp_trans);
	} else {
		/* Already visited this frame */
		guint32 frame_num = pinfo->fd->num;

		icmp_key[0].length = 2;
		icmp_key[0].key = key;
		icmp_key[1].length = 1;
		icmp_key[1].key = &frame_num;
		icmp_key[2].length = 0;
		icmp_key[2].key = NULL;

		icmp_trans =
		    (icmp_transaction_t *)se_tree_lookup32_array(icmp_info->matched_pdus,
					   icmp_key);
	}
	if (icmp_trans == NULL) {
		return NULL;
	}

	/* Print state tracking in the tree */
	if (icmp_trans->resp_frame) {
		it = proto_tree_add_uint(tree, hf_icmp_resp_in, NULL, 0, 0,
					 icmp_trans->resp_frame);
		PROTO_ITEM_SET_GENERATED(it);

		col_append_fstr(pinfo->cinfo, COL_INFO, " (reply in %d)",
				icmp_trans->resp_frame);
	}

	return icmp_trans;

}				/* transaction_start() */

/* ======================================================================= */
static icmp_transaction_t *transaction_end(packet_info * pinfo,
					   proto_tree * tree,
					   guint32 * key)
{
	conversation_t *conversation;
	icmp_conv_info_t *icmp_info;
	icmp_transaction_t *icmp_trans;
	emem_tree_key_t icmp_key[3];
	proto_item *it;
	nstime_t ns;
	double resp_time;

	conversation =
	    find_conversation(pinfo->fd->num, &pinfo->src, &pinfo->dst,
			      pinfo->ptype, 0, 0, 0);
	if (conversation == NULL) {
		return NULL;
	}

	icmp_info = (icmp_conv_info_t *)conversation_get_proto_data(conversation, proto_icmp);
	if (icmp_info == NULL) {
		return NULL;
	}

	if (!PINFO_FD_VISITED(pinfo)) {
		guint32 frame_num;

		icmp_key[0].length = 2;
		icmp_key[0].key = key;
		icmp_key[1].length = 0;
		icmp_key[1].key = NULL;
		icmp_trans =
		    (icmp_transaction_t *)se_tree_lookup32_array(icmp_info->unmatched_pdus,
					   icmp_key);
		if (icmp_trans == NULL) {
			return NULL;
		}

		/* we have already seen this response, or an identical one */
		if (icmp_trans->resp_frame != 0) {
			return NULL;
		}

		icmp_trans->resp_frame = PINFO_FD_NUM(pinfo);

		/* we found a match. Add entries to the matched table for both request and reply frames
		 */
		icmp_key[0].length = 2;
		icmp_key[0].key = key;
		icmp_key[1].length = 1;
		icmp_key[1].key = &frame_num;
		icmp_key[2].length = 0;
		icmp_key[2].key = NULL;

		frame_num = icmp_trans->rqst_frame;
		se_tree_insert32_array(icmp_info->matched_pdus, icmp_key,
				       (void *) icmp_trans);

		frame_num = icmp_trans->resp_frame;
		se_tree_insert32_array(icmp_info->matched_pdus, icmp_key,
				       (void *) icmp_trans);
	} else {
		/* Already visited this frame */
		guint32 frame_num = pinfo->fd->num;

		icmp_key[0].length = 2;
		icmp_key[0].key = key;
		icmp_key[1].length = 1;
		icmp_key[1].key = &frame_num;
		icmp_key[2].length = 0;
		icmp_key[2].key = NULL;

		icmp_trans =
		    (icmp_transaction_t *)se_tree_lookup32_array(icmp_info->matched_pdus,
					   icmp_key);

		if (icmp_trans == NULL) {
			return NULL;
		}
	}


	it = proto_tree_add_uint(tree, hf_icmp_resp_to, NULL, 0, 0,
				 icmp_trans->rqst_frame);
	PROTO_ITEM_SET_GENERATED(it);

	nstime_delta(&ns, &pinfo->fd->abs_ts, &icmp_trans->rqst_time);
	icmp_trans->resp_time = ns;
	resp_time = nstime_to_msec(&ns);
	it = proto_tree_add_double_format_value(tree, hf_icmp_resptime,
						NULL, 0, 0, resp_time,
						"%.3f ms", resp_time);
	PROTO_ITEM_SET_GENERATED(it);

	col_append_fstr(pinfo->cinfo, COL_INFO, " (request in %d)",
			icmp_trans->rqst_frame);

	return icmp_trans;

}				/* transaction_end() */

#define MSPERDAY            86400000

/* ======================================================================= */
static guint32
get_best_guess_mstimeofday(tvbuff_t * tvb, gint offset, guint32 comp_ts)
{
	guint32 be_ts, le_ts;

	/* Account for the special case from RFC 792 as best we can by clearing
	 * the msb.  Ref: [Page 16] of http://tools.ietf.org/html/rfc792:

	 If the time is not available in milliseconds or cannot be provided
	 with respect to midnight UT then any time can be inserted in a
	 timestamp provided the high order bit of the timestamp is also set
	 to indicate this non-standard value.
	 */
	be_ts = tvb_get_ntohl(tvb, offset) & 0x7fffffff;
	le_ts = tvb_get_letohl(tvb, offset) & 0x7fffffff;

	if (be_ts < MSPERDAY && le_ts >= MSPERDAY) {
		return be_ts;
	}

	if (le_ts < MSPERDAY && be_ts >= MSPERDAY) {
		return le_ts;
	}

	if (be_ts < MSPERDAY && le_ts < MSPERDAY) {
		guint32 saved_be_ts = be_ts;
		guint32 saved_le_ts = le_ts;

		/* Is this a rollover to a new day, clocks not synchronized, different
		 * timezones between originate and receive/transmit, .. what??? */
		if (be_ts < comp_ts && be_ts <= (MSPERDAY / 4)
		    && comp_ts >= (MSPERDAY - (MSPERDAY / 4)))
			be_ts += MSPERDAY;	/* Assume a rollover to a new day */
		if (le_ts < comp_ts && le_ts <= (MSPERDAY / 4)
		    && comp_ts >= (MSPERDAY - (MSPERDAY / 4)))
			le_ts += MSPERDAY;	/* Assume a rollover to a new day */
		if (abs(be_ts - comp_ts) < abs(le_ts - comp_ts))
			return saved_be_ts;
		return saved_le_ts;
	}

	/* Both are bigger than MSPERDAY, but neither one's msb's are set.  This
	 * is clearly invalid, but now what TODO?  For now, take the one closest to
	 * the comparative timestamp, which is another way of saying, "let's
	 * return a deterministic wild guess. */
	if (abs(be_ts - comp_ts) < abs(le_ts - comp_ts)) {
		return be_ts;
	}
	return le_ts;
}				/* get_best_guess_mstimeofday() */

/*
 * RFC 792 for basic ICMP.
 * RFC 1191 for ICMP_FRAG_NEEDED (with MTU of next hop).
 * RFC 1256 for router discovery messages.
 * RFC 2002 and 3012 for Mobile IP stuff.
 */
static void
dissect_icmp(tvbuff_t * tvb, packet_info * pinfo, proto_tree * tree)
{
	proto_tree *icmp_tree = NULL;
	proto_item *ti;
	guint8 icmp_type;
	guint8 icmp_code;
	guint8 icmp_original_dgram_length;
	guint length, reported_length;
	guint16 cksum, computed_cksum;
	const gchar *type_str, *code_str;
	guint8 num_addrs = 0;
	guint8 addr_entry_size = 0;
	int i;
	gboolean save_in_error_pkt;
	tvbuff_t *next_tvb;
	proto_item *item;
	guint32 conv_key[2];
	icmp_transaction_t *trans = NULL;
	nstime_t ts, time_relative;

	col_set_str(pinfo->cinfo, COL_PROTOCOL, "ICMP");
	col_clear(pinfo->cinfo, COL_INFO);

	/* To do: check for runts, errs, etc. */
	icmp_type = tvb_get_guint8(tvb, 0);
	icmp_code = tvb_get_guint8(tvb, 1);
	cksum = tvb_get_ntohs(tvb, 2);
	/*length of original datagram carried in the ICMP payload. In terms of 32 bit
	 * words.*/
	icmp_original_dgram_length = tvb_get_guint8(tvb, 5);

	type_str =
	    val_to_str_const(icmp_type, icmp_type_str,
			     "Unknown ICMP (obsolete or malformed?)");

	switch (icmp_type) {
	case ICMP_UNREACH:
		code_str =
		    val_to_str(icmp_code, unreach_code_str,
			       "Unknown code: %u");
		break;
	case ICMP_REDIRECT:
		code_str =
		    val_to_str(icmp_code, redir_code_str,
			       "Unknown code: %u");
		break;
	case ICMP_ALTHOST:
		code_str =
		    val_to_str(icmp_code, alt_host_code_str,
			       "Unknown code: %u");
		break;
	case ICMP_RTRADVERT:
		switch (icmp_code) {
		case 0:	/* Mobile-Ip */
		case 16:	/* Mobile-Ip */
			type_str = "Mobile IP Advertisement";
			break;
		}		/* switch icmp_code */
		code_str =
		    val_to_str(icmp_code, rtradvert_code_str,
			       "Unknown code: %u");
		break;
	case ICMP_TIMXCEED:
		code_str =
		    val_to_str(icmp_code, ttl_code_str,
			       "Unknown code: %u");
		break;
	case ICMP_PARAMPROB:
		code_str =
		    val_to_str(icmp_code, par_code_str,
			       "Unknown code: %u");
		break;
	case ICMP_PHOTURIS:
		code_str =
		    val_to_str(icmp_code, photuris_code_str,
			       "Unknown code: %u");
		break;
	default:
		code_str = NULL;
		break;
	}

	col_add_fstr(pinfo->cinfo, COL_INFO, "%-20s", type_str);
	if (code_str) {
		col_append_fstr(pinfo->cinfo, COL_INFO, " (%s)", code_str);
	}

	length = tvb_length(tvb);
	reported_length = tvb_reported_length(tvb);

	ti = proto_tree_add_item(tree, proto_icmp, tvb, 0, length, ENC_NA);
	icmp_tree = proto_item_add_subtree(ti, ett_icmp);

	ti = proto_tree_add_item(icmp_tree, hf_icmp_type, tvb, 0, 1,
				 ENC_BIG_ENDIAN);
	proto_item_append_text(ti, " (%s)", type_str);

	ti = proto_tree_add_item(icmp_tree, hf_icmp_code, tvb, 1, 1,
				 ENC_BIG_ENDIAN);
	if (code_str) {
		proto_item_append_text(ti, " (%s)", code_str);
	}

	if (!pinfo->fragmented && length >= reported_length
	    && !pinfo->flags.in_error_pkt) {
		/* The packet isn't part of a fragmented datagram, isn't
		   truncated, and isn't the payload of an error packet, so we can checksum
		   it. */

		computed_cksum =
		    ip_checksum(tvb_get_ptr(tvb, 0, reported_length),
				reported_length);
		if (computed_cksum == 0) {
			proto_tree_add_uint_format(icmp_tree,
						   hf_icmp_checksum, tvb,
						   2, 2, cksum,
						   "Checksum: 0x%04x [correct]",
						   cksum);
			item =
			    proto_tree_add_boolean(icmp_tree,
						   hf_icmp_checksum_bad,
						   tvb, 2, 2, FALSE);
			PROTO_ITEM_SET_HIDDEN(item);
		} else {
			proto_tree_add_uint_format(icmp_tree,
						   hf_icmp_checksum, tvb,
						   2, 2, cksum,
						   "Checksum: 0x%04x [incorrect, should be 0x%04x]",
						   cksum,
						   in_cksum_shouldbe(cksum,
								     computed_cksum));
			item =
			    proto_tree_add_boolean(icmp_tree,
						   hf_icmp_checksum_bad,
						   tvb, 2, 2, TRUE);
			PROTO_ITEM_SET_HIDDEN(item);
		}
	} else {
		proto_tree_add_uint(icmp_tree, hf_icmp_checksum, tvb, 2, 2,
				    cksum);
	}

	/* Decode the second 4 bytes of the packet. */
	switch (icmp_type) {
	case ICMP_ECHOREPLY:
	case ICMP_ECHO:
	case ICMP_TSTAMP:
	case ICMP_TSTAMPREPLY:
	case ICMP_IREQ:
	case ICMP_IREQREPLY:
	case ICMP_MASKREQ:
	case ICMP_MASKREPLY:
		proto_tree_add_item(icmp_tree, hf_icmp_ident, tvb, 4, 2,
				    ENC_BIG_ENDIAN);
		proto_tree_add_item(icmp_tree, hf_icmp_ident_le, tvb, 4, 2,
				    ENC_LITTLE_ENDIAN);
		proto_tree_add_item(icmp_tree, hf_icmp_seq_num, tvb, 6, 2,
				    ENC_BIG_ENDIAN);
		proto_tree_add_item(icmp_tree, hf_icmp_seq_num_le, tvb, 6,
				    2, ENC_LITTLE_ENDIAN);
		col_append_fstr(pinfo->cinfo, COL_INFO,
				" id=0x%04x, seq=%u/%u, ttl=%u",
				tvb_get_ntohs(tvb, 4), tvb_get_ntohs(tvb,
								     6),
				tvb_get_letohs(tvb, 6), pinfo->ip_ttl);
		break;

	case ICMP_UNREACH:

		/* If icmp_original_dgram_length > 0, then this packet is compliant with RFC 4884 and
		 * interpret the 6th octet as length of the original datagram
		 */
		if (icmp_original_dgram_length > 0) {
			ti = proto_tree_add_item(icmp_tree, hf_icmp_length,
						 tvb, 5, 1,
						 ENC_BIG_ENDIAN);
			proto_item_append_text(ti,
					       "Length of original datagram: %u",
					       icmp_original_dgram_length *
					       4);
		}


		switch (icmp_code) {
		case ICMP_FRAG_NEEDED:
			proto_tree_add_item(icmp_tree, hf_icmp_mtu, tvb, 6,
					    2, ENC_BIG_ENDIAN);
			break;
		}
		break;

	case ICMP_RTRADVERT:
		num_addrs = tvb_get_guint8(tvb, 4);
		proto_tree_add_text(icmp_tree, tvb, 4, 1,
				    "Number of addresses: %u", num_addrs);
		addr_entry_size = tvb_get_guint8(tvb, 5);
		proto_tree_add_text(icmp_tree, tvb, 5, 1,
				    "Address entry size: %u",
				    addr_entry_size);
		proto_tree_add_text(icmp_tree, tvb, 6, 2, "Lifetime: %s",
				    time_secs_to_str(tvb_get_ntohs
						     (tvb, 6)));
		break;

	case ICMP_PARAMPROB:
		proto_tree_add_text(icmp_tree, tvb, 4, 1, "Pointer: %u",
				    tvb_get_guint8(tvb, 4));
		if (icmp_original_dgram_length > 0) {
			ti = proto_tree_add_item(icmp_tree, hf_icmp_length,
						 tvb, 5, 1,
						 ENC_BIG_ENDIAN);
			proto_item_append_text(ti,
					       " Length of original datagram: %u",
					       icmp_original_dgram_length *
					       4);
		}
		break;

	case ICMP_REDIRECT:
		proto_tree_add_item(icmp_tree, hf_icmp_redir_gw, tvb, 4, 4,
				    ENC_BIG_ENDIAN);
		break;

	case ICMP_TIMXCEED:
		if (icmp_original_dgram_length > 0) {
			ti = proto_tree_add_item(icmp_tree, hf_icmp_length,
						 tvb, 5, 1,
						 ENC_BIG_ENDIAN);
			proto_item_append_text(ti,
					       " Length of original datagram: %u",
					       icmp_original_dgram_length *
					       4);
		}
	}

	/* Decode the additional information in the packet.  */
	switch (icmp_type) {
	case ICMP_UNREACH:
	case ICMP_TIMXCEED:
	case ICMP_PARAMPROB:
	case ICMP_SOURCEQUENCH:
	case ICMP_REDIRECT:
		/* Save the current value of the "we're inside an error packet"
		   flag, and set that flag; subdissectors may treat packets
		   that are the payload of error packets differently from
		   "real" packets. */
		save_in_error_pkt = pinfo->flags.in_error_pkt;
		pinfo->flags.in_error_pkt = TRUE;

		/* Decode the IP header and first 64 bits of data from the
		   original datagram. */
		next_tvb = tvb_new_subset_remaining(tvb, 8);

		/* If the packet is compliant with RFC 4884, then it has
		 * icmp_original_dgram_length*4 bytes of original IP packet that needs
		 * to be decoded, followed by extension objects.
		 */
		if (icmp_original_dgram_length
		    && (tvb_reported_length(tvb) >
			(guint) (8 + icmp_original_dgram_length * 4))
		    && (tvb_get_ntohs(tvb, 8 + 2) >
			(guint) icmp_original_dgram_length * 4)) {
			set_actual_length(next_tvb,
					  icmp_original_dgram_length * 4);
		} else {
			/* There is a collision between RFC 1812 and draft-ietf-mpls-icmp-02.
			   We don't know how to decode the 128th and following bytes of the ICMP payload.
			   According to draft-ietf-mpls-icmp-02, these bytes should be decoded as MPLS extensios
			   whereas RFC 1812 tells us to decode them as a portion of the original packet.
			   Let the user decide.

			   Here the user decided to favor MPLS extensions.
			   Force the IP dissector to decode only the first 128 bytes. */
			if ((tvb_reported_length(tvb) > 8 + 128) &&
			    favor_icmp_mpls_ext
			    && (tvb_get_ntohs(tvb, 8 + 2) > 128)) {
				set_actual_length(next_tvb, 128);
			}
		}

		call_dissector(ip_handle, next_tvb, pinfo, icmp_tree);

		/* Restore the "we're inside an error packet" flag. */
		pinfo->flags.in_error_pkt = save_in_error_pkt;

		/* Decode MPLS extensions if the payload has at least 128 bytes, and
		   - the original packet in the ICMP payload has less than 128 bytes, or
		   - the user favors the MPLS extensions analysis */
		if ((tvb_reported_length(tvb) > 8 + 128)
		    && (tvb_get_ntohs(tvb, 8 + 2) <= 128
			|| favor_icmp_mpls_ext)) {
			dissect_extensions(tvb, 8 + 128, icmp_tree);
		}
		break;
	case ICMP_ECHOREPLY:
	case ICMP_ECHO:
		if (icmp_type == ICMP_ECHOREPLY) {
			if (!pinfo->flags.in_error_pkt) {
				conv_key[0] =
				    (guint32) tvb_get_ntohs(tvb, 2);
				if (pinfo->flags.in_gre_pkt)
					conv_key[0] |= 0x00010000;	/* set a bit for "in GRE" */
				conv_key[1] =
				    (guint32) ((tvb_get_ntohs(tvb, 4) <<
						16) | tvb_get_ntohs(tvb,
								    6));
				trans =
				    transaction_end(pinfo, icmp_tree,
						    conv_key);
			}
		} else {
			if (!pinfo->flags.in_error_pkt) {
				guint16 tmp[2];

				tmp[0] = ~tvb_get_ntohs(tvb, 2);
				tmp[1] = ~0x0800;	/* The difference between echo request & reply */
				conv_key[0] =
				    ip_checksum((guint8 *) & tmp,
						sizeof(tmp));
				if (conv_key[0] == 0) {
					conv_key[0] = 0xffff;
				}
				if (pinfo->flags.in_gre_pkt) {
					conv_key[0] |= 0x00010000;	/* set a bit for "in GRE" */
				}
				conv_key[1] =
				    (guint32) ((tvb_get_ntohs(tvb, 4) <<
						16) | tvb_get_ntohs(tvb,
								    6));
				trans =
				    transaction_start(pinfo, icmp_tree,
						      conv_key);
			}
		}

		/* Make sure we have enough bytes in the payload before trying to
		 * see if the data looks like a timestamp; otherwise we'll get
		 * malformed packets as we try to access data that isn't there. */
		if (tvb_length_remaining(tvb, 8) < 8) {
			if (tvb_length_remaining(tvb, 8) > 0) {
				call_dissector(data_handle,
					       tvb_new_subset_remaining
					       (tvb, 8), pinfo, icmp_tree);
			}
			break;
		}

		/* Interpret the first 8 bytes of the icmp data as a timestamp
		 * But only if it does look like it's a timestamp.
		 *
		 * FIXME:
		 *    Timestamps could be in different formats depending on the OS
		 */
		ts.secs = tvb_get_ntohl(tvb, 8);
		ts.nsecs = tvb_get_ntohl(tvb, 8 + 4);	/* Leave at microsec resolution for now */
		if (abs((guint32) (ts.secs - pinfo->fd->abs_ts.secs)) >=
		    3600 * 24 || ts.nsecs >= 1000000) {
			/* Timestamp does not look right in BE, try LE representation */
			ts.secs = tvb_get_letohl(tvb, 8);
			ts.nsecs = tvb_get_letohl(tvb, 8 + 4);	/* Leave at microsec resolution for now */
		}
		if (abs((guint32) (ts.secs - pinfo->fd->abs_ts.secs)) <
		    3600 * 24 && ts.nsecs < 1000000) {
			ts.nsecs *= 1000;	/* Convert to nanosec resolution */
			proto_tree_add_time(icmp_tree, hf_icmp_data_time,
					    tvb, 8, 8, &ts);
			nstime_delta(&time_relative, &pinfo->fd->abs_ts,
				     &ts);
			ti = proto_tree_add_time(icmp_tree,
						 hf_icmp_data_time_relative,
						 tvb, 8, 8,
						 &time_relative);
			PROTO_ITEM_SET_GENERATED(ti);
			call_dissector(data_handle,
				       tvb_new_subset_remaining(tvb,
								8 + 8),
				       pinfo, icmp_tree);
		} else {
			call_dissector(data_handle,
				       tvb_new_subset_remaining(tvb, 8),
				       pinfo, icmp_tree);
		}
		break;

	case ICMP_RTRADVERT:
		if (addr_entry_size == 2) {
			for (i = 0; i < num_addrs; i++) {
				proto_tree_add_text(icmp_tree, tvb,
						    8 + (i * 8), 4,
						    "Router address: %s",
						    tvb_ip_to_str(tvb,
								  8 +
								  (i *
								   8)));
				proto_tree_add_text(icmp_tree, tvb,
						    12 + (i * 8), 4,
						    "Preference level: %d",
						    tvb_get_ntohl(tvb,
								  12 +
								  (i *
								   8)));
			}
			if ((icmp_code == 0) || (icmp_code == 16)) {
				/* Mobile-Ip */
				dissect_mip_extensions(tvb, 8 + i * 8,
						       icmp_tree);
			}
		} else {
			call_dissector(data_handle,
				       tvb_new_subset_remaining(tvb, 8),
				       pinfo, icmp_tree);
		}
		break;

	case ICMP_TSTAMP:
	case ICMP_TSTAMPREPLY:
		{
			guint32 frame_ts, orig_ts;

			frame_ts = (guint32)(((pinfo->fd->abs_ts.secs * 1000) +
				    (pinfo->fd->abs_ts.nsecs / 1000000)) %
			    86400000);

			orig_ts =
			    get_best_guess_mstimeofday(tvb, 8, frame_ts);
			proto_tree_add_text(icmp_tree, tvb, 8, 4,
					    "Originate timestamp: %s after midnight UTC",
					    time_msecs_to_str(orig_ts));

			proto_tree_add_text(icmp_tree, tvb, 12, 4,
					    "Receive timestamp: %s after midnight UTC",
					    time_msecs_to_str
					    (get_best_guess_mstimeofday
					     (tvb, 12, orig_ts)));
			proto_tree_add_text(icmp_tree, tvb, 16, 4,
					    "Transmit timestamp: %s after midnight UTC",
					    time_msecs_to_str
					    (get_best_guess_mstimeofday
					     (tvb, 16, orig_ts)));
		}
		break;

	case ICMP_MASKREQ:
	case ICMP_MASKREPLY:
		proto_tree_add_text(icmp_tree, tvb, 8, 4,
				    "Address mask: %s (0x%08x)",
				    tvb_ip_to_str(tvb, 8),
				    tvb_get_ntohl(tvb, 8));
		break;
	}

	if (trans) {
		tap_queue_packet(icmp_tap, pinfo, trans);
	}
}

void proto_register_icmp(void)
{
	static hf_register_info hf[] = {
		{&hf_icmp_type,
		 {"Type", "icmp.type", FT_UINT8, BASE_DEC, NULL, 0x0,
		  NULL, HFILL}},

		{&hf_icmp_code,
		 {"Code", "icmp.code", FT_UINT8, BASE_DEC, NULL, 0x0,
		  NULL, HFILL}},

		{&hf_icmp_checksum,
		 {"Checksum", "icmp.checksum", FT_UINT16, BASE_HEX, NULL,
		  0x0,
		  NULL, HFILL}},

		{&hf_icmp_checksum_bad,
		 {"Bad Checksum", "icmp.checksum_bad", FT_BOOLEAN,
		  BASE_NONE, NULL, 0x0,
		  NULL, HFILL}},

		{&hf_icmp_ident,
		 {"Identifier (BE)", "icmp.ident", FT_UINT16, BASE_DEC_HEX,
		  NULL, 0x0,
		  "Identifier (big endian representation)", HFILL}},

		{&hf_icmp_ident_le,
		 {"Identifier (LE)", "icmp.ident", FT_UINT16, BASE_DEC_HEX,
		  NULL, 0x0,
		  "Identifier (little endian representation)", HFILL}},

		{&hf_icmp_seq_num,
		 {"Sequence number (BE)", "icmp.seq", FT_UINT16,
		  BASE_DEC_HEX, NULL, 0x0,
		  "Sequence number (big endian representation)", HFILL}},

		{&hf_icmp_seq_num_le,
		 {"Sequence number (LE)", "icmp.seq_le", FT_UINT16,
		  BASE_DEC_HEX, NULL,
		  0x0, "Sequence number (little endian representation)",
		  HFILL}},

		{&hf_icmp_mtu,
		 {"MTU of next hop", "icmp.mtu", FT_UINT16, BASE_DEC, NULL,
		  0x0,
		  NULL, HFILL}},

		{&hf_icmp_redir_gw,
		 {"Gateway address", "icmp.redir_gw", FT_IPv4, BASE_NONE,
		  NULL, 0x0,
		  NULL, HFILL}},

		{&hf_icmp_mip_type,
		 {"Extension Type", "icmp.mip.type", FT_UINT8, BASE_DEC,
		  VALS(mip_extensions), 0x0, NULL, HFILL}},

		{&hf_icmp_mip_length,
		 {"Length", "icmp.mip.length", FT_UINT8, BASE_DEC, NULL,
		  0x0,
		  NULL, HFILL}},

		{&hf_icmp_mip_prefix_length,
		 {"Prefix Length", "icmp.mip.prefixlength", FT_UINT8,
		  BASE_DEC, NULL, 0x0,
		  NULL, HFILL}},

		{&hf_icmp_mip_seq,
		 {"Sequence Number", "icmp.mip.seq", FT_UINT16, BASE_DEC,
		  NULL, 0x0,
		  NULL, HFILL}},

		{&hf_icmp_mip_life,
		 {"Registration Lifetime", "icmp.mip.life", FT_UINT16,
		  BASE_DEC, NULL,
		  0x0,
		  NULL, HFILL}},

		{&hf_icmp_mip_flags,
		 {"Flags", "icmp.mip.flags", FT_UINT16, BASE_HEX, NULL,
		  0x0,
		  NULL, HFILL}},

		{&hf_icmp_mip_r,
		 {"Registration Required", "icmp.mip.r", FT_BOOLEAN, 16,
		  NULL, 0x8000,
		  "Registration with this FA is required", HFILL}},

		{&hf_icmp_mip_b,
		 {"Busy", "icmp.mip.b", FT_BOOLEAN, 16, NULL, 0x4000,
		  "This FA will not accept requests at this time", HFILL}},

		{&hf_icmp_mip_h,
		 {"Home Agent", "icmp.mip.h", FT_BOOLEAN, 16, NULL, 0x2000,
		  "Home Agent Services Offered", HFILL}},

		{&hf_icmp_mip_f,
		 {"Foreign Agent", "icmp.mip.f", FT_BOOLEAN, 16, NULL,
		  0x1000,
		  "Foreign Agent Services Offered", HFILL}},

		{&hf_icmp_mip_m,
		 {"Minimal Encapsulation", "icmp.mip.m", FT_BOOLEAN, 16,
		  NULL, 0x0800,
		  "Minimal encapsulation tunneled datagram support",
		  HFILL}},

		{&hf_icmp_mip_g,
		 {"GRE", "icmp.mip.g", FT_BOOLEAN, 16, NULL, 0x0400,
		  "GRE encapsulated tunneled datagram support", HFILL}},

		{&hf_icmp_mip_v,
		 {"VJ Comp", "icmp.mip.v", FT_BOOLEAN, 16, NULL, 0x0200,
		  "Van Jacobson Header Compression Support", HFILL}},

		{&hf_icmp_mip_rt,
		 {"Reverse tunneling", "icmp.mip.rt", FT_BOOLEAN, 16, NULL,
		  0x0100,
		  "Reverse tunneling support", HFILL}},

		{&hf_icmp_mip_u,
		 {"UDP tunneling", "icmp.mip.u", FT_BOOLEAN, 16, NULL,
		  0x0080,
		  "UDP tunneling support", HFILL}},

		{&hf_icmp_mip_x,
		 {"Revocation support", "icmp.mip.x", FT_BOOLEAN, 16, NULL,
		  0x0040,
		  "Registration revocation support", HFILL}},

		{&hf_icmp_mip_reserved,
		 {"Reserved", "icmp.mip.reserved", FT_UINT16, BASE_HEX,
		  NULL, 0x003f,
		  NULL, HFILL}},

		{&hf_icmp_mip_coa,
		 {"Care-Of-Address", "icmp.mip.coa", FT_IPv4, BASE_NONE,
		  NULL, 0x0,
		  NULL, HFILL}},

		{&hf_icmp_mip_challenge,
		 {"Challenge", "icmp.mip.challenge", FT_BYTES, BASE_NONE,
		  NULL, 0x0,
		  NULL, HFILL}},

		{&hf_icmp_ext,
		 {"ICMP Extensions", "icmp.ext", FT_NONE, BASE_NONE, NULL,
		  0x0,
		  NULL, HFILL}},

		{&hf_icmp_ext_version,
		 {"Version", "icmp.ext.version", FT_UINT8, BASE_DEC, NULL,
		  0x0,
		  NULL, HFILL}},

		{&hf_icmp_ext_reserved,
		 {"Reserved", "icmp.ext.res", FT_UINT16, BASE_HEX, NULL,
		  0x0,
		  NULL, HFILL}},

		{&hf_icmp_ext_checksum,
		 {"Checksum", "icmp.ext.checksum", FT_UINT16, BASE_HEX,
		  NULL, 0x0,
		  NULL, HFILL}},

		{&hf_icmp_ext_checksum_bad,
		 {"Bad Checksum", "icmp.ext.checksum_bad", FT_BOOLEAN,
		  BASE_NONE, NULL,
		  0x0,
		  NULL, HFILL}},

		{&hf_icmp_ext_length,
		 {"Length", "icmp.ext.length", FT_UINT16, BASE_DEC, NULL,
		  0x0,
		  NULL, HFILL}},

		{&hf_icmp_ext_class,
		 {"Class", "icmp.ext.class", FT_UINT8, BASE_DEC, NULL, 0x0,
		  NULL, HFILL}},

		{&hf_icmp_ext_c_type,
		 {"C-Type", "icmp.ext.ctype", FT_UINT8, BASE_DEC, NULL,
		  0x0,
		  NULL, HFILL}},

		{&hf_icmp_mpls_label,
		 {"Label", "icmp.mpls.label", FT_UINT24, BASE_DEC, NULL,
		  0x00fffff0,
		  NULL, HFILL}},

		{&hf_icmp_mpls_exp,
		 {"Experimental", "icmp.mpls.exp", FT_UINT24, BASE_DEC,
		  NULL, 0x0e,
		  NULL, HFILL}},

		{&hf_icmp_mpls_s,
		 {"Stack bit", "icmp.mpls.s", FT_BOOLEAN, 24,
		  TFS(&tfs_set_notset), 0x01,
		  NULL, HFILL}},

		{&hf_icmp_mpls_ttl,
		 {"Time to live", "icmp.mpls.ttl", FT_UINT8, BASE_DEC,
		  NULL, 0x0,
		  NULL, HFILL}},

		{&hf_icmp_resp_in,
		 {"Response frame", "icmp.resp_in", FT_FRAMENUM, BASE_NONE,
		  NULL, 0x0,
		  "The frame number of the corresponding response",
		  HFILL}},

		{&hf_icmp_resp_to,
		 {"Request frame", "icmp.resp_to", FT_FRAMENUM, BASE_NONE,
		  NULL, 0x0,
		  "The frame number of the corresponding request", HFILL}},

		{&hf_icmp_resptime,
		 {"Response time", "icmp.resptime", FT_DOUBLE, BASE_NONE,
		  NULL, 0x0,
		  "The time between the request and the response, in ms.",
		  HFILL}},

		{&hf_icmp_data_time,
		 {"Timestamp from icmp data", "icmp.data_time",
		  FT_ABSOLUTE_TIME,
		  ABSOLUTE_TIME_LOCAL, NULL, 0x0,
		  "The timestamp in the first 8 bytes of the icmp data",
		  HFILL}},

		{&hf_icmp_data_time_relative,
		 {"Timestamp from icmp data (relative)",
		  "icmp.data_time_relative",
		  FT_RELATIVE_TIME, BASE_NONE, NULL, 0x0,
		  "The timestamp of the packet, relative to the timestamp in the first 8 bytes of the icmp data",
		  HFILL}},

		{&hf_icmp_length,
		 {"Length of original datagram", "icmp.length", FT_UINT8,
		  BASE_DEC, NULL,
		  0x0,
		  "The length of the original datagram", HFILL}},
		{&hf_icmp_int_info_role,
		 {"Interface Role", "icmp.int_info.role",
		  FT_UINT8, BASE_DEC, VALS(interface_role_str),
		  INT_INFO_INTERFACE_ROLE,
		  NULL, HFILL}},
		{&hf_icmp_int_info_reserved,
		 {"Reserved", "icmp.int_info.reserved",
		  FT_UINT8, BASE_DEC, NULL, INT_INFO_RESERVED,
		  NULL, HFILL}},
		{&hf_icmp_int_info_ifindex,
		 {"ifIndex", "icmp.int_info.ifindex", FT_BOOLEAN, 8, NULL,
		  INT_INFO_IFINDEX,
		  "True: ifIndex of the interface included; False: ifIndex of the interface not included ",
		  HFILL}},
		{&hf_icmp_int_info_ipaddr,
		 {"IP Address", "icmp.int_info.ipaddr", FT_BOOLEAN, 8,
		  NULL,
		  INT_INFO_IPADDR,
		  "True: IP Address Sub-Object present; False: IP Address Sub-Object not present",
		  HFILL}},
		{&hf_icmp_int_info_name,
		 {"Interface Name", "icmp.int_info.name", FT_BOOLEAN, 8,
		  NULL,
		  INT_INFO_NAME,
		  "True: Interface Name Sub-Object present; False: Interface Name Sub-Object not present",
		  HFILL}},
		{&hf_icmp_int_info_mtu,
		 {"MTU", "icmp.int_info.mtu", FT_BOOLEAN, 8, NULL,
		  INT_INFO_MTU,
		  "True: MTU present; False: MTU not present", HFILL}},
		{&hf_icmp_int_info_afi,
		 {"Address Family Identifier", "icmp.int_info.afi",
		  FT_UINT16, BASE_DEC,
		  NULL, 0x0,
		  "Address Family of the interface address", HFILL}},
		{&hf_icmp_int_info_ipv4,
		 {"Source", "icmp.int_info.ipv4", FT_IPv4, BASE_NONE, NULL,
		  0x0,
		  NULL, HFILL}},
		{&hf_icmp_int_info_ipv6,
		 {"Source", "icmp.int_info.ipv6", FT_IPv6, BASE_NONE, NULL,
		  0x0,
		  NULL, HFILL}}
	};

	static gint *ett[] = {
		&ett_icmp,
		&ett_icmp_mip,
		&ett_icmp_mip_flags,
		/* MPLS extensions */
		&ett_icmp_ext,
		&ett_icmp_ext_object,
		&ett_icmp_mpls_stack_object,
		/* Interface Information Object RFC 5837 */
		&ett_icmp_interface_info_object,
		&ett_icmp_interface_ipaddr,
		&ett_icmp_interface_name
	};

	module_t *icmp_module;

	proto_icmp =
	    proto_register_protocol("Internet Control Message Protocol",
				    "ICMP", "icmp");
	proto_register_field_array(proto_icmp, hf, array_length(hf));
	proto_register_subtree_array(ett, array_length(ett));

	icmp_module = prefs_register_protocol(proto_icmp, NULL);

	prefs_register_bool_preference(icmp_module, "favor_icmp_mpls",
				       "Favor ICMP extensions for MPLS",
				       "Whether the 128th and following bytes of the ICMP payload should be decoded as MPLS extensions or as a portion of the original packet",
				       &favor_icmp_mpls_ext);

	register_dissector("icmp", dissect_icmp, proto_icmp);
	icmp_tap = register_tap("icmp");
}

void proto_reg_handoff_icmp(void)
{
	dissector_handle_t icmp_handle;

	/*
	 * Get handle for the IP dissector.
	 */
	ip_handle = find_dissector("ip");
	icmp_handle = find_dissector("icmp");
	data_handle = find_dissector("data");

	dissector_add_uint("ip.proto", IP_PROTO_ICMP, icmp_handle);
}
