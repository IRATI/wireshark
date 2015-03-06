/* packet-lldp.h
 * Routines for LLDP dissection
 * By Juan Gonzalez <juan.gonzalez@pikatech.com>
 * Copyright 2005 MITEL
 *
 * July 2005
 * Modified by: Brian Bogora <brian_bogora@mitel.com>
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
#ifndef PACKET_LLDP_H__
#define PACKET_LLDP_H__

#include "oui.h"

static const value_string tlv_oui_subtype_vals[] = {
	/* Currently, the manuf file calls this "Ieee8021"; "IEEE 802.1" looks better */
	{ OUI_IEEE_802_1,	"IEEE 802.1" },
	/* Currently, the manuf file calls this "Ieee8023"; "IEEE 802.3" looks better */
	{ OUI_IEEE_802_3,	"IEEE 802.3" },
	/* Currently, the manuf file calls this "Telecomm"; "TIA TR-41 Committee" looks better */
	{ OUI_MEDIA_ENDPOINT,	"TIA TR-41 Committee" },
	/* Currently, the manuf file calls this "Profibus" */
	{ OUI_PROFINET,         "PROFINET" },
	/* Currently, the manuf file calls this "Procurve", as it's assigned to HP! */
	{ OUI_IEEE_802_1QBG,	"IEEE 802.1Qbg" },
	{ 0, NULL }
};

/* TLV Types */
#define END_OF_LLDPDU_TLV_TYPE			0x00	/* Mandatory */
#define CHASSIS_ID_TLV_TYPE				0x01	/* Mandatory */
#define PORT_ID_TLV_TYPE				0x02	/* Mandatory */
#define TIME_TO_LIVE_TLV_TYPE			0x03	/* Mandatory */
#define PORT_DESCRIPTION_TLV_TYPE		0x04
#define SYSTEM_NAME_TLV_TYPE			0x05
#define SYSTEM_DESCRIPTION_TLV_TYPE		0x06
#define SYSTEM_CAPABILITIES_TLV_TYPE	0x07
#define MANAGEMENT_ADDR_TLV_TYPE		0x08
#define ORGANIZATION_SPECIFIC_TLV_TYPE	0x7F

/* Masks */
#define TLV_TYPE_MASK		0xFE00
#define TLV_TYPE(value)		(((value) & TLV_TYPE_MASK) >> 9)
#define TLV_INFO_LEN_MASK	0x01FF
#define TLV_INFO_LEN(value)	((value) & TLV_INFO_LEN_MASK)

/* IEEE 802.1Qbg Subtypes */
static const value_string ieee_802_1qbg_subtypes[] = {
	{ 0x00,	"EVB" },
	{ 0x01,	"CDCP" },
	{ 0x02,	"VDP" },
	{ 0, NULL }
};

gint32 dissect_lldp_end_of_lldpdu(tvbuff_t *, packet_info *, proto_tree *, guint32);

#endif /* PACKET_LLDP_H__ */
