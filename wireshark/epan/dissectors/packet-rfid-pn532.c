/* packet-rfid-pn532.c
 * Dissector for the NXP PN532 Protocol
 *
 * References:
 * http://www.nxp.com/documents/user_manual/141520.pdf
 *
 * Copyright 2012, Tyson Key <tyson.key@gmail.com>
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
 *
 */

#include "config.h"

#include <glib.h>
#include <epan/packet.h>
#include <epan/prefs.h>

static int proto_pn532 = -1;

/* Device-specific HFs */
static int hf_pn532_command = -1;
static int hf_pn532_direction = -1;
static int hf_pn532_MaxTg = -1;
static int hf_pn532_Tg = -1;
static int hf_pn532_NbTg = -1;
static int hf_pn532_BrTy = -1;
static int hf_pn532_error = -1;
static int hf_pn532_payload_length = -1;
static int hf_pn532_ic_version = -1;
static int hf_pn532_fw_version = -1;
static int hf_pn532_fw_revision = -1;
static int hf_pn532_fw_support = -1;

/* Card type-specific HFs */
static int hf_pn532_14443a_sak = -1;
static int hf_pn532_14443a_atqa = -1;
static int hf_pn532_14443a_uid = -1;
static int hf_pn532_14443a_uid_length = -1;
static int hf_pn532_14443a_ats = -1;
static int hf_pn532_14443b_pupi = -1;
static int hf_pn532_14443b_app_data = -1;
static int hf_pn532_14443b_proto_info = -1;

/* SAM Mode */
static int hf_pn532_sam_mode = -1;

/* Diagnose hardware status */
#define DIAGNOSE_REQ               0x00
#define DIAGNOSE_RSP               0x01

/* Get Firmware Version */
#define GET_FIRMWARE_VERSION_REQ   0x02
#define GET_FIRMWARE_VERSION_RSP   0x03

#define GET_GENERAL_STATUS         0x04

/* Read from a chipset register */
#define READ_REGISTER_REQ          0x06
#define READ_REGISTER_RSP          0x07

/* Write Register */
#define WRITE_REGISTER_REQ         0x08
#define WRITE_REGISTER_RSP         0x09

#define READ_GPIO                  0x0C
#define WRITE_GPIO                 0x0E
#define SET_SERIAL_BAUD_RATE       0x10
#define SET_PARAMETERS_REQ         0x12
#define SET_PARAMETERS_RSP         0x13
#define SAM_CONFIGURATION_REQ      0x14
#define SAM_CONFIGURATION_RSP      0x15
#define POWER_DOWN                 0x16

/* RF Communication Commands */
#define RF_CONFIGURATION_REQ       0x32
#define RF_CONFIGURATION_RSP       0x33

#define RF_REGULATION_TEST         0x58

/* - Initiator Commands - */
#define IN_JUMP_FOR_PSL            0x46
#define IN_JUMP_FOR_DEP            0x56

/* List targets (tags) in the field */
#define IN_LIST_PASSIVE_TARGET_REQ 0x4A
#define IN_LIST_PASSIVE_TARGET_RSP 0x4B

#define IN_ATR                     0x50
#define IN_PSL                     0x4E

/* Data Exchange */
#define IN_DATA_EXCHANGE_REQ       0x40
#define IN_DATA_EXCHANGE_RSP       0x41

/* Communicate through */
#define IN_COMMUNICATE_THRU_REQ    0x42
#define IN_COMMUNICATE_THRU_RSP    0x43

/* Deselect target token */
#define IN_DESELECT_REQ            0x44
#define IN_DESELECT_RSP            0x45

/* Release target token */
#define IN_RELEASE_REQ             0x52
#define IN_RELEASE_RSP             0x53

/* Select target token */
#define IN_SELECT_REQ              0x54
#define IN_SELECT_RSP              0x55

/* Auto/long-time polling*/
#define IN_AUTO_POLL_REQ           0x60
#define IN_AUTO_POLL_RSP           0x61

/* Target Commands */
#define TG_GET_DATA                0x86
#define TG_GET_INITIATOR_CMD       0x88
#define TG_GET_TARGET_STATUS       0x8A
#define TG_INIT_AS_TARGET          0x8C
#define TG_SET_DATA                0x8E
#define TG_RESP_TO_INITIATOR       0x90
#define TG_SET_GENERAL_BYTES       0x92
#define TG_SET_METADATA            0x94


/* TFI (Frame Identifier) Directions */
#define HOST_TO_PN532              0xD4
#define PN532_TO_HOST              0xD5

/* Baud rate and modulation types */
#define ISO_IEC_14443A_106         0x00
#define FELICA_212                 0x01
#define FELICA_424                 0x02
#define ISO_IEC_14443B_106         0x03
#define JEWEL_14443A_106           0x04

/* Error codes */
#define NO_ERROR                   0x00
#define UNACCEPTABLE_CMD           0x27

/* SAM Modes */
#define SAM_NORMAL_MODE            0x01
#define SAM_VIRTUAL_CARD           0x02
#define SAM_WIRED_CARD             0x03
#define SAM_DUAL_CARD              0x04 

/* Table of payload types - adapted from the I2C dissector */
enum {
    SUB_DATA = 0,
    SUB_FELICA,
    SUB_MIFARE,
    SUB_ISO7816,
    
    SUB_MAX
};

static dissector_handle_t sub_handles[SUB_MAX];
static gint sub_selected = SUB_DATA;

/* XXX: re-arranged from defs above to be in ascending order by value */
static const value_string pn532_commands[] = {
    {DIAGNOSE_REQ,               "Diagnose"},
    {DIAGNOSE_RSP,               "Diagnose (Response)"},

    /* Discover the device's firmware version */
    {GET_FIRMWARE_VERSION_REQ,   "GetFirmwareVersion"},
    {GET_FIRMWARE_VERSION_RSP,   "GetFirmwareVersion (Response)"},

    {GET_GENERAL_STATUS,         "GetGeneralStatus"},

    /* Read from a chipset register */
    {READ_REGISTER_REQ,          "ReadRegister"},
    {READ_REGISTER_RSP,          "ReadRegister (Response)"},

    /* Write to a chipset register */
    {WRITE_REGISTER_REQ,         "WriteRegister"},
    {WRITE_REGISTER_RSP,         "WriteRegister (Response)"},

    {READ_GPIO,                  "ReadGPIO"},
    {WRITE_GPIO,                 "WriteGPIO"},
    {SET_SERIAL_BAUD_RATE,       "SetSerialBaudRate"},
    
    /* Set Parameters */
    {SET_PARAMETERS_REQ,         "SetParameters"},
    {SET_PARAMETERS_RSP,         "SetParameters (Response)"},
    
    /* Secure Application Module Configuration */
    {SAM_CONFIGURATION_REQ,          "SAMConfiguration"},
    {SAM_CONFIGURATION_RSP,          "SAMConfiguration (Response)"},
    
    {POWER_DOWN,                 "PowerDown"},

    /* RF Configuration */
    {RF_CONFIGURATION_REQ,       "RFConfiguration"},
    {RF_CONFIGURATION_RSP,       "RFConfiguration (Response)"},

    /* Data Exchange */
    {IN_DATA_EXCHANGE_REQ,       "InDataExchange"},
    {IN_DATA_EXCHANGE_RSP,       "InDataExchange (Response)"},

    /* Communicate through */
    {IN_COMMUNICATE_THRU_REQ,    "InCommunicateThru"},
    {IN_COMMUNICATE_THRU_RSP,    "InCommunicateThru (Response)"},

    /* Deselect the target token */
    {IN_DESELECT_REQ,            "InDeselect"},
    {IN_DESELECT_RSP,            "InDeselect (Response)"},

    /* - Initiator Commands - */
    {IN_JUMP_FOR_PSL,            "InJumpForPSL"},

    /* List tags in the proximity of the reader's field */
    {IN_LIST_PASSIVE_TARGET_REQ, "InListPassiveTarget"},
    {IN_LIST_PASSIVE_TARGET_RSP, "InListPassiveTarget (Response)"},

    {IN_PSL,                     "InPSL"},
    {IN_ATR,                     "InATR"},

    /* Release the target token */
    {IN_RELEASE_REQ,             "InRelease"},
    {IN_RELEASE_RSP,             "InRelease (Response)"},

    /* Select target token */
    {IN_SELECT_REQ,              "InSelect"},
    {IN_SELECT_RSP,              "InSelect (Response)"},

    /* - Initiator Commands - */
    {IN_JUMP_FOR_DEP,            "InJumpForDEP"},

    /* RF Communication Commands */
    {RF_REGULATION_TEST,         "RFRegulationTest"},

    /* Automatic/long-time polling */
    {IN_AUTO_POLL_REQ,           "InAutoPoll"},
    {IN_AUTO_POLL_RSP,           "InAutoPoll (Response)"},

    /* Target Commands */
    {TG_GET_DATA,                "TgGetData"},
    {TG_GET_INITIATOR_CMD,       "TgGetInitiatorCommand"},
    {TG_GET_TARGET_STATUS,       "TgGetTargetStatus"},
    {TG_INIT_AS_TARGET,          "TgInitAsTarget"},
    {TG_SET_DATA,                "TgSetData"},
    {TG_RESP_TO_INITIATOR,       "TgResponseToInitiator"},
    {TG_SET_GENERAL_BYTES,       "TgSetGeneralBytes"},
    {TG_SET_METADATA,            "TgSetMetaData"},

    /* End of commands */
    {0x00, NULL}
};
static value_string_ext pn532_commands_ext = VALUE_STRING_EXT_INIT(pn532_commands);

/* TFI - 1 byte frame identifier; specifying direction of communication */
static const value_string pn532_directions[] = {
    {HOST_TO_PN532,             "Host to PN532"},
    {PN532_TO_HOST,             "PN532 to Host"},

    /* End of directions */
    {0x00, NULL}
};

/* Error/status codes */
static const value_string pn532_errors[] = {
    {NO_ERROR,          "No Error"},
    {UNACCEPTABLE_CMD,  "Unacceptable Command"},

    /* End of errors */
    {0x00, NULL}
};

/* Baud rates and modulation types */
static const value_string pn532_brtypes[] = {
    {ISO_IEC_14443A_106,        "ISO/IEC 14443-A at 106 kbps"},
    {FELICA_212,                "FeliCa at 212 kbps"},
    {FELICA_424,                "FeliCa at 424 kbps"},
    {ISO_IEC_14443B_106,        "ISO/IEC 14443-B at 106 kbps"},
    {JEWEL_14443A_106,          "InnoVision Jewel/Topaz at 106 kbps"},

    /* End of directions */
    {0x00, NULL}
};

/* SAM Modes */
static const value_string pn532_sam_modes[] = {
    {SAM_NORMAL_MODE,             "Normal Mode"},
    {SAM_VIRTUAL_CARD,            "Virtual Card Mode"},
    {SAM_WIRED_CARD,              "Wired Card Mode"},
    {SAM_DUAL_CARD,               "Dual Card Mode"},

    /* End of SAM modes */
    {0x00, NULL}
};

static dissector_table_t pn532_dissector_table;

/* Subtree handles: set by register_subtree_array */
static gint ett_pn532 = -1;

static void
dissect_pn532(tvbuff_t * tvb, packet_info * pinfo, proto_tree *tree)
{
    proto_item *item;
    proto_tree *pn532_tree;
    guint8      cmd;
    tvbuff_t   *next_tvb;

    col_set_str(pinfo->cinfo, COL_PROTOCOL, "PN532");
    col_set_str(pinfo->cinfo, COL_INFO, "PN532 Packet");

    /* Start with a top-level item to add everything else to */
    item = proto_tree_add_item(tree, proto_pn532, tvb, 0, -1, ENC_NA);
    pn532_tree = proto_item_add_subtree(item, ett_pn532);

    proto_tree_add_item(pn532_tree, hf_pn532_direction, tvb, 0, 1, ENC_NA);
    proto_tree_add_item(pn532_tree, hf_pn532_command, tvb, 1, 1, ENC_NA);

    /* Direction byte */
    cmd = tvb_get_guint8(tvb, 1);

    col_set_str(pinfo->cinfo, COL_INFO, val_to_str_ext_const(cmd, &pn532_commands_ext, "Unknown"));

    switch (cmd) {

    /* Device Diagnosis Request */
    case DIAGNOSE_REQ:
        break;

    /* Device Diagnosis Response */
    case DIAGNOSE_RSP:
        break;

    /* Device Firmware Version Request */
    case GET_FIRMWARE_VERSION_REQ:
        break;

    /* Device Firmware Version Response */
    case GET_FIRMWARE_VERSION_RSP:
        proto_tree_add_item(pn532_tree, hf_pn532_ic_version,  tvb, 2, 1, ENC_NA);
        proto_tree_add_item(pn532_tree, hf_pn532_fw_version,  tvb, 3, 1, ENC_NA);
        proto_tree_add_item(pn532_tree, hf_pn532_fw_revision, tvb, 4, 1, ENC_NA);
        proto_tree_add_item(pn532_tree, hf_pn532_fw_support,  tvb, 5, 1, ENC_NA);
        break;

    case GET_GENERAL_STATUS:
        break;

    case READ_REGISTER_REQ:
        break;

    case READ_REGISTER_RSP:
        break;

    case WRITE_REGISTER_REQ:
        break;

    case WRITE_REGISTER_RSP:
        break;

    case READ_GPIO:
        break;

    case WRITE_GPIO:
        break;

    case SET_SERIAL_BAUD_RATE:
        break;

    case SET_PARAMETERS_REQ:
        break;

    case SET_PARAMETERS_RSP:
        break;

    /* Secure Application/Security Access Module Configuration Request */
    case SAM_CONFIGURATION_REQ:
        /* Mode */
        proto_tree_add_item(pn532_tree, hf_pn532_sam_mode, tvb, 2, 1, ENC_BIG_ENDIAN);
        
        /* Timeout */
        
        /* IRQ */
        break;
        
    case SAM_CONFIGURATION_RSP:
        break;
        
    case POWER_DOWN:
        break;

    case RF_CONFIGURATION_REQ:
        break;

    case RF_CONFIGURATION_RSP:
        break;

    case RF_REGULATION_TEST:
        break;

    case IN_JUMP_FOR_DEP:
        break;

    case IN_JUMP_FOR_PSL:
        break;

        /* List targets (tags) in the field */
    case IN_LIST_PASSIVE_TARGET_REQ:

        /* Maximum number of supported tags */
        proto_tree_add_item(pn532_tree, hf_pn532_MaxTg, tvb, 2, 1, ENC_BIG_ENDIAN);

        /* Modulation and Baud Rate Type */
        proto_tree_add_item(pn532_tree, hf_pn532_BrTy, tvb, 3, 1, ENC_BIG_ENDIAN);

        /* Attempt to dissect FeliCa payloads */
        if ((tvb_get_guint8(tvb, 3) == FELICA_212) || (tvb_get_guint8(tvb, 3) == FELICA_424)) {

            next_tvb = tvb_new_subset_remaining(tvb, 4);
            call_dissector(sub_handles[SUB_FELICA], next_tvb, pinfo, tree);

        }

        break;

    case IN_LIST_PASSIVE_TARGET_RSP:
        proto_tree_add_item(pn532_tree, hf_pn532_NbTg, tvb, 2, 1, ENC_BIG_ENDIAN);

        /* Probably an ISO/IEC 14443-B tag */
        if (tvb_reported_length(tvb) == 20) {

            /* Add the PUPI */
            proto_tree_add_item(pn532_tree, hf_pn532_14443b_pupi, tvb, 5, 4, ENC_BIG_ENDIAN);

            /* Add the Application Data */
            proto_tree_add_item(pn532_tree, hf_pn532_14443b_app_data, tvb, 9, 4, ENC_BIG_ENDIAN);

            /* Add the Protocol Info */
            proto_tree_add_item(pn532_tree, hf_pn532_14443b_proto_info, tvb, 13, 3, ENC_BIG_ENDIAN);
        }

        /* Probably one of:
         * a MiFare DESFire card (23 bytes),
         * an MF UltraLight tag (17 bytes)
         * an MF Classic card with a 4 byte UID (14 bytes) */

        if ((tvb_reported_length(tvb) == 23) || (tvb_reported_length(tvb) == 17) || (tvb_reported_length(tvb) == 14)) {

            /* Add the ATQA/SENS_RES */
            proto_tree_add_item(pn532_tree, hf_pn532_14443a_atqa, tvb, 4, 2, ENC_BIG_ENDIAN);

            /* Add the SAK/SEL_RES value */
            proto_tree_add_item(pn532_tree, hf_pn532_14443a_sak, tvb, 6, 1, ENC_BIG_ENDIAN);
            
            /* Add the UID length */
            proto_tree_add_item(pn532_tree, hf_pn532_14443a_uid_length, tvb, 7, 1, ENC_BIG_ENDIAN);
            
            /* Add the UID */
            if (tvb_reported_length(tvb) != 14) {
                proto_tree_add_item(pn532_tree, hf_pn532_14443a_uid, tvb, 8, 7, ENC_BIG_ENDIAN);

                /* Probably MiFare DESFire, or some other 14443-A card with an ATS value/7 byte UID */
                if (tvb_reported_length(tvb) == 23) {

                    /* Add the ATS value */
                    proto_tree_add_item(pn532_tree, hf_pn532_14443a_ats, tvb, 16, 5, ENC_BIG_ENDIAN);
                }
            }
            /* Probably MiFare Classic with a 4 byte UID */
            else {
                proto_tree_add_item(pn532_tree, hf_pn532_14443a_uid, tvb, 8, 4, ENC_BIG_ENDIAN);
            }

        }

        /* Probably an EMV/ISO 14443-A (VISA - 30 bytes payload/MC - 33 bytes payload)
                 card with a 4 byte UID */

        if (tvb_reported_length(tvb) == 30 || tvb_reported_length(tvb) == 33) {

        /* Check to see if there's a plausible ATQA value (0x0004 for my MC/VISA cards) */

            if ((tvb_get_guint8(tvb, 4) == 0x00 && tvb_get_guint8(tvb, 5) == 0x04)) {
            
                /* Add the ATQA/SENS_RES */
                proto_tree_add_item(pn532_tree, hf_pn532_14443a_atqa, tvb, 4, 2, ENC_BIG_ENDIAN);
                
                /* Add the SAK/SEL_RES value */
                proto_tree_add_item(pn532_tree, hf_pn532_14443a_sak, tvb, 6, 1, ENC_BIG_ENDIAN);
                                        
                /* Add the UID length */
                proto_tree_add_item(pn532_tree, hf_pn532_14443a_uid_length, tvb, 7, 1, ENC_BIG_ENDIAN);
                
                /* Add the UID */
                proto_tree_add_item(pn532_tree, hf_pn532_14443a_uid, tvb, 8, 4, ENC_BIG_ENDIAN);
                
                /* ATS length is probably prepended to the ATS data... */
                
                /* Pass the ATS value to the Data dissector, since it's too long to handle normally
                    Don't care about the "status word" at the end, right now */
                next_tvb = tvb_new_subset_remaining(tvb, 13);
                call_dissector(sub_handles[SUB_DATA], next_tvb, pinfo, tree);
            }
        }

        /* See if we've got a FeliCa payload with a System Code */
        if (tvb_reported_length(tvb) == 26) {

            /* For FeliCa, this is at position 4. This doesn't exist for other payload types. */
            proto_tree_add_item(pn532_tree, hf_pn532_payload_length, tvb, 4, 1, ENC_BIG_ENDIAN);

            /* Use the length value (20?) at position 4, and skip the Status Word (9000) at the end */
            next_tvb = tvb_new_subset(tvb, 5, tvb_get_guint8(tvb, 4) - 1, 19);
            call_dissector(sub_handles[SUB_FELICA], next_tvb, pinfo, tree);
        }

        break;

    case IN_ATR:
        break;

    case IN_PSL:
        break;

    case IN_DATA_EXCHANGE_REQ:

        if (sub_selected == SUB_MIFARE) {
            /* Logical target number */
            proto_tree_add_item(pn532_tree, hf_pn532_Tg, tvb, 2, 1, ENC_BIG_ENDIAN);

            /* Seems to work for payloads from LibNFC's "nfc-mfultralight" command */
            next_tvb = tvb_new_subset_remaining(tvb, 3);
            call_dissector(sub_handles[SUB_MIFARE], next_tvb, pinfo, tree);
        } 
        else if (sub_selected == SUB_ISO7816) {
            /* Logical target number */
            proto_tree_add_item(pn532_tree, hf_pn532_Tg, tvb, 2, 1, ENC_BIG_ENDIAN);

            /* Seems to work for EMV payloads sent using TAMA shell scripts */
            next_tvb = tvb_new_subset_remaining(tvb, 3);

            /* Need to do this, for the ISO7816 dissector to work, it seems */
            pinfo->p2p_dir = P2P_DIR_SENT;
            call_dissector(sub_handles[SUB_ISO7816], next_tvb, pinfo, tree);
        }
        else {
        }

        break;

    case IN_DATA_EXCHANGE_RSP:

        if (sub_selected == SUB_ISO7816) {

            /* Seems to work for identifying responses to Select File requests...
               Might need to investigate "Status Words", later */
            next_tvb = tvb_new_subset_remaining(tvb, 2);

            /* Need to do this, for the ISO7816 dissector to work, it seems */
            pinfo->p2p_dir = P2P_DIR_RECV;
            call_dissector(sub_handles[SUB_ISO7816], next_tvb, pinfo, tree);
        }
        else {
        }
            
        break;

    case IN_COMMUNICATE_THRU_REQ:

        if (sub_selected == SUB_FELICA) {

            /* Alleged payload length for FeliCa */
            proto_tree_add_item(pn532_tree, hf_pn532_payload_length, tvb, 2, 1, ENC_BIG_ENDIAN);

            /* Attempt to dissect FeliCa payloads */
            next_tvb = tvb_new_subset_remaining(tvb, 3);
            call_dissector(sub_handles[SUB_FELICA], next_tvb, pinfo, tree);
        }

        /* MiFare transmissions may identify as spurious FeliCa packets, in some cases */
        else {
        }
      
        break;

    case IN_COMMUNICATE_THRU_RSP:
        if (sub_selected == SUB_FELICA) {

            /* Alleged payload length for FeliCa */
            proto_tree_add_item(pn532_tree, hf_pn532_payload_length, tvb, 3, 1, ENC_BIG_ENDIAN);

            /* Attempt to dissect FeliCa payloads */
            next_tvb = tvb_new_subset_remaining(tvb, 4);
            call_dissector(sub_handles[SUB_FELICA], next_tvb, pinfo, tree);
        }

        /* MiFare transmissions may identify as spurious FeliCa packets, in some cases */
        else {
        }

        break;

    /* Deselect a token */
    case IN_DESELECT_REQ:
        /* Logical target number */
        proto_tree_add_item(pn532_tree, hf_pn532_Tg, tvb, 2, 1, ENC_BIG_ENDIAN);
        break;

    case IN_DESELECT_RSP:
        proto_tree_add_item(pn532_tree, hf_pn532_error, tvb, 2, 1, ENC_BIG_ENDIAN);
        break;

    /* Release a token */
    case IN_RELEASE_REQ:
        /* Logical target number */
        proto_tree_add_item(pn532_tree, hf_pn532_Tg, tvb, 2, 1, ENC_BIG_ENDIAN);
        break;

    case IN_RELEASE_RSP:
        proto_tree_add_item(pn532_tree, hf_pn532_error, tvb, 2, 1, ENC_BIG_ENDIAN);
        break;

    /* Select a token */
    case IN_SELECT_REQ:
        /* Logical target number */
        proto_tree_add_item(pn532_tree, hf_pn532_Tg, tvb, 2, 1, ENC_BIG_ENDIAN);
        break;

    case IN_SELECT_RSP:
        proto_tree_add_item(pn532_tree, hf_pn532_error, tvb, 2, 1, ENC_BIG_ENDIAN);
        break;

    case IN_AUTO_POLL_REQ:
        break;

    case IN_AUTO_POLL_RSP:
        break;

    case TG_INIT_AS_TARGET:
        break;

    case TG_SET_GENERAL_BYTES:
        break;

    case TG_GET_DATA:
        break;

    case TG_SET_DATA:
        break;

    case TG_SET_METADATA:
        break;

    case TG_GET_INITIATOR_CMD:
        break;

    case TG_RESP_TO_INITIATOR:
        break;

    case TG_GET_TARGET_STATUS:
        break;

    default:
        break;
    }
}

void proto_register_pn532(void)
{
    static hf_register_info hf[] = {

        {&hf_pn532_command,
         {"Command", "pn532.cmd", FT_UINT8, BASE_HEX | BASE_EXT_STRING,
          &pn532_commands_ext, 0x0, NULL, HFILL}},
        {&hf_pn532_direction,
         {"Direction", "pn532.tfi", FT_UINT8, BASE_HEX,
          VALS(pn532_directions), 0x0, NULL, HFILL}},
        {&hf_pn532_error,
         {"Error Code", "pn532.error", FT_UINT8, BASE_HEX,
          VALS(pn532_errors), 0x0, NULL, HFILL}},
        {&hf_pn532_BrTy,
         {"Baud Rate and Modulation", "pn532.BrTy", FT_UINT8, BASE_HEX,
          VALS(pn532_brtypes), 0x0, NULL, HFILL}},
        {&hf_pn532_MaxTg,
         {"Maximum Number of Targets", "pn532.MaxTg", FT_INT8, BASE_DEC,
          NULL, 0x0, NULL, HFILL}},
        {&hf_pn532_Tg,
         {"Logical Target Number", "pn532.Tg", FT_INT8, BASE_DEC,
          NULL, 0x0, NULL, HFILL}},
        {&hf_pn532_NbTg,
         {"Number of Targets", "pn532.NbTg", FT_INT8, BASE_DEC,
          NULL, 0x0, NULL, HFILL}},
        {&hf_pn532_payload_length,
         {"Payload Length", "pn532.payload.length", FT_INT8, BASE_DEC,
          NULL, 0x0, NULL, HFILL}},
        {&hf_pn532_ic_version,
         {"Integrated Circuit Version", "pn532.ic.version", FT_INT8, BASE_DEC,
          NULL, 0x0, NULL, HFILL}},
        {&hf_pn532_fw_version,
         {"Firmware Version", "pn532.fw.version", FT_INT8, BASE_DEC,
          NULL, 0x0, NULL, HFILL}},
        {&hf_pn532_fw_revision,
         {"Firmware Revision", "pn532.fw.revision", FT_INT8, BASE_DEC,
          NULL, 0x0, NULL, HFILL}},
        {&hf_pn532_fw_support,
         {"Firmware Support", "pn532.fw.support", FT_INT8, BASE_DEC,
          NULL, 0x0, NULL, HFILL}},
        {&hf_pn532_14443a_sak,
         {"ISO/IEC 14443-A SAK", "pn532.iso.14443a.sak", FT_UINT8, BASE_HEX,
          NULL, 0x0, NULL, HFILL}},
        {&hf_pn532_14443a_atqa,
         {"ISO/IEC 14443-A ATQA", "pn532.iso.14443a.atqa", FT_UINT16, BASE_HEX,
          NULL, 0x0, NULL, HFILL}},
        {&hf_pn532_14443a_uid,
         {"ISO/IEC 14443-A UID", "pn532.iso.14443a.uid", FT_UINT64, BASE_HEX,
          NULL, 0x0, NULL, HFILL}},
        {&hf_pn532_14443a_uid_length,
         {"ISO/IEC 14443-A UID Length", "pn532.iso.14443a.uid.length", FT_INT8, BASE_DEC,
          NULL, 0x0, NULL, HFILL}},
        {&hf_pn532_14443a_ats,
         {"ISO/IEC 14443-A ATS", "pn532.iso.14443a.ats", FT_UINT64, BASE_HEX,
          NULL, 0x0, NULL, HFILL}},
        {&hf_pn532_14443b_pupi,
         {"ISO/IEC 14443-B PUPI", "pn532.iso.14443b.pupi", FT_UINT64, BASE_HEX,
          NULL, 0x0, NULL, HFILL}},
        {&hf_pn532_14443b_app_data,
         {"ISO/IEC 14443-B Application Data", "pn532.iso.14443b.app.data", FT_UINT64, BASE_HEX,
          NULL, 0x0, NULL, HFILL}},
        {&hf_pn532_14443b_proto_info,
         {"ISO/IEC 14443-B Protocol Info", "pn532.iso.14443b.protocol.info", FT_UINT64, BASE_HEX,
          NULL, 0x0, NULL, HFILL}},
        {&hf_pn532_sam_mode,
         {"SAM Mode", "pn532.sam.mode", FT_UINT8, BASE_HEX,
          VALS(pn532_sam_modes), 0x0, NULL, HFILL}},
    };

    static gint *ett[] = {
        &ett_pn532
    };
    
    module_t *pref_mod;
    
    static const enum_val_t sub_enum_vals[] = {
        { "data", "Data", SUB_DATA },
        { "felica", "Sony FeliCa", SUB_FELICA },
        { "mifare", "NXP MiFare", SUB_MIFARE },
        { "iso7816", "ISO 7816", SUB_ISO7816 }, 
        { NULL, NULL, 0 }
    };
    
    proto_pn532 = proto_register_protocol("NXP PN532", "PN532", "pn532");
    proto_register_field_array(proto_pn532, hf, array_length(hf));
    proto_register_subtree_array(ett, array_length(ett));
    
    pref_mod = prefs_register_protocol(proto_pn532, NULL);
    prefs_register_enum_preference(pref_mod, "prtype532", "Payload Type", "Protocol payload type",
        &sub_selected, sub_enum_vals, FALSE);

    pn532_dissector_table = register_dissector_table("pn532.payload", "PN532 Payload", FT_UINT8, BASE_DEC);

    register_dissector("pn532", dissect_pn532, proto_pn532);
}

/* Handler registration */
void proto_reg_handoff_pn532(void)
{
    
    sub_handles[SUB_DATA] = find_dissector("data");
    sub_handles[SUB_FELICA] = find_dissector("felica");
    sub_handles[SUB_MIFARE] = find_dissector("mifare");
    sub_handles[SUB_ISO7816] = find_dissector("iso7816");    
}

/*
 * Editor modelines - http://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 4
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * ex: set shiftwidth=4 tabstop=8 expandtab:
 * :indentSize=4:tabSize=8:noTabs=true:
 */
