/**-*-C-*-**********************************************************************
 *
 * text2pcap.c
 *
 * Utility to convert an ASCII hexdump into a libpcap-format capture file
 *
 * (c) Copyright 2001 Ashok Narayanan <ashokn@cisco.com>
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
 *******************************************************************************/

/*******************************************************************************
 *
 * This utility reads in an ASCII hexdump of this common format:
 *
 * 00000000  00 E0 1E A7 05 6F 00 10 5A A0 B9 12 08 00 46 00 .....o..Z.....F.
 * 00000010  03 68 00 00 00 00 0A 2E EE 33 0F 19 08 7F 0F 19 .h.......3.....
 * 00000020  03 80 94 04 00 00 10 01 16 A2 0A 00 03 50 00 0C .............P..
 * 00000030  01 01 0F 19 03 80 11 01 1E 61 00 0C 03 01 0F 19 .........a......
 *
 * Each bytestring line consists of an offset, one or more bytes, and
 * text at the end. An offset is defined as a hex string of more than
 * two characters. A byte is defined as a hex string of exactly two
 * characters. The text at the end is ignored, as is any text before
 * the offset. Bytes read from a bytestring line are added to the
 * current packet only if all the following conditions are satisfied:
 *
 * - No text appears between the offset and the bytes (any bytes appearing after
 *   such text would be ignored)
 *
 * - The offset must be arithmetically correct, i.e. if the offset is 00000020, then
 *   exactly 32 bytes must have been read into this packet before this. If the offset
 *   is wrong, the packet is immediately terminated
 *
 * A packet start is signaled by a zero offset.
 *
 * Lines starting with #TEXT2PCAP are directives. These allow the user
 * to embed instructions into the capture file which allows text2pcap
 * to take some actions (e.g. specifying the encapsulation
 * etc.). Currently no directives are implemented.
 *
 * Lines beginning with # which are not directives are ignored as
 * comments. Currently all non-hexdump text is ignored by text2pcap;
 * in the future, text processing may be added, but lines prefixed
 * with '#' will still be ignored.
 *
 * The output is a libpcap packet containing Ethernet frames by
 * default. This program takes options which allow the user to add
 * dummy Ethernet, IP and UDP or TCP headers to the packets in order
 * to allow dumps of L3 or higher protocols to be decoded.
 *
 * Considerable flexibility is built into this code to read hexdumps
 * of slightly different formats. For example, any text prefixing the
 * hexdump line is dropped (including mail forwarding '>'). The offset
 * can be any hex number of four digits or greater.
 *
 * This converter cannot read a single packet greater than 64KiB-1. Packet
 * snaplength is automatically set to 64KiB-1.
 */

#include "config.h"

/*
 * Just make sure we include the prototype for strptime as well
 * (needed for glibc 2.2) but make sure we do this only if not
 * yet defined.
 */
#ifndef __USE_XOPEN
#  define __USE_XOPEN
#endif
#ifndef _XOPEN_SOURCE
#  ifndef __sun
#    define _XOPEN_SOURCE 600
#  endif
#endif

/*
 * Defining _XOPEN_SOURCE is needed on some platforms, e.g. platforms
 * using glibc, to expand the set of things system header files define.
 *
 * Unfortunately, on other platforms, such as some versions of Solaris
 * (including Solaris 10), it *reduces* that set as well, causing
 * strptime() not to be declared, presumably because the version of the
 * X/Open spec that _XOPEN_SOURCE implies doesn't include strptime() and
 * blah blah blah namespace pollution blah blah blah.
 *
 * So we define __EXTENSIONS__ so that "strptime()" is declared.
 */
#ifndef __EXTENSIONS__
#  define __EXTENSIONS__
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wsutil/file_util.h>

#include <time.h>
#include <glib.h>

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <errno.h>
#include <assert.h>

#ifndef HAVE_GETOPT
#include "wsutil/wsgetopt.h"
#endif

#ifdef NEED_STRPTIME_H
# include "wsutil/strptime.h"
#endif

#include "pcapio.h"
#include "text2pcap.h"
#include "version.h"

#ifdef _WIN32
#include <wsutil/unicode-utils.h>
#endif /* _WIN32 */

/*--- Options --------------------------------------------------------------------*/

/* File format */
static gboolean use_pcapng = FALSE;

/* Debug level */
static int debug = 0;
/* Be quiet */
static int quiet = FALSE;

/* Dummy Ethernet header */
static int hdr_ethernet = FALSE;
static guint32 hdr_ethernet_proto = 0;

/* Dummy IP header */
static int hdr_ip = FALSE;
static long hdr_ip_proto = 0;

/* Dummy UDP header */
static int hdr_udp = FALSE;
static guint32 hdr_dest_port = 0;
static guint32 hdr_src_port = 0;

/* Dummy TCP header */
static int hdr_tcp = FALSE;

/* Dummy SCTP header */
static int hdr_sctp = FALSE;
static guint32 hdr_sctp_src  = 0;
static guint32 hdr_sctp_dest = 0;
static guint32 hdr_sctp_tag  = 0;

/* Dummy DATA chunk header */
static int hdr_data_chunk = FALSE;
static guint8  hdr_data_chunk_type = 0;
static guint8  hdr_data_chunk_bits = 0;
static guint32 hdr_data_chunk_tsn  = 0;
static guint16 hdr_data_chunk_sid  = 0;
static guint16 hdr_data_chunk_ssn  = 0;
static guint32 hdr_data_chunk_ppid = 0;

/* ASCII text dump identification */
static int identify_ascii = FALSE;

static gboolean has_direction = FALSE;
static guint32 direction = 0;

/*--- Local date -----------------------------------------------------------------*/

/* This is where we store the packet currently being built */
#define MAX_PACKET 65535
static guint8 packet_buf[MAX_PACKET];
static guint32 header_length;
static guint32 ip_offset;
static guint32 curr_offset;
static guint32 max_offset = MAX_PACKET;
static guint32 packet_start = 0;
static void start_new_packet(gboolean);

/* This buffer contains strings present before the packet offset 0 */
#define PACKET_PREAMBLE_MAX_LEN     2048
static guint8 packet_preamble[PACKET_PREAMBLE_MAX_LEN+1];
static int packet_preamble_len = 0;

/* Number of packets read and written */
static guint32 num_packets_read = 0;
static guint32 num_packets_written = 0;
static guint64 bytes_written = 0;

/* Time code of packet, derived from packet_preamble */
static time_t ts_sec  = 0;
static guint32 ts_usec = 0;
static char *ts_fmt = NULL;
static struct tm timecode_default;

static guint8* pkt_lnstart;

/* Input file */
static const char *input_filename;
static FILE *input_file = NULL;
/* Output file */
static const char *output_filename;
static FILE *output_file = NULL;

/* Offset base to parse */
static guint32 offset_base = 16;

extern FILE *yyin;

/* ----- State machine -----------------------------------------------------------*/

/* Current state of parser */
typedef enum {
    INIT,             /* Waiting for start of new packet */
    START_OF_LINE,    /* Starting from beginning of line */
    READ_OFFSET,      /* Just read the offset */
    READ_BYTE,        /* Just read a byte */
    READ_TEXT         /* Just read text - ignore until EOL */
} parser_state_t;
static parser_state_t state = INIT;

static const char *state_str[] = {"Init",
                           "Start-of-line",
                           "Offset",
                           "Byte",
                           "Text"
};

static const char *token_str[] = {"",
                           "Byte",
                           "Offset",
                           "Directive",
                           "Text",
                           "End-of-line"
};

/* ----- Skeleton Packet Headers --------------------------------------------------*/

typedef struct {
    guint8  dest_addr[6];
    guint8  src_addr[6];
    guint16 l3pid;
} hdr_ethernet_t;

static hdr_ethernet_t HDR_ETHERNET = {
    {0x0a, 0x02, 0x02, 0x02, 0x02, 0x02},
    {0x0a, 0x01, 0x01, 0x01, 0x01, 0x01},
    0};

typedef struct {
    guint8  ver_hdrlen;
    guint8  dscp;
    guint16 packet_length;
    guint16 identification;
    guint8  flags;
    guint8  fragment;
    guint8  ttl;
    guint8  protocol;
    guint16 hdr_checksum;
    guint32 src_addr;
    guint32 dest_addr;
} hdr_ip_t;

static hdr_ip_t HDR_IP = {0x45, 0, 0, 0x3412, 0, 0, 0xff, 0, 0,
#ifdef WORDS_BIGENDIAN
0x0a010101, 0x0a020202
#else
0x0101010a, 0x0202020a
#endif
};

static struct {         /* pseudo header for checksum calculation */
    guint32 src_addr;
    guint32 dest_addr;
    guint8  zero;
    guint8  protocol;
    guint16 length;
} pseudoh;

typedef struct {
    guint16 source_port;
    guint16 dest_port;
    guint16 length;
    guint16 checksum;
} hdr_udp_t;

static hdr_udp_t HDR_UDP = {0, 0, 0, 0};

typedef struct {
    guint16 source_port;
    guint16 dest_port;
    guint32 seq_num;
    guint32 ack_num;
    guint8  hdr_length;
    guint8  flags;
    guint16 window;
    guint16 checksum;
    guint16 urg;
} hdr_tcp_t;

static hdr_tcp_t HDR_TCP = {0, 0, 0, 0, 0x50, 0, 0, 0, 0};

typedef struct {
    guint16 src_port;
    guint16 dest_port;
    guint32 tag;
    guint32 checksum;
} hdr_sctp_t;

static hdr_sctp_t HDR_SCTP = {0, 0, 0, 0};

typedef struct {
    guint8  type;
    guint8  bits;
    guint16 length;
    guint32 tsn;
    guint16 sid;
    guint16 ssn;
    guint32 ppid;
} hdr_data_chunk_t;

static hdr_data_chunk_t HDR_DATA_CHUNK = {0, 0, 0, 0, 0, 0, 0};

static char tempbuf[64];

/*----------------------------------------------------------------------
 * Stuff for writing a PCap file
 */
#define PCAP_MAGIC          0xa1b2c3d4
#define PCAP_SNAPLEN        0xffff

/* "libpcap" file header (minus magic number). */
struct pcap_hdr {
    guint32 magic;          /* magic */
    guint16 version_major;  /* major version number */
    guint16 version_minor;  /* minor version number */
    guint32 thiszone;       /* GMT to local correction */
    guint32 sigfigs;        /* accuracy of timestamps */
    guint32 snaplen;        /* max length of captured packets, in octets */
    guint32 network;        /* data link type */
};

/* "libpcap" record header. */
struct pcaprec_hdr {
    guint32 ts_sec;         /* timestamp seconds */
    guint32 ts_usec;        /* timestamp microseconds */
    guint32 incl_len;       /* number of octets of packet saved in file */
    guint32 orig_len;       /* actual length of packet */
};

/* Link-layer type; see http://www.tcpdump.org/linktypes.html for details */
static guint32 pcap_link_type = 1;   /* Default is LINKTYPE_ETHERNET */

/*----------------------------------------------------------------------
 * Parse a single hex number
 * Will abort the program if it can't parse the number
 * Pass in TRUE if this is an offset, FALSE if not
 */
static guint32
parse_num (const char *str, int offset)
{
    guint32 num;
    char *c;

    num = (guint32)strtoul(str, &c, offset ? offset_base : 16);
    if (c==str) {
        fprintf(stderr, "FATAL ERROR: Bad hex number? [%s]\n", str);
        exit(-1);
    }
    return num;
}

/*----------------------------------------------------------------------
 * Write this byte into current packet
 */
static void
write_byte (const char *str)
{
    guint32 num;

    num = parse_num(str, FALSE);
    packet_buf[curr_offset] = (guint8) num;
    curr_offset ++;
    if (curr_offset - header_length >= max_offset) /* packet full */
        start_new_packet(TRUE);
}

/*----------------------------------------------------------------------
 * Write a number of bytes into current packet
 */

static void
write_bytes(const char bytes[], guint32 nbytes)
{
    guint32 i;

    if (curr_offset + nbytes < MAX_PACKET) {
        for (i = 0; i < nbytes; i++) {
            packet_buf[curr_offset] = bytes[i];
            curr_offset++;
        }
    }
}

/*----------------------------------------------------------------------
 * Remove bytes from the current packet
 */
static void
unwrite_bytes (guint32 nbytes)
{
    curr_offset -= nbytes;
}

/*----------------------------------------------------------------------
 * Compute one's complement checksum (from RFC1071)
 */
static guint16
in_checksum (void *buf, guint32 count)
{
    guint32 sum = 0;
    guint16 *addr = (guint16 *)buf;

    while (count > 1) {
        /*  This is the inner loop */
        sum += g_ntohs(* (guint16 *) addr);
        addr++;
        count -= 2;
    }

    /*  Add left-over byte, if any */
    if (count > 0)
        sum += g_ntohs(* (guint8 *) addr);

    /*  Fold 32-bit sum to 16 bits */
    while (sum>>16)
        sum = (sum & 0xffff) + (sum >> 16);

    sum = ~sum;
    return g_htons(sum);
}

/* The CRC32C code is taken from draft-ietf-tsvwg-sctpcsum-01.txt.
 * That code is copyrighted by D. Otis and has been modified.
 */

#define CRC32C(c,d) (c=(c>>8)^crc_c[(c^(d))&0xFF])
static guint32 crc_c[256] =
{
0x00000000L, 0xF26B8303L, 0xE13B70F7L, 0x1350F3F4L,
0xC79A971FL, 0x35F1141CL, 0x26A1E7E8L, 0xD4CA64EBL,
0x8AD958CFL, 0x78B2DBCCL, 0x6BE22838L, 0x9989AB3BL,
0x4D43CFD0L, 0xBF284CD3L, 0xAC78BF27L, 0x5E133C24L,
0x105EC76FL, 0xE235446CL, 0xF165B798L, 0x030E349BL,
0xD7C45070L, 0x25AFD373L, 0x36FF2087L, 0xC494A384L,
0x9A879FA0L, 0x68EC1CA3L, 0x7BBCEF57L, 0x89D76C54L,
0x5D1D08BFL, 0xAF768BBCL, 0xBC267848L, 0x4E4DFB4BL,
0x20BD8EDEL, 0xD2D60DDDL, 0xC186FE29L, 0x33ED7D2AL,
0xE72719C1L, 0x154C9AC2L, 0x061C6936L, 0xF477EA35L,
0xAA64D611L, 0x580F5512L, 0x4B5FA6E6L, 0xB93425E5L,
0x6DFE410EL, 0x9F95C20DL, 0x8CC531F9L, 0x7EAEB2FAL,
0x30E349B1L, 0xC288CAB2L, 0xD1D83946L, 0x23B3BA45L,
0xF779DEAEL, 0x05125DADL, 0x1642AE59L, 0xE4292D5AL,
0xBA3A117EL, 0x4851927DL, 0x5B016189L, 0xA96AE28AL,
0x7DA08661L, 0x8FCB0562L, 0x9C9BF696L, 0x6EF07595L,
0x417B1DBCL, 0xB3109EBFL, 0xA0406D4BL, 0x522BEE48L,
0x86E18AA3L, 0x748A09A0L, 0x67DAFA54L, 0x95B17957L,
0xCBA24573L, 0x39C9C670L, 0x2A993584L, 0xD8F2B687L,
0x0C38D26CL, 0xFE53516FL, 0xED03A29BL, 0x1F682198L,
0x5125DAD3L, 0xA34E59D0L, 0xB01EAA24L, 0x42752927L,
0x96BF4DCCL, 0x64D4CECFL, 0x77843D3BL, 0x85EFBE38L,
0xDBFC821CL, 0x2997011FL, 0x3AC7F2EBL, 0xC8AC71E8L,
0x1C661503L, 0xEE0D9600L, 0xFD5D65F4L, 0x0F36E6F7L,
0x61C69362L, 0x93AD1061L, 0x80FDE395L, 0x72966096L,
0xA65C047DL, 0x5437877EL, 0x4767748AL, 0xB50CF789L,
0xEB1FCBADL, 0x197448AEL, 0x0A24BB5AL, 0xF84F3859L,
0x2C855CB2L, 0xDEEEDFB1L, 0xCDBE2C45L, 0x3FD5AF46L,
0x7198540DL, 0x83F3D70EL, 0x90A324FAL, 0x62C8A7F9L,
0xB602C312L, 0x44694011L, 0x5739B3E5L, 0xA55230E6L,
0xFB410CC2L, 0x092A8FC1L, 0x1A7A7C35L, 0xE811FF36L,
0x3CDB9BDDL, 0xCEB018DEL, 0xDDE0EB2AL, 0x2F8B6829L,
0x82F63B78L, 0x709DB87BL, 0x63CD4B8FL, 0x91A6C88CL,
0x456CAC67L, 0xB7072F64L, 0xA457DC90L, 0x563C5F93L,
0x082F63B7L, 0xFA44E0B4L, 0xE9141340L, 0x1B7F9043L,
0xCFB5F4A8L, 0x3DDE77ABL, 0x2E8E845FL, 0xDCE5075CL,
0x92A8FC17L, 0x60C37F14L, 0x73938CE0L, 0x81F80FE3L,
0x55326B08L, 0xA759E80BL, 0xB4091BFFL, 0x466298FCL,
0x1871A4D8L, 0xEA1A27DBL, 0xF94AD42FL, 0x0B21572CL,
0xDFEB33C7L, 0x2D80B0C4L, 0x3ED04330L, 0xCCBBC033L,
0xA24BB5A6L, 0x502036A5L, 0x4370C551L, 0xB11B4652L,
0x65D122B9L, 0x97BAA1BAL, 0x84EA524EL, 0x7681D14DL,
0x2892ED69L, 0xDAF96E6AL, 0xC9A99D9EL, 0x3BC21E9DL,
0xEF087A76L, 0x1D63F975L, 0x0E330A81L, 0xFC588982L,
0xB21572C9L, 0x407EF1CAL, 0x532E023EL, 0xA145813DL,
0x758FE5D6L, 0x87E466D5L, 0x94B49521L, 0x66DF1622L,
0x38CC2A06L, 0xCAA7A905L, 0xD9F75AF1L, 0x2B9CD9F2L,
0xFF56BD19L, 0x0D3D3E1AL, 0x1E6DCDEEL, 0xEC064EEDL,
0xC38D26C4L, 0x31E6A5C7L, 0x22B65633L, 0xD0DDD530L,
0x0417B1DBL, 0xF67C32D8L, 0xE52CC12CL, 0x1747422FL,
0x49547E0BL, 0xBB3FFD08L, 0xA86F0EFCL, 0x5A048DFFL,
0x8ECEE914L, 0x7CA56A17L, 0x6FF599E3L, 0x9D9E1AE0L,
0xD3D3E1ABL, 0x21B862A8L, 0x32E8915CL, 0xC083125FL,
0x144976B4L, 0xE622F5B7L, 0xF5720643L, 0x07198540L,
0x590AB964L, 0xAB613A67L, 0xB831C993L, 0x4A5A4A90L,
0x9E902E7BL, 0x6CFBAD78L, 0x7FAB5E8CL, 0x8DC0DD8FL,
0xE330A81AL, 0x115B2B19L, 0x020BD8EDL, 0xF0605BEEL,
0x24AA3F05L, 0xD6C1BC06L, 0xC5914FF2L, 0x37FACCF1L,
0x69E9F0D5L, 0x9B8273D6L, 0x88D28022L, 0x7AB90321L,
0xAE7367CAL, 0x5C18E4C9L, 0x4F48173DL, 0xBD23943EL,
0xF36E6F75L, 0x0105EC76L, 0x12551F82L, 0xE03E9C81L,
0x34F4F86AL, 0xC69F7B69L, 0xD5CF889DL, 0x27A40B9EL,
0x79B737BAL, 0x8BDCB4B9L, 0x988C474DL, 0x6AE7C44EL,
0xBE2DA0A5L, 0x4C4623A6L, 0x5F16D052L, 0xAD7D5351L,
};

static guint32
crc32c(const guint8* buf, unsigned int len, guint32 crc32_init)
{
    unsigned int i;
    guint32 crc32;

    crc32 = crc32_init;
    for (i = 0; i < len; i++)
        CRC32C(crc32, buf[i]);

    return ( crc32 );
}

static guint32
finalize_crc32c(guint32 crc32)
{
    guint32 result;
    guint8 byte0,byte1,byte2,byte3;

    result = ~crc32;
    byte0 = result & 0xff;
    byte1 = (result>>8) & 0xff;
    byte2 = (result>>16) & 0xff;
    byte3 = (result>>24) & 0xff;
    result = ((byte0 << 24) | (byte1 << 16) | (byte2 << 8) | byte3);
    return ( result );
}

static guint16
number_of_padding_bytes (guint32 length)
{
    guint16 remainder;

    remainder = length % 4;

    if (remainder == 0)
        return 0;
    else
        return 4 - remainder;
}

/*----------------------------------------------------------------------
 * Write current packet out
 */
static void
write_current_packet(gboolean cont)
{
    guint32 length = 0;
    guint16 padding_length = 0;
    int err;
    gboolean success;

    if (curr_offset > header_length) {
        /* Write the packet */

        /* Compute packet length */
        length = curr_offset;
        if (hdr_sctp) {
            padding_length = number_of_padding_bytes(length - header_length );
        } else {
            padding_length = 0;
        }
        /* Reset curr_offset, since we now write the headers */
        curr_offset = 0;

        /* Write Ethernet header */
        if (hdr_ethernet) {
            HDR_ETHERNET.l3pid = g_htons(hdr_ethernet_proto);
            write_bytes((const char *)&HDR_ETHERNET, sizeof(HDR_ETHERNET));
        }

        /* Write IP header */
        if (hdr_ip) {
            HDR_IP.packet_length = g_htons(length - ip_offset + padding_length);
            HDR_IP.protocol = (guint8) hdr_ip_proto;
            HDR_IP.hdr_checksum = 0;
            HDR_IP.hdr_checksum = in_checksum(&HDR_IP, sizeof(HDR_IP));
            write_bytes((const char *)&HDR_IP, sizeof(HDR_IP));
        }

        /* Write UDP header */
        if (hdr_udp) {
            guint16 x16;
            guint32 u;

            /* initialize pseudo header for checksum calculation */
            pseudoh.src_addr    = HDR_IP.src_addr;
            pseudoh.dest_addr   = HDR_IP.dest_addr;
            pseudoh.zero        = 0;
            pseudoh.protocol    = (guint8) hdr_ip_proto;
            pseudoh.length      = g_htons(length - header_length + sizeof(HDR_UDP));
            /* initialize the UDP header */
            HDR_UDP.source_port = g_htons(hdr_src_port);
            HDR_UDP.dest_port = g_htons(hdr_dest_port);
            HDR_UDP.length = g_htons(length - header_length + sizeof(HDR_UDP));
            HDR_UDP.checksum = 0;
            /* Note: g_ntohs()/g_htons() macro arg may be eval'd twice so calc value before invoking macro */
            x16  = in_checksum(&pseudoh, sizeof(pseudoh));
            u    = g_ntohs(x16);
            x16  = in_checksum(&HDR_UDP, sizeof(HDR_UDP));
            u   += g_ntohs(x16);
            x16  = in_checksum(packet_buf + header_length, length - header_length);
            u   += g_ntohs(x16);
            x16  = (u & 0xffff) + (u>>16);
            HDR_UDP.checksum = g_htons(x16);
            if (HDR_UDP.checksum == 0) /* differentiate between 'none' and 0 */
                HDR_UDP.checksum = g_htons(1);
            write_bytes((const char *)&HDR_UDP, sizeof(HDR_UDP));
        }

        /* Write TCP header */
        if (hdr_tcp) {
            guint16 x16;
            guint32 u;

             /* initialize pseudo header for checksum calculation */
            pseudoh.src_addr    = HDR_IP.src_addr;
            pseudoh.dest_addr   = HDR_IP.dest_addr;
            pseudoh.zero        = 0;
            pseudoh.protocol    = (guint8) hdr_ip_proto;
            pseudoh.length      = g_htons(length - header_length + sizeof(HDR_TCP));
            /* initialize the TCP header */
            HDR_TCP.source_port = g_htons(hdr_src_port);
            HDR_TCP.dest_port = g_htons(hdr_dest_port);
            /* HDR_TCP.seq_num already correct */
            HDR_TCP.window = g_htons(0x2000);
            HDR_TCP.checksum = 0;
            /* Note: g_ntohs()/g_htons() macro arg may be eval'd twice so calc value before invoking macro */
            x16  = in_checksum(&pseudoh, sizeof(pseudoh));
            u    = g_ntohs(x16);
            x16  = in_checksum(&HDR_TCP, sizeof(HDR_TCP));
            u   += g_ntohs(x16);
            x16  = in_checksum(packet_buf + header_length, length - header_length);
            u   += g_ntohs(x16);
            x16  = (u & 0xffff) + (u>>16);
            HDR_TCP.checksum = g_htons(x16);
            if (HDR_TCP.checksum == 0) /* differentiate between 'none' and 0 */
                HDR_TCP.checksum = g_htons(1);
            write_bytes((const char *)&HDR_TCP, sizeof(HDR_TCP));
            HDR_TCP.seq_num = g_ntohl(HDR_TCP.seq_num) + length - header_length;
            HDR_TCP.seq_num = g_htonl(HDR_TCP.seq_num);
        }

        /* Compute DATA chunk header */
        if (hdr_data_chunk) {
            hdr_data_chunk_bits = 0;
            if (packet_start == 0) {
                hdr_data_chunk_bits |= 0x02;
            }
            if (!cont) {
                hdr_data_chunk_bits |= 0x01;
            }
            HDR_DATA_CHUNK.type   = hdr_data_chunk_type;
            HDR_DATA_CHUNK.bits   = hdr_data_chunk_bits;
            HDR_DATA_CHUNK.length = g_htons(length - header_length + sizeof(HDR_DATA_CHUNK));
            HDR_DATA_CHUNK.tsn    = g_htonl(hdr_data_chunk_tsn);
            HDR_DATA_CHUNK.sid    = g_htons(hdr_data_chunk_sid);
            HDR_DATA_CHUNK.ssn    = g_htons(hdr_data_chunk_ssn);
            HDR_DATA_CHUNK.ppid   = g_htonl(hdr_data_chunk_ppid);
            hdr_data_chunk_tsn++;
            if (!cont) {
                hdr_data_chunk_ssn++;
            }
        }

        /* Write SCTP common header */
        if (hdr_sctp) {
            guint32 zero = 0;

            HDR_SCTP.src_port  = g_htons(hdr_sctp_src);
            HDR_SCTP.dest_port = g_htons(hdr_sctp_dest);
            HDR_SCTP.tag       = g_htonl(hdr_sctp_tag);
            HDR_SCTP.checksum  = g_htonl(0);
            HDR_SCTP.checksum  = crc32c((guint8 *)&HDR_SCTP, sizeof(HDR_SCTP), ~0L);
            if (hdr_data_chunk) {
                HDR_SCTP.checksum  = crc32c((guint8 *)&HDR_DATA_CHUNK, sizeof(HDR_DATA_CHUNK), HDR_SCTP.checksum);
                HDR_SCTP.checksum  = crc32c((guint8 *)packet_buf + header_length, length - header_length, HDR_SCTP.checksum);
                HDR_SCTP.checksum  = crc32c((guint8 *)&zero, padding_length, HDR_SCTP.checksum);
            } else {
                HDR_SCTP.checksum  = crc32c((guint8 *)packet_buf + header_length, length - header_length, HDR_SCTP.checksum);
            }
            HDR_SCTP.checksum = finalize_crc32c(HDR_SCTP.checksum);
            HDR_SCTP.checksum  = g_htonl(HDR_SCTP.checksum);
            write_bytes((const char *)&HDR_SCTP, sizeof(HDR_SCTP));
        }

        /* Write DATA chunk header */
        if (hdr_data_chunk) {
            write_bytes((const char *)&HDR_DATA_CHUNK, sizeof(HDR_DATA_CHUNK));
        }

        /* Reset curr_offset, since we now write the trailers */
        curr_offset = length;

        /* Write DATA chunk padding */
        if (hdr_data_chunk && (padding_length > 0)) {
            memset(tempbuf, 0, padding_length);
            write_bytes((const char *)&tempbuf, padding_length);
            length += padding_length;
        }

        /* Write Ethernet trailer */
        if (hdr_ethernet && (length < 60)) {
            memset(tempbuf, 0, 60 - length);
            write_bytes((const char *)&tempbuf, 60 - length);
            length = 60;
        }
        if (use_pcapng) {
            success = libpcap_write_enhanced_packet_block(libpcap_write_to_file, output_file,
                                                          NULL,
                                                          ts_sec, ts_usec,
                                                          length, length,
                                                          0,
                                                          1000000,
                                                          packet_buf, direction,
                                                          &bytes_written, &err);
        } else {
            success = libpcap_write_packet(libpcap_write_to_file, output_file,
                                           ts_sec, ts_usec,
                                           length, length,
                                           packet_buf,
                                           &bytes_written, &err);
        }
        if (!success) {
            fprintf(stderr, "File write error [%s] : %s\n",
                    output_filename, g_strerror(err));
            exit(-1);
        }
        if (ts_fmt == NULL) {
            /* fake packet counter */
            ts_usec++;
        }
        if (!quiet) {
            fprintf(stderr, "Wrote packet of %u bytes.\n", length);
        }
        num_packets_written ++;
    }

    packet_start += curr_offset - header_length;
    curr_offset = header_length;
    return;
}

/*----------------------------------------------------------------------
 * Write file header and trailer
 */
static void
write_file_header (void)
{
    int err;
    gboolean success;

    if (use_pcapng) {
#ifdef GITVERSION
        const char *appname = "text2pcap (" GITVERSION " from " GITBRANCH ")";
#else
        const char *appname = "text2pcap";
#endif
        char comment[100];

        g_snprintf(comment, sizeof(comment), "Generated from input file %s.", input_filename);
        success = libpcap_write_session_header_block(libpcap_write_to_file, output_file,
                                                     comment,
                                                     NULL,
                                                     NULL,
                                                     appname,
                                                     -1,
                                                     &bytes_written,
                                                     &err);
        if (success) {
            success = libpcap_write_interface_description_block(libpcap_write_to_file, output_file,
                                                                NULL,
                                                                NULL,
                                                                NULL,
                                                                "",
                                                                NULL,
                                                                pcap_link_type,
                                                                PCAP_SNAPLEN,
                                                                &bytes_written,
                                                                0,
                                                                6,
                                                                &err);
        }
    } else {
        success = libpcap_write_file_header(libpcap_write_to_file, output_file, pcap_link_type, PCAP_SNAPLEN,
                                            FALSE, &bytes_written, &err);
    }
    if (!success) {
        fprintf(stderr, "File write error [%s] : %s\n",
                output_filename, g_strerror(err));
        exit(-1);
    }
}

static void
write_file_trailer (void)
{
    int err;
    gboolean success;

    if (use_pcapng) {
        success = libpcap_write_interface_statistics_block(libpcap_write_to_file, output_file,
                                                           0,
                                                           &bytes_written,
                                                           "Counters provided by text2pcap",
                                                           0,
                                                           0,
                                                           num_packets_written,
                                                           num_packets_written - num_packets_written,
                                                           &err);

    } else {
        success = TRUE;
    }
    if (!success) {
        fprintf(stderr, "File write error [%s] : %s\n",
                output_filename, g_strerror(err));
        exit(-1);
    }
   return;
}

/*----------------------------------------------------------------------
 * Append a token to the packet preamble.
 */
static void
append_to_preamble(char *str)
{
    size_t toklen;

    if (packet_preamble_len != 0) {
        if (packet_preamble_len == PACKET_PREAMBLE_MAX_LEN)
            return; /* no room to add more preamble */
        /* Add a blank separator between the previous token and this token. */
        packet_preamble[packet_preamble_len++] = ' ';
    }
    toklen = strlen(str);
    if (toklen != 0) {
        if (packet_preamble_len + toklen > PACKET_PREAMBLE_MAX_LEN)
            return; /* no room to add the token to the preamble */
        g_strlcpy(&packet_preamble[packet_preamble_len], str, PACKET_PREAMBLE_MAX_LEN);
        packet_preamble_len += (int) toklen;
        if (debug >= 2) {
            char *c;
            char xs[PACKET_PREAMBLE_MAX_LEN];
            g_strlcpy(xs, packet_preamble, PACKET_PREAMBLE_MAX_LEN);
            while ((c = strchr(xs, '\r')) != NULL) *c=' ';
            fprintf (stderr, "[[append_to_preamble: \"%s\"]]", xs);
        }
    }
}

/*----------------------------------------------------------------------
 * Parse the preamble to get the timecode.
 */

static void
parse_preamble (void)
{
    struct tm timecode;
    char *subsecs;
    char *p;
    int  subseclen;
    int  i;

     /*
     * Null-terminate the preamble.
     */
    packet_preamble[packet_preamble_len] = '\0';
    if (debug > 0)
        fprintf(stderr, "[[parse_preamble: \"%s\"]]\n", packet_preamble);

    if (has_direction) {
        switch (packet_preamble[0]) {
        case 'i':
        case 'I':
            direction = 0x00000001;
            packet_preamble[0] = ' ';
            break;
        case 'o':
        case 'O':
            direction = 0x00000002;
            packet_preamble[0] = ' ';
            break;
        default:
            direction = 0x00000000;
            break;
        }
        i = 0;
        while (packet_preamble[i] == ' ' ||
               packet_preamble[i] == '\r' ||
               packet_preamble[i] == '\t') {
            i++;
        }
        packet_preamble_len -= i;
        /* Also move the trailing '\0'. */
        memmove(packet_preamble, packet_preamble + i, packet_preamble_len + 1);
    }


    /*
     * If no "-t" flag was specified, don't attempt to parse the packet
     * preamble to extract a time stamp.
     */
    if (ts_fmt == NULL) {
        /* Clear Preamble */
        packet_preamble_len = 0;
        return;
    }

    /*
     * Initialize to today localtime, just in case not all fields
     * of the date and time are specified.
     */

    timecode = timecode_default;
    ts_usec = 0;

    /* Ensure preamble has more than two chars before attempting to parse.
     * This should cover line breaks etc that get counted.
     */
    if (strlen(packet_preamble) > 2) {
        /* Get Time leaving subseconds */
        subsecs = strptime( packet_preamble, ts_fmt, &timecode );
        if (subsecs != NULL) {
            /* Get the long time from the tm structure */
            /*  (will return -1 if failure)            */
            ts_sec  = mktime( &timecode );
        } else
            ts_sec = -1;    /* we failed to parse it */

        /* This will ensure incorrectly parsed dates get set to zero */
        if (-1 == ts_sec) {
            /* Sanitize - remove all '\r' */
            char *c;
            while ((c = strchr(packet_preamble, '\r')) != NULL) *c=' ';
            fprintf (stderr, "Failure processing time \"%s\" using time format \"%s\"\n   (defaulting to Jan 1,1970 00:00:00 GMT)\n",
                 packet_preamble, ts_fmt);
            if (debug >= 2) {
                fprintf(stderr, "timecode: %02d/%02d/%d %02d:%02d:%02d %d\n",
                    timecode.tm_mday, timecode.tm_mon, timecode.tm_year,
                    timecode.tm_hour, timecode.tm_min, timecode.tm_sec, timecode.tm_isdst);
            }
            ts_sec  = 0;  /* Jan 1,1970: 00:00 GMT; tshark/wireshark will display date/time as adjusted by timezone */
            ts_usec = 0;
        } else {
            /* Parse subseconds */
            ts_usec = (guint32)strtol(subsecs, &p, 10);
            if (subsecs == p) {
                /* Error */
                ts_usec = 0;
            } else {
                /*
                 * Convert that number to a number
                 * of microseconds; if it's N digits
                 * long, it's in units of 10^(-N) seconds,
                 * so, to convert it to units of
                 * 10^-6 seconds, we multiply by
                 * 10^(6-N).
                 */
                subseclen = (int) (p - subsecs);
                if (subseclen > 6) {
                    /*
                     * *More* than 6 digits; 6-N is
                     * negative, so we divide by
                     * 10^(N-6).
                     */
                    for (i = subseclen - 6; i != 0; i--)
                        ts_usec /= 10;
                } else if (subseclen < 6) {
                    for (i = 6 - subseclen; i != 0; i--)
                        ts_usec *= 10;
                }
            }
        }
    }
    if (debug >= 2) {
        char *c;
        while ((c = strchr(packet_preamble, '\r')) != NULL) *c=' ';
        fprintf(stderr, "[[parse_preamble: \"%s\"]]\n", packet_preamble);
        fprintf(stderr, "Format(%s), time(%u), subsecs(%u)\n", ts_fmt, (guint32)ts_sec, ts_usec);
    }


    /* Clear Preamble */
    packet_preamble_len = 0;
}

/*----------------------------------------------------------------------
 * Start a new packet
 */
static void
start_new_packet(gboolean cont)
{
    if (debug >= 1)
        fprintf(stderr, "Start new packet (cont = %s).\n", cont ? "TRUE" : "FALSE");

    /* Write out the current packet, if required */
    write_current_packet(cont);
    num_packets_read ++;

    /* Ensure we parse the packet preamble as it may contain the time */
    parse_preamble();
}

/*----------------------------------------------------------------------
 * Process a directive
 */
static void
process_directive (char *str)
{
    fprintf(stderr, "\n--- Directive [%s] currently unsupported ---\n", str + 10);
}

/*----------------------------------------------------------------------
 * Parse a single token (called from the scanner)
 */
void
parse_token (token_t token, char *str)
{
    guint32 num;
    int by_eol;
    int rollback = 0;
    int line_size;
    int i;
    char* s2;
    char tmp_str[3];

    /*
     * This is implemented as a simple state machine of five states.
     * State transitions are caused by tokens being received from the
     * scanner. The code should be self_documenting.
     */

    if (debug >= 2) {
        /* Sanitize - remove all '\r' */
        char *c;
        if (str!=NULL) { while ((c = strchr(str, '\r')) != NULL) *c=' '; }

        fprintf(stderr, "(%s, %s \"%s\") -> (",
                state_str[state], token_str[token], str ? str : "");
    }

    switch(state) {

    /* ----- Waiting for new packet -------------------------------------------*/
    case INIT:
        switch(token) {
        case T_TEXT:
            append_to_preamble(str);
            break;
        case T_DIRECTIVE:
            process_directive(str);
            break;
        case T_OFFSET:
            num = parse_num(str, TRUE);
            if (num == 0) {
                /* New packet starts here */
                start_new_packet(FALSE);
                state = READ_OFFSET;
                pkt_lnstart = packet_buf + num;
            }
            break;
        case T_EOL:
            /* Some describing text may be parsed as offset, but the invalid
               offset will be checked in the state of START_OF_LINE, so
               we add this transition to gain flexibility */
            state = START_OF_LINE;
            break;
        default:
            break;
        }
        break;

    /* ----- Processing packet, start of new line -----------------------------*/
    case START_OF_LINE:
        switch(token) {
        case T_TEXT:
            append_to_preamble(str);
            break;
        case T_DIRECTIVE:
            process_directive(str);
            break;
        case T_OFFSET:
            num = parse_num(str, TRUE);
            if (num == 0) {
                /* New packet starts here */
                start_new_packet(FALSE);
                packet_start = 0;
                state = READ_OFFSET;
            } else if ((num - packet_start) != curr_offset - header_length) {
                /*
                 * The offset we read isn't the one we expected.
                 * This may only mean that we mistakenly interpreted
                 * some text as byte values (e.g., if the text dump
                 * of packet data included a number with spaces around
                 * it).  If the offset is less than what we expected,
                 * assume that's the problem, and throw away the putative
                 * extra byte values.
                 */
                if (num < curr_offset) {
                    unwrite_bytes(curr_offset - num);
                    state = READ_OFFSET;
                } else {
                    /* Bad offset; switch to INIT state */
                    if (debug >= 1)
                        fprintf(stderr, "Inconsistent offset. Expecting %0X, got %0X. Ignoring rest of packet\n",
                                curr_offset, num);
                    write_current_packet(FALSE);
                    state = INIT;
                }
            } else
                state = READ_OFFSET;
                pkt_lnstart = packet_buf + num;
            break;
        case T_EOL:
            state = START_OF_LINE;
            break;
        default:
            break;
        }
        break;

    /* ----- Processing packet, read offset -----------------------------------*/
    case READ_OFFSET:
        switch(token) {
        case T_BYTE:
            /* Record the byte */
            state = READ_BYTE;
            write_byte(str);
            break;
        case T_TEXT:
        case T_DIRECTIVE:
        case T_OFFSET:
            state = READ_TEXT;
            break;
        case T_EOL:
            state = START_OF_LINE;
            break;
        default:
            break;
        }
        break;

    /* ----- Processing packet, read byte -------------------------------------*/
    case READ_BYTE:
        switch(token) {
        case T_BYTE:
            /* Record the byte */
            write_byte(str);
            break;
        case T_TEXT:
        case T_DIRECTIVE:
        case T_OFFSET:
        case T_EOL:
            by_eol = 0;
            state = READ_TEXT;
            if (token == T_EOL) {
                by_eol = 1;
                state = START_OF_LINE;
            }
            if (identify_ascii) {
                /* Here a line of pkt bytes reading is finished
                   compare the ascii and hex to avoid such situation:
                   "61 62 20 ab ", when ab is ascii dump then it should
                   not be treat as byte */
                rollback = 0;
                /* s2 is the ASCII string, s1 is the HEX string, e.g, when
                   s2 = "ab ", s1 = "616220"
                   we should find out the largest tail of s1 matches the head
                   of s2, it means the matched part in tail is the ASCII dump
                   of the head byte. These matched should be rollback */
                line_size = curr_offset-(int)(pkt_lnstart-packet_buf);
                s2 = (char*)g_malloc((line_size+1)/4+1);
                /* gather the possible pattern */
                for (i = 0; i < (line_size+1)/4; i++) {
                    tmp_str[0] = pkt_lnstart[i*3];
                    tmp_str[1] = pkt_lnstart[i*3+1];
                    tmp_str[2] = '\0';
                    /* it is a valid convertable string */
                    if (!isxdigit(tmp_str[0]) || !isxdigit(tmp_str[0])) {
                        break;
                    }
                    s2[i] = (char)strtoul(tmp_str, (char **)NULL, 16);
                    rollback++;
                    /* the 3rd entry is not a delimiter, so the possible byte pattern will not shown */
                    if (!(pkt_lnstart[i*3+2] == ' ')) {
                        if (by_eol != 1)
                            rollback--;
                        break;
                    }
                }
                /* If packet line start contains possible byte pattern, the line end
                   should contain the matched pattern if the user open the -a flag.
                   The packet will be possible invalid if the byte pattern cannot find
                   a matched one in the line of packet buffer.*/
                if (rollback > 0) {
                    if (strncmp(pkt_lnstart+line_size-rollback, s2, rollback) == 0) {
                        unwrite_bytes(rollback);
                    }
                    /* Not matched. This line contains invalid packet bytes, so
                       discard the whole line */
                    else {
                        unwrite_bytes(line_size);
                    }
                }
                g_free(s2);
            }
            break;
        default:
            break;
        }
        break;

    /* ----- Processing packet, read text -------------------------------------*/
    case READ_TEXT:
        switch(token) {
        case T_EOL:
            state = START_OF_LINE;
            break;
        default:
            break;
        }
        break;

    default:
        fprintf(stderr, "FATAL ERROR: Bad state (%d)", state);
        exit(-1);
    }

    if (debug>=2)
        fprintf(stderr, ", %s)\n", state_str[state]);

}

/*----------------------------------------------------------------------
 * Print usage string and exit
 */
static void
usage (void)
{
    fprintf(stderr,
            "Text2pcap %s"
#ifdef GITVERSION
            " (" GITVERSION " from " GITBRANCH ")"
#endif
            "\n"
            "Generate a capture file from an ASCII hexdump of packets.\n"
            "See http://www.wireshark.org for more information.\n"
            "\n"
            "Usage: text2pcap [options] <infile> <outfile>\n"
            "\n"
            "where  <infile> specifies input  filename (use - for standard input)\n"
            "      <outfile> specifies output filename (use - for standard output)\n"
            "\n"
            "Input:\n"
            "  -o hex|oct|dec         parse offsets as (h)ex, (o)ctal or (d)ecimal;\n"
            "                         default is hex.\n"
            "  -t <timefmt>           treat the text before the packet as a date/time code;\n"
            "                         the specified argument is a format string of the sort\n"
            "                         supported by strptime.\n"
            "                         Example: The time \"10:15:14.5476\" has the format code\n"
            "                         \"%%H:%%M:%%S.\"\n"
            "                         NOTE: The subsecond component delimiter, '.', must be\n"
            "                         given, but no pattern is required; the remaining\n"
            "                         number is assumed to be fractions of a second.\n"
            "                         NOTE: Date/time fields from the current date/time are\n"
            "                         used as the default for unspecified fields.\n"
            "  -D                     the text before the packet starts with an I or an O,\n"
            "                         indicating that the packet is inbound or outbound.\n"
            "                         This is only stored if the output format is PCAP-NG.\n"
            "  -a                     enable ASCII text dump identification.\n"
            "                         The start of the ASCII text dump can be identified\n"
            "                         and excluded from the packet data, even if it looks\n"
            "                         like a HEX dump.\n"
            "                         NOTE: Do not enable it if the input file does not\n"
            "                         contain the ASCII text dump.\n"
            "\n"
            "Output:\n"
            "  -l <typenum>           link-layer type number; default is 1 (Ethernet).  See\n"
            "                         http://www.tcpdump.org/linktypes.html for a list of\n"
            "                         numbers.  Use this option if your dump is a complete\n"
            "                         hex dump of an encapsulated packet and you wish to\n"
            "                         specify the exact type of encapsulation.\n"
            "                         Example: -l 7 for ARCNet packets.\n"
            "  -m <max-packet>        max packet length in output; default is %d\n"
            "\n"
            "Prepend dummy header:\n"
            "  -e <l3pid>             prepend dummy Ethernet II header with specified L3PID\n"
            "                         (in HEX).\n"
            "                         Example: -e 0x806 to specify an ARP packet.\n"
            "  -i <proto>             prepend dummy IP header with specified IP protocol\n"
            "                         (in DECIMAL).\n"
            "                         Automatically prepends Ethernet header as well.\n"
            "                         Example: -i 46\n"
            "  -u <srcp>,<destp>      prepend dummy UDP header with specified\n"
            "                         source and destination ports (in DECIMAL).\n"
            "                         Automatically prepends Ethernet & IP headers as well.\n"
            "                         Example: -u 1000,69 to make the packets look like\n"
            "                         TFTP/UDP packets.\n"
            "  -T <srcp>,<destp>      prepend dummy TCP header with specified\n"
            "                         source and destination ports (in DECIMAL).\n"
            "                         Automatically prepends Ethernet & IP headers as well.\n"
            "                         Example: -T 50,60\n"
            "  -s <srcp>,<dstp>,<tag> prepend dummy SCTP header with specified\n"
            "                         source/dest ports and verification tag (in DECIMAL).\n"
            "                         Automatically prepends Ethernet & IP headers as well.\n"
            "                         Example: -s 30,40,34\n"
            "  -S <srcp>,<dstp>,<ppi> prepend dummy SCTP header with specified\n"
            "                         source/dest ports and verification tag 0.\n"
            "                         Automatically prepends a dummy SCTP DATA\n"
            "                         chunk header with payload protocol identifier ppi.\n"
            "                         Example: -S 30,40,34\n"
            "\n"
            "Miscellaneous:\n"
            "  -h                     display this help and exit.\n"
            "  -d                     show detailed debug of parser states.\n"
            "  -q                     generate no output at all (automatically disables -d).\n"
            "  -n                     use PCAP-NG instead of PCAP as output format.\n"
            "",
            VERSION, MAX_PACKET);

    exit(-1);
}

/*----------------------------------------------------------------------
 * Parse CLI options
 */
static void
parse_options (int argc, char *argv[])
{
    int c;
    char *p;

#ifdef _WIN32
    arg_list_utf_16to8(argc, argv);
    create_app_running_mutex();
#endif /* _WIN32 */

    /* Scan CLI parameters */
    while ((c = getopt(argc, argv, "Ddhqe:i:l:m:no:u:s:S:t:T:a")) != -1) {
        switch(c) {
        case '?': usage(); break;
        case 'h': usage(); break;
        case 'd': if (!quiet) debug++; break;
        case 'D': has_direction = TRUE; break;
        case 'q': quiet = TRUE; debug = FALSE; break;
        case 'l': pcap_link_type = (guint32)strtol(optarg, NULL, 0); break;
        case 'm': max_offset = (guint32)strtol(optarg, NULL, 0); break;
        case 'n': use_pcapng = TRUE; break;
        case 'o':
            if (optarg[0]!='h' && optarg[0] != 'o' && optarg[0] != 'd') {
                fprintf(stderr, "Bad argument for '-o': %s\n", optarg);
                usage();
            }
            switch(optarg[0]) {
            case 'o': offset_base = 8; break;
            case 'h': offset_base = 16; break;
            case 'd': offset_base = 10; break;
            }
            break;
        case 'e':
            hdr_ethernet = TRUE;
            if (sscanf(optarg, "%x", &hdr_ethernet_proto) < 1) {
                fprintf(stderr, "Bad argument for '-e': %s\n", optarg);
                usage();
            }
            break;

        case 'i':
            hdr_ip = TRUE;
            hdr_ip_proto = strtol(optarg, &p, 10);
            if (p == optarg || *p != '\0' || hdr_ip_proto < 0 ||
                  hdr_ip_proto > 255) {
                fprintf(stderr, "Bad argument for '-i': %s\n", optarg);
                usage();
            }
            hdr_ethernet = TRUE;
            hdr_ethernet_proto = 0x800;
            break;

        case 's':
            hdr_sctp = TRUE;
            hdr_data_chunk = FALSE;
            hdr_tcp = FALSE;
            hdr_udp = FALSE;
            hdr_sctp_src   = (guint32)strtol(optarg, &p, 10);
            if (p == optarg || (*p != ',' && *p != '\0')) {
                fprintf(stderr, "Bad src port for '-%c'\n", c);
                usage();
            }
            if (*p == '\0') {
                fprintf(stderr, "No dest port specified for '-%c'\n", c);
                usage();
            }
            p++;
            optarg = p;
            hdr_sctp_dest = (guint32)strtol(optarg, &p, 10);
            if (p == optarg || (*p != ',' && *p != '\0')) {
                fprintf(stderr, "Bad dest port for '-s'\n");
                usage();
            }
            if (*p == '\0') {
                fprintf(stderr, "No tag specified for '-%c'\n", c);
                usage();
            }
            p++;
            optarg = p;
            hdr_sctp_tag = (guint32)strtol(optarg, &p, 10);
            if (p == optarg || *p != '\0') {
                fprintf(stderr, "Bad tag for '-%c'\n", c);
                usage();
            }

            hdr_ip = TRUE;
            hdr_ip_proto = 132;
            hdr_ethernet = TRUE;
            hdr_ethernet_proto = 0x800;
            break;
        case 'S':
            hdr_sctp = TRUE;
            hdr_data_chunk = TRUE;
            hdr_tcp = FALSE;
            hdr_udp = FALSE;
            hdr_sctp_src   = (guint32)strtol(optarg, &p, 10);
            if (p == optarg || (*p != ',' && *p != '\0')) {
                fprintf(stderr, "Bad src port for '-%c'\n", c);
                usage();
            }
            if (*p == '\0') {
                fprintf(stderr, "No dest port specified for '-%c'\n", c);
                usage();
            }
            p++;
            optarg = p;
            hdr_sctp_dest = (guint32)strtol(optarg, &p, 10);
            if (p == optarg || (*p != ',' && *p != '\0')) {
                fprintf(stderr, "Bad dest port for '-s'\n");
                usage();
            }
            if (*p == '\0') {
                fprintf(stderr, "No ppi specified for '-%c'\n", c);
                usage();
            }
            p++;
            optarg = p;
            hdr_data_chunk_ppid = (guint32)strtoul(optarg, &p, 10);
            if (p == optarg || *p != '\0') {
                fprintf(stderr, "Bad ppi for '-%c'\n", c);
                usage();
            }

            hdr_ip = TRUE;
            hdr_ip_proto = 132;
            hdr_ethernet = TRUE;
            hdr_ethernet_proto = 0x800;
            break;

        case 't':
            ts_fmt = optarg;
            break;

        case 'u':
            hdr_udp = TRUE;
            hdr_tcp = FALSE;
            hdr_sctp = FALSE;
            hdr_data_chunk = FALSE;
            hdr_src_port = (guint32)strtol(optarg, &p, 10);
            if (p == optarg || (*p != ',' && *p != '\0')) {
                fprintf(stderr, "Bad src port for '-u'\n");
                usage();
            }
            if (*p == '\0') {
                fprintf(stderr, "No dest port specified for '-u'\n");
                usage();
            }
            p++;
            optarg = p;
            hdr_dest_port = (guint32)strtol(optarg, &p, 10);
            if (p == optarg || *p != '\0') {
                fprintf(stderr, "Bad dest port for '-u'\n");
                usage();
            }
            hdr_ip = TRUE;
            hdr_ip_proto = 17;
            hdr_ethernet = TRUE;
            hdr_ethernet_proto = 0x800;
            break;

        case 'T':
            hdr_tcp = TRUE;
            hdr_udp = FALSE;
            hdr_sctp = FALSE;
            hdr_data_chunk = FALSE;
            hdr_src_port = (guint32)strtol(optarg, &p, 10);
            if (p == optarg || (*p != ',' && *p != '\0')) {
                fprintf(stderr, "Bad src port for '-T'\n");
                usage();
            }
            if (*p == '\0') {
                fprintf(stderr, "No dest port specified for '-u'\n");
                usage();
            }
            p++;
            optarg = p;
            hdr_dest_port = (guint32)strtol(optarg, &p, 10);
            if (p == optarg || *p != '\0') {
                fprintf(stderr, "Bad dest port for '-T'\n");
                usage();
            }
            hdr_ip = TRUE;
            hdr_ip_proto = 6;
            hdr_ethernet = TRUE;
            hdr_ethernet_proto = 0x800;
            break;

        case 'a':
            identify_ascii = TRUE;
            break;

        default:
            usage();
        }
    }

    if (optind >= argc || argc-optind < 2) {
        fprintf(stderr, "Must specify input and output filename\n");
        usage();
    }

    if (strcmp(argv[optind], "-")) {
        input_filename = g_strdup(argv[optind]);
        input_file = ws_fopen(input_filename, "rb");
        if (!input_file) {
            fprintf(stderr, "Cannot open file [%s] for reading: %s\n",
                    input_filename, g_strerror(errno));
            exit(-1);
        }
    } else {
        input_filename = "Standard input";
        input_file = stdin;
    }

    if (strcmp(argv[optind+1], "-")) {
        output_filename = g_strdup(argv[optind+1]);
        output_file = ws_fopen(output_filename, "wb");
        if (!output_file) {
            fprintf(stderr, "Cannot open file [%s] for writing: %s\n",
                    output_filename, g_strerror(errno));
            exit(-1);
        }
    } else {
        output_filename = "Standard output";
        output_file = stdout;
    }

    /* Some validation */
    if (pcap_link_type != 1 && hdr_ethernet) {
        fprintf(stderr, "Dummy headers (-e, -i, -u, -s, -S -T) cannot be specified with link type override (-l)\n");
        exit(-1);
    }

    /* Set up our variables */
    if (!input_file) {
        input_file = stdin;
        input_filename = "Standard input";
    }
    if (!output_file) {
        output_file = stdout;
        output_filename = "Standard output";
    }

    ts_sec = time(0);               /* initialize to current time */
    timecode_default = *localtime(&ts_sec);
    timecode_default.tm_isdst = -1; /* Unknown for now, depends on time given to the strptime() function */

    /* Display summary of our state */
    if (!quiet) {
        fprintf(stderr, "Input from: %s\n", input_filename);
        fprintf(stderr, "Output to: %s\n", output_filename);
        fprintf(stderr, "Output format: %s\n", use_pcapng ? "PCAP-NG" : "PCAP");

        if (hdr_ethernet) fprintf(stderr, "Generate dummy Ethernet header: Protocol: 0x%0X\n",
                                  hdr_ethernet_proto);
        if (hdr_ip) fprintf(stderr, "Generate dummy IP header: Protocol: %ld\n",
                            hdr_ip_proto);
        if (hdr_udp) fprintf(stderr, "Generate dummy UDP header: Source port: %u. Dest port: %u\n",
                             hdr_src_port, hdr_dest_port);
        if (hdr_tcp) fprintf(stderr, "Generate dummy TCP header: Source port: %u. Dest port: %u\n",
                             hdr_src_port, hdr_dest_port);
        if (hdr_sctp) fprintf(stderr, "Generate dummy SCTP header: Source port: %u. Dest port: %u. Tag: %u\n",
                              hdr_sctp_src, hdr_sctp_dest, hdr_sctp_tag);
        if (hdr_data_chunk) fprintf(stderr, "Generate dummy DATA chunk header: TSN: %u. SID: %d. SSN: %d. PPID: %u\n",
                                    hdr_data_chunk_tsn, hdr_data_chunk_sid, hdr_data_chunk_ssn, hdr_data_chunk_ppid);
    }
}

int
main(int argc, char *argv[])
{
    parse_options(argc, argv);

    assert(input_file != NULL);
    assert(output_file != NULL);

    write_file_header();

    header_length = 0;
    if (hdr_ethernet) {
        header_length += (int)sizeof(HDR_ETHERNET);
    }
    if (hdr_ip) {
        ip_offset = header_length;
        header_length += (int)sizeof(HDR_IP);
    }
    if (hdr_sctp) {
        header_length += (int)sizeof(HDR_SCTP);
    }
    if (hdr_data_chunk) {
        header_length += (int)sizeof(HDR_DATA_CHUNK);
    }
    if (hdr_tcp) {
        header_length += (int)sizeof(HDR_TCP);
    }
    if (hdr_udp) {
        header_length += (int)sizeof(HDR_UDP);
    }
    curr_offset = header_length;

    yyin = input_file;
    yylex();

    write_current_packet(FALSE);
    write_file_trailer();
    fclose(input_file);
    fclose(output_file);
    if (debug)
        fprintf(stderr, "\n-------------------------\n");
    if (!quiet) {
        fprintf(stderr, "Read %u potential packet%s, wrote %u packet%s (%" G_GINT64_MODIFIER "u byte%s).\n",
                num_packets_read, (num_packets_read == 1) ? "" : "s",
                num_packets_written, (num_packets_written == 1) ? "" : "s",
                bytes_written, (bytes_written == 1) ? "" : "s");
    }
    return 0;
}

/*
 * Editor modelines  -  http://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 *
 * vi: set shiftwidth=4 tabstop=4 expandtab:
 * :indentSize=4:tabSize=4:noTabs=true:
 */

