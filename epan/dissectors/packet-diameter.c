/* packet-diameter.c
 * Routines for Diameter packet disassembly
 *
 * $Id$
 *
 * Copyright (c) 2001 by David Frascone <dave@frascone.com>
 * Copyright (c) 2007 by Luis E. Garcia Ontanon <luis@ontanon.org>
 *
 * Support for Request-Answer tracking and Tapping
 * introduced by Abhik Sarkar
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
 * References:
 * 2004-03-11
 * http://www.ietf.org/rfc/rfc3588.txt
 * http://www.iana.org/assignments/radius-types
 * http://www.ietf.org/internet-drafts/draft-ietf-aaa-diameter-cc-03.txt
 * http://www.ietf.org/internet-drafts/draft-ietf-aaa-diameter-nasreq-14.txt
 * http://www.ietf.org/internet-drafts/draft-ietf-aaa-diameter-mobileip-16.txt
 * http://www.ietf.org/internet-drafts/draft-ietf-aaa-diameter-sip-app-01.txt
 * http://www.ietf.org/html.charters/aaa-charter.html
 * http://www.iana.org/assignments/address-family-numbers
 * http://www.iana.org/assignments/enterprise-numbers
 * http://www.iana.org/assignments/aaa-parameters
*/

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include <glib.h>

#include <epan/packet.h>
#include <epan/filesystem.h>
#include <epan/prefs.h>
#include <epan/sminmpec.h>
#include <epan/emem.h>
#include <epan/wmem/wmem.h>
#include <epan/expert.h>
#include <epan/conversation.h>
#include <epan/tap.h>
#include <epan/diam_dict.h>
#include <epan/sctpppids.h>
#include <epan/show_exception.h>
#include "packet-tcp.h"
#include "packet-diameter.h"

/* Diameter Header Flags */
/* RPETrrrrCCCCCCCCCCCCCCCCCCCCCCCC  */
#define DIAM_FLAGS_R 0x80
#define DIAM_FLAGS_P 0x40
#define DIAM_FLAGS_E 0x20
#define DIAM_FLAGS_T 0x10
#define DIAM_FLAGS_RESERVED4 0x08
#define DIAM_FLAGS_RESERVED5 0x04
#define DIAM_FLAGS_RESERVED6 0x02
#define DIAM_FLAGS_RESERVED7 0x01
#define DIAM_FLAGS_RESERVED  0x0f

#define DIAM_LENGTH_MASK  0x00ffffffl
#define DIAM_COMMAND_MASK DIAM_LENGTH_MASK
#define DIAM_GET_FLAGS(dh)                ((dh.flagsCmdCode & ~DIAM_COMMAND_MASK) >> 24)
#define DIAM_GET_VERSION(dh)              ((dh.versionLength & (~DIAM_LENGTH_MASK)) >> 24)
#define DIAM_GET_COMMAND(dh)              (dh.flagsCmdCode & DIAM_COMMAND_MASK)
#define DIAM_GET_LENGTH(dh)               (dh.versionLength & DIAM_LENGTH_MASK)

/* Diameter AVP Flags */
#define AVP_FLAGS_P 0x20
#define AVP_FLAGS_V 0x80
#define AVP_FLAGS_M 0x40
#define AVP_FLAGS_RESERVED3 0x10
#define AVP_FLAGS_RESERVED4 0x08
#define AVP_FLAGS_RESERVED5 0x04
#define AVP_FLAGS_RESERVED6 0x02
#define AVP_FLAGS_RESERVED7 0x01
#define AVP_FLAGS_RESERVED 0x1f          /* 00011111  -- V M P X X X X X */

#define DIAMETER_V16 16
#define DIAMETER_RFC 1

/* Conversation Info */
typedef struct _diameter_conv_info_t {
        emem_tree_t *pdus_tree;
} diameter_conv_info_t;

typedef struct _diam_ctx_t {
	proto_tree *tree;
	packet_info *pinfo;
	emem_tree_t *avps;
	gboolean version_rfc;
} diam_ctx_t;

typedef struct _diam_avp_t diam_avp_t;
typedef struct _avp_type_t avp_type_t;

typedef const char *(*diam_avp_dissector_t)(diam_ctx_t *, diam_avp_t *, tvbuff_t *);


typedef struct _diam_vnd_t {
	guint32 code;
	GArray *vs_avps;
	value_string_ext *vs_avps_ext;
	GArray *vs_cmds;
} diam_vnd_t;

struct _diam_avp_t {
	guint32 code;
	const diam_vnd_t *vendor;
	diam_avp_dissector_t dissector_v16;
	diam_avp_dissector_t dissector_rfc;

	gint ett;
	int hf_value;
	void *type_data;
};

#define VND_AVP_VS(v)      ((value_string *)(void *)((v)->vs_avps->data))
#define VND_AVP_VS_LEN(v)  ((v)->vs_avps->len)
#define VND_CMD_VS(v)      ((value_string *)(void *)((v)->vs_cmds->data))

typedef struct _diam_dictionary_t {
	emem_tree_t *avps;
	emem_tree_t *vnds;
	value_string *applications;
	value_string *commands;
} diam_dictionary_t;

typedef diam_avp_t *(*avp_constructor_t)(const avp_type_t *, guint32, const diam_vnd_t *, const char *,  const value_string *, void *);

struct _avp_type_t {
	const char *name;
	diam_avp_dissector_t v16;
	diam_avp_dissector_t rfc;
	enum ftenum ft;
	int base;
	avp_constructor_t build;
};

struct _build_dict {
	GArray *hf;
	GPtrArray *ett;
	GHashTable *types;
	GHashTable *avps;
};


typedef struct _address_avp_t {
	gint ett;
	int hf_address_type;
	int hf_ipv4;
	int hf_ipv6;
	int hf_other;
} address_avp_t;

typedef enum {
	REASEMBLE_NEVER = 0,
	REASEMBLE_AT_END,
	REASEMBLE_BY_LENGTH
} avp_reassemble_mode_t;

typedef struct _proto_avp_t {
	char *name;
	dissector_handle_t handle;
	avp_reassemble_mode_t reassemble_mode;
} proto_avp_t;

static const char *simple_avp(diam_ctx_t *, diam_avp_t *, tvbuff_t *);

static diam_vnd_t unknown_vendor = { 0xffffffff, NULL, NULL, NULL };
static diam_vnd_t no_vnd = { 0, NULL, NULL, NULL };
static diam_avp_t unknown_avp = {0, &unknown_vendor, simple_avp, simple_avp, -1, -1, NULL };
static GArray *all_cmds;
static diam_dictionary_t dictionary = { NULL, NULL, NULL, NULL };
static struct _build_dict build_dict;
static const value_string *vnd_short_vs;
static dissector_handle_t data_handle;
static dissector_handle_t eap_handle;

static const value_string diameter_avp_data_addrfamily_vals[]= {
	{1,"IPv4"},
	{2,"IPv6"},
	{3,"NSAP"},
	{4,"HDLC"},
	{5,"BBN"},
	{6,"IEEE-802"},
	{7,"E-163"},
	{8,"E-164"},
	{9,"F-69"},
	{10,"X-121"},
	{11,"IPX"},
	{12,"Appletalk"},
	{13,"Decnet4"},
	{14,"Vines"},
	{15,"E-164-NSAP"},
	{16,"DNS"},
	{17,"DistinguishedName"},
	{18,"AS"},
	{19,"XTPoIPv4"},
	{20,"XTPoIPv6"},
	{21,"XTPNative"},
	{22,"FibrePortName"},
	{23,"FibreNodeName"},
	{24,"GWID"},
	{0,NULL}
};
static value_string_ext diameter_avp_data_addrfamily_vals_ext = VALUE_STRING_EXT_INIT(diameter_avp_data_addrfamily_vals);

static int proto_diameter = -1;
static int hf_diameter_length = -1;
static int hf_diameter_code = -1;
static int hf_diameter_hopbyhopid =-1;
static int hf_diameter_endtoendid =-1;
static int hf_diameter_version = -1;
static int hf_diameter_vendor_id = -1;
static int hf_diameter_application_id = -1;
static int hf_diameter_flags = -1;
static int hf_diameter_flags_request = -1;
static int hf_diameter_flags_proxyable = -1;
static int hf_diameter_flags_error = -1;
static int hf_diameter_flags_T		= -1;
static int hf_diameter_flags_reserved4 = -1;
static int hf_diameter_flags_reserved5 = -1;
static int hf_diameter_flags_reserved6 = -1;
static int hf_diameter_flags_reserved7 = -1;

static int hf_diameter_avp = -1;
static int hf_diameter_avp_len = -1;
static int hf_diameter_avp_code = -1;
static int hf_diameter_avp_flags = -1;
static int hf_diameter_avp_flags_vendor_specific = -1;
static int hf_diameter_avp_flags_mandatory = -1;
static int hf_diameter_avp_flags_protected = -1;
static int hf_diameter_avp_flags_reserved3 = -1;
static int hf_diameter_avp_flags_reserved4 = -1;
static int hf_diameter_avp_flags_reserved5 = -1;
static int hf_diameter_avp_flags_reserved6 = -1;
static int hf_diameter_avp_flags_reserved7 = -1;
static int hf_diameter_avp_vendor_id = -1;
static int hf_diameter_avp_data_wrong_length = -1;
static int hf_diameter_avp_pad = -1;

static int hf_diameter_answer_in = -1;
static int hf_diameter_answer_to = -1;
static int hf_diameter_answer_time = -1;

/* AVPs with special/extra decoding */
static int hf_framed_ipv6_prefix_reserved = -1;
static int hf_framed_ipv6_prefix_length = -1;
static int hf_framed_ipv6_prefix_bytes = -1;
static int hf_framed_ipv6_prefix_ipv6 = -1;

static gint ett_diameter = -1;
static gint ett_diameter_flags = -1;
static gint ett_diameter_avp_flags = -1;
static gint ett_diameter_avpinfo = -1;
static gint ett_unknown = -1;
static gint ett_err = -1;

/* Tap for Diameter */
static int diameter_tap = -1;

/* For conversations */


static dissector_handle_t diameter_tcp_handle;
static dissector_handle_t diameter_sctp_handle;
static range_t *global_diameter_tcp_port_range;
static range_t *global_diameter_sctp_port_range;
/* This is used for TCP and SCTP */
#define DEFAULT_DIAMETER_PORT_RANGE "3868"

/* desegmentation of Diameter over TCP */
static gboolean gbl_diameter_desegment = TRUE;

/* Dissector tables */
static dissector_table_t diameter_dissector_table;
static dissector_table_t diameter_3gpp_avp_dissector_table;
static dissector_table_t diameter_ericsson_avp_dissector_table;

static const char *avpflags_str[] = {
	"---",
	"--P",
	"-M-",
	"-MP",
	"V--",
	"V-P",
	"VM-",
	"VMP",
};

static gint
compare_avps (gconstpointer  a, gconstpointer  b)
{
	value_string *vsa = (value_string *)a;
	value_string *vsb = (value_string *)b;

	if(vsa->value > vsb->value)
		return 1;
	if(vsa->value < vsb->value)
		return -1;

	return 0;
}

/* Special decoding of some AVPs */

static int
dissect_diameter_vendor_id(tvbuff_t *tvb, packet_info *pinfo _U_, proto_tree *tree, void *data _U_)
{
	int offset = 0;

	proto_tree_add_item(tree, hf_diameter_vendor_id, tvb, 0, 4, ENC_BIG_ENDIAN);

	offset++;
	return offset;
}

static int
dissect_diameter_eap_payload(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree, void *data _U_)
{
	gboolean save_writable;

	/* Ensure the packet is displayed as Diameter, not EAP */
	save_writable = col_get_writable(pinfo->cinfo);
	col_set_writable(pinfo->cinfo, FALSE);

	call_dissector(eap_handle, tvb, pinfo, tree);

	col_set_writable(pinfo->cinfo, save_writable);
	return tvb_length(tvb);
}

/* From RFC 3162 section 2.3 */
static int
dissect_diameter_base_framed_ipv6_prefix(tvbuff_t *tvb, packet_info *pinfo _U_, proto_tree *tree, void *data _U_)
{
	guint8 prefix_len, prefix_len_bytes;

	proto_tree_add_item(tree, hf_framed_ipv6_prefix_reserved, tvb, 0, 1, ENC_BIG_ENDIAN);
	proto_tree_add_item(tree, hf_framed_ipv6_prefix_length, tvb, 1, 1, ENC_BIG_ENDIAN);

	prefix_len = tvb_get_guint8(tvb, 1);
	prefix_len_bytes = prefix_len / 8;
	if (prefix_len % 8)
		prefix_len_bytes++;

	proto_tree_add_item(tree, hf_framed_ipv6_prefix_bytes, tvb, 2, prefix_len_bytes, ENC_NA);

	/* If we have a fully IPv6 address, display it as such */
	if (prefix_len_bytes == 16)
		proto_tree_add_item(tree, hf_framed_ipv6_prefix_ipv6, tvb, 2, prefix_len_bytes, ENC_NA);

	return(prefix_len_bytes+2);
}

/* Call subdissectors for AVPs.
 * This is a separate function to avoid having any local variables that might
 * get clobbered by the exception longjmp() (without having to declare the
 * variables as volatile and deal with casting them).
 */
static void
call_avp_subdissector(guint32 vendorid, guint32 code, tvbuff_t *subtvb, packet_info *pinfo, proto_tree *avp_tree)
{
	TRY {
		switch (vendorid) {
		case 0:
			dissector_try_uint(diameter_dissector_table, code, subtvb, pinfo, avp_tree);
			break;
		case VENDOR_ERICSSON:
			dissector_try_uint(diameter_ericsson_avp_dissector_table, code, subtvb, pinfo, avp_tree);
			break;
		case VENDOR_THE3GPP:
			dissector_try_uint(diameter_3gpp_avp_dissector_table, code, subtvb, pinfo, avp_tree);
			break;
		default:
			break;
		}

		/* Debug
		proto_tree_add_text(avp_tree, subtvb, 0, -1, "AVP %u data, Vendor Id %u ",code,vendorid);
		*/
	}
	CATCH_NONFATAL_ERRORS {
		show_exception(subtvb, pinfo, avp_tree, EXCEPT_CODE, GET_MESSAGE);
	}
	ENDTRY;
}

/* Dissect an AVP at offset */
static int
dissect_diameter_avp(diam_ctx_t *c, tvbuff_t *tvb, int offset)
{
	guint32 code           = tvb_get_ntohl(tvb,offset);
	guint32 len            = tvb_get_ntohl(tvb,offset+4);
	guint32 vendor_flag    = len & 0x80000000;
	guint32 flags_bits_idx = (len & 0xE0000000) >> 29;
	guint32 flags_bits     = (len & 0xFF000000) >> 24;
	guint32 vendorid       = vendor_flag ? tvb_get_ntohl(tvb,offset+8) : 0 ;
	emem_tree_key_t k[]    = {
		{1,&code},
		{1,&vendorid},
		{0,NULL}
	};
	diam_avp_t *a          = (diam_avp_t *)emem_tree_lookup32_array(dictionary.avps,k);
	proto_item *pi, *avp_item;
	proto_tree *avp_tree, *save_tree;
	tvbuff_t *subtvb;
	diam_vnd_t *vendor;
	const char *code_str;
	const char *avp_str;
	guint8 pad_len;

	len &= 0x00ffffff;
	pad_len =  (len % 4) ? 4 - (len % 4) : 0 ;

	if (!a) {
		a = &unknown_avp;

		if (vendor_flag) {
			if (! (vendor = (diam_vnd_t *)emem_tree_lookup32(dictionary.vnds,vendorid) ))
				vendor = &unknown_vendor;
		} else {
			vendor = &no_vnd;
		}
	} else {
		vendor = (diam_vnd_t *)a->vendor;
	}

	if(vendor->vs_avps_ext == NULL) {
		g_array_sort(vendor->vs_avps, compare_avps);
		vendor->vs_avps_ext = value_string_ext_new(VND_AVP_VS(vendor), VND_AVP_VS_LEN(vendor)+1,
							   g_strdup_printf("diameter_vendor_%s",val_to_str_ext_const(vendorid, &sminmpec_values_ext, "Unknown")));
#if 0
		{ /* Debug code */
			value_string *vendor_avp_vs=VALUE_STRING_EXT_VS_P(vendor->vs_avps_ext);
			gint i = 0;
			while(vendor_avp_vs[i].strptr!=NULL) {
				g_warning("%u %s",vendor_avp_vs[i].value,vendor_avp_vs[i].strptr);
				i++;
			}
		}
#endif
	}

	/* Add root of tree for this AVP */
	avp_item = proto_tree_add_item(c->tree, hf_diameter_avp, tvb, offset, len + pad_len, ENC_NA);
	avp_tree = proto_item_add_subtree(avp_item, a->ett);

	pi = proto_tree_add_item(avp_tree,hf_diameter_avp_code,tvb,offset,4,ENC_BIG_ENDIAN);
	code_str = val_to_str_ext_const(code, vendor->vs_avps_ext, "Unknown");
	proto_item_append_text(pi," %s", code_str);

	/* Code */
	if (a == &unknown_avp) {
		proto_tree *tu = proto_item_add_subtree(pi,ett_unknown);
		pi = proto_tree_add_text(tu,tvb,offset,4,"Unknown AVP, "
					 "if you know what this is you can add it to dictionary.xml");
		expert_add_info_format(c->pinfo, pi, PI_UNDECODED, PI_WARN,
		                       "Unknown AVP %u (vendor=%s)", code,
		                       val_to_str_ext_const(vendorid, &sminmpec_values_ext, "Unknown"));
		PROTO_ITEM_SET_GENERATED(pi);
	}

	offset += 4;

	proto_item_set_text(avp_item,"AVP: %s(%u) l=%u f=%s", code_str, code, len, avpflags_str[flags_bits_idx]);

	/* Flags */
	pi = proto_tree_add_item(avp_tree,hf_diameter_avp_flags,tvb,offset,1,ENC_BIG_ENDIAN);
	{
		proto_tree *flags_tree = proto_item_add_subtree(pi,ett_diameter_avp_flags);
		proto_tree_add_item(flags_tree,hf_diameter_avp_flags_vendor_specific,tvb,offset,1,ENC_BIG_ENDIAN);
		proto_tree_add_item(flags_tree,hf_diameter_avp_flags_mandatory,tvb,offset,1,ENC_BIG_ENDIAN);
		proto_tree_add_item(flags_tree,hf_diameter_avp_flags_protected,tvb,offset,1,ENC_BIG_ENDIAN);
		pi = proto_tree_add_item(flags_tree,hf_diameter_avp_flags_reserved3,tvb,offset,1,ENC_BIG_ENDIAN);
		if(flags_bits & 0x10) proto_item_set_expert_flags(pi, PI_MALFORMED, PI_WARN);
		pi = proto_tree_add_item(flags_tree,hf_diameter_avp_flags_reserved4,tvb,offset,1,ENC_BIG_ENDIAN);
		if(flags_bits & 0x08) proto_item_set_expert_flags(pi, PI_MALFORMED, PI_WARN);
		pi = proto_tree_add_item(flags_tree,hf_diameter_avp_flags_reserved5,tvb,offset,1,ENC_BIG_ENDIAN);
		if(flags_bits & 0x04) proto_item_set_expert_flags(pi, PI_MALFORMED, PI_WARN);
		proto_tree_add_item(flags_tree,hf_diameter_avp_flags_reserved6,tvb,offset,1,ENC_BIG_ENDIAN);
		if(flags_bits & 0x02) proto_item_set_expert_flags(pi, PI_MALFORMED, PI_WARN);
		proto_tree_add_item(flags_tree,hf_diameter_avp_flags_reserved7,tvb,offset,1,ENC_BIG_ENDIAN);
		if(flags_bits & 0x01) proto_item_set_expert_flags(pi, PI_MALFORMED, PI_WARN);
	}
	offset += 1;

	/* Length */
	proto_tree_add_item(avp_tree,hf_diameter_avp_len,tvb,offset,3,ENC_BIG_ENDIAN);
	offset += 3;

	/* Vendor flag */
	if (vendor_flag) {
		proto_item_append_text(avp_item," vnd=%s", val_to_str(vendorid, vnd_short_vs, "%d"));
		pi = proto_tree_add_item(avp_tree,hf_diameter_avp_vendor_id,tvb,offset,4,ENC_BIG_ENDIAN);
		if (vendor == &unknown_vendor) {
			proto_tree *tu = proto_item_add_subtree(pi,ett_unknown);
			pi = proto_tree_add_text(tu,tvb,offset,4,"Unknown Vendor, "
					     "if you know whose this is you can add it to dictionary.xml");
			expert_add_info_format(c->pinfo, pi, PI_UNDECODED, PI_WARN, "Unknown Vendor");
			PROTO_ITEM_SET_GENERATED(pi);
		}
		offset += 4;
	}

	if ( len == (guint32)(vendor_flag ? 12 : 8) ) {
		/* Data is empty so return now */
		pi = proto_tree_add_text(avp_tree,tvb,offset,0,"No data");
		expert_add_info_format(c->pinfo, pi, PI_UNDECODED, PI_WARN, "Data is empty");
		PROTO_ITEM_SET_GENERATED(pi);
		/* pad_len is always 0 in this case, but kept here for consistency */
		return len+pad_len;
	}

	subtvb = tvb_new_subset(tvb,offset,len-(8+(vendor_flag?4:0)),len-(8+(vendor_flag?4:0)));
	offset += len-(8+(vendor_flag?4:0));

	save_tree = c->tree;
	c->tree = avp_tree;
	if (c->version_rfc) {
		avp_str = a->dissector_rfc(c,a,subtvb);
	} else {
		avp_str = a->dissector_v16(c,a,subtvb);
	}
	c->tree = save_tree;

	if (avp_str) proto_item_append_text(avp_item," val=%s", avp_str);

	call_avp_subdissector(vendorid, code, subtvb, c->pinfo, avp_tree);

	if (pad_len) {
		guint8 i;

		pi = proto_tree_add_item(avp_tree, hf_diameter_avp_pad, tvb, offset, pad_len, ENC_NA);
		for (i=0; i < pad_len; i++) {
			if (tvb_get_guint8(tvb, offset++) != 0) {
				expert_add_info_format(c->pinfo, pi, PI_MALFORMED, PI_NOTE, "Padding is non-zero");
				break;
			}
		}
	}

	return len+pad_len;
}

static const char *
address_rfc_avp(diam_ctx_t *c, diam_avp_t *a, tvbuff_t *tvb)
{
	char *label = (char *)ep_alloc(ITEM_LABEL_LENGTH+1);
	address_avp_t *t = (address_avp_t *)a->type_data;
	proto_item *pi = proto_tree_add_item(c->tree,a->hf_value,tvb,0,tvb_length(tvb),ENC_BIG_ENDIAN);
	proto_tree *pt = proto_item_add_subtree(pi,t->ett);
	guint32 addr_type = tvb_get_ntohs(tvb,0);
	gint len = tvb_length_remaining(tvb,2);

	proto_tree_add_item(pt,t->hf_address_type,tvb,0,2,ENC_NA);
	switch (addr_type ) {
		case 1:
			if (len != 4) {
				pi = proto_tree_add_text(pt,tvb,2,len,"Wrong length for IPv4 Address: %d instead of 4",len);
				expert_add_info_format(c->pinfo, pi, PI_MALFORMED, PI_WARN, "Wrong length for IPv4 Address");
				return "[Malformed]";
			}
			pi = proto_tree_add_item(pt,t->hf_ipv4,tvb,2,4,ENC_BIG_ENDIAN);
			break;
		case 2:
			if (len != 16) {
				pi = proto_tree_add_text(pt,tvb,2,len,"Wrong length for IPv6 Address: %d instead of 16",len);
				expert_add_info_format(c->pinfo, pi, PI_MALFORMED, PI_WARN, "Wrong length for IPv6 Address");
				return "[Malformed]";
			}
			pi = proto_tree_add_item(pt,t->hf_ipv6,tvb,2,16,ENC_NA);
			break;
		default:
			pi = proto_tree_add_item(pt,t->hf_other,tvb,2,-1,ENC_BIG_ENDIAN);
			break;
	}

	proto_item_fill_label(PITEM_FINFO(pi), label);
	label = strstr(label,": ")+2;
	return label;
}

static const char *
proto_avp(diam_ctx_t *c, diam_avp_t *a, tvbuff_t *tvb)
{
	proto_avp_t *t = (proto_avp_t *)a->type_data;

	col_set_writable(c->pinfo->cinfo, FALSE);

	if (!t->handle) {
		t->handle = find_dissector(t->name);
		if(!t->handle) t->handle = data_handle;
	}

	TRY {
	call_dissector(t->handle, tvb, c->pinfo, c->tree);
	}
	CATCH_NONFATAL_ERRORS {
		show_exception(tvb, c->pinfo, c->tree, EXCEPT_CODE, GET_MESSAGE);
	}
	ENDTRY;

	return "";
}

static const char *
time_avp(diam_ctx_t *c, diam_avp_t *a, tvbuff_t *tvb)
{
	int len = tvb_length(tvb);
	char *label = (char *)ep_alloc(ITEM_LABEL_LENGTH+1);
	proto_item *pi;

	if ( len != 4 ) {
		pi = proto_tree_add_text(c->tree, tvb, 0, 4, "Error! AVP value MUST be 4 bytes");
		expert_add_info_format(c->pinfo, pi, PI_MALFORMED, PI_NOTE,
				       "Bad Timestamp Length (%u)", len);
		return "[Malformed]";
	}

	pi = proto_tree_add_item(c->tree, (a->hf_value), tvb, 0, 4, ENC_TIME_NTP|ENC_BIG_ENDIAN);
	proto_item_fill_label(PITEM_FINFO(pi), label);
	label = strstr(label,": ")+2;
	return label;
}

static const char *
address_v16_avp(diam_ctx_t *c, diam_avp_t *a, tvbuff_t *tvb)
{
	char *label = (char *)ep_alloc(ITEM_LABEL_LENGTH+1);
	address_avp_t *t = (address_avp_t *)a->type_data;
	proto_item *pi = proto_tree_add_item(c->tree,a->hf_value,tvb,0,tvb_length(tvb),ENC_BIG_ENDIAN);
	proto_tree *pt = proto_item_add_subtree(pi,t->ett);
	guint32 len = tvb_length(tvb);

	switch (len) {
		case 4:
			pi = proto_tree_add_item(pt,t->hf_ipv4,tvb,0,4,ENC_BIG_ENDIAN);
			break;
		case 16:
			pi = proto_tree_add_item(pt,t->hf_ipv6,tvb,0,16,ENC_NA);
			break;
		default:
			pi = proto_tree_add_item(pt,t->hf_other,tvb,0,len,ENC_BIG_ENDIAN);
			expert_add_info_format(c->pinfo, pi, PI_MALFORMED, PI_NOTE,
					       "Bad Address Length (%u)", len);

			break;
	}

	proto_item_fill_label(PITEM_FINFO(pi), label);
	label = strstr(label,": ")+2;
	return label;
}

static const char *
simple_avp(diam_ctx_t *c, diam_avp_t *a, tvbuff_t *tvb)
{
	char *label = (char *)ep_alloc(ITEM_LABEL_LENGTH+1);
	proto_item *pi = proto_tree_add_item(c->tree,a->hf_value,tvb,0,tvb_length(tvb),ENC_BIG_ENDIAN);
	proto_item_fill_label(PITEM_FINFO(pi), label);
	label = strstr(label,": ")+2;
	return label;
}

static const char *
utf8_avp(diam_ctx_t *c, diam_avp_t *a, tvbuff_t *tvb)
{
	char *label = (char *)ep_alloc(ITEM_LABEL_LENGTH+1);
	proto_item *pi = proto_tree_add_item(c->tree,a->hf_value,tvb,0,tvb_length(tvb),ENC_UTF_8|ENC_BIG_ENDIAN);
	proto_item_fill_label(PITEM_FINFO(pi), label);
	label = strstr(label,": ")+2;
	return label;
}

static const char *
integer32_avp(diam_ctx_t *c, diam_avp_t *a, tvbuff_t *tvb)
{
	char *label;
	proto_item *pi;

	/* Verify length before adding */
	gint length = tvb_length_remaining(tvb,0);
	if (length == 4) {
		pi= proto_tree_add_item(c->tree,a->hf_value,tvb,0,tvb_length_remaining(tvb,0),ENC_BIG_ENDIAN);
		label = (char *)ep_alloc(ITEM_LABEL_LENGTH+1);
		proto_item_fill_label(PITEM_FINFO(pi), label);
		label = strstr(label,": ")+2;
	}
	else {
		pi = proto_tree_add_bytes_format(c->tree, hf_diameter_avp_data_wrong_length,
						 tvb, 0, length, NULL,
						"Error!  Bad Integer32 Length");
		expert_add_info_format(c->pinfo, pi, PI_MALFORMED, PI_NOTE,
				       "Bad Integer32 Length (%u)", length);
		PROTO_ITEM_SET_GENERATED(pi);
		label = NULL;
	}
	return label;
}

static const char *
integer64_avp(diam_ctx_t *c, diam_avp_t *a, tvbuff_t *tvb)
{
	char *label;
	proto_item *pi;

	/* Verify length before adding */
	gint length = tvb_length_remaining(tvb,0);
	if (length == 8) {
		pi= proto_tree_add_item(c->tree,a->hf_value,tvb,0,tvb_length_remaining(tvb,0),ENC_BIG_ENDIAN);
		label = (char *)ep_alloc(ITEM_LABEL_LENGTH+1);
		proto_item_fill_label(PITEM_FINFO(pi), label);
		label = strstr(label,": ")+2;
	}
	else {
		pi = proto_tree_add_bytes_format(c->tree, hf_diameter_avp_data_wrong_length,
						 tvb, 0, length, NULL,
						"Error!  Bad Integer64 Length");
		expert_add_info_format(c->pinfo, pi, PI_MALFORMED, PI_NOTE,
				       "Bad Integer64 Length (%u)", length);
		PROTO_ITEM_SET_GENERATED(pi);
		label = NULL;
	}
	return label;
}

static const char *
unsigned32_avp(diam_ctx_t *c, diam_avp_t *a, tvbuff_t *tvb)
{
	char *label;
	proto_item *pi;

	/* Verify length before adding */
	gint length = tvb_length_remaining(tvb,0);
	if (length == 4) {
		pi= proto_tree_add_item(c->tree,a->hf_value,tvb,0,tvb_length_remaining(tvb,0),ENC_BIG_ENDIAN);
		label = (char *)ep_alloc(ITEM_LABEL_LENGTH+1);
		proto_item_fill_label(PITEM_FINFO(pi), label);
		label = strstr(label,": ")+2;
	}
	else {
		pi = proto_tree_add_bytes_format(c->tree, hf_diameter_avp_data_wrong_length,
						 tvb, 0, length, NULL,
						"Error!  Bad Unsigned32 Length");
		expert_add_info_format(c->pinfo, pi, PI_MALFORMED, PI_NOTE,
				       "Bad Unsigned32 Length (%u)", length);
		PROTO_ITEM_SET_GENERATED(pi);
		label = NULL;
	}
	return label;
}

static const char *
unsigned64_avp(diam_ctx_t *c, diam_avp_t *a, tvbuff_t *tvb)
{
	char *label;
	proto_item *pi;

	/* Verify length before adding */
	gint length = tvb_length_remaining(tvb,0);
	if (length == 8) {
		pi= proto_tree_add_item(c->tree,a->hf_value,tvb,0,tvb_length_remaining(tvb,0),ENC_BIG_ENDIAN);
		label = (char *)ep_alloc(ITEM_LABEL_LENGTH+1);
		proto_item_fill_label(PITEM_FINFO(pi), label);
		label = strstr(label,": ")+2;
	}
	else {
		pi = proto_tree_add_bytes_format(c->tree, hf_diameter_avp_data_wrong_length,
						 tvb, 0, length, NULL,
						"Error!  Bad Unsigned64 Length");
		expert_add_info_format(c->pinfo, pi, PI_MALFORMED, PI_NOTE,
				       "Bad Unsigned64 Length (%u)", length);
		PROTO_ITEM_SET_GENERATED(pi);
		label = NULL;
	}
	return label;
}

static const char *
float32_avp(diam_ctx_t *c, diam_avp_t *a, tvbuff_t *tvb)
{
	char *label;
	proto_item *pi;

	/* Verify length before adding */
	gint length = tvb_length_remaining(tvb,0);
	if (length == 4) {
		pi= proto_tree_add_item(c->tree,a->hf_value,tvb,0,tvb_length_remaining(tvb,0),ENC_BIG_ENDIAN);
		label = (char *)ep_alloc(ITEM_LABEL_LENGTH+1);
		proto_item_fill_label(PITEM_FINFO(pi), label);
		label = strstr(label,": ")+2;
	}
	else {
		pi = proto_tree_add_bytes_format(c->tree, hf_diameter_avp_data_wrong_length,
						 tvb, 0, length, NULL,
						"Error!  Bad Float32 Length");
		expert_add_info_format(c->pinfo, pi, PI_MALFORMED, PI_NOTE,
				       "Bad Float32 Length (%u)", length);
		PROTO_ITEM_SET_GENERATED(pi);
		label = NULL;
	}
	return label;
}

static const char *
float64_avp(diam_ctx_t *c, diam_avp_t *a, tvbuff_t *tvb)
{
	char *label;
	proto_item *pi;

	/* Verify length before adding */
	gint length = tvb_length_remaining(tvb,0);
	if (length == 8) {
		pi= proto_tree_add_item(c->tree,a->hf_value,tvb,0,tvb_length_remaining(tvb,0),ENC_BIG_ENDIAN);
		label = (char *)ep_alloc(ITEM_LABEL_LENGTH+1);
		proto_item_fill_label(PITEM_FINFO(pi), label);
		label = strstr(label,": ")+2;
	}
	else {
		pi = proto_tree_add_bytes_format(c->tree, hf_diameter_avp_data_wrong_length,
						 tvb, 0, length, NULL,
						"Error!  Bad Float64 Length");
		expert_add_info_format(c->pinfo, pi, PI_MALFORMED, PI_NOTE,
				       "Bad Float64 Length (%u)", length);
		PROTO_ITEM_SET_GENERATED(pi);
		label = NULL;
	}
	return label;
}

static const char *
grouped_avp(diam_ctx_t *c, diam_avp_t *a, tvbuff_t *tvb)
{
	int offset = 0;
	int len = tvb_length(tvb);
	proto_item *pi = proto_tree_add_item(c->tree, a->hf_value, tvb , 0 , -1, ENC_BIG_ENDIAN);
	proto_tree *pt = c->tree;

	c->tree = proto_item_add_subtree(pi,a->ett);

	while (offset < len) {
		offset += dissect_diameter_avp(c, tvb, offset);
	}

	c->tree = pt;

	return NULL;
}

static const char *msgflags_str[] = {
	"----", "---T", "--E-", "--ET",
	"-P--", "-P-T", "-PE-", "-PET",
	"R---", "R--T", "R-E-", "R-ET",
	"RP--", "RP-T", "RPE-", "RPET"
};

static void
dissect_diameter_common(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree)
{
	guint32 first_word  = tvb_get_ntohl(tvb,0);
	guint32 version = (first_word & 0xff000000) >> 24;
	guint32 flags_bits = (tvb_get_ntohl(tvb,4) & 0xff000000) >> 24;
	int packet_len = first_word & 0x00ffffff;
	proto_item *pi, *cmd_item, *app_item, *version_item;
	proto_tree *diam_tree;
	diam_ctx_t *c = (diam_ctx_t *)ep_alloc0(sizeof(diam_ctx_t));
	int offset;
	value_string *cmd_vs;
	const char *cmd_str;
	guint32 cmd = tvb_get_ntoh24(tvb,5);
	guint32 fourth = tvb_get_ntohl(tvb,8);
	guint32 hop_by_hop_id, end_to_end_id;
	conversation_t *conversation;
	diameter_conv_info_t *diameter_conv_info;
	diameter_req_ans_pair_t *diameter_pair = NULL;
	emem_tree_t *pdus_tree;
	proto_item *it;
	nstime_t ns;
	void *pd_save;

	pd_save = pinfo->private_data;
	col_set_str(pinfo->cinfo, COL_PROTOCOL, "DIAMETER");

	pi = proto_tree_add_item(tree,proto_diameter,tvb,0,-1,ENC_NA);
	diam_tree = proto_item_add_subtree(pi,ett_diameter);

	c->tree = diam_tree;
	c->pinfo = pinfo;

	version_item = proto_tree_add_item(diam_tree,hf_diameter_version,tvb,0,1,ENC_BIG_ENDIAN);
	proto_tree_add_item(diam_tree,hf_diameter_length,tvb,1,3,ENC_BIG_ENDIAN);

	pi = proto_tree_add_item(diam_tree,hf_diameter_flags,tvb,4,1,ENC_BIG_ENDIAN);
	{
		proto_tree *pt = proto_item_add_subtree(pi,ett_diameter_flags);
		proto_tree_add_item(pt,hf_diameter_flags_request,tvb,4,1,ENC_BIG_ENDIAN);
		proto_tree_add_item(pt,hf_diameter_flags_proxyable,tvb,4,1,ENC_BIG_ENDIAN);
		proto_tree_add_item(pt,hf_diameter_flags_error,tvb,4,1,ENC_BIG_ENDIAN);
		proto_tree_add_item(pt,hf_diameter_flags_T,tvb,4,1,ENC_BIG_ENDIAN);
		proto_tree_add_item(pt,hf_diameter_flags_reserved4,tvb,4,1,ENC_BIG_ENDIAN);
		if(flags_bits & 0x08) proto_item_set_expert_flags(pi, PI_MALFORMED, PI_WARN);
		pi = proto_tree_add_item(pt,hf_diameter_flags_reserved5,tvb,4,1,ENC_BIG_ENDIAN);
		if(flags_bits & 0x04) proto_item_set_expert_flags(pi, PI_MALFORMED, PI_WARN);
		pi = proto_tree_add_item(pt,hf_diameter_flags_reserved6,tvb,4,1,ENC_BIG_ENDIAN);
		if(flags_bits & 0x02) proto_item_set_expert_flags(pi, PI_MALFORMED, PI_WARN);
		pi = proto_tree_add_item(pt,hf_diameter_flags_reserved7,tvb,4,1,ENC_BIG_ENDIAN);
		if(flags_bits & 0x01) proto_item_set_expert_flags(pi, PI_MALFORMED, PI_WARN);
	}

	cmd_item = proto_tree_add_item(diam_tree,hf_diameter_code,tvb,5,3,ENC_BIG_ENDIAN);

	switch (version) {
		case DIAMETER_V16: {
			guint32 vendorid = tvb_get_ntohl(tvb,8);
			diam_vnd_t *vendor;

			if (! ( vendor = (diam_vnd_t *)emem_tree_lookup32(dictionary.vnds,vendorid) ) ) {
				vendor = &unknown_vendor;
			}

			cmd_vs = VND_CMD_VS(vendor);
			proto_tree_add_item(diam_tree, hf_diameter_vendor_id,tvb,8,4,ENC_BIG_ENDIAN);

			c->version_rfc = FALSE;
			break;
		}
		case DIAMETER_RFC: {
			/* Store the application id to be used by subdissectors */
			pinfo->private_data = &fourth;

			cmd_vs = (value_string *)(void *)all_cmds->data;

			app_item = proto_tree_add_item(diam_tree, hf_diameter_application_id, tvb, 8, 4, ENC_BIG_ENDIAN);
			if (try_val_to_str(fourth, dictionary.applications) == NULL) {
				proto_tree *tu = proto_item_add_subtree(app_item,ett_unknown);
				proto_item *iu = proto_tree_add_text(tu, tvb, 8, 4, "Unknown Application Id, "
				                                     "if you know what this is you can add it to dictionary.xml");
				expert_add_info_format(c->pinfo, iu, PI_UNDECODED, PI_WARN,
				                       "Unknown Application Id (%u)", fourth);
				PROTO_ITEM_SET_GENERATED(iu);
			}

			c->version_rfc = TRUE;
			break;
		}
		default:
		{
			proto_tree *pt = proto_item_add_subtree(version_item,ett_err);
			proto_item *pi_local = proto_tree_add_text(pt,tvb,0,1,"Unknown Diameter Version (decoding as RFC 3588)");
			expert_add_info_format(pinfo, pi_local, PI_UNDECODED, PI_WARN, "Unknown Diameter Version");
			PROTO_ITEM_SET_GENERATED(pi);
			c->version_rfc = TRUE;
			cmd_vs = VND_CMD_VS(&no_vnd);
			break;
		}
	}
	cmd_str = val_to_str_const(cmd, cmd_vs, "Unknown");

	col_add_fstr(pinfo->cinfo, COL_INFO,
			 "cmd=%s%s(%d) flags=%s %s=%s(%d) h2h=%x e2e=%x",
			 cmd_str,
			 ((flags_bits>>4)&0x08) ? "Request" : "Answer",
			 cmd,
			 msgflags_str[((flags_bits>>4)&0x0f)],
			 c->version_rfc ? "appl" : "vend",
			 val_to_str_const(fourth, c->version_rfc ? dictionary.applications : vnd_short_vs, "Unknown"),
			 fourth,
			 tvb_get_ntohl(tvb,12),
			 tvb_get_ntohl(tvb,16));

	/* Append name to command item, warn if unknown */
	proto_item_append_text(cmd_item," %s", cmd_str);
	if (strcmp(cmd_str, "Unknown") == 0) {
		proto_tree *tu = proto_item_add_subtree(cmd_item,ett_unknown);
		proto_item *iu = proto_tree_add_text(tu,tvb, 5 ,3,"Unknown command, "
		                                     "if you know what this is you can add it to dictionary.xml");
		expert_add_info_format(c->pinfo, iu, PI_UNDECODED, PI_WARN, "Unknown command (%u)", cmd);
		PROTO_ITEM_SET_GENERATED(iu);
	}


	hop_by_hop_id = tvb_get_ntohl(tvb, 12);
	proto_tree_add_item(diam_tree,hf_diameter_hopbyhopid,tvb,12,4,ENC_BIG_ENDIAN);
	end_to_end_id = tvb_get_ntohl(tvb, 16);
	proto_tree_add_item(diam_tree,hf_diameter_endtoendid,tvb,16,4,ENC_BIG_ENDIAN);

	/* Conversation tracking stuff */
	/*
	 * FIXME: Looking at epan/conversation.c it seems unlike that this will work properly in
	 * multi-homed SCTP connections. This will probably need to be fixed at some point.
	 */

	conversation = find_or_create_conversation(pinfo);

	diameter_conv_info = (diameter_conv_info_t *)conversation_get_proto_data(conversation, proto_diameter);
	if (!diameter_conv_info) {
		diameter_conv_info = (diameter_conv_info_t *)se_alloc(sizeof(diameter_conv_info_t));
		diameter_conv_info->pdus_tree = se_tree_create_non_persistent(
					EMEM_TREE_TYPE_RED_BLACK, "diameter_pdu_trees");

		conversation_add_proto_data(conversation, proto_diameter, diameter_conv_info);
	}

	/* pdus_tree is an se_tree keyed by frame number (in order to handle hop-by-hop collisions */
	pdus_tree = (emem_tree_t *)se_tree_lookup32(diameter_conv_info->pdus_tree, hop_by_hop_id);

	if (pdus_tree == NULL && (flags_bits & DIAM_FLAGS_R)) {
		/* This is the first request we've seen with this hop-by-hop id */
		pdus_tree = se_tree_create_non_persistent(EMEM_TREE_TYPE_RED_BLACK, "diameter_pdus");
		se_tree_insert32(diameter_conv_info->pdus_tree, hop_by_hop_id, pdus_tree);
	}

	if (pdus_tree) {
		if (!pinfo->fd->flags.visited) {
			if (flags_bits & DIAM_FLAGS_R) {
				/* This is a request */
				diameter_pair = (diameter_req_ans_pair_t *)se_alloc(sizeof(diameter_req_ans_pair_t));
				diameter_pair->hop_by_hop_id = hop_by_hop_id;
				diameter_pair->end_to_end_id = end_to_end_id;
				diameter_pair->cmd_code = cmd;
				diameter_pair->result_code = 0;
				diameter_pair->cmd_str = cmd_str;
				diameter_pair->req_frame = PINFO_FD_NUM(pinfo);
				diameter_pair->ans_frame = 0;
				diameter_pair->req_time = pinfo->fd->abs_ts;
				se_tree_insert32(pdus_tree, PINFO_FD_NUM(pinfo), (void *)diameter_pair);
			} else {
				/* Look for a request which occurs earlier in the trace than this answer. */
				diameter_pair = (diameter_req_ans_pair_t *)se_tree_lookup32_le(pdus_tree, PINFO_FD_NUM(pinfo));

				/* Verify the end-to-end-id matches before declaring a match */
				if (diameter_pair && diameter_pair->end_to_end_id == end_to_end_id) {
					diameter_pair->ans_frame = PINFO_FD_NUM(pinfo);
				}
			}
		} else {
			/* Look for a request which occurs earlier in the trace than this answer. */
			diameter_pair = (diameter_req_ans_pair_t *)se_tree_lookup32_le(pdus_tree, PINFO_FD_NUM(pinfo));

			/* If the end-to-end ID doesn't match then this is not the request we were
			 * looking for.
			 */
			if (diameter_pair && diameter_pair->end_to_end_id != end_to_end_id)
				diameter_pair = NULL;
		}
	}

	if (!diameter_pair) {
		/* create a "fake" diameter_pair structure */
		diameter_pair = (diameter_req_ans_pair_t *)ep_alloc(sizeof(diameter_req_ans_pair_t));
		diameter_pair->hop_by_hop_id = hop_by_hop_id;
		diameter_pair->cmd_code = cmd;
		diameter_pair->result_code = 0;
		diameter_pair->cmd_str = cmd_str;
		diameter_pair->req_frame = 0;
		diameter_pair->ans_frame = 0;
		diameter_pair->req_time = pinfo->fd->abs_ts;
	}
	diameter_pair->processing_request=(flags_bits & DIAM_FLAGS_R)!=0;

	if (!tree) return;

	/* print state tracking info in the tree */
	if (flags_bits & DIAM_FLAGS_R) {
		/* This is a request */
		if (diameter_pair->ans_frame) {
			it = proto_tree_add_uint(diam_tree, hf_diameter_answer_in,
					tvb, 0, 0, diameter_pair->ans_frame);
			PROTO_ITEM_SET_GENERATED(it);
		}
	} else {
		/* This is an answer */
		if (diameter_pair->req_frame) {
			it = proto_tree_add_uint(diam_tree, hf_diameter_answer_to,
					tvb, 0, 0, diameter_pair->req_frame);
			PROTO_ITEM_SET_GENERATED(it);

			nstime_delta(&ns, &pinfo->fd->abs_ts, &diameter_pair->req_time);
			diameter_pair->srt_time = ns;
			it = proto_tree_add_time(diam_tree, hf_diameter_answer_time, tvb, 0, 0, &ns);
			PROTO_ITEM_SET_GENERATED(it);
			/* TODO: Populate result_code in tap record from AVP 268 */
		}
	}

	offset = 20;

	/* Dissect AVPs until the end of the packet is reached */
	while (offset < packet_len) {
		offset += dissect_diameter_avp(c, tvb, offset);
	}

	/* Handle requests for which no answers were found and
	 * anawers for which no requests were found in the tap listener.
	 * In case if you don't need unpaired requests/answers use:
	 * if(diameter_pair->processing_request || !diameter_pair->req_frame)
	 *   return;
	 */
	tap_queue_packet(diameter_tap, pinfo, diameter_pair);

	pinfo->private_data = pd_save;
}

static guint
get_diameter_pdu_len(packet_info *pinfo _U_, tvbuff_t *tvb, int offset)
{
	/* Get the length of the Diameter packet. */
	return tvb_get_ntoh24(tvb, offset + 1);
}

static gboolean
check_diameter(tvbuff_t *tvb)
{
	if (tvb_length(tvb) < 1)
		return FALSE;   /* not enough bytes to check the version */

	if (tvb_get_guint8(tvb, 0) != 1)
		return FALSE;   /* not version 1 */

		  /*
		   * XXX - fetch length and make sure it's at least MIN_DIAMETER_SIZE?
		   * Fetch flags and check that none of the DIAM_FLAGS_RESERVED bits
		   * are set?
		   */
	return TRUE;
}

/************************************************/
/* Main dissection function                     */
static int
dissect_diameter(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree, void *data _U_)
{
	if (!check_diameter(tvb))
		return 0;
	dissect_diameter_common(tvb, pinfo, tree);
	return tvb_length(tvb);
}

static void
dissect_diameter_tcp(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree)
{
	/* Check if we have the start of a PDU or if this is segment */
	if (!check_diameter(tvb)) {
		col_set_str(pinfo->cinfo, COL_PROTOCOL, "DIAMETER");
		col_set_str(pinfo->cinfo, COL_INFO, "Continuation");
		call_dissector(data_handle, tvb, pinfo, tree);
	} else {
		tcp_dissect_pdus(tvb, pinfo, tree, gbl_diameter_desegment, 4,
				 get_diameter_pdu_len, dissect_diameter_common);
	}
}


static char *
alnumerize(char *name)
{
	char *r = name;
	char *w = name;
	char c;

	for (;(c = *r); r++) {
		if (isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.') {
			*(w++) = c;
		}
	}

	*w = '\0';

	return name;
}


static guint
reginfo(int *hf_ptr, const char *name, const char *abbr, const char *desc,
	enum ftenum ft, base_display_e base, value_string_ext *vs_ext,
	guint32 mask)
{
	hf_register_info hf = { hf_ptr, {
				name ? wmem_strdup(wmem_epan_scope(), name) : wmem_strdup(wmem_epan_scope(), abbr),
				wmem_strdup(wmem_epan_scope(), abbr),
				ft,
				base,
				NULL,
				mask,
				wmem_strdup(wmem_epan_scope(), desc),
				HFILL }};

	if(vs_ext) {
		hf.hfinfo.strings = vs_ext;
	}

	g_array_append_vals(build_dict.hf,&hf,1);
	return build_dict.hf->len - 1;
}

static void
basic_avp_reginfo(diam_avp_t *a, const char *name, enum ftenum ft,
		  base_display_e base, value_string_ext *vs_ext)
{
	hf_register_info hf[] = { { &(a->hf_value),
				  { NULL, NULL, ft, base, NULL, 0x0,
				  a->vendor->code ?
				  wmem_strdup_printf(wmem_epan_scope(), "vendor=%d code=%d", a->vendor->code, a->code)
				  : wmem_strdup_printf(wmem_epan_scope(), "code=%d", a->code),
				  HFILL }}
	};
	gint *ettp = &(a->ett);

	hf->hfinfo.name = wmem_strdup_printf(wmem_epan_scope(), "%s",name);
	hf->hfinfo.abbrev = alnumerize(wmem_strdup_printf(wmem_epan_scope(), "diameter.%s",name));
	if(vs_ext) {
		hf->hfinfo.strings = vs_ext;
	}

	g_array_append_vals(build_dict.hf,hf,1);
	g_ptr_array_add(build_dict.ett,ettp);
}

static diam_avp_t *
build_address_avp(const avp_type_t *type _U_, guint32 code,
		  const diam_vnd_t *vendor, const char *name,
		  const value_string *vs _U_, void *data _U_)
{
	diam_avp_t *a = (diam_avp_t *)g_malloc0(sizeof(diam_avp_t));
	address_avp_t *t = (address_avp_t *)g_malloc(sizeof(address_avp_t));
	gint *ettp = &(t->ett);

	a->code = code;
	a->vendor = vendor;
/*
 * It seems like the radius AVPs 1-255 will use the defs from RADIUS in which case:
 * http://www.ietf.org/rfc/rfc2865.txt?number=2865
 * Address
 *    The Address field is four octets.  The value 0xFFFFFFFF indicates
 *    that the NAS Should allow the user to select an address (e.g.
 *    Negotiated).  The value 0xFFFFFFFE indicates that the NAS should
 *    select an address for the user (e.g. Assigned from a pool of
 *    addresses kept by the NAS).  Other valid values indicate that the
 *    NAS should use that value as the user's IP address.
 *
 * Where as in Diameter:
 * RFC3588
 * Address
 *    The Address format is derived from the OctetString AVP Base
 *    Format.  It is a discriminated union, representing, for example a
 *    32-bit (IPv4) [IPV4] or 128-bit (IPv6) [IPV6] address, most
 *    significant octet first.  The first two octets of the Address
 *    AVP represents the AddressType, which contains an Address Family
 *    defined in [IANAADFAM].  The AddressType is used to discriminate
 *    the content and format of the remaining octets.
 */
	a->dissector_v16 = address_v16_avp;
	if (code<256) {
		a->dissector_rfc = address_v16_avp;
	} else {
		a->dissector_rfc = address_rfc_avp;
	}
	a->ett = -1;
	a->hf_value = -1;
	a->type_data = t;

	t->ett = -1;
	t->hf_address_type = -1;
	t->hf_ipv4 = -1;
	t->hf_ipv6 = -1;
	t->hf_other = -1;

	basic_avp_reginfo(a,name,FT_BYTES,BASE_NONE,NULL);

	reginfo(&(t->hf_address_type), ep_strdup_printf("%s Address Family",name),
		alnumerize(ep_strdup_printf("diameter.%s.addr_family",name)),
		NULL, FT_UINT16, (base_display_e)(BASE_DEC|BASE_EXT_STRING), &diameter_avp_data_addrfamily_vals_ext, 0);

	reginfo(&(t->hf_ipv4), ep_strdup_printf("%s Address",name),
		alnumerize(ep_strdup_printf("diameter.%s.IPv4",name)),
		NULL, FT_IPv4, BASE_NONE, NULL, 0);

	reginfo(&(t->hf_ipv6), ep_strdup_printf("%s Address",name),
		alnumerize(ep_strdup_printf("diameter.%s.IPv6",name)),
		NULL, FT_IPv6, BASE_NONE, NULL, 0);

	reginfo(&(t->hf_other), ep_strdup_printf("%s Address",name),
		alnumerize(ep_strdup_printf("diameter.%s.Bytes",name)),
		NULL, FT_BYTES, BASE_NONE, NULL, 0);

	g_ptr_array_add(build_dict.ett,ettp);

	return a;
}

static diam_avp_t *
build_proto_avp(const avp_type_t *type _U_, guint32 code,
		const diam_vnd_t *vendor, const char *name _U_,
		const value_string *vs _U_, void *data)
{
	diam_avp_t *a = (diam_avp_t *)g_malloc0(sizeof(diam_avp_t));
	proto_avp_t *t = (proto_avp_t *)g_malloc0(sizeof(proto_avp_t));
	gint *ettp = &(a->ett);

	a->code = code;
	a->vendor = vendor;
	a->dissector_v16 = proto_avp;
	a->dissector_rfc = proto_avp;
	a->ett = -1;
	a->hf_value = -2;
	a->type_data = t;

	t->name = (char *)data;
	t->handle = NULL;
	t->reassemble_mode = REASEMBLE_NEVER;

	g_ptr_array_add(build_dict.ett,ettp);

	return a;
}

static diam_avp_t *
build_simple_avp(const avp_type_t *type, guint32 code, const diam_vnd_t *vendor,
		 const char *name, const value_string *vs, void *data _U_)
{
	diam_avp_t *a;
	value_string_ext *vs_ext = NULL;
	base_display_e base;
	guint i = 0;

	/*
	 * Only 32-bit or shorter integral types can have a list of values.
	 */
	base = (base_display_e)type->base;
	if (vs != NULL) {
		switch (type->ft) {

		case FT_UINT8:
		case FT_UINT16:
		case FT_UINT32:
		case FT_INT8:
		case FT_INT16:
		case FT_INT32:
			break;

		default:
			fprintf(stderr,"Diameter Dictionary: AVP '%s' has a list of values but isn't of a 32-bit or shorter integral type\n",
				name);
			return NULL;
		}
		while (vs[i].strptr) {
		  i++;
		}
		vs_ext = value_string_ext_new((value_string *)vs, i+1, wmem_strdup_printf(wmem_epan_scope(), "%s_vals_ext",name));
		base = (base_display_e)(base|BASE_EXT_STRING);
	}

	a = (diam_avp_t *)wmem_alloc0(wmem_epan_scope(), sizeof(diam_avp_t));
	a->code = code;
	a->vendor = vendor;
	a->dissector_v16 = type->v16;
	a->dissector_rfc = type->rfc;
	a->ett = -1;
	a->hf_value = -1;

	basic_avp_reginfo(a,name,type->ft,base,vs_ext);

	return a;
}



static const avp_type_t basic_types[] = {
	{"octetstring"		, simple_avp		, simple_avp	, FT_BYTES		, BASE_NONE		, build_simple_avp  },
	{"utf8string"		, utf8_avp		, utf8_avp	, FT_STRING		, BASE_NONE		, build_simple_avp  },
	{"grouped"		, grouped_avp		, grouped_avp	, FT_BYTES		, BASE_NONE		, build_simple_avp  },
	{"integer32"		, integer32_avp		, integer32_avp	, FT_INT32		, BASE_DEC		, build_simple_avp  },
	{"unsigned32"		, unsigned32_avp	, unsigned32_avp, FT_UINT32		, BASE_DEC		, build_simple_avp  },
	{"integer64"		, integer64_avp		, integer64_avp	, FT_INT64		, BASE_DEC		, build_simple_avp  },
	{"unsigned64"		, unsigned64_avp	, unsigned64_avp, FT_UINT64		, BASE_DEC		, build_simple_avp  },
	{"float32"		, float32_avp		, float32_avp	, FT_FLOAT		, BASE_NONE		, build_simple_avp  },
	{"float64"		, float64_avp		, float64_avp	, FT_DOUBLE		, BASE_NONE		, build_simple_avp  },
	{"ipaddress"		, NULL			, NULL		, FT_NONE		, BASE_NONE		, build_address_avp },
	{"diameteruri"		, utf8_avp		, utf8_avp	, FT_STRING		, BASE_NONE		, build_simple_avp  },
	{"diameteridentity"	, utf8_avp		, utf8_avp	, FT_STRING		, BASE_NONE		, build_simple_avp  },
	{"ipfilterrule"		, utf8_avp		, utf8_avp	, FT_STRING		, BASE_NONE		, build_simple_avp  },
	{"qosfilterrule"	, utf8_avp		, utf8_avp	, FT_STRING		, BASE_NONE		, build_simple_avp  },
	{"time"			, time_avp		, time_avp	, FT_ABSOLUTE_TIME	, ABSOLUTE_TIME_UTC	, build_simple_avp  },
	{NULL, NULL, NULL, FT_NONE, BASE_NONE, NULL }
};



/*
 * This is like g_str_hash() (as of GLib 2.4.8), but it maps all
 * upper-case ASCII characters to their ASCII lower-case equivalents.
 * We can't use g_strdown(), as that doesn't do an ASCII mapping;
 * in Turkish locales, for example, there are two lower-case "i"s
 * and two upper-case "I"s, with and without dots - the ones with
 * dots map between each other, as do the ones without dots, so "I"
 * doesn't map to "i".
 */
static guint
strcase_hash(gconstpointer key)
{
	const char *p = (const char *)key;
	guint h = *p;
	char c;

	if (h) {
		if (h >= 'A' && h <= 'Z')
			h = h - 'A' + 'a';
		for (p += 1; *p != '\0'; p++) {
			c = *p;
			if (c >= 'A' && c <= 'Z')
				c = c - 'A' + 'a';
			h = (h << 5) - h + c;
		}
	}

	return h;
}

/*
 * Again, use g_ascii_strcasecmp(), not strcasecmp(), so that only ASCII
 * letters are mapped, and they're mapped to the lower-case ASCII
 * equivalents.
 */
static gboolean
strcase_equal(gconstpointer ka, gconstpointer kb)
{
	const char *a = (const char *)ka;
	const char *b = (const char *)kb;
	return g_ascii_strcasecmp(a,b) == 0;
}


/* Note: Dynamic "value string arrays" (e.g., vs_cmds, vs_avps, ...) are constructed using */
/*       "zero-terminated" GArrays so that they will have the same form as standard        */
/*       value_string arrays created at compile time. Since the last entry in a            */
/*       value_string array must be {0, NULL}, we are assuming that NULL == 0 (hackish).   */

static int
dictionary_load(void)
{
	ddict_t *d;
	ddict_application_t *p;
	ddict_vendor_t *v;
	ddict_cmd_t *c;
	ddict_typedefn_t *t;
	ddict_avp_t *a;
	gboolean do_debug_parser = getenv("WIRESHARK_DEBUG_DIAM_DICT_PARSER") ? TRUE : FALSE;
	gboolean do_dump_dict = getenv("WIRESHARK_DUMP_DIAM_DICT") ? TRUE : FALSE;
	char *dir = ep_strdup_printf("%s" G_DIR_SEPARATOR_S "diameter" G_DIR_SEPARATOR_S, get_datafile_dir());
	const avp_type_t *type;
	const avp_type_t *octetstring = &basic_types[0];
	diam_avp_t *avp;
	GHashTable *vendors = g_hash_table_new(strcase_hash,strcase_equal);
	diam_vnd_t *vnd;
	GArray *vnd_shrt_arr = g_array_new(TRUE,TRUE,sizeof(value_string));

	build_dict.hf = g_array_new(FALSE,TRUE,sizeof(hf_register_info));
	build_dict.ett = g_ptr_array_new();
	build_dict.types = g_hash_table_new(strcase_hash,strcase_equal);
	build_dict.avps = g_hash_table_new(strcase_hash,strcase_equal);

	dictionary.vnds = pe_tree_create(EMEM_TREE_TYPE_RED_BLACK,"diameter_vnds");
	dictionary.avps = pe_tree_create(EMEM_TREE_TYPE_RED_BLACK,"diameter_avps");

	unknown_vendor.vs_cmds = g_array_new(TRUE,TRUE,sizeof(value_string));
	unknown_vendor.vs_avps = g_array_new(TRUE,TRUE,sizeof(value_string));
	no_vnd.vs_cmds = g_array_new(TRUE,TRUE,sizeof(value_string));
	no_vnd.vs_avps = g_array_new(TRUE,TRUE,sizeof(value_string));

	all_cmds = g_array_new(TRUE,TRUE,sizeof(value_string));

	pe_tree_insert32(dictionary.vnds,0,&no_vnd);
	g_hash_table_insert(vendors,(gchar *)"None",&no_vnd);

	/* initialize the types hash with the known basic types */
	for (type = basic_types; type->name; type++) {
		g_hash_table_insert(build_dict.types,(gchar *)type->name,(void *)type);
	}

	/* load the dictionary */
	d = ddict_scan(dir,"dictionary.xml",do_debug_parser);
	if (d == NULL) {
		return 0;
	}

	if (do_dump_dict) ddict_print(stdout, d);

	/* populate the types */
	for (t = d->typedefns; t; t = t->next) {
		const avp_type_t *parent = NULL;
		/* try to get the parent type */

		if (t->name == NULL) {
			fprintf(stderr,"Diameter Dictionary: Invalid Type (empty name): parent==%s\n",
				t->parent ? t->parent : "(null)");
			continue;
		}


		if (g_hash_table_lookup(build_dict.types,t->name))
			continue;

		if (t->parent) {
			parent = (avp_type_t *)g_hash_table_lookup(build_dict.types,t->parent);
		}

		if (!parent) parent = octetstring;

		/* insert the parent type for this type */
		g_hash_table_insert(build_dict.types,t->name,(void *)parent);
	}

	/* populate the applications */
	if ((p = d->applications)) {
		GArray *arr = g_array_new(TRUE,TRUE,sizeof(value_string));

		for (; p; p = p->next) {
			value_string item = {p->code,p->name};
			g_array_append_val(arr,item);
		}

		dictionary.applications = (value_string *)arr->data;
		g_array_free(arr,FALSE);
	}

	if ((v = d->vendors)) {
		for ( ; v; v = v->next) {
			value_string item = {v->code,v->name};

			if (v->name == NULL) {
				fprintf(stderr,"Diameter Dictionary: Invalid Vendor (empty name): code==%d\n",v->code);
				continue;
			}

			if (g_hash_table_lookup(vendors,v->name))
				continue;

			g_array_append_val(vnd_shrt_arr,item);

			vnd = (diam_vnd_t *)g_malloc(sizeof(diam_vnd_t));
			vnd->code = v->code;
			vnd->vs_cmds = g_array_new(TRUE,TRUE,sizeof(value_string));
			vnd->vs_avps = g_array_new(TRUE,TRUE,sizeof(value_string));
			vnd->vs_avps_ext = NULL;
			pe_tree_insert32(dictionary.vnds,vnd->code,vnd);
			g_hash_table_insert(vendors,v->name,vnd);
		}
	}

	vnd_short_vs = (value_string *)vnd_shrt_arr->data;
	g_array_free(vnd_shrt_arr,FALSE);

	if ((c = d->cmds)) {
		for (; c; c = c->next) {
			if (c->vendor == NULL) {
				fprintf(stderr,"Diameter Dictionary: Invalid Vendor (empty name) for command %s\n",
					c->name ? c->name : "(null)");
				continue;
			}

			if ((vnd = (diam_vnd_t *)g_hash_table_lookup(vendors,c->vendor))) {
				value_string item = {c->code,c->name};
				g_array_append_val(vnd->vs_cmds,item);
				/* Also add to all_cmds as used by RFC version */
				g_array_append_val(all_cmds,item);
			} else {
				fprintf(stderr,"Diameter Dictionary: No Vendor: %s\n",c->vendor);
			}
		}
	}


	for (a = d->avps; a; a = a->next) {
		ddict_enum_t *e;
		value_string *vs = NULL;
		const char *vend = a->vendor ? a->vendor : "None";
		ddict_xmlpi_t *x;
		void *avp_data = NULL;

		if (a->name == NULL) {
			fprintf(stderr,"Diameter Dictionary: Invalid AVP (empty name)\n");
			continue;
		}

		if ((vnd = (diam_vnd_t *)g_hash_table_lookup(vendors,vend))) {
			value_string vndvs = {a->code,a->name};
			g_array_append_val(vnd->vs_avps,vndvs);
		} else {
			fprintf(stderr,"Diameter Dictionary: No Vendor: %s\n",vend);
			vnd = &unknown_vendor;
		}

		if ((e = a->enums)) {
			GArray *arr = g_array_new(TRUE,TRUE,sizeof(value_string));

			for (; e; e = e->next) {
				value_string item = {e->code,e->name};
				g_array_append_val(arr,item);
			}
			g_array_sort(arr, compare_avps);
			vs = (value_string *)arr->data;
		}

		type = NULL;

		for( x = d->xmlpis; x; x = x->next ) {
			if ( (strcase_equal(x->name,"avp-proto") && strcase_equal(x->key,a->name))
				 || (a->type && strcase_equal(x->name,"type-proto") && strcase_equal(x->key,a->type))
				 ) {
				static avp_type_t proto_type = {"proto", proto_avp, proto_avp, FT_UINT32, BASE_HEX, build_proto_avp};
				type =  &proto_type;

				avp_data = x->value;
				break;
			}
		}

		if ( (!type) && a->type )
			type = (avp_type_t *)g_hash_table_lookup(build_dict.types,a->type);

		if (!type) type = octetstring;

		avp = type->build( type, a->code, vnd, a->name, vs, avp_data);
		if (avp != NULL) {
			g_hash_table_insert(build_dict.avps, a->name, avp);

			{
				emem_tree_key_t k[] = {
					{ 1, &(a->code) },
					{ 1, &(vnd->code) },
					{ 0 , NULL }
				};
				pe_tree_insert32_array(dictionary.avps,k,avp);
			}
		}
	}
	g_hash_table_destroy(build_dict.types);
	g_hash_table_destroy(build_dict.avps);
	g_hash_table_destroy(vendors);

	return 1;
}

static void
tcp_range_delete_callback(guint32 port)
{
	dissector_delete_uint("tcp.port", port, diameter_tcp_handle);
}

static void
tcp_range_add_callback(guint32 port)
{
	dissector_add_uint("tcp.port", port, diameter_tcp_handle);
}

static void
sctp_range_delete_callback(guint32 port)
{
	dissector_delete_uint("sctp.port", port, diameter_sctp_handle);
}

static void
sctp_range_add_callback(guint32 port)
{
	dissector_add_uint("sctp.port", port, diameter_sctp_handle);
}

/* registration with the filtering engine */
void proto_reg_handoff_diameter(void);

/*
 * This does most of the registration work; see proto_register_diameter()
 * for the reason why we split it off.
 */
static void
real_proto_register_diameter(void)
{
	module_t *diameter_module;
	guint i, ett_length;

	hf_register_info hf_base[] = {
	{ &hf_diameter_version,
		  { "Version", "diameter.version", FT_UINT8, BASE_HEX, NULL, 0x00,
			  NULL, HFILL }},
	{ &hf_diameter_length,
		  { "Length","diameter.length", FT_UINT24, BASE_DEC, NULL, 0x0,
			  NULL, HFILL }},
	{ &hf_diameter_flags,
		  { "Flags", "diameter.flags", FT_UINT8, BASE_HEX, NULL, 0x0,
			  NULL, HFILL }},
	{ &hf_diameter_flags_request,
		  { "Request", "diameter.flags.request", FT_BOOLEAN, 8, TFS(&tfs_set_notset), DIAM_FLAGS_R,
			  NULL, HFILL }},
	{ &hf_diameter_flags_proxyable,
		  { "Proxyable", "diameter.flags.proxyable", FT_BOOLEAN, 8, TFS(&tfs_set_notset), DIAM_FLAGS_P,
			  NULL, HFILL }},
	{ &hf_diameter_flags_error,
		  { "Error","diameter.flags.error", FT_BOOLEAN, 8, TFS(&tfs_set_notset), DIAM_FLAGS_E,
			  NULL, HFILL }},
	{ &hf_diameter_flags_T,
		  { "T(Potentially re-transmitted message)","diameter.flags.T", FT_BOOLEAN, 8, TFS(&tfs_set_notset), DIAM_FLAGS_T,
			  NULL, HFILL }},
	{ &hf_diameter_flags_reserved4,
		  { "Reserved","diameter.flags.reserved4", FT_BOOLEAN, 8, TFS(&tfs_set_notset),
			  DIAM_FLAGS_RESERVED4, NULL, HFILL }},
	{ &hf_diameter_flags_reserved5,
		  { "Reserved","diameter.flags.reserved5", FT_BOOLEAN, 8, TFS(&tfs_set_notset),
			  DIAM_FLAGS_RESERVED5, NULL, HFILL }},
	{ &hf_diameter_flags_reserved6,
		  { "Reserved","diameter.flags.reserved6", FT_BOOLEAN, 8, TFS(&tfs_set_notset),
			  DIAM_FLAGS_RESERVED6, NULL, HFILL }},
	{ &hf_diameter_flags_reserved7,
		  { "Reserved","diameter.flags.reserved7", FT_BOOLEAN, 8, TFS(&tfs_set_notset),
			  DIAM_FLAGS_RESERVED7, NULL, HFILL }},
	{ &hf_diameter_vendor_id,
		  { "VendorId",	"diameter.vendorId", FT_UINT32, BASE_DEC|BASE_EXT_STRING, &sminmpec_values_ext,
			  0x0, NULL, HFILL }},
	{ &hf_diameter_application_id,
		  { "ApplicationId", "diameter.applicationId", FT_UINT32, BASE_DEC, VALS(dictionary.applications),
			  0x0, NULL, HFILL }},
	{ &hf_diameter_hopbyhopid,
		  { "Hop-by-Hop Identifier", "diameter.hopbyhopid", FT_UINT32,
			  BASE_HEX, NULL, 0x0, NULL, HFILL }},
	{ &hf_diameter_endtoendid,
		  { "End-to-End Identifier", "diameter.endtoendid", FT_UINT32,
			  BASE_HEX, NULL, 0x0, NULL, HFILL }},
	{ &hf_diameter_avp,
		  { "AVP","diameter.avp", FT_BYTES, BASE_NONE,
			  NULL, 0x0, NULL, HFILL }},
	{ &hf_diameter_avp_len,
		  { "AVP Length","diameter.avp.len", FT_UINT24, BASE_DEC,
			  NULL, 0x0, NULL, HFILL }},
	{ &hf_diameter_avp_code,
		  { "AVP Code", "diameter.avp.code", FT_UINT32, BASE_DEC, NULL, 0, NULL, HFILL }},
	{ &hf_diameter_avp_flags,
		  { "AVP Flags","diameter.avp.flags", FT_UINT8, BASE_HEX,
			  NULL, 0x0, NULL, HFILL }},
	{ &hf_diameter_avp_flags_vendor_specific,
		  { "Vendor-Specific", "diameter.flags.vendorspecific", FT_BOOLEAN, 8, TFS(&tfs_set_notset), AVP_FLAGS_V,
			  NULL, HFILL }},
	{ &hf_diameter_avp_flags_mandatory,
		  { "Mandatory", "diameter.flags.mandatory", FT_BOOLEAN, 8, TFS(&tfs_set_notset), AVP_FLAGS_M,
			  NULL, HFILL }},
	{ &hf_diameter_avp_flags_protected,
		  { "Protected","diameter.avp.flags.protected", FT_BOOLEAN, 8, TFS(&tfs_set_notset), AVP_FLAGS_P,
			  NULL, HFILL }},
	{ &hf_diameter_avp_flags_reserved3,
		  { "Reserved","diameter.avp.flags.reserved3", FT_BOOLEAN, 8, TFS(&tfs_set_notset),
			  AVP_FLAGS_RESERVED3,	NULL, HFILL }},
	{ &hf_diameter_avp_flags_reserved4,
		  { "Reserved","diameter.avp.flags.reserved4", FT_BOOLEAN, 8, TFS(&tfs_set_notset),
			  AVP_FLAGS_RESERVED4,	NULL, HFILL }},
	{ &hf_diameter_avp_flags_reserved5,
		  { "Reserved","diameter.avp.flags.reserved5", FT_BOOLEAN, 8, TFS(&tfs_set_notset),
			  AVP_FLAGS_RESERVED5,	NULL, HFILL }},
	{ &hf_diameter_avp_flags_reserved6,
		  { "Reserved","diameter.avp.flags.reserved6", FT_BOOLEAN, 8, TFS(&tfs_set_notset),
			  AVP_FLAGS_RESERVED6,	NULL, HFILL }},
	{ &hf_diameter_avp_flags_reserved7,
		  { "Reserved","diameter.avp.flags.reserved7", FT_BOOLEAN, 8, TFS(&tfs_set_notset),
			  AVP_FLAGS_RESERVED7,	NULL, HFILL }},
	{ &hf_diameter_avp_vendor_id,
		  { "AVP Vendor Id","diameter.avp.vendorId", FT_UINT32, BASE_DEC|BASE_EXT_STRING,
			  &sminmpec_values_ext, 0x0, NULL, HFILL }},
	{ &(unknown_avp.hf_value),
		  { "Value","diameter.avp.unknown", FT_BYTES, BASE_NONE, NULL, 0x0, NULL, HFILL }},
	{ &hf_diameter_avp_data_wrong_length,
		  { "Data","diameter.avp.invalid-data", FT_BYTES, BASE_NONE, NULL, 0x0, NULL, HFILL }},
	{ &hf_diameter_avp_pad,
		  { "Padding","diameter.avp.pad", FT_BYTES, BASE_NONE, NULL, 0x0, NULL, HFILL }},
	{ &hf_diameter_code,
		  { "Command Code", "diameter.cmd.code", FT_UINT32, BASE_DEC, NULL, 0, NULL, HFILL }},
	{ &hf_diameter_answer_in,
		{ "Answer In", "diameter.answer_in", FT_FRAMENUM, BASE_NONE, NULL, 0x0,
		"The answer to this diameter request is in this frame", HFILL }},
	{ &hf_diameter_answer_to,
		{ "Request In", "diameter.answer_to", FT_FRAMENUM, BASE_NONE, NULL, 0x0,
		"This is an answer to the diameter request in this frame", HFILL }},
	{ &hf_diameter_answer_time,
		{ "Response Time", "diameter.resp_time", FT_RELATIVE_TIME, BASE_NONE, NULL, 0x0,
		"The time between the request and the answer", HFILL }},
	{ &hf_framed_ipv6_prefix_reserved,
	    { "Framed IPv6 Prefix Reserved byte", "diameter.framed_ipv6_prefix_reserved",
	    FT_UINT8, BASE_HEX, NULL, 0, NULL, HFILL }},
	{ &hf_framed_ipv6_prefix_length,
	    { "Framed IPv6 Prefix length (in bits)", "diameter.framed_ipv6_prefix_length",
	    FT_UINT8, BASE_DEC, NULL, 0, NULL, HFILL }},
	{ &hf_framed_ipv6_prefix_bytes,
	    { "Framed IPv6 Prefix as a bytestring", "diameter.framed_ipv6_prefix_bytes",
	    FT_BYTES, BASE_NONE, NULL, 0, NULL, HFILL }},
	{ &hf_framed_ipv6_prefix_ipv6,
	    { "Framed IPv6 Prefix as an IPv6 address", "diameter.framed_ipv6_prefix_ipv6",
	    FT_IPv6, BASE_NONE, NULL, 0, "This field is present only if the prefix length is 128", HFILL }}
	};

	gint *ett_base[] = {
		&ett_diameter,
		&ett_diameter_flags,
		&ett_diameter_avp_flags,
		&ett_diameter_avpinfo,
		&ett_unknown,
		&ett_err,
		&(unknown_avp.ett)
	};

	g_array_append_vals(build_dict.hf, hf_base, array_length(hf_base));
	ett_length = array_length(ett_base);
	for (i = 0; i < ett_length; i++) {
		g_ptr_array_add(build_dict.ett, ett_base[i]);
	}

	proto_diameter = proto_register_protocol ("Diameter Protocol", "DIAMETER", "diameter");

	proto_register_field_array(proto_diameter, (hf_register_info *)(void *)build_dict.hf->data, build_dict.hf->len);
	proto_register_subtree_array((gint **)build_dict.ett->pdata, build_dict.ett->len);

	g_array_free(build_dict.hf,FALSE);
	g_ptr_array_free(build_dict.ett,TRUE);

	/* Allow dissector to find be found by name. */
	new_register_dissector("diameter", dissect_diameter, proto_diameter);

	/* Register dissector table(s) to do sub dissection of AVPs (OctetStrings) */
	diameter_dissector_table = register_dissector_table("diameter.base", "DIAMETER_BASE_AVPS", FT_UINT32, BASE_DEC);
	diameter_3gpp_avp_dissector_table = register_dissector_table("diameter.3gpp", "DIAMETER_3GPP_AVPS", FT_UINT32, BASE_DEC);
	diameter_ericsson_avp_dissector_table = register_dissector_table("diameter.ericsson", "DIAMETER_ERICSSON_AVPS", FT_UINT32, BASE_DEC);

	/* Set default TCP ports */
	range_convert_str(&global_diameter_tcp_port_range, DEFAULT_DIAMETER_PORT_RANGE, MAX_UDP_PORT);
	range_convert_str(&global_diameter_sctp_port_range, DEFAULT_DIAMETER_PORT_RANGE, MAX_SCTP_PORT);

	/* Register configuration options for ports */
	diameter_module = prefs_register_protocol(proto_diameter,
						  proto_reg_handoff_diameter);

	prefs_register_range_preference(diameter_module, "tcp.ports", "Diameter TCP ports",
					"TCP ports to be decoded as Diameter (default: "
					DEFAULT_DIAMETER_PORT_RANGE ")",
					&global_diameter_tcp_port_range, MAX_UDP_PORT);

	prefs_register_range_preference(diameter_module, "sctp.ports",
					"Diameter SCTP Ports",
					"SCTP ports to be decoded as Diameter (default: "
					DEFAULT_DIAMETER_PORT_RANGE ")",
					&global_diameter_sctp_port_range, MAX_SCTP_PORT);

	/* Desegmentation */
	prefs_register_bool_preference(diameter_module, "desegment",
				       "Reassemble Diameter messages\nspanning multiple TCP segments",
				       "Whether the Diameter dissector should reassemble messages spanning multiple TCP segments."
				       " To use this option, you must also enable \"Allow subdissectors to reassemble TCP streams\" in the TCP protocol settings.",
				       &gbl_diameter_desegment);

	/*  Register some preferences we no longer support, so we can report
	 *  them as obsolete rather than just illegal.
	 */
	prefs_register_obsolete_preference(diameter_module, "version");
	prefs_register_obsolete_preference(diameter_module, "udp.port");
	prefs_register_obsolete_preference(diameter_module, "tcp.port");
	prefs_register_obsolete_preference(diameter_module, "sctp.port");
	prefs_register_obsolete_preference(diameter_module, "command_in_header");
	prefs_register_obsolete_preference(diameter_module, "dictionary.name");
	prefs_register_obsolete_preference(diameter_module, "dictionary.use");
	prefs_register_obsolete_preference(diameter_module, "allow_zero_as_app_id");
	prefs_register_obsolete_preference(diameter_module, "suppress_console_output");

	/* Register tap */
	diameter_tap = register_tap("diameter");
}

void
proto_register_diameter(void)
{
	/*
	 * The hf_base[] array for Diameter refers to a variable
	 * that is set by dictionary_load(), so we need to call
	 * dictionary_load() before hf_base[] is initialized.
	 *
	 * To ensure that, we call dictionary_load() and then
	 * call a routine that defines hf_base[] and does all
	 * the registration work.
	 */
	dictionary_load();
	real_proto_register_diameter();
} /* proto_register_diameter */

void
proto_reg_handoff_diameter(void)
{
	static gboolean Initialized=FALSE;
	static range_t *diameter_tcp_port_range;
	static range_t *diameter_sctp_port_range;

	if (!Initialized) {
		diameter_sctp_handle = find_dissector("diameter");
		diameter_tcp_handle = create_dissector_handle(dissect_diameter_tcp,
							      proto_diameter);
		data_handle = find_dissector("data");
		eap_handle = find_dissector("eap");

		dissector_add_uint("sctp.ppi", DIAMETER_PROTOCOL_ID, diameter_sctp_handle);

		/* Register special decoding for some AVPs */
		/* AVP Code: 97 Framed-IPv6-Address */
		dissector_add_uint("diameter.base", 97,
			new_create_dissector_handle(dissect_diameter_base_framed_ipv6_prefix, proto_diameter));
		/* AVP Code: 266 Vendor-Id */
		dissector_add_uint("diameter.base", 266,
			new_create_dissector_handle(dissect_diameter_vendor_id, proto_diameter));
		/* AVP Code: 462 EAP-Payload */
		dissector_add_uint("diameter.base", 462,
			new_create_dissector_handle(dissect_diameter_eap_payload, proto_diameter));
		/* AVP Code: 463 EAP-Reissued-Payload */
		dissector_add_uint("diameter.base", 463,
			new_create_dissector_handle(dissect_diameter_eap_payload, proto_diameter));

		Initialized=TRUE;
	} else {
		range_foreach(diameter_tcp_port_range, tcp_range_delete_callback);
		range_foreach(diameter_sctp_port_range, sctp_range_delete_callback);
		g_free(diameter_tcp_port_range);
		g_free(diameter_sctp_port_range);
	}

	/* set port for future deletes */
	diameter_tcp_port_range = range_copy(global_diameter_tcp_port_range);
	diameter_sctp_port_range = range_copy(global_diameter_sctp_port_range);
	range_foreach(diameter_tcp_port_range, tcp_range_add_callback);
	range_foreach(diameter_sctp_port_range, sctp_range_add_callback);
}

