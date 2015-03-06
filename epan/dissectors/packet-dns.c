/* packet-dns.c
 * Routines for DNS packet disassembly
 * Copyright 2004, Nicolas DICHTEL - 6WIND - <nicolas.dichtel@6wind.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*
 * RFC 1034, RFC 1035
 * RFC 2136 for dynamic DNS
 * http://datatracker.ietf.org/doc/draft-cheshire-dnsext-multicastdns/
 *  for multicast DNS
 * RFC 4795 for link-local multicast name resolution (LLMNR)
 */

#include "config.h"

#include <string.h>
#include <memory.h>

#include <glib.h>
#include <epan/packet.h>
#include <epan/conversation.h>
#include <epan/emem.h>
#include <epan/ipproto.h>
#include <epan/addr_resolv.h>
#include "packet-dns.h"
#include "packet-tcp.h"
#include <epan/prefs.h>
#include <epan/strutil.h>
#include <epan/expert.h>
#include <epan/afn.h>

void proto_register_dns(void);
void proto_reg_handoff_dns(void);

static int proto_dns = -1;
static int hf_dns_length = -1;
static int hf_dns_flags = -1;
static int hf_dns_flags_response = -1;
static int hf_dns_flags_opcode = -1;
static int hf_dns_flags_authoritative = -1;
static int hf_dns_flags_conflict_query = -1;
static int hf_dns_flags_conflict_response = -1;
static int hf_dns_flags_truncated = -1;
static int hf_dns_flags_recdesired = -1;
static int hf_dns_flags_tentative = -1;
static int hf_dns_flags_recavail = -1;
static int hf_dns_flags_z = -1;
static int hf_dns_flags_authenticated = -1;
static int hf_dns_flags_ad = -1;
static int hf_dns_flags_checkdisable = -1;
static int hf_dns_flags_rcode = -1;
static int hf_dns_transaction_id = -1;
static int hf_dns_count_questions = -1;
static int hf_dns_count_zones = -1;
static int hf_dns_count_answers = -1;
static int hf_dns_count_prerequisites = -1;
static int hf_dns_count_updates = -1;
static int hf_dns_count_auth_rr = -1;
static int hf_dns_count_add_rr = -1;
static int hf_dns_qry_name = -1;
static int hf_dns_qry_type = -1;
static int hf_dns_qry_class = -1;
static int hf_dns_qry_class_mdns = -1;
static int hf_dns_qry_qu = -1;
static int hf_dns_srv_service = -1;
static int hf_dns_srv_proto = -1;
static int hf_dns_srv_name = -1;
static int hf_dns_srv_priority = -1;
static int hf_dns_srv_weight = -1;
static int hf_dns_srv_port = -1;
static int hf_dns_srv_target = -1;
static int hf_dns_rr_name = -1;
static int hf_dns_rr_type = -1;
static int hf_dns_rr_class = -1;
static int hf_dns_rr_class_mdns = -1;
static int hf_dns_rr_cache_flush = -1;
static int hf_dns_rr_ttl = -1;
static int hf_dns_rr_len = -1;
static int hf_dns_rr_addr = -1;
static int hf_dns_rr_primaryname = -1;
static int hf_dns_rr_udp_payload_size = -1;
static int hf_dns_soa_mname = -1;
static int hf_dns_soa_rname = -1;
static int hf_dns_soa_serial_number = -1;
static int hf_dns_soa_refresh_interval = -1;
static int hf_dns_soa_retry_interval = -1;
static int hf_dns_soa_expire_limit = -1;
static int hf_dns_soa_minimum_ttl = -1;
static int hf_dns_ptr_domain_name = -1;
static int hf_dns_wks_address = -1;
static int hf_dns_wks_protocol = -1;
static int hf_dns_wks_bits = -1;
static int hf_dns_hinfo_cpu_length = -1;
static int hf_dns_hinfo_cpu = -1;
static int hf_dns_hinfo_os_length = -1;
static int hf_dns_hinfo_os = -1;
static int hf_dns_mx_preference = -1;
static int hf_dns_mx_mail_exchange = -1;
static int hf_dns_txt_length = -1;
static int hf_dns_txt = -1;
static int hf_dns_spf_length = -1;
static int hf_dns_spf = -1;
static int hf_dns_rrsig_type_covered = -1;
static int hf_dns_rrsig_algorithm = -1;
static int hf_dns_rrsig_labels = -1;
static int hf_dns_rrsig_original_ttl = -1;
static int hf_dns_rrsig_signature_expiration = -1;
static int hf_dns_rrsig_signature_inception = -1;
static int hf_dns_rrsig_key_tag = -1;
static int hf_dns_rrsig_signers_name = -1;
static int hf_dns_rrsig_signature = -1;
static int hf_dns_dnskey_flags = -1;
static int hf_dns_dnskey_flags_zone_key = -1;
static int hf_dns_dnskey_flags_key_revoked = -1;
static int hf_dns_dnskey_flags_secure_entry_point = -1;
static int hf_dns_dnskey_flags_reserved = -1;
static int hf_dns_dnskey_protocol = -1;
static int hf_dns_dnskey_algorithm = -1;
static int hf_dns_dnskey_key_id = -1;
static int hf_dns_dnskey_public_key = -1;
static int hf_dns_t_key_flags = -1;
static int hf_dns_t_key_flags_authentication = -1;
static int hf_dns_t_key_flags_confidentiality = -1;
static int hf_dns_t_key_flags_key_required = -1;
static int hf_dns_t_key_flags_associated_user = -1;
static int hf_dns_t_key_flags_associated_named_entity = -1;
static int hf_dns_t_key_flags_ipsec = -1;
static int hf_dns_t_key_flags_mime = -1;
static int hf_dns_t_key_flags_signatory = -1;
static int hf_dns_rr_ns = -1;
static int hf_dns_rr_opt = -1;
static int hf_dns_rr_opt_code = -1;
static int hf_dns_rr_opt_len = -1;
static int hf_dns_rr_opt_data = -1;
static int hf_dns_rr_opt_client_family = -1;
static int hf_dns_rr_opt_client_netmask = -1;
static int hf_dns_rr_opt_client_scope = -1;
static int hf_dns_rr_opt_client_addr = -1;
static int hf_dns_rr_opt_client_addr4 = -1;
static int hf_dns_rr_opt_client_addr6 = -1;
static int hf_dns_nsec3_algo = -1;
static int hf_dns_nsec3_flags = -1;
static int hf_dns_nsec3_flag_optout = -1;
static int hf_dns_nsec3_iterations = -1;
static int hf_dns_nsec3_salt_length = -1;
static int hf_dns_nsec3_salt_value = -1;
static int hf_dns_nsec3_hash_length = -1;
static int hf_dns_nsec3_hash_value = -1;
static int hf_dns_tlsa_certificate_usage = -1;
static int hf_dns_tlsa_selector = -1;
static int hf_dns_tlsa_matching_type = -1;
static int hf_dns_tlsa_certificate_association_data = -1;
static int hf_dns_tsig_error = -1;
static int hf_dns_tsig_fudge = -1;
static int hf_dns_tsig_mac_size = -1;
static int hf_dns_tsig_mac = -1;
static int hf_dns_tsig_original_id = -1;
static int hf_dns_tsig_algorithm_name = -1;
static int hf_dns_tsig_other_len = -1;
static int hf_dns_tsig_other_data = -1;
static int hf_dns_response_in = -1;
static int hf_dns_response_to = -1;
static int hf_dns_time = -1;
static int hf_dns_sshfp_fingerprint = -1;
static int hf_dns_hip_hit = -1;
static int hf_dns_hip_pk = -1;
static int hf_dns_dhcid_rdata = -1;
static int hf_dns_apl_coded_prefix = -1;
static int hf_dns_apl_negation = -1;
static int hf_dns_apl_afdlength = -1;
static int hf_dns_nsap_rdata = -1;

static gint ett_dns = -1;
static gint ett_dns_qd = -1;
static gint ett_dns_rr = -1;
static gint ett_dns_qry = -1;
static gint ett_dns_ans = -1;
static gint ett_dns_flags = -1;
static gint ett_dns_opts = -1;
static gint ett_nsec3_flags = -1;
static gint ett_t_key_flags = -1;
static gint ett_t_key = -1;
static gint ett_dns_mac = -1;

static dissector_table_t dns_tsig_dissector_table=NULL;

/* Added to be able to configure DNS ports. */
static dissector_handle_t dns_tcp_handle;
static dissector_handle_t dns_udp_handle;

static range_t *global_dns_tcp_port_range;
static range_t *global_dns_udp_port_range;

/* desegmentation of DNS over TCP */
static gboolean dns_desegment = TRUE;

/* Dissector handle for GSSAPI */
static dissector_handle_t gssapi_handle;
static dissector_handle_t ntlmssp_handle;

/* Structure containing transaction specific information */
typedef struct _dns_transaction_t {
  guint32 req_frame;
  guint32 rep_frame;
  nstime_t req_time;
} dns_transaction_t;

/* Structure containing conversation specific information */
typedef struct _dns_conv_info_t {
  emem_tree_t *pdus;
} dns_conv_info_t;

/* DNS structs and definitions */

/* Ports used for DNS. */
#define DEFAULT_DNS_PORT_RANGE   "53"
#define SCTP_PORT_DNS             53
#define UDP_PORT_MDNS           5353
#define TCP_PORT_MDNS           5353
#define UDP_PORT_LLMNR          5355
#if 0
/* PPID used for DNS/SCTP (will be changed when IANA assigned) */
#define DNS_PAYLOAD_PROTOCOL_ID 1000
#endif

/* Offsets of fields in the DNS header. */
#define DNS_ID           0
#define DNS_FLAGS        2
#define DNS_QUEST        4
#define DNS_ANS          6
#define DNS_AUTH         8
#define DNS_ADD         10

/* Length of DNS header. */
#define DNS_HDRLEN      12

/* type values  */
#define T_A              1              /* host address */
#define T_NS             2              /* authoritative name server */
#define T_MD             3              /* mail destination (obsolete) */
#define T_MF             4              /* mail forwarder (obsolete) */
#define T_CNAME          5              /* canonical name */
#define T_SOA            6              /* start of authority zone */
#define T_MB             7              /* mailbox domain name (experimental) */
#define T_MG             8              /* mail group member (experimental) */
#define T_MR             9              /* mail rename domain name (experimental) */
#define T_NULL          10              /* null RR (experimental) */
#define T_WKS           11              /* well known service */
#define T_PTR           12              /* domain name pointer */
#define T_HINFO         13              /* host information */
#define T_MINFO         14              /* mailbox or mail list information */
#define T_MX            15              /* mail routing information */
#define T_TXT           16              /* text strings */
#define T_RP            17              /* responsible person (RFC 1183) */
#define T_AFSDB         18              /* AFS data base location (RFC 1183) */
#define T_X25           19              /* X.25 address (RFC 1183) */
#define T_ISDN          20              /* ISDN address (RFC 1183) */
#define T_RT            21              /* route-through (RFC 1183) */
#define T_NSAP          22              /* OSI NSAP (RFC 1706) */
#define T_NSAP_PTR      23              /* PTR equivalent for OSI NSAP (RFC 1348 - obsolete) */
#define T_SIG           24              /* digital signature (RFC 2535) */
#define T_KEY           25              /* public key (RFC 2535) */
#define T_PX            26              /* pointer to X.400/RFC822 mapping info (RFC 1664) */
#define T_GPOS          27              /* geographical position (RFC 1712) */
#define T_AAAA          28              /* IPv6 address (RFC 1886) */
#define T_LOC           29              /* geographical location (RFC 1876) */
#define T_NXT           30              /* "next" name (RFC 2535) */
#define T_EID           31              /* Endpoint Identifier */
#define T_NIMLOC        32              /* Nimrod Locator */
#define T_SRV           33              /* service location (RFC 2052) */
#define T_ATMA          34              /* ATM Address */
#define T_NAPTR         35              /* naming authority pointer (RFC 3403) */
#define T_KX            36              /* Key Exchange (RFC 2230) */
#define T_CERT          37              /* Certificate (RFC 4398) */
#define T_A6            38              /* IPv6 address with indirection (RFC 2874 - obsolete) */
#define T_DNAME         39              /* Non-terminal DNS name redirection (RFC 2672) */
#define T_SINK          40              /* SINK */
#define T_OPT           41              /* OPT pseudo-RR (RFC 2671) */
#define T_APL           42              /* Lists of Address Prefixes (APL RR) (RFC 3123) */
#define T_DS            43              /* Delegation Signature(RFC 3658) */
#define T_SSHFP         44              /* Using DNS to Securely Publish SSH Key Fingerprints (RFC 4255) */
#define T_IPSECKEY      45              /* RFC 4025 */
#define T_RRSIG         46              /* RFC 4034 */
#define T_NSEC          47              /* RFC 4034 */
#define T_DNSKEY        48              /* RFC 4034 */
#define T_DHCID         49              /* DHCID RR (RFC 4701) */
#define T_NSEC3         50              /* Next secure hash (RFC 5155) */
#define T_NSEC3PARAM    51              /* NSEC3 parameters (RFC 5155) */
#define T_TLSA          52              /* TLSA (RFC 6698) */
#define T_HIP           55              /* Host Identity Protocol (HIP) RR (RFC 5205) */
#define T_NINFO         56              /* NINFO */
#define T_RKEY          57              /* RKEY */
#define T_TALINK        58              /* Trust Anchor LINK */
#define T_CDS           59              /* Child DS */
#define T_SPF           99              /* SPF RR (RFC 4408) section 3 */
#define T_TKEY         249              /* Transaction Key (RFC 2930) */
#define T_TSIG         250              /* Transaction Signature (RFC 2845) */
#define T_IXFR         251              /* incremental transfer (RFC 1995) */
#define T_AXFR         252              /* transfer of an entire zone (RFC 5936) */
#define T_MAILB        253              /* mailbox-related RRs (MB, MG or MR) (RFC 1035) */
#define T_MAILA        254              /* mail agent RRs (OBSOLETE - see MX) (RFC 1035) */
#define T_ANY          255              /* A request for all records (RFC 1035) */
#define T_DLV        32769              /* DNSSEC Lookaside Validation (DLV) DNS Resource Record (RFC 4431) */
#define T_WINS       65281              /* Microsoft's WINS RR */
#define T_WINS_R     65282              /* Microsoft's WINS-R RR */

/* Class values */
#define C_IN             1              /* the Internet */
#define C_CS             2              /* CSNET (obsolete) */
#define C_CH             3              /* CHAOS */
#define C_HS             4              /* Hesiod */
#define C_NONE         254              /* none */
#define C_ANY          255              /* any */

#define C_QU            (1<<15)         /* High bit is set in queries for unicast queries */
#define C_FLUSH         (1<<15)         /* High bit is set for MDNS cache flush */

/* Bit fields in the flags */
#define F_RESPONSE      (1<<15)         /* packet is response */
#define F_OPCODE        (0xF<<11)       /* query opcode */
#define OPCODE_SHIFT    11
#define F_AUTHORITATIVE (1<<10)         /* response is authoritative */
#define F_CONFLICT      (1<<10)         /* conflict detected */
#define F_TRUNCATED     (1<<9)          /* response is truncated */
#define F_RECDESIRED    (1<<8)          /* recursion desired */
#define F_TENTATIVE     (1<<8)          /* response is tentative */
#define F_RECAVAIL      (1<<7)          /* recursion available */
#define F_Z             (1<<6)          /* Z */
#define F_AUTHENTIC     (1<<5)          /* authentic data (RFC2535) */
#define F_CHECKDISABLE  (1<<4)          /* checking disabled (RFC2535) */
#define F_RCODE         (0xF<<0)        /* reply code */

/* Optcode values for EDNS0 options (RFC 2671) */
#define O_LLQ            1              /* Long-lived query (on-hold, draft-sekar-dns-llq) */
#define O_UL             2              /* Update lease (on-hold, draft-sekar-dns-ul) */
#define O_NSID           3              /* Name Server Identifier (RFC 5001) */
#define O_OWNER          4              /* Owner, reserved (draft-cheshire-edns0-owner-option) */
#define O_CLIENT_SUBNET  0x50fa         /* Client subnet (placeholder value, draft-vandergaast-edns-client-subnet) */

static const true_false_string tfs_flags_response = {
  "Message is a response",
  "Message is a query"
};

static const true_false_string tfs_flags_authoritative = {
  "Server is an authority for domain",
  "Server is not an authority for domain"
};

static const true_false_string tfs_flags_conflict_query = {
  "The sender received multiple responses",
  "None"
};

static const true_false_string tfs_flags_conflict_response = {
  "The name is not considered unique",
  "The name is considered unique"
};

static const true_false_string tfs_flags_truncated = {
  "Message is truncated",
  "Message is not truncated"
};

static const true_false_string tfs_flags_recdesired = {
  "Do query recursively",
  "Don't do query recursively"
};

static const true_false_string tfs_flags_tentative = {
  "Tentative",
  "Not tentative"
};

static const true_false_string tfs_flags_recavail = {
  "Server can do recursive queries",
  "Server can't do recursive queries"
};

static const true_false_string tfs_flags_z = {
  "reserved - incorrect!",
  "reserved (0)"
};

static const true_false_string tfs_flags_authenticated = {
  "Answer/authority portion was authenticated by the server",
  "Answer/authority portion was not authenticated by the server"
};

static const true_false_string tfs_flags_checkdisable = {
  "Acceptable",
  "Unacceptable"
};

/* Opcodes */
#define OPCODE_QUERY    0         /* standard query */
#define OPCODE_IQUERY   1         /* inverse query */
#define OPCODE_STATUS   2         /* server status request */
#define OPCODE_NOTIFY   4         /* zone change notification */
#define OPCODE_UPDATE   5         /* dynamic update */

static const value_string opcode_vals[] = {
  { OPCODE_QUERY,  "Standard query"           },
  { OPCODE_IQUERY, "Inverse query"            },
  { OPCODE_STATUS, "Server status request"    },
  { OPCODE_NOTIFY, "Zone change notification" },
  { OPCODE_UPDATE, "Dynamic update"           },
  { 0,              NULL                      } };

/* Reply codes */
#define RCODE_NOERROR    0
#define RCODE_FORMERR    1
#define RCODE_SERVFAIL   2
#define RCODE_NXDOMAIN   3
#define RCODE_NOTIMPL    4
#define RCODE_REFUSED    5
#define RCODE_YXDOMAIN   6
#define RCODE_YXRRSET    7
#define RCODE_NXRRSET    8
#define RCODE_NOTAUTH    9
#define RCODE_NOTZONE   10

static const value_string rcode_vals[] = {
  { RCODE_NOERROR,   "No error"             },
  { RCODE_FORMERR,   "Format error"         },
  { RCODE_SERVFAIL,  "Server failure"       },
  { RCODE_NXDOMAIN,  "No such name"         },
  { RCODE_NOTIMPL,   "Not implemented"      },
  { RCODE_REFUSED,   "Refused"              },
  { RCODE_YXDOMAIN,  "Name exists"          },
  { RCODE_YXRRSET,   "RRset exists"         },
  { RCODE_NXRRSET,   "RRset does not exist" },
  { RCODE_NOTAUTH,   "Not authoritative"    },
  { RCODE_NOTZONE,   "Name out of zone"     },
  { 0,               NULL                   } };

#define NSEC3_HASH_RESERVED  0
#define NSEC3_HASH_SHA1      1

#define NSEC3_FLAG_OPTOUT    1

static const value_string hash_algorithms[] = {
  { NSEC3_HASH_RESERVED,  "Reserved"        },
  { NSEC3_HASH_SHA1,      "SHA-1"           },
  { 0,                    NULL              } };

static const true_false_string tfs_flags_nsec3_optout = {
  "Additional insecure delegations allowed",
  "Additional insecure delegations forbidden"
};
static const true_false_string tfs_required_experimental = { "Experimental or optional", "Required" };

/* TSIG/TKEY extended errors */
#define TSIGERROR_BADSIG   (16)
#define TSIGERROR_BADKEY   (17)
#define TSIGERROR_BADTIME  (18)
#define TSIGERROR_BADMODE  (19)
#define TSIGERROR_BADNAME  (20)
#define TSIGERROR_BADALG   (21)

static const value_string tsigerror_vals[] = {
  { TSIGERROR_BADSIG,   "Bad signature"        },
  { TSIGERROR_BADKEY,   "Bad key"              },
  { TSIGERROR_BADTIME,  "Bad time failure"     },
  { TSIGERROR_BADMODE,  "Bad mode such name"   },
  { TSIGERROR_BADNAME,  "Bad name implemented" },
  { TSIGERROR_BADALG,   "Bad algorithm"        },
  { 0,                  NULL                   } };

#define TKEYMODE_SERVERASSIGNED             (1)
#define TKEYMODE_DIFFIEHELLMAN              (2)
#define TKEYMODE_GSSAPI                     (3)
#define TKEYMODE_RESOLVERASSIGNED           (4)
#define TKEYMODE_DELETE                     (5)

#define TDSDIGEST_RESERVED (0)
#define TDSDIGEST_SHA1     (1)
#define TDSDIGEST_SHA256   (2)
#define TDSDIGEST_GOST     (3)
#define TDSDIGEST_SHA384   (4)
/*
 * SSHFP (RFC 4255) algorithm number and fingerprint types
 */
#define TSSHFP_ALGO_RESERVED   (0)
#define TSSHFP_ALGO_RSA        (1)
#define TSSHFP_ALGO_DSA        (2)
#define TSSHFP_FTYPE_RESERVED  (0)
#define TSSHFP_FTYPE_SHA1      (1)

/* HIP PK ALGO RFC 5205 */
#define THIP_ALGO_DSA          (1)
#define THIP_ALGO_RSA          (2)
#define THIP_ALGO_RESERVED     (0)

/* RFC 3123 */
#define TAPL_ADDR_FAMILY_IPV4   (AFNUM_INET)
#define TAPL_ADDR_FAMILY_IPV6   (AFNUM_INET6)
#define DNS_APL_NEGATION       (1<<7)
#define DNS_APL_AFDLENGTH      (0x7F<<0)

static const true_false_string tfs_dns_apl_negation = {
  "Yes (!)",
  "No (0)"
};

static const value_string afamily_vals[] = {
  { AFNUM_INET,      "IPv4" },
  { AFNUM_INET6,     "IPv6" },
  { 0,               NULL  }
};

/* See RFC 1035 for all RR types for which no RFC is listed, except for
   the ones with "???", and for the Microsoft WINS and WINS-R RRs, for
   which one should look at

http://www.windows.com/windows2000/en/server/help/sag_DNS_imp_UsingWinsLookup.htm

   and

http://www.microsoft.com/windows2000/library/resources/reskit/samplechapters/cncf/cncf_imp_wwaw.asp

   which discuss them to some extent. */
/* http://www.iana.org/assignments/dns-parameters */

static const value_string dns_types[] = {
  { 0,            "Unused"     },
  { T_A,          "A"          },
  { T_NS,         "NS"         },
  { T_MD,         "MD"         },
  { T_MF,         "MF"         },
  { T_CNAME,      "CNAME"      },
  { T_SOA,        "SOA"        },
  { T_MB,         "MB"         },
  { T_MG,         "MG"         },
  { T_MR,         "MR"         },
  { T_NULL,       "NULL"       },
  { T_WKS,        "WKS"        },
  { T_PTR,        "PTR"        },
  { T_HINFO,      "HINFO"      },
  { T_MINFO,      "MINFO"      },
  { T_MX,         "MX"         },
  { T_TXT,        "TXT"        },
  { T_RP,         "RP"         }, /* RFC 1183 */
  { T_AFSDB,      "AFSDB"      }, /* RFC 1183 */
  { T_X25,        "X25"        }, /* RFC 1183 */
  { T_ISDN,       "ISDN"       }, /* RFC 1183 */
  { T_RT,         "RT"         }, /* RFC 1183 */
  { T_NSAP,       "NSAP"       }, /* RFC 1706 */
  { T_NSAP_PTR,   "NSAP-PTR"   }, /* RFC 1348 */
  { T_SIG,        "SIG"        }, /* RFC 2535 */
  { T_KEY,        "KEY"        }, /* RFC 2535 */
  { T_PX,         "PX"         }, /* RFC 1664 */
  { T_GPOS,       "GPOS"       }, /* RFC 1712 */
  { T_AAAA,       "AAAA"       }, /* RFC 1886 */
  { T_LOC,        "LOC"        }, /* RFC 1886 */
  { T_NXT,        "NXT"        }, /* RFC 1876 */
  { T_EID,        "EID"        },
  { T_NIMLOC,     "NIMLOC"     },
  { T_SRV,        "SRV"        }, /* RFC 2052 */
  { T_ATMA,       "ATMA"       },
  { T_NAPTR,      "NAPTR"      }, /* RFC 3403 */
  { T_KX,         "KX"         }, /* RFC 2230 */
  { T_CERT,       "CERT"       }, /* RFC 4398 */
  { T_A6,         "A6"         }, /* RFC 2874 */
  { T_DNAME,      "DNAME"      }, /* RFC 2672 */
  { T_SINK,       "SINK"       },
  { T_OPT,        "OPT"        }, /* RFC 2671 */
  { T_APL,        "APL"        }, /* RFC 3123 */
  { T_DS,         "DS"         }, /* RFC 3658 */
  { T_SSHFP,      "SSHFP"      }, /* RFC 4255 */
  { T_IPSECKEY,   "IPSECKEY"   }, /* RFC 4025 */
  { T_RRSIG,      "RRSIG"      }, /* RFC 4034 */
  { T_NSEC,       "NSEC"       }, /* RFC 4034 */
  { T_DNSKEY,     "DNSKEY"     }, /* RFC 4034 */
  { T_DHCID,      "DHCID"      }, /* RFC 4701 */
  { T_NSEC3,      "NSEC3"      }, /* RFC 5155 */
  { T_NSEC3PARAM, "NSEC3PARAM" }, /* RFC 5155 */
  { T_TLSA,       "TLSA"       },
  { T_HIP,        "HIP"        }, /* RFC 5205 */
  { T_RKEY,       "RKEY"       },
  { T_TALINK,     "TALINK"     },
  { T_CDS,        "CDS"        },
  { T_SPF,        "SPF"        }, /* RFC 4408 */
  { 100,          "UINFO"      }, /* IANA reserved */
  { 101,          "UID"        }, /* IANA reserved */
  { 102,          "GID"        }, /* IANA reserved */
  { 103,          "UNSPEC"     }, /* IANA reserved */

  { T_TKEY,       "TKEY"       },
  { T_TSIG,       "TSIG"       },
  { T_IXFR,       "IXFR"       },
  { T_AXFR,       "AXFR"       },
  { T_MAILA,      "MAILB"      },
  { T_MAILB,      "MAILA"      },
  { T_ANY,        "ANY"        },

  { T_DLV,        "DLV"        }, /* RFC 4431 */
  
  { T_WINS,       "WINS"       },
  { T_WINS_R,     "WINS-R"     },

  {0,             NULL}
};

static const char *
dns_type_name (guint type)
{
  return val_to_str(type, dns_types, "Unknown (%u)");
}

static const char *
dns_type_description (guint type)
{
  static const char *type_names[] = {
    "unused",
    "Host address",
    "Authoritative name server",
    "Mail destination",
    "Mail forwarder",
    "Canonical name for an alias",
    "Start of zone of authority",
    "Mailbox domain name",
    "Mail group member",
    "Mail rename domain name",
    "Null resource record",
    "Well-known service description",
    "Domain name pointer",
    "Host information",
    "Mailbox or mail list information",
    "Mail exchange",
    "Text strings",
    "Responsible person",                   /* RFC 1183 */
    "AFS data base location",               /* RFC 1183 */
    "X.25 address",                         /* RFC 1183 */
    "ISDN number",                          /* RFC 1183 */
    "Route through",                        /* RFC 1183 */
    "OSI NSAP",                             /* RFC 1706 */
    "OSI NSAP name pointer",                /* RFC 1348 */
    "Signature",                            /* RFC 2535 */
    "Public key",                           /* RFC 2535 */
    "Pointer to X.400/RFC822 mapping info", /* RFC 1664 */
    "Geographical position",                /* RFC 1712 */
    "IPv6 address",                         /* RFC 1886 */
    "Location",                             /* RFC 1876 */
    "Next",                                 /* RFC 2535 */
    "Endpoint identifier",
    "Nimrod locator",
    "Service location",                     /* RFC 2052 */
    "ATM address",
    "Naming authority pointer",             /* RFC 2168 */
    "Key Exchange",                         /* RFC 2230 */
    "Certificate",                          /* RFC 4398 */
    "IPv6 address with indirection",        /* RFC 2874 */
    "Non-terminal DNS name redirection",    /* RFC 2672 */
    "SINK",
    "EDNS0 option",                         /* RFC 2671 */
    "Lists of Address Prefixes",            /* RFC 3123 */
    "Delegation Signer",                    /* RFC 3658 */
    "SSH public host key fingerprint",      /* RFC 4255 */
    "Key to use with IPSEC",                /* draft-ietf-ipseckey-rr */
    "RR signature",                         /* future RFC 2535bis */
    "Next secured",                         /* future RFC 2535bis */
    "DNS public key",                       /* future RFC 2535bis */
    "DHCP Information",                     /* RFC 4701 */
    "Next secured hash",                    /* RFC 5155 */
    "NSEC3 parameters",                     /* RFC 5155 */
    "TLSA",                                 /* RFC 6698 */
    NULL,
    NULL,
    "Host Identity Protocol",               /* RFC 5205 */
    "NINFO",
    "RKEY",
    "Trust Anchor LINK",
    "Child DS"
  };
  const char *short_name;
  const char *long_name;

  short_name = dns_type_name(type);
  if (short_name == NULL) {
    return ep_strdup_printf("Unknown (%u)", type);
  }
  if (type < array_length(type_names)) {
    long_name = type_names[type];
  } else {
    /* special cases */
    switch (type) {
        /* meta */
      case T_TKEY:
        long_name = "Transaction Key";
        break;
      case T_TSIG:
        long_name = "Transaction Signature";
        break;

        /* queries  */
      case T_IXFR:
        long_name = "Request for incremental zone transfer";   /* RFC 1995 */
        break;
      case T_AXFR:
        long_name = "Request for full zone transfer";
        break;
      case T_MAILB:
        long_name = "Request for mailbox-related records";
        break;
      case T_MAILA:
        long_name = "Request for mail agent resource records";
        break;
      case T_ANY:
        long_name = "Request for all records";
        break;
      default:
        long_name = NULL;
        break;
      }
  }

  if (long_name != NULL) {
    return ep_strdup_printf("%s (%s)", short_name, long_name);
  } else {
    return ep_strdup(short_name);
  }
}

static const value_string edns0_opt_code_vals[] = {
  {0,            "Reserved"},
  {O_LLQ,        "LLQ - Long-lived query"},
  {O_UL,         "UL - Update lease"},
  {O_NSID,       "NSID - Name Server Identifier"},
  {O_OWNER,      "Owner (reserved)"},
  {O_CLIENT_SUBNET, "Experimental - CSUBNET - Client subnet" },
  {0,            NULL}
 };
/* DNS-Based Authentication of Named Entities (DANE) Parameters
   http://www.iana.org/assignments/dane-parameters (last updated 2012-08-14)
 */
/* TLSA Certificate Usages */
#define TLSA_CU_CA 0
#define TLSA_CU_SC 1
#define TLSA_CU_TA 2
#define TLSA_CU_DI 3

static const value_string tlsa_certificate_usage_vals[] = {
  {TLSA_CU_CA, "CA constraint"},
  {TLSA_CU_SC, "Service certificate constraint"},
  {TLSA_CU_TA, "Trust anchor assertion"},
  {TLSA_CU_DI, "Domain-issued certificate"},
  {0,            NULL}
};

/* TLSA Selectors */
#define TLSA_S_FC 0
#define TLSA_S_SPKI 1

static const value_string tlsa_selector_vals[] = {
  {TLSA_S_FC, "Full certificate"},
  {TLSA_S_SPKI, "SubjectPublicKeyInfo"},
  {0,            NULL}
};

/* TLSA Matching Types */
#define TLSA_MT_NHU 0
#define TLSA_MT_S256 1
#define TLSA_MT_S512 2

static const value_string tlsa_matching_type_vals[] = {
  {TLSA_MT_NHU, "No Hash Used"},
  {TLSA_MT_S256, "SHA-256"},
  {TLSA_MT_S512, "SHA-512"},
  {0,            NULL}
};

static const value_string dns_classes[] = {
  {C_IN,   "IN"},
  {C_CS,   "CS"},
  {C_CH,   "CH"},
  {C_HS,   "HS"},
  {C_NONE, "NONE"},
  {C_ANY,  "ANY"},
  {0,NULL}
};

const char *
dns_class_name(int dns_class)
{
  return val_to_str(dns_class, dns_classes, "Unknown (%u)");
}

/* This function returns the number of bytes consumed and the expanded string
 * in *name.
 * The string is allocated with ep scope and does not need to be free()d.
 * it will be automatically free()d when the packet has been dissected.
 */
int
expand_dns_name(tvbuff_t *tvb, int offset, int max_len, int dns_data_offset,
    const guchar **name)
{
  int     start_offset    = offset;
  guchar *np;
  int     len             = -1;
  int     chars_processed = 0;
  int     data_size       = tvb_reported_length_remaining(tvb, dns_data_offset);
  int     component_len;
  int     indir_offset;
  int     maxname;

  const int min_len = 1;        /* Minimum length of encoded name (for root) */
        /* If we're about to return a value (probably negative) which is less
         * than the minimum length, we're looking at bad data and we're liable
         * to put the dissector into a loop.  Instead we throw an exception */

  maxname=MAXDNAME;
  np=(guchar *)ep_alloc(maxname);
  *name=np;

  maxname--;   /* reserve space for the trailing '\0' */
  for (;;) {
    if (max_len && offset - start_offset > max_len - 1) {
      break;
    }
    component_len = tvb_get_guint8(tvb, offset);
    offset++;
    if (component_len == 0) {
      break;
    }
    chars_processed++;
    switch (component_len & 0xc0) {

      case 0x00:
        /* Label */
        if (np != *name) {
          /* Not the first component - put in a '.'. */
          if (maxname > 0) {
            *np++ = '.';
            maxname--;
          }
        }
        while (component_len > 0) {
          if (max_len && offset - start_offset > max_len - 1) {
            THROW(ReportedBoundsError);
          }
          if (maxname > 0) {
            *np++ = tvb_get_guint8(tvb, offset);
            maxname--;
          }
          component_len--;
          offset++;
          chars_processed++;
        }
        break;

      case 0x40:
        /* Extended label (RFC 2673) */
        switch (component_len & 0x3f) {

          case 0x01:
            /* Bitstring label */
          {
            int bit_count;
            int label_len;
            int print_len;

            bit_count = tvb_get_guint8(tvb, offset);
            offset++;
            label_len = (bit_count - 1) / 8 + 1;

            if (maxname > 0) {
              print_len = g_snprintf(np, maxname + 1, "\\[x");
              if (print_len != -1 && print_len <= maxname) {
                /* Some versions of g_snprintf return -1 if they'd truncate
                   the output.  Others return <buf_size> or greater. */
                np      += print_len;
                maxname -= print_len;
              } else {
                /* Nothing printed, as there's no room.
                   Suppress all subsequent printing. */
                maxname = 0;
              }
            }
            while (label_len--) {
              if (maxname > 0) {
                print_len = g_snprintf(np, maxname + 1, "%02x",
                                       tvb_get_guint8(tvb, offset));
                if (print_len != -1 && print_len <= maxname) {
                  /* Some versions of g_snprintf return -1 if they'd truncate
                     the output.  Others return <buf_size> or greater. */
                  np      += print_len;
                  maxname -= print_len;
                } else {
                  /* Nothing printed, as there's no room.
                     Suppress all subsequent printing. */
                  maxname = 0;
                }
              }
              offset++;
            }
            if (maxname > 0) {
              print_len = g_snprintf(np, maxname + 1, "/%d]", bit_count);
              if (print_len != -1 && print_len <= maxname) {
                /* Some versions of g_snprintf return -1 if they'd truncate
                   the output.  Others return <buf_size> or greater. */
                np      += print_len;
                maxname -= print_len;
              } else {
                /* Nothing printed, as there's no room.
                   Suppress all subsequent printing. */
                maxname = 0;
              }
            }
          }
          break;

          default:
            *name="<Unknown extended label>";
            /* Parsing will probably fail from here on, since the */
            /* label length is unknown... */
            len = offset - start_offset;
            if (len < min_len) {
              THROW(ReportedBoundsError);
            }
            return len;
        }
        break;

      case 0x80:
        THROW(ReportedBoundsError);

      case 0xc0:
        /* Pointer. */
        indir_offset = dns_data_offset +
          (((component_len & ~0xc0) << 8) | tvb_get_guint8(tvb, offset));
        offset++;
        chars_processed++;

        /* If "len" is negative, we are still working on the original name,
           not something pointed to by a pointer, and so we should set "len"
           to the length of the original name. */
        if (len < 0) {
          len = offset - start_offset;
        }
        /* If we've looked at every character in the message, this pointer
           will make us look at some character again, which means we're
           looping. */
        if (chars_processed >= data_size) {
          *name="<Name contains a pointer that loops>";
          if (len < min_len) {
            THROW(ReportedBoundsError);
          }
          return len;
        }

        offset = indir_offset;
        break;   /* now continue processing from there */
    }
  }

  *np = '\0';
  /* If "len" is negative, we haven't seen a pointer, and thus haven't
     set the length, so set it. */
  if (len < 0) {
    len = offset - start_offset;
  }
  if (len < min_len) {
    THROW(ReportedBoundsError);
  }
  return len;
}

int
get_dns_name(tvbuff_t *tvb, int offset, int max_len, int dns_data_offset,
    const guchar **name)
{
  int len;

  len = expand_dns_name(tvb, offset, max_len, dns_data_offset, name);

  /* Zero-length name means "root server" */
  if (**name == '\0') {
    *name="<Root>";
  }

  return len;
}

static int
get_dns_name_type_class(tvbuff_t *tvb, int offset, int dns_data_offset,
    const guchar **name_ret, int *name_len_ret, int *type_ret, int *class_ret)
{
  int len;
  int name_len;
  int type;
  int dns_class;
  int start_offset = offset;

  /* XXX Fix data length */
  name_len = get_dns_name(tvb, offset, 0, dns_data_offset, name_ret);
  offset += name_len;

  type = tvb_get_ntohs(tvb, offset);
  offset += 2;

  dns_class = tvb_get_ntohs(tvb, offset);
  offset += 2;

  *type_ret = type;
  *class_ret = dns_class;
  *name_len_ret = name_len;

  len = offset - start_offset;
  return len;
}

static double
rfc1867_size(tvbuff_t *tvb, int offset)
{
  guint8  val;
  double  size;
  guint32 exponent;

  val = tvb_get_guint8(tvb, offset);
  size = (val & 0xF0) >> 4;
  exponent = (val & 0x0F);
  while (exponent != 0) {
    size *= 10;
    exponent--;
  }
  return size / 100;  /* return size in meters, not cm */
}

static char *
rfc1867_angle(tvbuff_t *tvb, int offset, const char *nsew)
{
  guint32     angle;
  char        direction;
  guint32     degrees, minutes, secs, tsecs;
              /* "%u deg %u min %u.%03u sec %c" */
  static char buf[10+1+3+1 + 2+1+3+1 + 2+1+3+1+3+1 + 1 + 1];

  angle = tvb_get_ntohl(tvb, offset);

  if (angle < 0x80000000U) {
    angle = 0x80000000U - angle;
    direction = nsew[1];
  } else {
    angle = angle - 0x80000000U;
    direction = nsew[0];
  }
  tsecs = angle % 1000;
  angle = angle / 1000;
  secs = angle % 60;
  angle = angle / 60;
  minutes = angle % 60;
  degrees = angle / 60;
  g_snprintf(buf, sizeof(buf), "%u deg %u min %u.%03u sec %c", degrees, minutes, secs,
             tsecs, direction);
  return buf;
}

static int
dissect_dns_query(tvbuff_t *tvb, int offset, int dns_data_offset,
  column_info *cinfo, proto_tree *dns_tree, gboolean is_mdns)
{
  int           len;
  const guchar *name;
  gchar        *name_out;
  int           name_len;
  int           type;
  int           dns_class;
  int           qu;
  const char   *type_name;
  int           data_start;
  proto_tree   *q_tree;
  proto_item   *tq;

  data_start = offset;

  len = get_dns_name_type_class(tvb, offset, dns_data_offset, &name, &name_len,
    &type, &dns_class);
  if (is_mdns) {
    /* Split the QU flag and the class */
    qu = dns_class & C_QU;
    dns_class &= ~C_QU;
  } else {
    qu = 0;
  }

  type_name = dns_type_name(type);

  /*
   * The name might contain octets that aren't printable characters,
   * format it for display.
   */
  name_out = format_text(name, strlen(name));

  if (cinfo != NULL) {
    col_append_fstr(cinfo, COL_INFO, " %s %s", type_name, name_out);
    if (is_mdns) {
      col_append_fstr(cinfo, COL_INFO, ", \"%s\" question", qu ? "QU" : "QM");
    }
  }
  if (dns_tree != NULL) {
    tq = proto_tree_add_text(dns_tree, tvb, offset, len, "%s: type %s, class %s",
                             name_out, type_name, dns_class_name(dns_class));
    if (is_mdns) {
      proto_item_append_text(tq, ", \"%s\" question", qu ? "QU" : "QM");
    }
    q_tree = proto_item_add_subtree(tq, ett_dns_qd);

    proto_tree_add_string(q_tree, hf_dns_qry_name, tvb, offset, name_len, name);
    offset += name_len;

    proto_tree_add_uint_format(q_tree, hf_dns_qry_type, tvb, offset, 2, type,
                               "Type: %s", dns_type_description(type));
    offset += 2;

    if (is_mdns) {
      proto_tree_add_uint(q_tree, hf_dns_qry_class_mdns, tvb, offset, 2, dns_class);
      proto_tree_add_boolean(q_tree, hf_dns_qry_qu, tvb, offset, 2, qu);
    } else {
      proto_tree_add_uint(q_tree, hf_dns_qry_class, tvb, offset, 2, dns_class);
    }

    offset += 2;
  }

  if (data_start + len != offset) {
    /* Add expert info ? (about incorrect len...)*/
  }
  return len;
}


static proto_tree *
add_rr_to_tree(proto_item *trr, int rr_type, tvbuff_t *tvb, int offset,
  const guchar *name, int namelen, int type, int dns_class, int flush,
  guint ttl, gushort data_len, gboolean is_mdns)
{
  proto_tree  *rr_tree;
  gchar      **srv_rr_info;

  rr_tree = proto_item_add_subtree(trr, rr_type);

  if (type == T_SRV) {
    srv_rr_info = g_strsplit(name, ".", 3);

    /* The + 1 on the strings is to skip the leading '_' */

    proto_tree_add_string(rr_tree, hf_dns_srv_service, tvb, offset,
                          namelen, srv_rr_info[0]);

    if (srv_rr_info[1]) {
      proto_tree_add_string(rr_tree, hf_dns_srv_proto, tvb, offset,
                            namelen, srv_rr_info[1]);

      if (srv_rr_info[2]) {
        proto_tree_add_string(rr_tree, hf_dns_srv_name, tvb, offset,
                              namelen, srv_rr_info[2]);
      }
    }

    g_strfreev(srv_rr_info);
  } else {
    proto_tree_add_string(rr_tree, hf_dns_rr_name, tvb, offset, namelen, name);
  }

  offset += namelen;

  proto_tree_add_uint_format(rr_tree, hf_dns_rr_type, tvb, offset, 2, type,
                             "Type: %s", dns_type_description(type));
  offset += 2;
  if (is_mdns) {
    proto_tree_add_uint(rr_tree, hf_dns_rr_class_mdns, tvb, offset, 2, dns_class);
    proto_tree_add_boolean(rr_tree, hf_dns_rr_cache_flush, tvb, offset, 2, flush);
  } else {
    proto_tree_add_uint(rr_tree, hf_dns_rr_class, tvb, offset, 2, dns_class);
  }
  offset += 2;
  proto_tree_add_uint_format(rr_tree, hf_dns_rr_ttl, tvb, offset, 4, ttl,
                             "Time to live: %s", time_secs_to_str(ttl));
  offset += 4;
  proto_tree_add_uint(rr_tree, hf_dns_rr_len, tvb, offset, 2, data_len);
  return rr_tree;
}


static proto_tree *
add_opt_rr_to_tree(proto_item *trr, int rr_type, tvbuff_t *tvb, int offset,
  const char *name, int namelen, int type, int dns_class, int flush,
  guint ttl, gushort data_len, gboolean is_mdns)
{
  proto_tree *rr_tree, *Z_tree;
  proto_item *Z_item = NULL;

  rr_tree = proto_item_add_subtree(trr, rr_type);
  proto_tree_add_string(rr_tree, hf_dns_rr_name, tvb, offset, namelen, name);
  offset += namelen;
  proto_tree_add_uint_format(rr_tree, hf_dns_rr_type, tvb, offset, 2, type,
                             "Type: %s", dns_type_description(type));
  offset += 2;
  if (is_mdns) {
    proto_tree_add_uint(rr_tree, hf_dns_rr_udp_payload_size, tvb, offset, 2, dns_class);
    proto_tree_add_boolean(rr_tree, hf_dns_rr_cache_flush, tvb, offset, 2,
       flush);
  } else {
    proto_tree_add_text(rr_tree, tvb, offset, 2, "UDP payload size: %u", dns_class & 0xffff);
  }
  offset += 2;
  proto_tree_add_text(rr_tree, tvb, offset, 1, "Higher bits in extended RCODE: 0x%x",
      (ttl >> 24) & 0xff);
  offset++;
  proto_tree_add_text(rr_tree, tvb, offset, 1, "EDNS0 version: %u",
      (ttl >> 16) & 0xff);
  offset++;
  Z_item = proto_tree_add_text(rr_tree, tvb, offset, 2, "Z: 0x%x", ttl & 0xffff);
  if (ttl & 0x8000) {
     Z_tree = proto_item_add_subtree(Z_item, rr_type);
     proto_tree_add_text(Z_tree, tvb, offset, 2, "Bit 0 (DO bit): 1 (Accepts DNSSEC security RRs)");
     proto_tree_add_text(Z_tree, tvb, offset, 2, "Bits 1-15: 0x%x (reserved)", ttl & 0x7fff);
  }
  offset += 2;
  proto_tree_add_uint(rr_tree, hf_dns_rr_len, tvb, offset, 2, data_len);
  return rr_tree;
}

static int
dissect_type_bitmap(proto_tree *rr_tree, tvbuff_t *tvb, int cur_offset, int rr_len)
{
  int    mask, blockbase, blocksize;
  int    i, initial_offset, rr_type;
  guint8 bits;

  initial_offset = cur_offset;
  while (rr_len != 0) {
    blockbase = tvb_get_guint8(tvb, cur_offset);
    blocksize = tvb_get_guint8(tvb, cur_offset + 1);
    cur_offset += 2;
    rr_len     -= 2;
    rr_type = blockbase * 256;
    for( ; blocksize; blocksize-- ) {
      bits = tvb_get_guint8(tvb, cur_offset);
      mask = 1<<7;
      for (i = 0; i < 8; i++) {
        if (bits & mask) {
          proto_tree_add_text(rr_tree, tvb, cur_offset, 1,
            "RR type in bit map: %s",
            dns_type_description(rr_type));
        }
        mask >>= 1;
        rr_type++;
      }
      cur_offset += 1;
      rr_len     -= 1;
    }
  }
  return(initial_offset - cur_offset);
}

/*
 * SIG, KEY, and CERT RR algorithms.
 * http://www.iana.org/assignments/dns-sec-alg-numbers/dns-sec-alg-numbers.txt (last updated 2012-04-13)
 */
#define DNS_ALGO_RSAMD5               1 /* RSA/MD5 */
#define DNS_ALGO_DH                   2 /* Diffie-Hellman */
#define DNS_ALGO_DSA                  3 /* DSA */
#define DNS_ALGO_ECC                  4 /* Elliptic curve crypto */
#define DNS_ALGO_RSASHA1              5 /* RSA/SHA1 */
#define DNS_ALGO_DSA_NSEC3_SHA1       6 /* DSA + NSEC3/SHA1 */
#define DNS_ALGO_RSASHA1_NSEC3_SHA1   7 /* RSA/SHA1 + NSEC3/SHA1 */
#define DNS_ALGO_RSASHA256            8 /* RSA/SHA-256 */
#define DNS_ALGO_RSASHA512           10 /* RSA/SHA-512 */
#define DNS_ALGO_ECCGOST             12 /* GOST R 34.10-2001 */
#define DNS_ALGO_ECDSAP256SHA256     13 /* ECDSA Curve P-256 with SHA-256 */
#define DNS_ALGO_ECDSAP386SHA386     14 /* ECDSA Curve P-386 with SHA-386 */
#define DNS_ALGO_HMACMD5            157 /* HMAC/MD5 */
#define DNS_ALGO_INDIRECT           252 /* Indirect key */
#define DNS_ALGO_PRIVATEDNS         253 /* Private, domain name  */
#define DNS_ALGO_PRIVATEOID         254 /* Private, OID */

static const value_string dnssec_algo_vals[] = {
  { DNS_ALGO_RSAMD5,            "RSA/MD5" },
  { DNS_ALGO_DH,                "Diffie-Hellman" },
  { DNS_ALGO_DSA,               "DSA" },
  { DNS_ALGO_ECC,               "Elliptic curve crypto" },
  { DNS_ALGO_RSASHA1,           "RSA/SHA1" },
  { DNS_ALGO_DSA_NSEC3_SHA1,    "DSA + NSEC3/SHA1" },
  { DNS_ALGO_RSASHA1_NSEC3_SHA1,"RSA/SHA1 + NSEC3/SHA1" },
  { DNS_ALGO_RSASHA256,         "RSA/SHA-256" },
  { DNS_ALGO_RSASHA512,         "RSA/SHA-512" },
  { DNS_ALGO_ECCGOST,           "GOST R 34.10-2001" },
  { DNS_ALGO_ECDSAP256SHA256,   "ECDSA Curve P-256 with SHA-256" },
  { DNS_ALGO_ECDSAP386SHA386,   "ECDSA Curve P-386 with SHA-386" },
  { DNS_ALGO_HMACMD5,           "HMAC/MD5" },
  { DNS_ALGO_INDIRECT,          "Indirect key" },
  { DNS_ALGO_PRIVATEDNS,        "Private, domain name" },
  { DNS_ALGO_PRIVATEOID,        "Private, OID" },
  { 0,                          NULL }
};

/* DNSKEY : RFC4034 */
#define DNSKEY_FLAGS_ZK 0x0100
#define DNSKEY_FLAGS_KR 0x0080
#define DNSKEY_FLAGS_SEP 0x0001
#define DNSKEY_FLAGS_RSV 0xFE7E

static const true_false_string dns_dnskey_zone_key_tfs = { "This is the zone key for specified zone", "This it not a zone key" };

/* See RFC 4398 */
#define DNS_CERT_PKIX             1     /* X509 certificate */
#define DNS_CERT_SPKI             2     /* Simple public key certificate */
#define DNS_CERT_PGP              3     /* OpenPGP packet */
#define DNS_CERT_IPKIX            4     /* Indirect PKIX */
#define DNS_CERT_ISPKI            5     /* Indirect SPKI */
#define DNS_CERT_IPGP             6     /* Indirect PGP */
#define DNS_CERT_ACPKIX           7     /* Attribute certificate */
#define DNS_CERT_IACPKIX          8     /* Indirect ACPKIX */
#define DNS_CERT_PRIVATEURI     253     /* Private, URI */
#define DNS_CERT_PRIVATEOID     254     /* Private, OID */

static const value_string cert_vals[] = {
  { DNS_CERT_PKIX,       "PKIX" },
  { DNS_CERT_SPKI,       "SPKI" },
  { DNS_CERT_PGP,        "PGP" },
  { DNS_CERT_IPKIX,      "IPKIX" },
  { DNS_CERT_ISPKI,      "ISPKI" },
  { DNS_CERT_IPGP,       "IPGP" },
  { DNS_CERT_ACPKIX,     "ACPKIX" },
  { DNS_CERT_IACPKIX,    "IACPKIX" },
  { DNS_CERT_PRIVATEURI, "Private, URI" },
  { DNS_CERT_PRIVATEOID, "Private, OID" },
  { 0,                   NULL }
};

/**
 *   Compute the key id of a KEY RR depending of the algorithm used.
 */
static guint16
compute_key_id(tvbuff_t *tvb, int offset, int size, guint8 algo)
{
  guint32 ac;
  guint8  c1, c2;

  DISSECTOR_ASSERT(size >= 4);

  switch( algo ) {
     case DNS_ALGO_RSAMD5:
       return (guint16)(tvb_get_guint8(tvb, offset + size - 3) << 8) + tvb_get_guint8( tvb, offset + size - 2 );
     default:
       for (ac = 0; size > 1; size -= 2, offset += 2) {
         c1 = tvb_get_guint8( tvb, offset );
         c2 = tvb_get_guint8( tvb, offset + 1 );
         ac +=  (c1 << 8) + c2 ;
       }
       if (size > 0) {
         c1 = tvb_get_guint8( tvb, offset );
         ac += c1 << 8;
       }
       ac += (ac >> 16) & 0xffff;
       return (guint16)(ac & 0xffff);
  }
}


static int
dissect_dns_answer(tvbuff_t *tvb, int offsetx, int dns_data_offset,
  column_info *cinfo, proto_tree *dns_tree, packet_info *pinfo,
  gboolean is_mdns)
{
  int           len;
  const guchar *name;
  gchar        *name_out;
  int           name_len;
  int           type;
  int           dns_class;
  int           flush;
  const char   *class_name;
  const char   *type_name;
  int           data_offset;
  int           cur_offset;
  int           data_start;
  guint         ttl;
  gushort       data_len;
  proto_tree   *rr_tree = NULL;
  proto_item   *trr     = NULL;

  data_start = data_offset = offsetx;
  cur_offset = offsetx;

  len = get_dns_name_type_class(tvb, offsetx, dns_data_offset, &name, &name_len,
                                &type, &dns_class);
  data_offset += len;
  cur_offset += len;
  if (is_mdns) {
    /* Split the FLUSH flag and the class */
    flush = dns_class & C_FLUSH;
    dns_class &= ~C_FLUSH;
  } else {
    flush = 0;
  }
  type_name = dns_type_name(type);
  class_name = dns_class_name(dns_class);

  ttl = tvb_get_ntohl(tvb, data_offset);
  data_offset += 4;
  cur_offset += 4;

  data_len = tvb_get_ntohs(tvb, data_offset);
  data_offset += 2;
  cur_offset  += 2;

  if (cinfo != NULL) {
    col_append_fstr(cinfo, COL_INFO, " %s", type_name);
    if (is_mdns && flush) {
      col_append_str(cinfo, COL_INFO, ", cache flush");
    }
  }
  if (dns_tree != NULL) {
    /*
     * The name might contain octets that aren't printable characters,
     * format it for display.
     */
    name_out = format_text(name, strlen(name));
    if (type != T_OPT) {
      trr = proto_tree_add_text(dns_tree, tvb, offsetx,
                                (data_offset - data_start) + data_len,
                                "%s: type %s, class %s",
                                name_out, type_name, class_name);
      rr_tree = add_rr_to_tree(trr, ett_dns_rr, tvb, offsetx, name, name_len,
                               type, dns_class, flush, ttl, data_len, is_mdns);
    } else  {
      trr = proto_tree_add_text(dns_tree, tvb, offsetx,
                                (data_offset - data_start) + data_len,
                                "%s: type %s", name_out, type_name);
      rr_tree = add_opt_rr_to_tree(trr, ett_dns_rr, tvb, offsetx, name, name_len,
                                   type, dns_class, flush, ttl, data_len, is_mdns);
    }
    if (is_mdns && flush) {
      proto_item_append_text(trr, ", cache flush");
    }
  }

  if (data_len == 0) {
    return data_offset - data_start;
  }

  switch (type) {

    case T_A:
    {
      const char *addr;
      guint32 addr_int;

      addr = tvb_ip_to_str(tvb, cur_offset);
      if (cinfo != NULL) {
        col_append_fstr(cinfo, COL_INFO, " %s", addr);
      }

      proto_item_append_text(trr, ", addr %s", addr);
      proto_tree_add_item(rr_tree, hf_dns_rr_addr, tvb, cur_offset, 4, ENC_BIG_ENDIAN);

      if ((dns_class & 0x7f) == C_IN) {
        tvb_memcpy(tvb, &addr_int, cur_offset, sizeof(addr_int));
        add_ipv4_name(addr_int, name);
      }
    }
    break;

    case T_NS:
    {
      const guchar *ns_name;
      int ns_name_len;

      ns_name_len = get_dns_name(tvb, cur_offset, 0, dns_data_offset, &ns_name);
      name_out = format_text(ns_name, strlen(ns_name));
      if (cinfo != NULL) {
        col_append_fstr(cinfo, COL_INFO, " %s", name_out);
      }
      proto_item_append_text(trr, ", ns %s", name_out);
      proto_tree_add_string(rr_tree, hf_dns_rr_ns, tvb, cur_offset, ns_name_len, name_out);

    }
    break;

    case T_CNAME:
    {
      const guchar *cname;
      int cname_len;

      cname_len = get_dns_name(tvb, cur_offset, 0, dns_data_offset, &cname);
      name_out = format_text(cname, strlen(cname));
      if (cinfo != NULL) {
        col_append_fstr(cinfo, COL_INFO, " %s", name_out);
      }
      proto_item_append_text(trr, ", cname %s", name_out);
      proto_tree_add_string(rr_tree, hf_dns_rr_primaryname, tvb, cur_offset, cname_len, name_out);

    }
    break;

    case T_SOA: /* Start Of Authority zone (6) */
    {
      const guchar *mname;
      int           mname_len;
      const guchar *rname;
      int           rname_len;
      proto_item    *ti_soa;

      /* XXX Fix data length */
      mname_len = get_dns_name(tvb, cur_offset, 0, dns_data_offset, &mname);
      name_out = format_text(mname, strlen(mname));
      if (cinfo != NULL) {
        col_append_fstr(cinfo, COL_INFO, " %s", name_out);
      }
      proto_item_append_text(trr, ", mname %s", name_out);
      proto_tree_add_string(rr_tree, hf_dns_soa_mname, tvb, cur_offset, mname_len, name_out);
      cur_offset += mname_len;

      /* XXX Fix data length */
      rname_len = get_dns_name(tvb, cur_offset, 0, dns_data_offset, &rname);
      name_out = format_text(rname, strlen(rname));
      proto_tree_add_string(rr_tree, hf_dns_soa_rname, tvb, cur_offset, rname_len, name_out);
      cur_offset += rname_len;

      proto_tree_add_item(rr_tree, hf_dns_soa_serial_number, tvb, cur_offset, 4, ENC_BIG_ENDIAN);
      cur_offset += 4;

      ti_soa = proto_tree_add_item(rr_tree, hf_dns_soa_refresh_interval, tvb, cur_offset, 4, ENC_BIG_ENDIAN);
      proto_item_append_text(ti_soa, " (%s)", time_secs_to_str(tvb_get_ntohl(tvb, cur_offset)));
      cur_offset += 4;

      ti_soa = proto_tree_add_item(rr_tree, hf_dns_soa_retry_interval, tvb, cur_offset, 4, ENC_BIG_ENDIAN);
      proto_item_append_text(ti_soa, " (%s)", time_secs_to_str(tvb_get_ntohl(tvb, cur_offset)));
      cur_offset += 4;

      ti_soa = proto_tree_add_item(rr_tree, hf_dns_soa_expire_limit, tvb, cur_offset, 4, ENC_BIG_ENDIAN);
      proto_item_append_text(ti_soa, " (%s)", time_secs_to_str(tvb_get_ntohl(tvb, cur_offset)));
      cur_offset += 4;

      ti_soa = proto_tree_add_item(rr_tree, hf_dns_soa_minimum_ttl, tvb, cur_offset, 4, ENC_BIG_ENDIAN);
      proto_item_append_text(ti_soa, " (%s)", time_secs_to_str(tvb_get_ntohl(tvb, cur_offset)));

    }
    break;

    case T_PTR:  /* Domain Name Pointer (12) */
    {
      const guchar *pname;
      int           pname_len;

      /* XXX Fix data length */
      pname_len = get_dns_name(tvb, cur_offset, 0, dns_data_offset, &pname);
      name_out = format_text(pname, strlen(pname));
      if (cinfo != NULL) {
        col_append_fstr(cinfo, COL_INFO, " %s", name_out);
      }
      proto_item_append_text(trr, ", %s", name_out);
      proto_tree_add_string(rr_tree, hf_dns_ptr_domain_name, tvb, cur_offset, pname_len, name_out);

    }
    break;

    case T_WKS: /* well known service (11) */
    {
      int            rr_len   = data_len;
      const char    *wks_addr;
      guint8         protocol;
      guint8         bits;
      int            mask;
      int            port_num;
      int            i;
      proto_item     *ti_wks;
      emem_strbuf_t *bitnames = ep_strbuf_new_label(NULL);

      if (rr_len < 4) {
        goto bad_rr;
      }
      wks_addr = tvb_ip_to_str(tvb, cur_offset);
      if (cinfo != NULL) {
        col_append_fstr(cinfo, COL_INFO, " %s", wks_addr);
      }
      proto_item_append_text(trr, ", addr %s", wks_addr);
      proto_tree_add_item(rr_tree, hf_dns_wks_address, tvb, cur_offset, 4, ENC_BIG_ENDIAN);
      cur_offset += 4;
      rr_len     -= 4;

      if (rr_len < 1) {
        goto bad_rr;
      }
      proto_tree_add_item(rr_tree, hf_dns_wks_protocol, tvb, cur_offset, 4, ENC_BIG_ENDIAN);
      protocol = tvb_get_guint8(tvb, cur_offset);
      cur_offset += 1;
      rr_len     -= 1;

      port_num = 0;
      while (rr_len != 0) {
        bits = tvb_get_guint8(tvb, cur_offset);
        if (bits != 0) {
          mask = 1<<7;
          ep_strbuf_truncate(bitnames, 0);
          for (i = 0; i < 8; i++) {
            if (bits & mask) {
              if (bitnames->len > 0) {
                ep_strbuf_append(bitnames, ", ");
              }
              switch (protocol) {

                case IP_PROTO_TCP:
                  ep_strbuf_append(bitnames, get_tcp_port(port_num));
                  break;

                case IP_PROTO_UDP:
                  ep_strbuf_append(bitnames, get_udp_port(port_num));
                  break;

                default:
                  ep_strbuf_append_printf(bitnames, "%u", port_num);
                  break;
              }
            }
            mask >>= 1;
            port_num++;
          }

          ti_wks = proto_tree_add_item(rr_tree, hf_dns_wks_bits, tvb, cur_offset, 1, ENC_BIG_ENDIAN);
          proto_item_append_text(ti_wks, " (%s)", bitnames->str);
        } else {
          port_num += 8;
        }
        cur_offset += 1;
        rr_len     -= 1;
      }

    }
    break;

    case T_HINFO: /* Host Information (13) */
    {
      int         cpu_offset;
      int         cpu_len;
      const char *cpu;
      int         os_offset;
      int         os_len;
      const char *os;

      cpu_offset = cur_offset;
      cpu_len = tvb_get_guint8(tvb, cpu_offset);
      cpu = tvb_get_ephemeral_string(tvb, cpu_offset + 1, cpu_len);
      os_offset = cpu_offset + 1 + cpu_len;
      os_len = tvb_get_guint8(tvb, os_offset);
      os = tvb_get_ephemeral_string(tvb, os_offset + 1, os_len);
      if (cinfo != NULL) {
        col_append_fstr(cinfo, COL_INFO, " %.*s %.*s", cpu_len, cpu,
                        os_len, os);
      }
      proto_item_append_text(trr, ", CPU %.*s, OS %.*s",
                             cpu_len, cpu, os_len, os);

      proto_tree_add_item(rr_tree, hf_dns_hinfo_cpu_length, tvb, cur_offset, 1, ENC_BIG_ENDIAN);
      cur_offset += 1;
      proto_tree_add_item(rr_tree, hf_dns_hinfo_cpu, tvb, cur_offset, cpu_len, ENC_BIG_ENDIAN);
      cur_offset += cpu_len;

      proto_tree_add_item(rr_tree, hf_dns_hinfo_os_length, tvb, cur_offset, 1, ENC_BIG_ENDIAN);
      cur_offset += 1;
      proto_tree_add_item(rr_tree, hf_dns_hinfo_os, tvb, cur_offset, os_len, ENC_BIG_ENDIAN);
      /* cur_offset += os_len;*/


    }
    break;

    case T_MX: /* Mail Exchange (15) */
    {
      guint16       preference = 0;
      const guchar *mx_name;
      int           mx_name_len;

      preference = tvb_get_ntohs(tvb, cur_offset);
      /* XXX Fix data length */
      mx_name_len = get_dns_name(tvb, cur_offset + 2, 0, dns_data_offset, &mx_name);
      name_out = format_text(mx_name, strlen(mx_name));
      if (cinfo != NULL) {
        col_append_fstr(cinfo, COL_INFO, " %u %s", preference, name_out);
      }
      proto_item_append_text(trr, ", preference %u, mx %s",
                             preference, name_out);
      proto_tree_add_item(rr_tree, hf_dns_mx_preference, tvb, cur_offset, 2, ENC_BIG_ENDIAN);
      cur_offset += 2;
      proto_tree_add_string(rr_tree, hf_dns_mx_mail_exchange, tvb, cur_offset, mx_name_len, name_out);
      /* cur_offset += mx_name_len; */

    }
    break;

    case T_TXT: /* Text Strings (16) */
    {
      int rr_len = data_len;
      int txt_offset;
      int txt_len;


      txt_offset = cur_offset;
      while (rr_len != 0) {
        txt_len = tvb_get_guint8(tvb, txt_offset);
        proto_tree_add_item(rr_tree, hf_dns_txt_length, tvb, txt_offset, 1, ENC_BIG_ENDIAN);
        txt_offset += 1;
        rr_len     -= 1;
        proto_tree_add_item(rr_tree, hf_dns_txt, tvb, txt_offset, txt_len, ENC_BIG_ENDIAN);
        txt_offset +=  txt_len;
        rr_len     -= txt_len;
      }

    }
    break;

    case T_SPF: /* Sender Policy Framework (99) */
    {
      int rr_len = data_len;
      int spf_offset;
      int spf_len;


      spf_offset = cur_offset;
      while (rr_len != 0) {
        spf_len = tvb_get_guint8(tvb, spf_offset);
        proto_tree_add_item(rr_tree, hf_dns_spf_length, tvb, spf_offset, 1, ENC_BIG_ENDIAN);
        spf_offset += 1;
        rr_len     -= 1;
        proto_tree_add_item(rr_tree, hf_dns_spf, tvb, spf_offset, spf_len, ENC_BIG_ENDIAN);
        spf_offset +=  spf_len;
        rr_len     -= spf_len;
      }

    }
    break;

    case T_RRSIG: /* RRSIG (46) */
    case T_SIG: /* Security Signature (24) */
    {
      int           rr_len = data_len;
      const guchar *signer_name;
      int           signer_name_len;
      proto_item    *ti;

      if (rr_len < 2) {
        goto bad_rr;
      }
      ti = proto_tree_add_item(rr_tree, hf_dns_rrsig_type_covered, tvb, cur_offset, 2, ENC_BIG_ENDIAN);
      /* Fix me : need to remove dns_type_description and replace by value_string */
      proto_item_append_text(ti, " (%s)", dns_type_description(tvb_get_ntohs(tvb, cur_offset)));
      cur_offset += 2;
      rr_len     -= 2;

      proto_tree_add_item(rr_tree, hf_dns_rrsig_algorithm, tvb, cur_offset, 1, ENC_BIG_ENDIAN);
      cur_offset += 1;
      rr_len     -= 1;

      if (rr_len < 1) {
        goto bad_rr;
      }
      proto_tree_add_item(rr_tree, hf_dns_rrsig_labels, tvb, cur_offset, 1, ENC_BIG_ENDIAN);
      cur_offset += 1;
      rr_len     -= 1;

      if (rr_len < 4) {
        goto bad_rr;
      }
      ti = proto_tree_add_item(rr_tree, hf_dns_rrsig_original_ttl, tvb, cur_offset, 4, ENC_BIG_ENDIAN);
      proto_item_append_text(ti, " (%s)", time_secs_to_str(tvb_get_ntohl(tvb, cur_offset)));
      cur_offset += 4;
      rr_len     -= 4;

      if (rr_len < 4) {
        goto bad_rr;
      }
      proto_tree_add_item(rr_tree, hf_dns_rrsig_signature_expiration, tvb, cur_offset, 4, ENC_BIG_ENDIAN);
      cur_offset += 4;
      rr_len     -= 4;

      if (rr_len < 4) {
        goto bad_rr;
      }
      proto_tree_add_item(rr_tree, hf_dns_rrsig_signature_inception, tvb, cur_offset, 4, ENC_BIG_ENDIAN);
      cur_offset += 4;
      rr_len     -= 4;

      if (rr_len < 2) {
        goto bad_rr;
      }
      proto_tree_add_item(rr_tree, hf_dns_rrsig_key_tag, tvb, cur_offset, 2, ENC_BIG_ENDIAN);
      cur_offset += 2;
      rr_len     -= 2;

      /* XXX Fix data length */
      signer_name_len = get_dns_name(tvb, cur_offset, 0, dns_data_offset, &signer_name);
      proto_tree_add_string(rr_tree, hf_dns_rrsig_signers_name, tvb, cur_offset, signer_name_len, signer_name);
      cur_offset += signer_name_len;
      rr_len     -= signer_name_len;

      if (rr_len != 0) {
        proto_tree_add_item(rr_tree, hf_dns_rrsig_signature, tvb, cur_offset, rr_len, ENC_BIG_ENDIAN);
      }
    }
    break;

    case T_DNSKEY: /* DNSKEY (48) */
    {
      int         rr_len = data_len;
      proto_item *tf, *ti_gen;
      proto_tree *flags_tree;
      guint16     key_id;
      guint8 algo;


      if (rr_len < 2) {
        goto bad_rr;
      }


      tf = proto_tree_add_item(rr_tree, hf_dns_dnskey_flags, tvb, cur_offset, 2, ENC_BIG_ENDIAN);
      flags_tree = proto_item_add_subtree(tf, ett_t_key_flags);
      proto_tree_add_item(flags_tree, hf_dns_dnskey_flags_zone_key, tvb, cur_offset, 2, ENC_BIG_ENDIAN);
      proto_tree_add_item(flags_tree, hf_dns_dnskey_flags_key_revoked, tvb, cur_offset, 2, ENC_BIG_ENDIAN);
      proto_tree_add_item(flags_tree, hf_dns_dnskey_flags_secure_entry_point, tvb, cur_offset, 2, ENC_BIG_ENDIAN);
      proto_tree_add_item(flags_tree, hf_dns_dnskey_flags_reserved, tvb, cur_offset, 2, ENC_BIG_ENDIAN);

      cur_offset += 2;
      rr_len     -= 2;

      if (rr_len < 1) {
        goto bad_rr;
      }
      /* Must have value 3, Add check ? */
      proto_tree_add_item(flags_tree, hf_dns_dnskey_protocol, tvb, cur_offset, 1, ENC_BIG_ENDIAN);
      cur_offset += 1;
      rr_len     -= 1;

      if (rr_len < 1) {
        goto bad_rr;
      }
      proto_tree_add_item(flags_tree, hf_dns_dnskey_algorithm, tvb, cur_offset, 1, ENC_BIG_ENDIAN);
      algo = tvb_get_guint8(tvb, cur_offset);

      cur_offset += 1;
      rr_len     -= 1;

      key_id = compute_key_id(tvb, cur_offset-4, rr_len+4, algo);
      ti_gen = proto_tree_add_uint(rr_tree, hf_dns_dnskey_key_id, tvb, 0, 0, key_id);
      PROTO_ITEM_SET_GENERATED(ti_gen);

      proto_tree_add_item(rr_tree, hf_dns_dnskey_public_key, tvb, cur_offset, rr_len, ENC_BIG_ENDIAN);

    }
    break;

    case T_KEY:
    {
      int         rr_len = data_len;
      guint16     flags;
      proto_item *tf, *ti_gen;
      proto_tree *flags_tree;
      guint8      algo;
      guint16     key_id;


      if (rr_len < 2) {
        goto bad_rr;
      }
      flags = tvb_get_ntohs(tvb, cur_offset);
      tf = proto_tree_add_item(rr_tree, hf_dns_t_key_flags, tvb, cur_offset, 2, ENC_BIG_ENDIAN);
      flags_tree = proto_item_add_subtree(tf, ett_t_key_flags);
      proto_tree_add_item(flags_tree, hf_dns_t_key_flags_authentication, tvb, cur_offset, 2, ENC_BIG_ENDIAN);
      proto_tree_add_item(flags_tree, hf_dns_t_key_flags_confidentiality, tvb, cur_offset, 2, ENC_BIG_ENDIAN);
      if ((flags & 0xC000) != 0xC000) {
        /* We have a key */
        proto_tree_add_item(flags_tree, hf_dns_t_key_flags_key_required, tvb, cur_offset, 2, ENC_BIG_ENDIAN);
        proto_tree_add_item(flags_tree, hf_dns_t_key_flags_associated_user, tvb, cur_offset, 2, ENC_BIG_ENDIAN);
        proto_tree_add_item(flags_tree, hf_dns_t_key_flags_associated_named_entity, tvb, cur_offset, 2, ENC_BIG_ENDIAN);
        proto_tree_add_item(flags_tree, hf_dns_t_key_flags_ipsec, tvb, cur_offset, 2, ENC_BIG_ENDIAN);
        proto_tree_add_item(flags_tree, hf_dns_t_key_flags_mime, tvb, cur_offset, 2, ENC_BIG_ENDIAN);
        proto_tree_add_item(flags_tree, hf_dns_t_key_flags_signatory, tvb, cur_offset, 2, ENC_BIG_ENDIAN);
      }
      cur_offset += 2;
      rr_len     -= 2;

      if (rr_len < 1) {
        goto bad_rr;
      }
      proto_tree_add_text(rr_tree, tvb, cur_offset, 1, "Protocol: %u",
                          tvb_get_guint8(tvb, cur_offset));
      cur_offset += 1;
      rr_len     -= 1;

      if (rr_len < 1) {
        goto bad_rr;
      }
      algo = tvb_get_guint8(tvb, cur_offset);
      proto_tree_add_text(rr_tree, tvb, cur_offset, 1, "Algorithm: %s",
                          val_to_str(algo, dnssec_algo_vals, "Unknown (0x%02X)"));
      cur_offset += 1;
      rr_len     -= 1;

      key_id = compute_key_id(tvb, cur_offset-4, rr_len+4, algo);
      ti_gen = proto_tree_add_text(rr_tree, tvb, 0, 0, "Key id: %u", key_id);
      PROTO_ITEM_SET_GENERATED(ti_gen);

      if (rr_len != 0) {
        proto_tree_add_text(rr_tree, tvb, cur_offset, rr_len, "Public key");
      }

    }
    break;
    case T_IPSECKEY:
    {
      int           rr_len = data_len;
      guint8        gw_type, algo;
      const guchar *gw;
      int           gw_name_len;

      static const value_string gw_algo[] = {
        { 1,     "DSA" },
        { 2,     "RSA" },
        { 0,      NULL }
      };



      if (rr_len < 3) {
        goto bad_rr;
      }
      proto_tree_add_text(rr_tree, tvb, cur_offset, 1, "Gateway precedence: %u",
                          tvb_get_guint8(tvb, cur_offset));
      cur_offset += 1;
      rr_len     -= 1;

      gw_type = tvb_get_guint8(tvb, cur_offset);
      cur_offset += 1;
      rr_len     -= 1;

      algo = tvb_get_guint8(tvb, cur_offset);
      proto_tree_add_text(rr_tree, tvb, cur_offset, 1, "Algorithm: %s",
                          val_to_str(algo, gw_algo, "Unknown (0x%02X)"));
      cur_offset += 1;
      rr_len     -= 1;
      switch( gw_type ) {
        case 0:
          proto_tree_add_text(rr_tree, tvb, cur_offset, 0, "Gateway: no gateway");
          break;
        case 1:
          proto_tree_add_text(rr_tree, tvb, cur_offset, 4, "Gateway: %s",
                              tvb_ip_to_str(tvb, cur_offset) );

          cur_offset += 4;
          rr_len     -= 4;
          break;
        case 2:
          proto_tree_add_text(rr_tree, tvb, cur_offset, 16, "Gateway: %s",
                              tvb_ip6_to_str(tvb, cur_offset));

          cur_offset += 16;
          rr_len     -= 16;
          break;
        case 3:
          /* XXX Fix data length */
          gw_name_len = get_dns_name(tvb, cur_offset, 0, dns_data_offset, &gw);
          proto_tree_add_text(rr_tree, tvb, cur_offset, gw_name_len,
                              "Gateway: %s", format_text(gw, strlen(gw)));

          cur_offset += gw_name_len;
          rr_len     -= gw_name_len;
          break;
        default:
          proto_tree_add_text(rr_tree, tvb, cur_offset, 0, "Gateway: Unknown gateway type(%u)", gw_type);
          break;
      }
      if (rr_len != 0) {
        proto_tree_add_text(rr_tree, tvb, cur_offset, rr_len, "Public key");
      }
    }
    break;

    case T_AAAA:
    {
      const char        *addr6;
      struct e_in6_addr  addr_in6;

      addr6 = tvb_ip6_to_str(tvb, cur_offset);
      if (cinfo != NULL) {
        col_append_fstr(cinfo, COL_INFO, " %s", addr6);
      }

      proto_item_append_text(trr, ", addr %s", addr6);
      proto_tree_add_text(rr_tree, tvb, cur_offset, 16, "Addr: %s", addr6);

      if ((dns_class & 0x7f) == C_IN) {
        tvb_memcpy(tvb, &addr_in6, cur_offset, sizeof(addr_in6));
        add_ipv6_name(&addr_in6, name);
      }
    }
    break;

    case T_A6:
    {
      unsigned short     pre_len;
      unsigned short     suf_len;
      unsigned short     suf_octet_count;
      const guchar      *pname;
      int                pname_len;
      int                a6_offset;
      int                suf_offset;
      struct e_in6_addr  suffix;

      a6_offset = cur_offset;
      pre_len = tvb_get_guint8(tvb, cur_offset);
      cur_offset++;
      suf_len = 128 - pre_len;
      suf_octet_count = suf_len ? (suf_len - 1) / 8 + 1 : 0;
      /* Pad prefix */
      for (suf_offset = 0; suf_offset < 16 - suf_octet_count; suf_offset++) {
        suffix.bytes[suf_offset] = 0;
      }
      for (; suf_offset < 16; suf_offset++) {
        suffix.bytes[suf_offset] = tvb_get_guint8(tvb, cur_offset);
        cur_offset++;
      }

      if (pre_len > 0) {
        /* XXX Fix data length */
        pname_len = get_dns_name(tvb, cur_offset, 0, dns_data_offset,
                                 &pname);
      } else {
        pname="";
        pname_len = 0;
      }
      name_out = format_text(pname, strlen(pname));

      if (cinfo != NULL) {
        col_append_fstr(cinfo, COL_INFO, " %d %s %s",
                        pre_len,
                        ip6_to_str(&suffix),
                        name_out);
      }

      proto_tree_add_text(rr_tree, tvb, a6_offset, 1,
                          "Prefix len: %u", pre_len);
      a6_offset++;
      if (suf_len) {
        proto_tree_add_text(rr_tree, tvb, a6_offset, suf_octet_count,
                            "Address suffix: %s",
                            ip6_to_str(&suffix));
        a6_offset += suf_octet_count;
      }
      if (pre_len > 0) {
        proto_tree_add_text(rr_tree, tvb, a6_offset, pname_len,
                            "Prefix name: %s", name_out);
      }
      proto_item_append_text(trr, ", addr %d %s %s",
                             pre_len,
                             ip6_to_str(&suffix),
                             name_out);

    }
    break;

    case T_DNAME:
    {
      const guchar *dname;
      int           dname_len;

      /* XXX Fix data length */
      dname_len = get_dns_name(tvb, cur_offset, 0, dns_data_offset,
                               &dname);
      name_out = format_text(dname, strlen(dname));
      if (cinfo != NULL) {
        col_append_fstr(cinfo, COL_INFO, " %s", name_out);
      }
      proto_item_append_text(trr, ", dname %s", name_out);
      proto_tree_add_text(rr_tree, tvb, cur_offset,
                          dname_len, "Target name: %s", name_out);

    }
    break;

    case T_LOC:
    {
      guint8 version;


      version = tvb_get_guint8(tvb, cur_offset);
      proto_tree_add_text(rr_tree, tvb, cur_offset, 1, "Version: %u", version);
      if (version == 0) {
        /* Version 0, the only version RFC 1876 discusses. */
        cur_offset++;

        proto_tree_add_text(rr_tree, tvb, cur_offset, 1, "Size: %g m",
                            rfc1867_size(tvb, cur_offset));
        cur_offset++;

        proto_tree_add_text(rr_tree, tvb, cur_offset, 1, "Horizontal precision: %g m",
                            rfc1867_size(tvb, cur_offset));
        cur_offset++;

        proto_tree_add_text(rr_tree, tvb, cur_offset, 1, "Vertical precision: %g m",
                            rfc1867_size(tvb, cur_offset));
        cur_offset++;

        proto_tree_add_text(rr_tree, tvb, cur_offset, 4, "Latitude: %s",
                            rfc1867_angle(tvb, cur_offset, "NS"));
        cur_offset += 4;

        proto_tree_add_text(rr_tree, tvb, cur_offset, 4, "Longitude: %s",
                            rfc1867_angle(tvb, cur_offset, "EW"));
        cur_offset += 4;

        proto_tree_add_text(rr_tree, tvb, cur_offset, 4, "Altitude: %g m",
                            ((gint32)tvb_get_ntohl(tvb, cur_offset) - 10000000)/100.0);
      } else {
        proto_tree_add_text(rr_tree, tvb, cur_offset, data_len, "Data");
      }
    }
    break;

    case T_NSEC:
    {
      int           rr_len = data_len;
      const guchar *next_domain_name;
      int           next_domain_name_len;

      /* XXX Fix data length */
      next_domain_name_len = get_dns_name(tvb, cur_offset, 0, dns_data_offset,
                                          &next_domain_name);
      name_out = format_text(next_domain_name, strlen(next_domain_name));
      if (cinfo != NULL) {
        col_append_fstr(cinfo, COL_INFO, " %s", name_out);
      }
      proto_item_append_text(trr, ", next domain name %s", name_out);
      proto_tree_add_text(rr_tree, tvb, cur_offset, next_domain_name_len,
                          "Next domain name: %s", name_out);
      cur_offset += next_domain_name_len;
      rr_len     -= next_domain_name_len;
      dissect_type_bitmap(rr_tree, tvb, cur_offset, rr_len);

    }
    break;

    case T_NSEC3:
    {
      int         rr_len, initial_offset = cur_offset;
      guint8      salt_len, hash_len;
      proto_item *flags_item;
      proto_tree *flags_tree;


      proto_tree_add_item(rr_tree, hf_dns_nsec3_algo, tvb, cur_offset++, 1, ENC_BIG_ENDIAN);
      flags_item = proto_tree_add_item(rr_tree, hf_dns_nsec3_flags, tvb, cur_offset, 1, ENC_BIG_ENDIAN);
      flags_tree = proto_item_add_subtree(flags_item, ett_nsec3_flags);
      proto_tree_add_item(flags_tree, hf_dns_nsec3_flag_optout, tvb, cur_offset, 1, ENC_BIG_ENDIAN);
      cur_offset++;
      proto_tree_add_item(rr_tree, hf_dns_nsec3_iterations, tvb, cur_offset, 2, ENC_BIG_ENDIAN);
      cur_offset += 2;
      salt_len = tvb_get_guint8(tvb, cur_offset);
      proto_tree_add_item(rr_tree, hf_dns_nsec3_salt_length, tvb, cur_offset++, 1, ENC_BIG_ENDIAN);
      proto_tree_add_item(rr_tree, hf_dns_nsec3_salt_value, tvb, cur_offset, salt_len, ENC_NA);
      cur_offset += salt_len;
      hash_len = tvb_get_guint8(tvb, cur_offset);
      proto_tree_add_item(rr_tree, hf_dns_nsec3_hash_length, tvb, cur_offset++, 1, ENC_BIG_ENDIAN);
      proto_tree_add_item(rr_tree, hf_dns_nsec3_hash_value, tvb, cur_offset, hash_len, ENC_NA);
      cur_offset += hash_len;
      rr_len = data_len - (cur_offset - initial_offset);
      dissect_type_bitmap(rr_tree, tvb, cur_offset, rr_len);

    }
    break;

    case T_NSEC3PARAM:
    {
      int salt_len;
      if (cinfo != NULL) {
        col_append_fstr(cinfo, COL_INFO, " %s", name);
      }

      proto_tree_add_item(rr_tree, hf_dns_nsec3_algo, tvb, cur_offset, 1, ENC_BIG_ENDIAN);
      cur_offset ++;
      proto_tree_add_item(rr_tree, hf_dns_nsec3_flags, tvb, cur_offset, 1, ENC_BIG_ENDIAN);
      cur_offset ++;
      proto_tree_add_item(rr_tree, hf_dns_nsec3_iterations, tvb, cur_offset, 2, ENC_BIG_ENDIAN);
      cur_offset += 2;
      salt_len = tvb_get_guint8(tvb, cur_offset);
      proto_tree_add_item(rr_tree, hf_dns_nsec3_salt_length, tvb, cur_offset, 1, ENC_BIG_ENDIAN);
      cur_offset ++;
      proto_tree_add_item(rr_tree, hf_dns_nsec3_salt_value, tvb, cur_offset, salt_len, ENC_NA);

    }
    break;

    case T_TLSA: /* DNS-Based Authentication of Named Entities (52) */
    {
      int     rr_len = data_len;
      if (cinfo != NULL) {
        col_append_fstr(cinfo, COL_INFO, " %s", name);
      }

      proto_tree_add_item(rr_tree, hf_dns_tlsa_certificate_usage, tvb, cur_offset, 1, ENC_BIG_ENDIAN);
      cur_offset ++;
      rr_len --;
      proto_tree_add_item(rr_tree, hf_dns_tlsa_selector, tvb, cur_offset, 1, ENC_BIG_ENDIAN);
      cur_offset ++;
      rr_len --;
      proto_tree_add_item(rr_tree, hf_dns_tlsa_matching_type, tvb, cur_offset, 1, ENC_BIG_ENDIAN);
      cur_offset ++;
      rr_len --;
      proto_tree_add_item(rr_tree, hf_dns_tlsa_certificate_association_data, tvb, cur_offset, rr_len, ENC_BIG_ENDIAN);

    }
    break;

    case T_NXT:
    {
      int           rr_len = data_len;
      const guchar *next_domain_name;
      int           next_domain_name_len;
      int           rr_type;
      guint8        bits;
      int           mask;
      int           i;

      /* XXX Fix data length */
      next_domain_name_len = get_dns_name(tvb, cur_offset, 0, dns_data_offset,
                                          &next_domain_name);
      name_out = format_text(next_domain_name, strlen(next_domain_name));
      if (cinfo != NULL) {
        col_append_fstr(cinfo, COL_INFO, " %s", name_out);
      }
      proto_item_append_text(trr, ", next domain name %s", name_out);
      proto_tree_add_text(rr_tree, tvb, cur_offset, next_domain_name_len,
                          "Next domain name: %s", name_out);
      cur_offset += next_domain_name_len;
      rr_len     -= next_domain_name_len;
      rr_type = 0;
      while (rr_len != 0) {
        bits = tvb_get_guint8(tvb, cur_offset);
        mask = 1<<7;
        for (i = 0; i < 8; i++) {
          if (bits & mask) {
            proto_tree_add_text(rr_tree, tvb, cur_offset, 1,
                                "RR type in bit map: %s",
                                dns_type_description(rr_type));
          }
          mask >>= 1;
          rr_type++;
        }
        cur_offset += 1;
        rr_len     -= 1;
      }

    }
    break;

    case T_KX:
    {
      guint16       preference = 0;
      const guchar *kx_name;
      int           kx_name_len;

      /* XXX Fix data length */
      kx_name_len = get_dns_name(tvb, cur_offset + 2, 0, dns_data_offset, &kx_name);
      name_out = format_text(kx_name, strlen(kx_name));
      if (cinfo != NULL) {
        col_append_fstr(cinfo, COL_INFO, " %u %s", preference, name_out);
      }
      proto_item_append_text(trr, ", preference %u, kx %s",
                             preference, name_out);
      proto_tree_add_text(rr_tree, tvb, cur_offset, 2, "Preference: %u", preference);
      proto_tree_add_text(rr_tree, tvb, cur_offset + 2, kx_name_len, "Key exchange: %s",
                          name_out);

    }
    break;

    case T_CERT:
    {
      guint16 cert_type, cert_keytag;
      guint8  cert_keyalg;
      int     rr_len = data_len;


      if (rr_len < 2) {
        goto bad_rr;
      }
      cert_type = tvb_get_ntohs(tvb, cur_offset);
      proto_tree_add_text(rr_tree, tvb, cur_offset, 2, "Type: %s",
                          val_to_str(cert_type, cert_vals,
                                     "Unknown (0x%02X)"));
      cur_offset += 2;
      rr_len     -= 2;

      if (rr_len < 2) {
        goto bad_rr;
      }
      cert_keytag = tvb_get_ntohs(tvb, cur_offset);
      proto_tree_add_text(rr_tree, tvb, cur_offset, 2, "Key tag: 0x%04x",
                          cert_keytag);
      cur_offset += 2;
      rr_len     -= 2;

      if (rr_len < 1) {
        goto bad_rr;
      }
      cert_keyalg = tvb_get_guint8(tvb, cur_offset);
      proto_tree_add_text(rr_tree, tvb, cur_offset, 1, "Algorithm: %s",
                          val_to_str(cert_keyalg, dnssec_algo_vals,
                                     "Unknown (0x%02X)"));
      cur_offset += 1;
      rr_len     -= 1;

      if (rr_len != 0) {
        proto_tree_add_text(rr_tree, tvb, cur_offset, rr_len, "Certificate or CRL");
      }
    }

    break;

    case T_OPT:
    {
      int rropt_len = data_len;
      guint16 optcode, optlen;
      proto_item *rropt;
      proto_tree *rropt_tree;

      while (rropt_len > 0) {
        if (rropt_len < 2) {
          goto bad_rr;
        }
        optcode = tvb_get_ntohs(tvb, cur_offset);
        rropt_len -= 2;

        if (rropt_len < 2) {
          goto bad_rr;
        }
        optlen = tvb_get_ntohs(tvb, cur_offset + 2);
        rropt_len -= 2;

        if (rropt_len < optlen) {
          goto bad_rr;
        }

        rropt = proto_tree_add_item(rr_tree, hf_dns_rr_opt, tvb, cur_offset, 4 + optlen, ENC_NA);
        proto_item_append_text(rropt, ": %s", val_to_str(optcode, edns0_opt_code_vals, "Unknown (%d)"));
        rropt_tree = proto_item_add_subtree(rropt, ett_dns_opts);
        proto_tree_add_item(rropt_tree, hf_dns_rr_opt_code, tvb, cur_offset, 2, ENC_BIG_ENDIAN);
        cur_offset += 2;
        rropt = proto_tree_add_item(rropt_tree, hf_dns_rr_opt_len, tvb, cur_offset, 2, ENC_BIG_ENDIAN);
        cur_offset += 2;

        proto_tree_add_item(rropt_tree, hf_dns_rr_opt_data, tvb, cur_offset, optlen, ENC_NA);
        switch(optcode) {
          case O_CLIENT_SUBNET:{
            guint16 family;
            union {
              guint32 addr;
              guint8 bytes[16];
            } ip_addr = {0};

            family = tvb_get_ntohs(tvb, cur_offset);
            proto_tree_add_item(rropt_tree, hf_dns_rr_opt_client_family, tvb, cur_offset, 2, ENC_BIG_ENDIAN);
            cur_offset += 2;
            proto_tree_add_item(rropt_tree, hf_dns_rr_opt_client_netmask, tvb, cur_offset, 1, ENC_BIG_ENDIAN);
            cur_offset += 1;
            proto_tree_add_item(rropt_tree, hf_dns_rr_opt_client_scope, tvb, cur_offset, 1, ENC_BIG_ENDIAN);
            cur_offset += 1;

            if (optlen-4 > 16) {
              expert_add_info_format(pinfo, rropt, PI_MALFORMED, PI_ERROR,
                  "Length too long for any type of IP address.");
              /* Avoid stack-smashing which occurs otherwise with the
               * following tvb_memcpy. */
              optlen = 20;
            }
            tvb_memcpy(tvb, ip_addr.bytes, cur_offset, (optlen - 4));
            switch(family) {
              case AFNUM_INET:
              proto_tree_add_ipv4(rropt_tree, hf_dns_rr_opt_client_addr4, tvb,
                                  cur_offset, (optlen - 4), ip_addr.addr);
              break;
              case AFNUM_INET6:
              proto_tree_add_ipv6(rropt_tree, hf_dns_rr_opt_client_addr6, tvb,
                                  cur_offset, (optlen - 4), ip_addr.bytes);
              break;
              default:
              proto_tree_add_item(rropt_tree, hf_dns_rr_opt_client_addr, tvb, cur_offset, (optlen - 4),
                                  ENC_NA);

              break;
            }
            cur_offset += (optlen - 4);
            rropt_len  -= optlen;
        }
        break;
          default:
          cur_offset += optlen;
          rropt_len  -= optlen;
        break;
        }

      }
    }
    break;

    case T_DS:
    case T_DLV:
    {
      guint16 keytag, digest_data_size;
      guint8  ds_algorithm, ds_digest;
      int     rr_len = data_len;

      static const value_string tds_digests[] = {
        { TDSDIGEST_RESERVED, "Reserved digest" },
        { TDSDIGEST_SHA1,     "SHA-1" },
        { TDSDIGEST_SHA256,   "SHA-256" },
        { TDSDIGEST_GOST,     "GOST R 34.11-94" },
        { TDSDIGEST_SHA384,   "SHA-384" },
        { 0, NULL }
      };


      if (rr_len < 2) {
        goto bad_rr;
      }
      keytag = tvb_get_ntohs(tvb, cur_offset);
      proto_tree_add_text(rr_tree, tvb, cur_offset, 2, "Key id: %04u", keytag);
      cur_offset += 2;
      rr_len     -= 2;

      if (rr_len < 1) {
        goto bad_rr;
      }
      ds_algorithm = tvb_get_guint8(tvb, cur_offset);
      proto_tree_add_text(rr_tree, tvb, cur_offset, 1,
                          "Algorithm: %s", val_to_str(ds_algorithm, dnssec_algo_vals,"Unknown (0x%02X)") );
      cur_offset += 1;
      rr_len     -= 1;

      if (rr_len < 1) {
        goto bad_rr;
      }
      ds_digest = tvb_get_guint8(tvb, cur_offset);
      proto_tree_add_text(rr_tree, tvb, cur_offset, 1,
                          "Digest type: %s", val_to_str(ds_digest, tds_digests, "Unknown (0x%02X)"));
      cur_offset += 1;
      rr_len     -= 1;

      if (ds_digest == TDSDIGEST_SHA1) {
        digest_data_size = 20; /* SHA1 key is always 20 bytes long */
        if (rr_len < digest_data_size) {
          goto bad_rr;
        }
        proto_tree_add_text(rr_tree, tvb, cur_offset, digest_data_size, "Public key");
      }

      if (ds_digest == TDSDIGEST_SHA256) {
        digest_data_size = 32; /* SHA256 key is always 32 bytes long */
        if (rr_len < digest_data_size) {
          goto bad_rr;
        }
        proto_tree_add_text(rr_tree, tvb, cur_offset, digest_data_size, "Public key");
      }

      if (ds_digest == TDSDIGEST_GOST) {
        digest_data_size = 64; /* GOST key is always 64 bytes long */
        if (rr_len < digest_data_size) {
          goto bad_rr;
        }
        proto_tree_add_text(rr_tree, tvb, cur_offset, digest_data_size, "Public key");
      }

      if (ds_digest == TDSDIGEST_SHA384) {
        digest_data_size = 48; /* SHA384 key is always 48 bytes long */
        if (rr_len < digest_data_size) {
          goto bad_rr;
        }
        proto_tree_add_text(rr_tree, tvb, cur_offset, digest_data_size, "Public key");
      }
    }
    break;

    case T_TKEY:
    {
      const guchar *tkey_algname;
      int           tkey_algname_len;
      guint16       tkey_mode, tkey_error, tkey_keylen, tkey_otherlen;
      int           rr_len = data_len;
      nstime_t      nstime;

      static const value_string tkey_modes[] = {
        { TKEYMODE_SERVERASSIGNED,   "Server assigned"   },
        { TKEYMODE_DIFFIEHELLMAN,    "Diffie Hellman"    },
        { TKEYMODE_GSSAPI,           "GSSAPI"            },
        { TKEYMODE_RESOLVERASSIGNED, "Resolver assigned" },
        { TKEYMODE_DELETE,           "Delete"            },
        { 0,                         NULL                } };


      proto_tree *key_tree;
      proto_item *key_item;

      /* XXX Fix data length */
      tkey_algname_len = get_dns_name(tvb, cur_offset, 0, dns_data_offset, &tkey_algname);
      proto_tree_add_text(rr_tree, tvb, cur_offset, tkey_algname_len,
                          "Algorithm name: %s",
                          format_text(tkey_algname, strlen(tkey_algname)));
      cur_offset += tkey_algname_len;
      rr_len     -= tkey_algname_len;

      if (rr_len < 4) {
        goto bad_rr;
      }
      nstime.secs = tvb_get_ntohl(tvb, cur_offset);
      nstime.nsecs = 0;
      proto_tree_add_text(rr_tree, tvb, cur_offset, 4, "Signature inception: %s",
                          abs_time_to_str(&nstime, ABSOLUTE_TIME_LOCAL, TRUE));
      cur_offset += 4;
      rr_len     -= 4;

      if (rr_len < 4) {
        goto bad_rr;
      }
      nstime.secs = tvb_get_ntohl(tvb, cur_offset);
      nstime.nsecs = 0;
      proto_tree_add_text(rr_tree, tvb, cur_offset, 4, "Signature expiration: %s",
                          abs_time_to_str(&nstime, ABSOLUTE_TIME_LOCAL, TRUE));
      cur_offset += 4;
      rr_len     -= 4;

      if (rr_len < 2) {
        goto bad_rr;
      }
      tkey_mode = tvb_get_ntohs(tvb, cur_offset);
      proto_tree_add_text(rr_tree, tvb, cur_offset, 2, "Mode: %s",
                          val_to_str(tkey_mode, tkey_modes,
                                     "Unknown (0x%04X)"));
      cur_offset += 2;
      rr_len     -= 2;

      if (rr_len < 2) {
        goto bad_rr;
      }
      tkey_error = tvb_get_ntohs(tvb, cur_offset);
      proto_tree_add_text(rr_tree, tvb, cur_offset, 2, "Error: %s",
                          val_to_str(tkey_error, rcode_vals,
                                     val_to_str(tkey_error, tsigerror_vals, "Unknown error (%x)")));
      cur_offset += 2;
      rr_len     -= 2;

      tkey_keylen = tvb_get_ntohs(tvb, cur_offset);
      proto_tree_add_text(rr_tree, tvb, cur_offset, 2, "Key Size: %u",
                          tkey_keylen);
      cur_offset += 2;
      rr_len     -= 2;

      if (tkey_keylen != 0) {
        key_item = proto_tree_add_text(
          rr_tree, tvb, cur_offset, tkey_keylen, "Key Data");

        key_tree = proto_item_add_subtree(key_item, ett_t_key);

        switch(tkey_mode) {
          case TKEYMODE_GSSAPI: {
            tvbuff_t *gssapi_tvb;

            /*
             * XXX - in at least one capture, this appears to
             * be an NTLMSSP blob, with no ASN.1 in it, in
             * a query.
             *
             * See RFC 3645 which might indicate what's going
             * on here.  (The key is an output_token from
             * GSS_Init_sec_context.)
             *
             * How the heck do we know what method is being
             * used, so we know how to decode the key?  Do we
             * have to look at the algorithm name, e.g.
             * "gss.microsoft.com"?  We currently do as the
             * the SMB dissector does in some cases, and check
             * whether the security blob begins with "NTLMSSP".
             */
            gssapi_tvb = tvb_new_subset(
              tvb, cur_offset, tkey_keylen, tkey_keylen);
            if (tvb_strneql(gssapi_tvb, 0, "NTLMSSP", 7) == 0) {
              call_dissector(ntlmssp_handle, gssapi_tvb, pinfo, key_tree);
            } else {
              call_dissector(gssapi_handle, gssapi_tvb, pinfo, key_tree);
            }
            break;
          }
          default:

            /* No dissector for this key mode */

            break;
        }

        cur_offset += tkey_keylen;
        rr_len     -= tkey_keylen;
      }

      if (rr_len < 2) {
        goto bad_rr;
      }
      tkey_otherlen = tvb_get_ntohs(tvb, cur_offset);
      proto_tree_add_text(rr_tree, tvb, cur_offset, 2, "Other Size: %u",
                          tkey_otherlen);
      cur_offset += 2;
      rr_len     -= 2;

      if (tkey_otherlen != 0) {
        if (rr_len < tkey_otherlen) {
          goto bad_rr;
          }
        proto_tree_add_text(rr_tree, tvb, cur_offset, tkey_otherlen, "Other Data");
      }

    }
    break;

    case T_TSIG:
    {
      guint16       tsig_error, tsig_timehi, tsig_siglen, tsig_otherlen;
      guint32       tsig_timelo;
      const guchar *tsig_raw_algname;
      char         *tsig_algname;
      int           tsig_algname_len;
      nstime_t      nstime;
      int           rr_len = data_len;

      /* XXX Fix data length */
      tsig_algname_len = get_dns_name(tvb, cur_offset, 0, dns_data_offset, &tsig_raw_algname);
      tsig_algname=format_text(tsig_raw_algname, strlen(tsig_raw_algname));
      proto_tree_add_string(rr_tree, hf_dns_tsig_algorithm_name, tvb, cur_offset, tsig_algname_len, tsig_algname);
      cur_offset += tsig_algname_len;
      rr_len     -= tsig_algname_len;

      if (rr_len < 6) {
        goto bad_rr;
      }
      tsig_timehi = tvb_get_ntohs(tvb, cur_offset);
      tsig_timelo = tvb_get_ntohl(tvb, cur_offset + 2);
      nstime.secs = tsig_timelo;
      nstime.nsecs = 0;
      proto_tree_add_text(rr_tree, tvb, cur_offset, 6, "Time signed: %s%s",
                          abs_time_to_str(&nstime, ABSOLUTE_TIME_LOCAL, TRUE),
                          tsig_timehi == 0 ? "" : "(high bits set)");
      cur_offset += 6;
      rr_len     -= 6;

      if (rr_len < 2) {
        goto bad_rr;
      }
      proto_tree_add_item(rr_tree, hf_dns_tsig_fudge, tvb, cur_offset, 2, ENC_BIG_ENDIAN);
      cur_offset += 2;
      rr_len     -= 2;

      if (rr_len < 2) {
        goto bad_rr;
      }
      tsig_siglen = tvb_get_ntohs(tvb, cur_offset);
      proto_tree_add_item(rr_tree, hf_dns_tsig_mac_size, tvb, cur_offset, 2, ENC_BIG_ENDIAN);
      cur_offset += 2;
      rr_len     -= 2;

      if (tsig_siglen != 0) {
        proto_item *mac_item;
        proto_tree *mac_tree;
        tvbuff_t   *sub_tvb;

        if (rr_len < tsig_siglen) {
          goto bad_rr;
        }
        mac_item = proto_tree_add_item(rr_tree, hf_dns_tsig_mac, tvb, cur_offset, tsig_siglen, ENC_NA);
        mac_tree = proto_item_add_subtree(mac_item, ett_dns_mac);

        sub_tvb=tvb_new_subset(tvb, cur_offset, tsig_siglen, tsig_siglen);

        if (!dissector_try_string(dns_tsig_dissector_table, tsig_algname, sub_tvb, pinfo, mac_tree)) {
          proto_tree_add_text(mac_tree, sub_tvb, 0, tvb_length(sub_tvb),
                              "No dissector for algorithm:%s", tsig_algname);
        }

        cur_offset += tsig_siglen;
        rr_len     -= tsig_siglen;
      }

      if (rr_len < 2) {
        goto bad_rr;
      }
      proto_tree_add_item(rr_tree, hf_dns_tsig_original_id, tvb, cur_offset, 2, ENC_BIG_ENDIAN);
      cur_offset += 2;
      rr_len     -= 2;

      if (rr_len < 2) {
        goto bad_rr;
      }
      tsig_error = tvb_get_ntohs(tvb, cur_offset);
      proto_tree_add_uint_format(rr_tree, hf_dns_tsig_error, tvb, cur_offset, 2, tsig_error, "Error: %s (%d)",
                                 val_to_str_const(tsig_error, rcode_vals,
                                            val_to_str_const(tsig_error, tsigerror_vals, "Unknown error")),
                                 tsig_error);
      cur_offset += 2;
      rr_len     -= 2;

      if (rr_len < 2) {
        goto bad_rr;
      }
      tsig_otherlen = tvb_get_ntohs(tvb, cur_offset);
      proto_tree_add_item(rr_tree, hf_dns_tsig_other_len, tvb, cur_offset, 2, ENC_BIG_ENDIAN);
      cur_offset += 2;
      rr_len     -= 2;

      if (tsig_otherlen != 0) {
        if (rr_len < tsig_otherlen) {
          goto bad_rr;
        }
        proto_tree_add_item(rr_tree, hf_dns_tsig_other_data, tvb, cur_offset, tsig_otherlen, ENC_NA);
      }

    }
    break;

    case T_WINS:
    {
      int     rr_len = data_len;
      guint32 local_flag;
      guint32 lookup_timeout;
      guint32 cache_timeout;
      guint32 nservers;


      if (rr_len < 4) {
        goto bad_rr;
      }
      local_flag = tvb_get_ntohl(tvb, cur_offset);

      proto_tree_add_text(rr_tree, tvb, cur_offset, 4, "Local flag: %s",
                          local_flag ? "true" : "false");

      cur_offset += 4;
      rr_len     -= 4;

      if (rr_len < 4) {
        goto bad_rr;
      }
      lookup_timeout = tvb_get_ntohl(tvb, cur_offset);

      proto_tree_add_text(rr_tree, tvb, cur_offset, 4, "Lookup timeout: %u seconds",
                          lookup_timeout);

      cur_offset += 4;
      rr_len     -= 4;

      if (rr_len < 4) {
        goto bad_rr;
      }
      cache_timeout = tvb_get_ntohl(tvb, cur_offset);

      proto_tree_add_text(rr_tree, tvb, cur_offset, 4, "Cache timeout: %u seconds",
                          cache_timeout);

      cur_offset += 4;
      rr_len     -= 4;

      if (rr_len < 4) {
        goto bad_rr;
      }
      nservers = tvb_get_ntohl(tvb, cur_offset);

      proto_tree_add_text(rr_tree, tvb, cur_offset, 4, "Number of WINS servers: %u",
                          nservers);

      cur_offset += 4;
      rr_len     -= 4;

      while (rr_len != 0 && nservers != 0) {
        if (rr_len < 4) {
          goto bad_rr;
        }
        proto_tree_add_text(rr_tree, tvb, cur_offset, 4, "WINS server address: %s",
                            tvb_ip_to_str(tvb, cur_offset));

        cur_offset += 4;
        rr_len     -= 4;
        nservers--;
      }

    }
    break;

    case T_WINS_R:
    {
      int           rr_len = data_len;
      guint32       local_flag;
      guint32       lookup_timeout;
      guint32       cache_timeout;
      const guchar *dname;
      int           dname_len;

      if (rr_len < 4) {
        goto bad_rr;
      }
      local_flag = tvb_get_ntohl(tvb, cur_offset);

      proto_tree_add_text(rr_tree, tvb, cur_offset, 4, "Local flag: %s",
                          local_flag ? "true" : "false");

      cur_offset += 4;
      rr_len     -= 4;

      if (rr_len < 4) {
        goto bad_rr;
      }
      lookup_timeout = tvb_get_ntohl(tvb, cur_offset);

      proto_tree_add_text(rr_tree, tvb, cur_offset, 4, "Lookup timeout: %u seconds",
                          lookup_timeout);

      cur_offset += 4;
      rr_len     -= 4;

      if (rr_len < 4) {
        goto bad_rr;
      }
      cache_timeout = tvb_get_ntohl(tvb, cur_offset);

      proto_tree_add_text(rr_tree, tvb, cur_offset, 4, "Cache timeout: %u seconds",
                          cache_timeout);

      cur_offset += 4;
      /* rr_len     -= 4; */

      /* XXX Fix data length */
      dname_len = get_dns_name(tvb, cur_offset, 0, dns_data_offset, &dname);
      name_out = format_text(dname, strlen(dname));
      if (cinfo != NULL) {
        col_append_fstr(cinfo, COL_INFO, " %s", name_out);
      }
      proto_item_append_text(trr, ", name result domain %s", name_out);
      proto_tree_add_text(rr_tree, tvb, cur_offset, dname_len, "Name result domain: %s",
                          name_out);
    }
    break;

    case T_SRV:
    {
      guint16       priority = 0;
      guint16       weight   = 0;
      guint16       port     = 0;
      const guchar *target;
      int           target_len;

      priority = tvb_get_ntohs(tvb, cur_offset);
      weight = tvb_get_ntohs(tvb, cur_offset+2);
      port = tvb_get_ntohs(tvb, cur_offset+4);

      /* XXX Fix data length */
      target_len = get_dns_name(tvb, cur_offset + 6, 0, dns_data_offset, &target);
      name_out = format_text(target, strlen(target));
      if (cinfo != NULL) {
        col_append_fstr(cinfo, COL_INFO, " %u %u %u %s", priority, weight, port, name_out);
      }
      proto_item_append_text(trr,
                             ", priority %u, weight %u, port %u, target %s",
                             priority, weight, port, name_out);
      proto_tree_add_uint(rr_tree, hf_dns_srv_priority, tvb, cur_offset, 2, priority);
      proto_tree_add_uint(rr_tree, hf_dns_srv_weight, tvb, cur_offset + 2, 2, weight);
      proto_tree_add_uint(rr_tree, hf_dns_srv_port, tvb, cur_offset + 4, 2, port);
      proto_tree_add_string(rr_tree, hf_dns_srv_target, tvb, cur_offset + 6, target_len, name_out);
    }
    break;

    case T_NAPTR:
    {
      int           offset = cur_offset;
      guint16       order;
      guint16       preference;
      gchar        *flags;
      guint8        flags_len;
      guchar       *service;
      guint8        service_len;
      guchar       *regex;
      guint8        regex_len;
      const guchar *replacement;
      int           replacement_len;

      order = tvb_get_ntohs(tvb, offset);
      offset += 2;
      preference = tvb_get_ntohs(tvb, offset);
      offset += 2;
      flags_len = tvb_get_guint8(tvb, offset);
      offset++;
      flags = tvb_get_ephemeral_string(tvb, offset, flags_len);
      offset += flags_len;
      service_len = tvb_get_guint8(tvb, offset);
      offset++;
      service = tvb_get_ephemeral_string(tvb, offset, service_len);
      offset += service_len;
      regex_len = tvb_get_guint8(tvb, offset);
      offset++;
      regex = tvb_get_ephemeral_string(tvb, offset, regex_len);
      offset += regex_len;
      replacement_len = get_dns_name(tvb, offset, 0, dns_data_offset, &replacement);
      name_out = format_text(replacement, strlen(replacement));

      if (cinfo != NULL) {
        col_append_fstr(cinfo, COL_INFO, " %u %u %s", order, preference, flags);
      }

      proto_item_append_text(trr, ", order %u, preference %u, flags %s",
                             order, preference, flags);
      offset = cur_offset;
      proto_tree_add_text(rr_tree, tvb, offset, 2, "Order: %u", order);
      offset += 2;
      proto_tree_add_text(rr_tree, tvb, offset, 2, "Preference: %u", preference);
      offset += 2;
      proto_tree_add_text(rr_tree, tvb, offset, 1, "Flags length: %u", flags_len);
      offset++;
      proto_tree_add_text(rr_tree, tvb, offset, flags_len, "Flags: \"%s\"", flags);
      offset += flags_len;
      proto_tree_add_text(rr_tree, tvb, offset, 1, "Service length: %u", service_len);
      offset++;
      proto_tree_add_text(rr_tree, tvb, offset, service_len, "Service: \"%s\"", service);
      offset += service_len;
      proto_tree_add_text(rr_tree, tvb, offset, 1, "Regex length: %u", regex_len);
      offset++;
      proto_tree_add_text(rr_tree, tvb, offset, regex_len, "Regex: \"%s\"", regex);
      offset += regex_len;
      proto_tree_add_text(rr_tree, tvb, offset, 1, "Replacement length: %u", replacement_len);
      offset++;
      proto_tree_add_text(rr_tree, tvb, offset, replacement_len, "Replacement: %s", name_out);

    }
    break;

    case T_SSHFP:
    {
      guint8 sshfp_algorithm, sshfp_type;
      int    rr_len = data_len;

      static const value_string sshfp_algo[] = {
        { TSSHFP_ALGO_RESERVED, "Reserved" },
        { TSSHFP_ALGO_RSA,      "RSA" },
        { TSSHFP_ALGO_DSA,      "DSA" },
        { 0, NULL }
      };

      static const value_string sshfp_fingertype[] = {
        { TSSHFP_FTYPE_RESERVED,  "Reserved" },
        { TSSHFP_FTYPE_SHA1,      "SHA1" },
        { 0, NULL }
      };


      if (rr_len < 1) {
        goto bad_rr;
      }
      sshfp_algorithm = tvb_get_guint8(tvb, cur_offset);
      proto_tree_add_text(rr_tree, tvb, cur_offset, 1,
                          "Algorithm: %s", val_to_str(sshfp_algorithm, sshfp_algo, "Unknown (0x%02X)"));
      cur_offset += 1;
      rr_len     -= 1;

      if (rr_len < 1) {
        goto bad_rr;
      }
      sshfp_type = tvb_get_guint8(tvb, cur_offset);
      proto_tree_add_text(rr_tree, tvb, cur_offset, 1,
                          "Fingerprint type: %s", val_to_str(sshfp_type, sshfp_fingertype, "Unknown (0x%02X)"));
      cur_offset += 1;
      rr_len     -= 1;

      if (rr_len < 1) {
        goto bad_rr;
      }
      if (rr_len != 0) {
        proto_tree_add_item(rr_tree, hf_dns_sshfp_fingerprint, tvb, cur_offset, rr_len, ENC_NA);
      }
    }
    break;

    case T_HIP:
    {
      guint8        hit_len, algo;
      guint16       pk_len;
      int           rr_len = data_len;
      int           rendezvous_len;
      const guchar *rend_server_dns_name;

      static const value_string hip_algo_vals[] = {
        { THIP_ALGO_DSA,       "DSA" },
        { THIP_ALGO_RSA,       "RSA" },
        { THIP_ALGO_RESERVED,  "Reserved" },
        { 0,                   NULL }
      };

      if (cinfo != NULL) {
        col_append_fstr(cinfo, COL_INFO, " %s", name);
      }

      if (rr_len < 1) {
        goto bad_rr;
      }
      hit_len = tvb_get_guint8(tvb, cur_offset);
      proto_tree_add_text(rr_tree, tvb, cur_offset, 1, "HIT length: %u", hit_len);
      cur_offset += 1;
      rr_len     -= 1;

      if (rr_len < 1) {
        goto bad_rr;
      }
      algo = tvb_get_guint8(tvb, cur_offset);
      proto_tree_add_text(rr_tree, tvb, cur_offset, 1,
                          "PK algorithm: %s", val_to_str(algo, hip_algo_vals, "Unknown (0x%02X)"));
      cur_offset += 1;
      rr_len     -= 1;

      if (rr_len < 1) {
        goto bad_rr;
      }
      pk_len = tvb_get_ntohs(tvb, cur_offset);
      proto_tree_add_text(rr_tree, tvb, cur_offset, 2, "PK length: %u", pk_len);
      cur_offset += 2;
      rr_len     -= 2;

      if (rr_len < 1) {
        goto bad_rr;
      }
      proto_tree_add_item(rr_tree, hf_dns_hip_hit, tvb, cur_offset, hit_len, ENC_NA);
      cur_offset += hit_len;
      rr_len     -= hit_len;

      if (rr_len < 1) {
        goto bad_rr;
      }
      proto_tree_add_item(rr_tree, hf_dns_hip_pk, tvb, cur_offset, pk_len, ENC_NA);
      cur_offset += pk_len;
      rr_len     -= pk_len;

      if (rr_len < 1) {
        goto bad_rr;
      }
      while (rr_len > 1) {
        rendezvous_len = get_dns_name(tvb, cur_offset, 0, dns_data_offset, &rend_server_dns_name);
        proto_tree_add_text(rr_tree, tvb, cur_offset, rendezvous_len, "Rendezvous Server: %s",
                            format_text(rend_server_dns_name, strlen(rend_server_dns_name)));
        cur_offset += rendezvous_len;
        rr_len     -= rendezvous_len;
      }

    }
    break;

    case T_DHCID:
    {
      if (cinfo != NULL) {
        col_append_fstr(cinfo, COL_INFO, " %s", name);
      }

      if (data_len < 1) {
        goto bad_rr;
      }
      proto_tree_add_item(rr_tree, hf_dns_dhcid_rdata, tvb, cur_offset, data_len, ENC_NA);


    }
    break;

    case T_APL:
    {
      int      rr_len = data_len;
      guint16  afamily;
      guint8   afdpart_len;
      guint8  *addr_copy;


      if (cinfo != NULL) {
        col_append_fstr(cinfo, COL_INFO, " %s", name);
      }

      while (rr_len > 1) {

        if (rr_len < 1) {
          goto bad_rr;
        }
        afamily = tvb_get_ntohs(tvb, cur_offset);
        proto_tree_add_text(rr_tree, tvb, cur_offset, 2,
                            "Address Family: %s", val_to_str(afamily, afamily_vals, "Unknown (0x%02X)"));
        cur_offset += 2;
        rr_len     -= 2;

        if (rr_len < 1) {
          goto bad_rr;
        }
        proto_tree_add_item(rr_tree, hf_dns_apl_coded_prefix, tvb, cur_offset, 1, ENC_BIG_ENDIAN);
        cur_offset += 1;
        rr_len     -= 1;

        if (rr_len < 1) {
          goto bad_rr;
        }
        afdpart_len = tvb_get_guint8(tvb, cur_offset) & DNS_APL_AFDLENGTH;
        proto_tree_add_item(rr_tree, hf_dns_apl_negation, tvb, cur_offset, 1, ENC_BIG_ENDIAN);
        proto_tree_add_item(rr_tree, hf_dns_apl_afdlength, tvb, cur_offset, 1, ENC_BIG_ENDIAN);
        cur_offset += 1;
        rr_len     -= 1;

        if (rr_len < 1) {
          goto bad_rr;
        }
        if (afamily == 1 && afdpart_len <= 4) {
          addr_copy = (guint8 *)se_alloc0(4);
        } else if (afamily == 2 && afdpart_len <= 16) {
          addr_copy = (guint8 *)se_alloc0(16);
        } else {
          goto bad_rr;
        }
        tvb_memcpy(tvb, (guint8 *)addr_copy, cur_offset, afdpart_len);
        proto_tree_add_text(rr_tree, tvb, cur_offset, afdpart_len,
                            "%s address: %s", val_to_str_const(afamily, afamily_vals, "Unknown"),
                            (afamily == 0x02) ? ip6_to_str((const struct e_in6_addr *)addr_copy)
                            : ip_to_str(addr_copy) );
        cur_offset += afdpart_len;
        rr_len     -= afdpart_len;
      }
    }
    break;

    case T_GPOS:
    {
      guint8 long_len, lat_len, alt_len;

      if (cinfo != NULL) {
        col_append_fstr(cinfo, COL_INFO, " %s", name);
      }
      if (data_len < 1) {
        goto bad_rr;
      }
      long_len = tvb_get_guint8(tvb, cur_offset);
      proto_tree_add_text(rr_tree, tvb, cur_offset + 1, long_len,
                          "Longitude: %.*s", long_len, tvb_get_ephemeral_string(tvb, cur_offset +1 , long_len));
      cur_offset += 1 + long_len;

      lat_len = tvb_get_guint8(tvb, cur_offset);
      proto_tree_add_text(rr_tree, tvb, cur_offset + 1, lat_len,
                          "Latitude: %.*s", lat_len, tvb_get_ephemeral_string(tvb, cur_offset + 1, lat_len));
      cur_offset += 1 + lat_len;

      alt_len = tvb_get_guint8(tvb, cur_offset);
      proto_tree_add_text(rr_tree, tvb, cur_offset + 1, alt_len,
                          "Altitude: %.*s", alt_len, tvb_get_ephemeral_string(tvb, cur_offset + 1, alt_len));
    }
    break;

    case T_RP:
    {
      int           mbox_dname_len, txt_dname_len;
      const guchar *mbox_dname, *txt_dname;

      if (cinfo != NULL) {
        col_append_fstr(cinfo, COL_INFO, " %s", name);
      }
      if (data_len < 1) {
        goto bad_rr;
      }
      mbox_dname_len = get_dns_name(tvb, cur_offset, 0, dns_data_offset, &mbox_dname);
      proto_tree_add_text(rr_tree, tvb, cur_offset, mbox_dname_len,
                          "Mailbox: %s", format_text(mbox_dname, strlen(mbox_dname)));
      cur_offset += mbox_dname_len;
      txt_dname_len = get_dns_name(tvb, cur_offset, 0, dns_data_offset, &txt_dname);
      proto_tree_add_text(rr_tree, tvb, cur_offset, txt_dname_len,
                          "TXT RR: %s", format_text(txt_dname, strlen(txt_dname)));
    }
    break;

    case T_AFSDB:
    case T_RT:
    {
      guint16       subtype = 0;
      const guchar *host_name;
      int           host_name_len;

      if (cinfo != NULL) {
        col_append_fstr(cinfo, COL_INFO, " %s", name);
      }
      subtype = tvb_get_ntohs(tvb, cur_offset);
      host_name_len = get_dns_name(tvb, cur_offset + 2, 0, dns_data_offset, &host_name);

      if (data_len < 1) {
        goto bad_rr;
      }
      proto_tree_add_text(rr_tree, tvb, cur_offset, 2,
                          (type == T_AFSDB) ? "Subtype: %u" : "Preference: %u", subtype);
      proto_tree_add_text(rr_tree, tvb, cur_offset + 2, host_name_len,
                          (type == T_AFSDB) ? "Hostname: %s" : "Intermediate-Host: %s",
                          format_text(host_name, strlen(host_name)));


    }
    break;

    case T_X25:
    {
      guint8 x25_len;

      if (cinfo != NULL) {
        col_append_fstr(cinfo, COL_INFO, " %s", name);
      }
      if (data_len < 1) {
        goto bad_rr;
      }
      x25_len = tvb_get_guint8(tvb, cur_offset);
      proto_tree_add_text(rr_tree, tvb, cur_offset, x25_len + 1,
                          "PSDN-Address: %.*s", x25_len,
                          tvb_get_ephemeral_string(tvb, cur_offset +1, x25_len));
    }
    break;

    case T_ISDN:
    {
      guint8 isdn_address_len, isdn_sa_len;
      int    rr_len = data_len;

      if (cinfo != NULL) {
        col_append_fstr(cinfo, COL_INFO, " %s", name);
      }
      if (rr_len < 1) {
        goto bad_rr;
      }
      isdn_address_len = tvb_get_guint8(tvb, cur_offset);
      proto_tree_add_text(rr_tree, tvb, cur_offset, isdn_address_len + 1,
                          "ISDN Address: %.*s", isdn_address_len,
                          tvb_get_ephemeral_string(tvb, cur_offset +1, isdn_address_len));
      cur_offset += 1 + isdn_address_len;
      rr_len     -= 1 + isdn_address_len;

      if (rr_len > 1)   /* ISDN SA is optional */ {
        isdn_sa_len = tvb_get_guint8(tvb, cur_offset);
        proto_tree_add_text(rr_tree, tvb, cur_offset, isdn_sa_len + 1,
                            "Subaddress: %.*s", isdn_sa_len,
                            tvb_get_ephemeral_string(tvb, cur_offset +1, isdn_sa_len));
      }
    }
    break;

    case T_PX:
    {
      int           px_map822_len, px_mapx400_len;
      const guchar *px_map822_dnsname, *px_mapx400_dnsname;

      if (cinfo != NULL) {
        col_append_fstr(cinfo, COL_INFO, " %s", name);
      }
      if (data_len < 1) {
        goto bad_rr;
      }
      proto_tree_add_text(rr_tree, tvb, cur_offset, 2, "Preference: %u", tvb_get_ntohs(tvb, cur_offset));
      cur_offset += 2;
      px_map822_len = get_dns_name(tvb, cur_offset, 0, dns_data_offset, &px_map822_dnsname);
      proto_tree_add_text(rr_tree, tvb, cur_offset, px_map822_len,
                          "MAP822: %s", format_text(px_map822_dnsname, strlen(px_map822_dnsname)));
      cur_offset += px_map822_len;
      px_mapx400_len = get_dns_name(tvb, cur_offset, 0, dns_data_offset, &px_mapx400_dnsname);
      proto_tree_add_text(rr_tree, tvb, cur_offset, px_mapx400_len,
                          "MAPX400: %s", format_text(px_mapx400_dnsname, strlen(px_mapx400_dnsname)) );
      /*cur_offset += px_mapx400_len;*/
    }
    break;

    case T_NSAP:
    {
      if (cinfo != NULL) {
        col_append_fstr(cinfo, COL_INFO, " %s", name);
      }
      if (data_len < 1) {
        goto bad_rr;
      }
      proto_tree_add_item(rr_tree, hf_dns_nsap_rdata, tvb, cur_offset, data_len, ENC_NA);
    }
    break;

    case T_NSAP_PTR:
    {
      int           nsap_ptr_owner_len;
      const guchar *nsap_ptr_owner;

      if (cinfo != NULL) {
        col_append_fstr(cinfo, COL_INFO, " %s", name);
      }
      if (data_len < 1) {
        goto bad_rr;
      }
      nsap_ptr_owner_len = get_dns_name(tvb, cur_offset, 0, dns_data_offset, &nsap_ptr_owner);
      proto_tree_add_text(rr_tree, tvb, cur_offset, nsap_ptr_owner_len,
                          "Owner: %s", format_text(nsap_ptr_owner, strlen(nsap_ptr_owner)) );
    }
    break;

    case T_MB:
    case T_MF:
    case T_MD:
    case T_MG:
    case T_MR:
    {
      int           hostname_len;
      const guchar *hostname_str;

      if (cinfo != NULL) {
        col_append_fstr(cinfo, COL_INFO, " %s", name);
      }
      if (data_len < 1) {
        goto bad_rr;
      }
      hostname_len = get_dns_name(tvb, cur_offset, 0, dns_data_offset, &hostname_str);
      proto_tree_add_text(rr_tree, tvb, cur_offset, hostname_len, "Host: %s", format_text(hostname_str, strlen(hostname_str)));
    }
    break;

    case T_MINFO:
    {
      int rmailbx_len, emailbx_len;
      const guchar *rmailbx_str, *emailbx_str;

      if (cinfo != NULL) {
        col_append_fstr(cinfo, COL_INFO, " %s", name);
      }
      if (data_len < 1) {
        goto bad_rr;
      }
      rmailbx_len = get_dns_name(tvb, cur_offset, 0, dns_data_offset, &rmailbx_str);
      proto_tree_add_text(rr_tree, tvb, cur_offset, rmailbx_len,
                          "Responsible Mailbox: %s", format_text(rmailbx_str, strlen(rmailbx_str)));
      cur_offset += rmailbx_len;
      emailbx_len = get_dns_name(tvb, cur_offset, 0, dns_data_offset, &emailbx_str);
      proto_tree_add_text(rr_tree, tvb, cur_offset, emailbx_len,
                          "Error Mailbox: %s", format_text(emailbx_str, strlen(emailbx_str)));
    }
    break;

    case T_NULL:
    {
      if (cinfo != NULL) {
        col_append_fstr(cinfo, COL_INFO, " %s", name);
      }
      proto_tree_add_text(rr_tree, tvb, cur_offset, data_len, "Data");
    }
    break;

    /* TODO: parse more record types */

    default:

      proto_tree_add_text(rr_tree, tvb, cur_offset, data_len, "Data");
      break;
  }

  data_offset += data_len;

  return data_offset - data_start;

bad_rr:

  proto_item_append_text(trr, ", bad RR length %d, too short",
                         data_len);


  data_offset += data_len;

  return data_offset - data_start;
}

static int
dissect_query_records(tvbuff_t *tvb, int cur_off, int dns_data_offset,
    int count, column_info *cinfo, proto_tree *dns_tree, gboolean isupdate,
    gboolean is_mdns)
{
  int         start_off, add_off;
  proto_tree *qatree = NULL;
  proto_item *ti     = NULL;

  start_off = cur_off;
  if (dns_tree) {
    const char *s = (isupdate ?  "Zone" : "Queries");
    ti = proto_tree_add_text(dns_tree, tvb, start_off, -1, "%s", s);
    qatree = proto_item_add_subtree(ti, ett_dns_qry);
  }
  while (count-- > 0) {
    add_off = dissect_dns_query(tvb, cur_off, dns_data_offset, cinfo, qatree,
                                is_mdns);
    cur_off += add_off;
  }
  if (ti) {
    proto_item_set_len(ti, cur_off - start_off);
  }
  return cur_off - start_off;
}

static int
dissect_answer_records(tvbuff_t *tvb, int cur_off, int dns_data_offset,
    int count, column_info *cinfo, proto_tree *dns_tree, const char *name,
    packet_info *pinfo, gboolean is_mdns)
{
  int         start_off, add_off;
  proto_tree *qatree = NULL;
  proto_item *ti     = NULL;

  start_off = cur_off;
  if (dns_tree) {
    ti = proto_tree_add_text(dns_tree, tvb, start_off, -1, "%s", name);
    qatree = proto_item_add_subtree(ti, ett_dns_ans);
  }
  while (count-- > 0) {
    add_off = dissect_dns_answer(
      tvb, cur_off, dns_data_offset, cinfo, qatree, pinfo, is_mdns);
    cur_off += add_off;
  }
  if (ti) {
    proto_item_set_len(ti, cur_off - start_off);
  }
  return cur_off - start_off;
}

static void
dissect_dns_common(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree,
    gboolean is_tcp, gboolean is_mdns, gboolean is_llmnr)
{
  int                offset   = is_tcp ? 2 : 0;
  int                dns_data_offset;
  column_info       *cinfo;
  proto_tree        *dns_tree = NULL, *field_tree;
  proto_item        *ti, *tf;
  guint16            id, flags, opcode, rcode, quest, ans, auth, add;
  int                cur_off;
  gboolean           isupdate;
  conversation_t    *conversation;
  dns_conv_info_t   *dns_info;
  dns_transaction_t *dns_trans;

  dns_data_offset = offset;

  col_clear(pinfo->cinfo, COL_INFO);


  /* To do: check for errs, etc. */
  id    = tvb_get_ntohs(tvb, offset + DNS_ID);
  flags = tvb_get_ntohs(tvb, offset + DNS_FLAGS);
  opcode = (guint16) ((flags & F_OPCODE) >> OPCODE_SHIFT);
  rcode  = (guint16)  (flags & F_RCODE);

  if (check_col(pinfo->cinfo, COL_INFO)) {
    col_add_fstr(pinfo->cinfo, COL_INFO, "%s%s 0x%04x ",
                val_to_str(opcode, opcode_vals, "Unknown operation (%u)"),
                (flags&F_RESPONSE)?" response":"", id);

    if (flags & F_RESPONSE) {
      if (rcode != RCODE_NOERROR) {
        col_append_str(pinfo-> cinfo,COL_INFO,
                val_to_str(rcode, rcode_vals, "Unknown error (%u)"));
      }
    }
    cinfo = pinfo->cinfo;
  } else {
    /* Set "cinfo" to NULL; we pass a NULL "cinfo" to the query and answer
       dissectors, as a way of saying that they shouldn't add stuff
       to the COL_INFO column (a call to "check_col(cinfo, COL_INFO)"
       is more expensive than a check that a pointer isn't NULL). */
    cinfo = NULL;
  }
  if (opcode == OPCODE_UPDATE) {
    isupdate = TRUE;
  } else {
    isupdate = FALSE;
  }
  if (tree) {
    if (is_llmnr) {
      ti = proto_tree_add_protocol_format(tree, proto_dns, tvb, 0, -1,
        "Link-local Multicast Name Resolution (%s)", (flags & F_RESPONSE) ? "response" : "query");
    } else {
      ti = proto_tree_add_protocol_format(tree, proto_dns, tvb, 0, -1,
        "Domain Name System (%s)", (flags & F_RESPONSE) ? "response" : "query");
    }

    dns_tree = proto_item_add_subtree(ti, ett_dns);
  }

  /*
   * Do we have a conversation for this connection?
   */
  conversation = find_or_create_conversation(pinfo);

  /*
   * Do we already have a state structure for this conv
   */
  dns_info = (dns_conv_info_t *)conversation_get_proto_data(conversation, proto_dns);
  if (!dns_info) {
    /* No.  Attach that information to the conversation, and add
     * it to the list of information structures.
     */
    dns_info = se_new(dns_conv_info_t);
    dns_info->pdus=se_tree_create_non_persistent(EMEM_TREE_TYPE_RED_BLACK, "dns_pdus");
    conversation_add_proto_data(conversation, proto_dns, dns_info);
  }
  if (!pinfo->fd->flags.visited) {
    if (!(flags&F_RESPONSE)) {
      /* This is a request */
      dns_trans=se_new(dns_transaction_t);
      dns_trans->req_frame=pinfo->fd->num;
      dns_trans->rep_frame=0;
      dns_trans->req_time=pinfo->fd->abs_ts;
      se_tree_insert32(dns_info->pdus, id, (void *)dns_trans);
    } else {
      dns_trans=(dns_transaction_t *)se_tree_lookup32(dns_info->pdus, id);
      if (dns_trans) {
        dns_trans->rep_frame=pinfo->fd->num;
      }
    }
  } else {
    dns_trans=(dns_transaction_t *)se_tree_lookup32(dns_info->pdus, id);
  }
  if (!dns_trans) {
    /* create a "fake" pana_trans structure */
    dns_trans=ep_new(dns_transaction_t);
    dns_trans->req_frame=0;
    dns_trans->rep_frame=0;
    dns_trans->req_time=pinfo->fd->abs_ts;
  }

  /* print state tracking in the tree */
  if (!(flags&F_RESPONSE)) {
    /* This is a request */
    if (dns_trans->rep_frame) {
      proto_item *it;

      it=proto_tree_add_uint(dns_tree, hf_dns_response_in, tvb, 0, 0, dns_trans->rep_frame);
      PROTO_ITEM_SET_GENERATED(it);
    }
  } else {
    /* This is a reply */
    if (dns_trans->req_frame) {
      proto_item *it;
      nstime_t ns;

      it=proto_tree_add_uint(dns_tree, hf_dns_response_to, tvb, 0, 0, dns_trans->req_frame);
      PROTO_ITEM_SET_GENERATED(it);

      nstime_delta(&ns, &pinfo->fd->abs_ts, &dns_trans->req_time);
      it=proto_tree_add_time(dns_tree, hf_dns_time, tvb, 0, 0, &ns);
      PROTO_ITEM_SET_GENERATED(it);
    }
  }

  if (is_tcp) {
    /* Put the length indication into the tree. */
    proto_tree_add_item(dns_tree, hf_dns_length, tvb, offset - 2, 2, ENC_BIG_ENDIAN);
  }

  proto_tree_add_uint(dns_tree, hf_dns_transaction_id, tvb,
                offset + DNS_ID, 2, id);

  tf = proto_tree_add_item(dns_tree, hf_dns_flags, tvb,
                offset + DNS_FLAGS, 2, ENC_BIG_ENDIAN);
  proto_item_append_text(tf, " %s",
                val_to_str_const(opcode, opcode_vals, "Unknown operation"));
  if (flags & F_RESPONSE) {
  proto_item_append_text(tf, " response, %s",
                val_to_str_const(rcode, rcode_vals, "Unknown error"));
  }
  field_tree = proto_item_add_subtree(tf, ett_dns_flags);
  proto_tree_add_item(field_tree, hf_dns_flags_response,
                tvb, offset + DNS_FLAGS, 2, ENC_BIG_ENDIAN);
  proto_tree_add_item(field_tree, hf_dns_flags_opcode,
                tvb, offset + DNS_FLAGS, 2, ENC_BIG_ENDIAN);
  if (is_llmnr) {
    if (flags & F_RESPONSE) {
      proto_tree_add_item(field_tree, hf_dns_flags_conflict_response,
                tvb, offset + DNS_FLAGS, 2, ENC_BIG_ENDIAN);
    } else {
      proto_tree_add_item(field_tree, hf_dns_flags_conflict_query,
                tvb, offset + DNS_FLAGS, 2, ENC_BIG_ENDIAN);
    }
    proto_tree_add_item(field_tree, hf_dns_flags_truncated,
                tvb, offset + DNS_FLAGS, 2, ENC_BIG_ENDIAN);
    proto_tree_add_item(field_tree, hf_dns_flags_tentative,
                tvb, offset + DNS_FLAGS, 2, ENC_BIG_ENDIAN);
    if (flags & F_RESPONSE) {
      proto_tree_add_item(field_tree, hf_dns_flags_rcode,
                tvb, offset + DNS_FLAGS, 2, ENC_BIG_ENDIAN);
    }
  } else {
    if (flags & F_RESPONSE) {
      proto_tree_add_item(field_tree, hf_dns_flags_authoritative,
                tvb, offset + DNS_FLAGS, 2, ENC_BIG_ENDIAN);
    }
    proto_tree_add_item(field_tree, hf_dns_flags_truncated,
                tvb, offset + DNS_FLAGS, 2, ENC_BIG_ENDIAN);
    proto_tree_add_item(field_tree, hf_dns_flags_recdesired,
                tvb, offset + DNS_FLAGS, 2, ENC_BIG_ENDIAN);
    if (flags & F_RESPONSE) {
      proto_tree_add_item(field_tree, hf_dns_flags_recavail,
                tvb, offset + DNS_FLAGS, 2, ENC_BIG_ENDIAN);
    }
    proto_tree_add_item(field_tree, hf_dns_flags_z,
                tvb, offset + DNS_FLAGS, 2, ENC_BIG_ENDIAN);
    if (flags & F_RESPONSE) {
      proto_tree_add_item(field_tree, hf_dns_flags_authenticated,
                tvb, offset + DNS_FLAGS, 2, ENC_BIG_ENDIAN);
    } else if (flags & F_AUTHENTIC) {
      proto_tree_add_item(field_tree, hf_dns_flags_ad,
                                 tvb, offset + DNS_FLAGS, 2, ENC_BIG_ENDIAN);
    }
    proto_tree_add_item(field_tree, hf_dns_flags_checkdisable,
                tvb, offset + DNS_FLAGS, 2, ENC_BIG_ENDIAN);
    if (flags & F_RESPONSE) {
      proto_tree_add_item(field_tree, hf_dns_flags_rcode,
                tvb, offset + DNS_FLAGS, 2, ENC_BIG_ENDIAN);
    }
  }

  quest = tvb_get_ntohs(tvb, offset + DNS_QUEST);
  if (tree) {
    if (isupdate) {
      proto_tree_add_uint(dns_tree, hf_dns_count_zones, tvb,
                          offset + DNS_QUEST, 2, quest);
    } else {
      proto_tree_add_uint(dns_tree, hf_dns_count_questions, tvb,
                          offset + DNS_QUEST, 2, quest);
    }
  }
  ans = tvb_get_ntohs(tvb, offset + DNS_ANS);
  if (tree) {
    if (isupdate) {
      proto_tree_add_uint(dns_tree, hf_dns_count_prerequisites, tvb,
                          offset + DNS_ANS, 2, ans);
    } else {
      proto_tree_add_uint(dns_tree, hf_dns_count_answers, tvb,
                          offset + DNS_ANS, 2, ans);
    }
  }
  auth = tvb_get_ntohs(tvb, offset + DNS_AUTH);
  if (tree) {
    if (isupdate) {
      proto_tree_add_uint(dns_tree, hf_dns_count_updates, tvb,
                          offset + DNS_AUTH, 2, auth);
    } else {
      proto_tree_add_uint(dns_tree, hf_dns_count_auth_rr, tvb,
                          offset + DNS_AUTH, 2, auth);
    }
  }
  add = tvb_get_ntohs(tvb, offset + DNS_ADD);
  if (tree) {
    proto_tree_add_uint(dns_tree, hf_dns_count_add_rr, tvb,
                        offset + DNS_ADD, 2, add);

  }
  cur_off = offset + DNS_HDRLEN;

  if (quest > 0) {
    /* If this is a response, don't add information about the queries
       to the summary, just add information about the answers. */
    cur_off += dissect_query_records(tvb, cur_off, dns_data_offset, quest,
                                     (!(flags & F_RESPONSE) ? cinfo : NULL),
                                     dns_tree, isupdate, is_mdns);
  }

  if (ans > 0) {
    /* If this is a request, don't add information about the answers
       to the summary, just add information about the queries. */
    cur_off += dissect_answer_records(tvb, cur_off, dns_data_offset, ans,
                                      ((flags & F_RESPONSE) ? cinfo : NULL),
                                      dns_tree, (isupdate ?
                                                 "Prerequisites" : "Answers"),
                                      pinfo, is_mdns);
  }

  /* Don't add information about the authoritative name servers, or the
     additional records, to the summary. */
  if (auth > 0) {
    cur_off += dissect_answer_records(tvb, cur_off, dns_data_offset, auth,
                                      NULL, dns_tree,
                                      (isupdate ? "Updates" :
                                       "Authoritative nameservers"),
                                      pinfo, is_mdns);
  }

  if (add > 0) {
    dissect_answer_records(tvb, cur_off, dns_data_offset, add,
                                      NULL, dns_tree, "Additional records",
                                      pinfo, is_mdns);
  }
}

static void
dissect_dns_udp(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree)
{
  col_set_str(pinfo->cinfo, COL_PROTOCOL, "DNS");

  dissect_dns_common(tvb, pinfo, tree, FALSE, FALSE, FALSE);
}

static void
dissect_dns_sctp(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree)
{
  col_set_str(pinfo->cinfo, COL_PROTOCOL, "DNS");

  dissect_dns_common(tvb, pinfo, tree, FALSE, FALSE, FALSE);
}

static void
dissect_mdns_udp(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree)
{
  col_set_str(pinfo->cinfo, COL_PROTOCOL, "MDNS");

  dissect_dns_common(tvb, pinfo, tree, FALSE, TRUE, FALSE);
}

static void
dissect_llmnr_udp(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree)
{
  col_set_str(pinfo->cinfo, COL_PROTOCOL, "LLMNR");

  dissect_dns_common(tvb, pinfo, tree, FALSE, FALSE, TRUE);
}

static guint
get_dns_pdu_len(packet_info *pinfo _U_, tvbuff_t *tvb, int offset)
{
  guint16 plen;

  /*
   * Get the length of the DNS packet.
   */
  plen = tvb_get_ntohs(tvb, offset);

  /*
   * That length doesn't include the length field itself; add that in.
   */
  return plen + 2;
}

static void
dissect_dns_tcp_pdu(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree)
{
  col_set_str(pinfo->cinfo, COL_PROTOCOL, "DNS");

  dissect_dns_common(tvb, pinfo, tree, TRUE, FALSE, FALSE);
}

static void
dissect_dns_tcp(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree)
{
  tcp_dissect_pdus(tvb, pinfo, tree, dns_desegment, 2, get_dns_pdu_len,
                   dissect_dns_tcp_pdu);
}

static void
tcp_range_delete_callback(guint32 port)
{
  dissector_delete_uint("tcp.port", port, dns_tcp_handle);
}

static void
udp_range_delete_callback(guint32 port)
{
  dissector_delete_uint("udp.port", port, dns_udp_handle);
}

static void
tcp_range_add_callback(guint32 port)
{
  dissector_add_uint("tcp.port", port, dns_tcp_handle);
}

static void
udp_range_add_callback(guint32 port)
{
  dissector_add_uint("udp.port", port, dns_udp_handle);
}

void
proto_reg_handoff_dns(void)
{
  dissector_handle_t dns_sctp_handle;
  dissector_handle_t mdns_udp_handle;
  dissector_handle_t llmnr_udp_handle;

  static range_t *dns_tcp_port_range;
  static range_t *dns_udp_port_range;

  static gboolean Initialized = FALSE;

  if (!Initialized) {
    dns_udp_handle = create_dissector_handle(dissect_dns_udp, proto_dns);
    dns_tcp_handle = create_dissector_handle(dissect_dns_tcp, proto_dns);
    Initialized    = TRUE;

  } else {
    range_foreach(dns_tcp_port_range, tcp_range_delete_callback);
    range_foreach(dns_udp_port_range, udp_range_delete_callback);
    g_free(dns_tcp_port_range);
    g_free(dns_udp_port_range);
  }
    
  dns_tcp_port_range = range_copy(global_dns_tcp_port_range);
  dns_udp_port_range = range_copy(global_dns_udp_port_range);
  range_foreach(dns_tcp_port_range, tcp_range_add_callback);
  range_foreach(dns_udp_port_range, udp_range_add_callback);

  dns_sctp_handle  = create_dissector_handle(dissect_dns_sctp, proto_dns);
  mdns_udp_handle  = create_dissector_handle(dissect_mdns_udp, proto_dns);
  llmnr_udp_handle = create_dissector_handle(dissect_llmnr_udp, proto_dns);

  dissector_add_uint("udp.port", UDP_PORT_MDNS, mdns_udp_handle);
  dissector_add_uint("tcp.port", TCP_PORT_MDNS, dns_tcp_handle);
  dissector_add_uint("udp.port", UDP_PORT_LLMNR, llmnr_udp_handle);
  dissector_add_uint("sctp.port", SCTP_PORT_DNS, dns_sctp_handle);
#if 0
  dissector_add_uint("sctp.ppi",  DNS_PAYLOAD_PROTOCOL_ID, dns_sctp_handle);
#endif

  gssapi_handle  = find_dissector("gssapi");
  ntlmssp_handle = find_dissector("ntlmssp");
}

void
proto_register_dns(void)
{
  static hf_register_info hf[] = {
    { &hf_dns_length,
      { "Length", "dns.length",
        FT_UINT16, BASE_DEC, NULL, 0x0,
        "Length of DNS-over-TCP request or response", HFILL }},

    { &hf_dns_flags,
      { "Flags", "dns.flags",
        FT_UINT16, BASE_HEX, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_flags_response,
      { "Response", "dns.flags.response",
        FT_BOOLEAN, 16, TFS(&tfs_flags_response), F_RESPONSE,
        "Is the message a response?", HFILL }},

    { &hf_dns_flags_opcode,
      { "Opcode", "dns.flags.opcode",
        FT_UINT16, BASE_DEC, VALS(opcode_vals), F_OPCODE,
        "Operation code", HFILL }},

    { &hf_dns_flags_authoritative,
      { "Authoritative", "dns.flags.authoritative",
        FT_BOOLEAN, 16, TFS(&tfs_flags_authoritative), F_AUTHORITATIVE,
        "Is the server is an authority for the domain?", HFILL }},

    { &hf_dns_flags_conflict_query,
      { "Conflict", "dns.flags.conflict",
        FT_BOOLEAN, 16, TFS(&tfs_flags_conflict_query), F_CONFLICT,
        "Did we receive multiple responses to a query?", HFILL }},

    { &hf_dns_flags_conflict_response,
      { "Conflict", "dns.flags.conflict",
        FT_BOOLEAN, 16, TFS(&tfs_flags_conflict_response), F_CONFLICT,
        "Is the name considered unique?", HFILL }},

    { &hf_dns_flags_truncated,
      { "Truncated", "dns.flags.truncated",
        FT_BOOLEAN, 16, TFS(&tfs_flags_truncated), F_TRUNCATED,
        "Is the message truncated?", HFILL }},

    { &hf_dns_flags_recdesired,
      { "Recursion desired", "dns.flags.recdesired",
        FT_BOOLEAN, 16, TFS(&tfs_flags_recdesired), F_RECDESIRED,
        "Do query recursively?", HFILL }},

    { &hf_dns_flags_tentative,
      { "Tentative", "dns.flags.tentative",
        FT_BOOLEAN, 16, TFS(&tfs_flags_tentative), F_TENTATIVE,
        "Is the responder authoritative for the name, but not yet verified the uniqueness?", HFILL }},

    { &hf_dns_flags_recavail,
      { "Recursion available", "dns.flags.recavail",
        FT_BOOLEAN, 16, TFS(&tfs_flags_recavail), F_RECAVAIL,
        "Can the server do recursive queries?", HFILL }},

    { &hf_dns_flags_z,
      { "Z", "dns.flags.z",
        FT_BOOLEAN, 16, TFS(&tfs_flags_z), F_Z,
        "Z flag", HFILL }},

    { &hf_dns_flags_authenticated,
      { "Answer authenticated", "dns.flags.authenticated",
        FT_BOOLEAN, 16, TFS(&tfs_flags_authenticated), F_AUTHENTIC,
        "Was the reply data authenticated by the server?", HFILL }},

    { &hf_dns_flags_ad,
      { "AD bit", "dns.flags.authenticated",
        FT_BOOLEAN, 16, TFS(&tfs_set_notset), F_AUTHENTIC,
        NULL, HFILL }},

    { &hf_dns_flags_checkdisable,
      { "Non-authenticated data", "dns.flags.checkdisable",
        FT_BOOLEAN, 16, TFS(&tfs_flags_checkdisable), F_CHECKDISABLE,
        "Is non-authenticated data acceptable?", HFILL }},

    { &hf_dns_flags_rcode,
      { "Reply code", "dns.flags.rcode",
        FT_UINT16, BASE_DEC, VALS(rcode_vals), F_RCODE,
        NULL, HFILL }},

    { &hf_dns_transaction_id,
      { "Transaction ID", "dns.id",
        FT_UINT16, BASE_HEX, NULL, 0x0,
        "Identification of transaction", HFILL }},

    { &hf_dns_qry_type,
      { "Type", "dns.qry.type",
        FT_UINT16, BASE_HEX, VALS(dns_types), 0x0,
        "Query Type", HFILL }},

    { &hf_dns_qry_class,
      { "Class", "dns.qry.class",
        FT_UINT16, BASE_HEX, VALS(dns_classes), 0x0,
        "Query Class", HFILL }},

    { &hf_dns_qry_class_mdns,
      { "Class", "dns.qry.class",
        FT_UINT16, BASE_HEX, VALS(dns_classes), 0x7FFF,
        "Query Class", HFILL }},

    { &hf_dns_qry_qu,
      { "\"QU\" question", "dns.qry.qu",
        FT_BOOLEAN, 16, NULL, C_QU,
        "QU flag", HFILL }},

    { &hf_dns_qry_name,
      { "Name", "dns.qry.name",
        FT_STRING, BASE_NONE, NULL, 0x0,
        "Query Name", HFILL }},

    { &hf_dns_rr_type,
      { "Type", "dns.resp.type",
        FT_UINT16, BASE_HEX, VALS(dns_types), 0x0,
        "Response Type", HFILL }},

    { &hf_dns_rr_class,
      { "Class", "dns.resp.class",
        FT_UINT16, BASE_HEX, VALS(dns_classes), 0x0,
        "Response Class", HFILL }},

    { &hf_dns_rr_class_mdns,
      { "Class", "dns.resp.class",
        FT_UINT16, BASE_HEX, VALS(dns_classes), 0x7FFF,
        "Response Class", HFILL }},

    { &hf_dns_rr_cache_flush,
      { "Cache flush", "dns.resp.cache_flush",
        FT_BOOLEAN, 16, NULL, C_FLUSH,
        "Cache flush flag", HFILL }},

    { &hf_dns_srv_service,
      { "Service", "dns.srv.service",
        FT_STRING, BASE_NONE, NULL, 0x0,
        "Desired service", HFILL }},

    { &hf_dns_srv_proto,
      { "Protocol", "dns.srv.proto",
        FT_STRING, BASE_NONE, NULL, 0x0,
        "Desired protocol", HFILL }},

    { &hf_dns_srv_name,
      { "Name", "dns.srv.name",
        FT_STRING, BASE_NONE, NULL, 0x0,
        "Domain this resource record refers to", HFILL }},

    { &hf_dns_srv_priority,
      { "Priority", "dns.srv.priority",
        FT_UINT16, BASE_DEC, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_srv_weight,
      { "Weight", "dns.srv.weight",
        FT_UINT16, BASE_DEC, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_srv_port,
      { "Port", "dns.srv.port",
        FT_UINT16, BASE_DEC, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_srv_target,
      { "Target", "dns.srv.target",
        FT_STRING, BASE_NONE, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_rr_name,
      { "Name", "dns.resp.name",
        FT_STRING, BASE_NONE, NULL, 0x0,
        "Response Name", HFILL }},

    { &hf_dns_rr_ttl,
      { "Time to live", "dns.resp.ttl",
        FT_UINT32, BASE_DEC, NULL, 0x0,
        "Response TTL", HFILL }},

    { &hf_dns_rr_len,
      { "Data length", "dns.resp.len",
        FT_UINT32, BASE_DEC, NULL, 0x0,
        "Response Length", HFILL }},

    { &hf_dns_rr_addr,
      { "Addr", "dns.resp.addr",
        FT_IPv4, BASE_NONE, NULL, 0x0,
        "Response Address", HFILL }},

    { &hf_dns_rr_primaryname,
      { "Primaryname", "dns.resp.primaryname",
        FT_STRING, BASE_NONE, NULL, 0x0,
        "Response Primary Name", HFILL }},

    { &hf_dns_rr_udp_payload_size,
      { "UDP payload size", "dns.resp.udp_payload_size",
        FT_UINT16, BASE_HEX, NULL, 0x7FFF,
        NULL, HFILL }},

    { &hf_dns_soa_mname,
      { "Primary name server", "dns.soa.mname",
        FT_STRING, BASE_NONE, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_soa_rname,
      { "Responsible authority's mailbox", "dns.soa.rname",
        FT_STRING, BASE_NONE, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_soa_serial_number,
      { "Serial Number", "dns.soa.serial_number",
        FT_UINT32, BASE_DEC, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_soa_refresh_interval,
      { "Refresh Interval", "dns.soa.refresh_interval",
        FT_UINT32, BASE_DEC, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_soa_retry_interval,
      { "Retry Interval", "dns.soa.retry_interval",
        FT_UINT32, BASE_DEC, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_soa_expire_limit,
      { "Expire limit", "dns.soa.expire_limit",
        FT_UINT32, BASE_DEC, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_soa_minimum_ttl,
      { "Minimum TTL", "dns.soa.mininum_ttl",
        FT_UINT32, BASE_DEC, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_ptr_domain_name,
      { "Domain Name", "dns.ptr.domain_name",
        FT_STRING, BASE_NONE, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_wks_address,
      { "Address", "dns.wks.address",
        FT_IPv4, BASE_NONE, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_wks_protocol,
      { "Protocol", "dns.wks.protocol",
        FT_UINT8, BASE_DEC | BASE_EXT_STRING, &ipproto_val_ext, 0x0,
        NULL, HFILL }},

    { &hf_dns_wks_bits,
      { "Bits", "dns.wks.bits",
        FT_UINT8, BASE_HEX, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_hinfo_cpu_length,
      { "CPU Length", "dns.hinfo.cpu_length",
        FT_UINT8, BASE_DEC, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_hinfo_cpu,
      { "CPU", "dns.hinfo.cpu",
        FT_STRING, BASE_NONE, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_hinfo_os_length,
      { "OS Length", "dns.hinfo.os_length",
        FT_UINT8, BASE_DEC, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_hinfo_os,
      { "OS", "dns.hinfo.os",
        FT_STRING, BASE_NONE, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_mx_preference,
      { "Preference", "dns.mx.preference",
        FT_UINT16, BASE_DEC, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_mx_mail_exchange,
      { "Mail Exchange", "dns.mx.mail_exchange",
        FT_STRING, BASE_NONE, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_txt_length,
      { "TXT Length", "dns.txt.length",
        FT_UINT8, BASE_DEC, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_txt,
      { "TXT", "dns.txt",
        FT_STRING, BASE_NONE, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_spf_length,
      { "SPF Length", "dns.spf.length",
        FT_UINT8, BASE_DEC, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_spf,
      { "SPF", "dns.spf",
        FT_STRING, BASE_NONE, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_rrsig_type_covered,
      { "Type Covered", "dns.rrsig.type_covered",
        FT_UINT16, BASE_DEC, NULL, 0x0,
        "Identifies the type of the RRset that is covered by this RRSIG record", HFILL }},

    { &hf_dns_rrsig_algorithm,
      { "Algorithm", "dns.rrsig.algorithm",
        FT_UINT8, BASE_DEC, VALS(dnssec_algo_vals), 0x0,
        "Identifies the cryptographic algorithm used to create the signature", HFILL }},

    { &hf_dns_rrsig_labels,
      { "Labels", "dns.rrsig.labels",
        FT_UINT8, BASE_DEC, NULL, 0x0,
        "Specifies the number of labels in the original RRSIG RR owner name", HFILL }},

    { &hf_dns_rrsig_original_ttl,
      { "Original TTL", "dns.rrsig.original_ttl",
        FT_UINT32, BASE_DEC, NULL, 0x0,
        "Specifies the TTL of the covered RRset as it appears in the authoritative zone", HFILL }},

    { &hf_dns_rrsig_signature_expiration,
      { "Signature Expiration", "dns.rrsig.signature_expiration",
        FT_ABSOLUTE_TIME, ABSOLUTE_TIME_LOCAL, NULL, 0x0,
        "Specify a validity period for the signature", HFILL }},

    { &hf_dns_rrsig_signature_inception,
      { "Signature Inception", "dns.rrsig.signature_inception",
        FT_ABSOLUTE_TIME, ABSOLUTE_TIME_LOCAL, NULL, 0x0,
        "Specify a validity period for the signature", HFILL }},

    { &hf_dns_rrsig_key_tag,
      { "Key Tag", "dns.rrsig.key_tag",
        FT_UINT16, BASE_DEC, NULL, 0x0,
        "Contains the key tag value of the DNSKEY RR that validates this signature", HFILL }},

    { &hf_dns_rrsig_signers_name,
      { "Signer's name", "dns.rrsig.signers_name",
        FT_STRING, BASE_NONE, NULL, 0x0,
        "Identifies the owner name of the DNSKEY RR that a validator is supposed to use to validate this signature", HFILL }},

    { &hf_dns_rrsig_signature,
      { "Signature", "dns.rrsig.signature",
        FT_BYTES, BASE_NONE, NULL, 0x0,
        "Contains the cryptographic signature that covers the RRSIG RDATA", HFILL }},

    { &hf_dns_dnskey_flags,
      { "Flags", "dns.dnskey.flags",
        FT_UINT16, BASE_HEX, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_dnskey_flags_zone_key,
      { "Zone Key", "dns.dnskey.flags.zone_key",
        FT_BOOLEAN, 16, TFS(&dns_dnskey_zone_key_tfs), DNSKEY_FLAGS_ZK,
        NULL, HFILL }},

    { &hf_dns_dnskey_flags_key_revoked,
      { "Key Revoked", "dns.dnskey.flags.key_revoked",
        FT_BOOLEAN, 16, TFS(&tfs_yes_no), DNSKEY_FLAGS_KR,
        NULL, HFILL }},

    { &hf_dns_dnskey_flags_secure_entry_point,
      { "Key Signing Key", "dns.dnskey.flags.secure_entry_point",
        FT_BOOLEAN, 16, TFS(&tfs_yes_no), DNSKEY_FLAGS_SEP,
        NULL, HFILL }},

    { &hf_dns_dnskey_flags_reserved,
      { "Key Signing Key", "dns.dnskey.flags.reserved",
        FT_UINT16, BASE_HEX, NULL, DNSKEY_FLAGS_RSV,
        "Must be zero", HFILL }},

    { &hf_dns_dnskey_protocol,
      { "Protocol", "dns.dnskey.protocol",
        FT_UINT8, BASE_DEC, NULL, 0x0,
        "Must be 3", HFILL }},

    { &hf_dns_dnskey_algorithm,
      { "Algorithm", "dns.dnskey.algorithm",
        FT_UINT8, BASE_DEC, VALS(dnssec_algo_vals), 0x0,
        "Identifies the public key's cryptographic algorithm and determines the format of the Public Key field", HFILL }},

    { &hf_dns_dnskey_key_id,
      { "Key id", "dns.dnskey.key_id",
        FT_UINT16, BASE_DEC, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_dnskey_public_key,
      { "Public Key", "dns.dnskey.public_key",
        FT_BYTES, BASE_NONE, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_t_key_flags,
      { "Flags", "dns.t_key.flags",
        FT_UINT16, BASE_HEX, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_t_key_flags_authentication,
      { "Key allowed for authentication", "dns.t_key.flags.authentication",
        FT_BOOLEAN, 16, TFS(&tfs_not_allowed_allowed), 0x8000,
        NULL, HFILL }},

    { &hf_dns_t_key_flags_confidentiality,
      { "Key allowed for confidentiality", "dns.t_key.flags.confidentiality",
        FT_BOOLEAN, 16, TFS(&tfs_not_allowed_allowed), 0x4000,
        NULL, HFILL }},

    { &hf_dns_t_key_flags_key_required,
      { "Key required", "dns.t_key.flags.required",
        FT_BOOLEAN, 16, TFS(&tfs_required_experimental), 0x2000,
        NULL, HFILL }},

    { &hf_dns_t_key_flags_associated_user,
      { "Key is associated with a user", "dns.t_key.flags.associated_user",
        FT_BOOLEAN, 16, TFS(&tfs_yes_no), 0x0400,
        NULL, HFILL }},

    { &hf_dns_t_key_flags_associated_named_entity,
      { "Key is associated with the named entity", "dns.t_key.flags.associated_named_entity",
        FT_BOOLEAN, 16, TFS(&tfs_yes_no), 0x0200,
        NULL, HFILL }},

    { &hf_dns_t_key_flags_ipsec,
      { "Key use with IPSEC", "dns.t_key.flags.ipsec",
        FT_BOOLEAN, 16, TFS(&tfs_valid_invalid), 0x0080,
        NULL, HFILL }},

    { &hf_dns_t_key_flags_mime,
      { "Key use with MIME security multiparts", "dns.t_key.flags.mime",
        FT_BOOLEAN, 16, TFS(&tfs_valid_invalid), 0x0040,
        NULL, HFILL }},

    { &hf_dns_t_key_flags_signatory,
      { "Signatory", "dns.t_key.flags.signatory",
        FT_UINT16, BASE_DEC, NULL, 0x000F,
        NULL, HFILL }},

    { &hf_dns_rr_ns,
      { "Name Server", "dns.resp.ns",
        FT_STRING, BASE_NONE, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_rr_opt,
     { "Option", "dns.rr.opt",
        FT_NONE, BASE_NONE,
        NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_rr_opt_code,
      { "Option Code", "dns.rr.opt.code",
        FT_UINT16, BASE_DEC,
        VALS(edns0_opt_code_vals), 0x0,
        NULL, HFILL }},

    { &hf_dns_rr_opt_len,
      { "Option Length", "dns.rr.opt.len",
        FT_UINT16, BASE_DEC, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_rr_opt_data,
      { "Option Data", "dns.rr.opt.data",
        FT_BYTES, BASE_NONE, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_rr_opt_client_family,
      { "Family", "dns.rr.opt.client.family",
        FT_UINT16, BASE_DEC,
        VALS(afamily_vals), 0x0,
        NULL, HFILL }},

    { &hf_dns_rr_opt_client_netmask,
      { "Source Netmask", "dns.rr.opt.client.netmask",
        FT_UINT8, BASE_DEC, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_rr_opt_client_scope,
      { "Scope Netmask", "dns.rr.opt.client.scope",
        FT_UINT8, BASE_DEC, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_rr_opt_client_addr,
      { "Client Subnet", "dns.rr.opt.client.addr",
        FT_BYTES, BASE_NONE, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_rr_opt_client_addr4,
      { "Client Subnet", "dns.rr.opt.client.addr4",
        FT_IPv4, BASE_NONE, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_rr_opt_client_addr6,
      { "Client Subnet", "dns.rr.opt.client.addr6",
        FT_IPv6, BASE_NONE, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_count_questions,
      { "Questions", "dns.count.queries",
        FT_UINT16, BASE_DEC, NULL, 0x0,
        "Number of queries in packet", HFILL }},

    { &hf_dns_count_zones,
      { "Zones", "dns.count.zones",
        FT_UINT16, BASE_DEC, NULL, 0x0,
        "Number of zones in packet", HFILL }},

    { &hf_dns_count_answers,
      { "Answer RRs", "dns.count.answers",
        FT_UINT16, BASE_DEC, NULL, 0x0,
        "Number of answers in packet", HFILL }},

    { &hf_dns_count_prerequisites,
      { "Prerequisites", "dns.count.prerequisites",
        FT_UINT16, BASE_DEC, NULL, 0x0,
        "Number of prerequisites in packet", HFILL }},

    { &hf_dns_count_auth_rr,
      { "Authority RRs", "dns.count.auth_rr",
        FT_UINT16, BASE_DEC, NULL, 0x0,
        "Number of authoritative records in packet", HFILL }},

    { &hf_dns_count_updates,
      { "Updates", "dns.count.updates",
        FT_UINT16, BASE_DEC, NULL, 0x0,
        "Number of updates records in packet", HFILL }},

    { &hf_dns_nsec3_algo,
      { "Hash algorithm", "dns.nsec3.algo",
        FT_UINT8, BASE_DEC, VALS(hash_algorithms), 0,
        NULL, HFILL }},

    { &hf_dns_nsec3_flags,
      { "NSEC3 flags", "dns.nsec3.flags",
        FT_UINT8, BASE_DEC, NULL, 0,
        NULL, HFILL }},

    { &hf_dns_nsec3_flag_optout,
      { "NSEC3 Opt-out flag", "dns.nsec3.flags.opt_out",
        FT_BOOLEAN, 8, TFS(&tfs_flags_nsec3_optout), NSEC3_FLAG_OPTOUT,
        NULL, HFILL }},

    { &hf_dns_nsec3_iterations,
      { "NSEC3 iterations", "dns.nsec3.iterations",
        FT_UINT16, BASE_DEC, NULL, 0,
        "Number of hashing iterations", HFILL }},

    { &hf_dns_nsec3_salt_length,
      { "Salt length", "dns.nsec3.salt_length",
        FT_UINT8, BASE_DEC, NULL, 0,
        "Length of salt in bytes", HFILL }},

    { &hf_dns_nsec3_salt_value,
      { "Salt value", "dns.nsec3.salt_value",
        FT_BYTES, BASE_NONE, NULL, 0,
        NULL, HFILL }},

    { &hf_dns_nsec3_hash_length,
      { "Hash length", "dns.nsec3.hash_length",
        FT_UINT8, BASE_DEC, NULL, 0,
        "Length in bytes of next hashed owner", HFILL }},

    { &hf_dns_nsec3_hash_value,
      { "Next hashed owner", "dns.nsec3.hash_value",
        FT_BYTES, BASE_NONE, NULL, 0,
        NULL, HFILL }},

    { &hf_dns_tlsa_certificate_usage,
      { "Certificate Usage", "dns.tlsa.certificate_usage",
        FT_UINT8, BASE_DEC, VALS(tlsa_certificate_usage_vals), 0,
        "Specifies the provided association that will be used to match the certificate presented in the TLS handshake", HFILL }},

    { &hf_dns_tlsa_selector,
      { "Selector", "dns.tlsa.selector",
        FT_UINT8, BASE_DEC, VALS(tlsa_selector_vals), 0,
        "Specifies which part of the TLS certificate presented by the server will be matched against the association data", HFILL }},

    { &hf_dns_tlsa_matching_type,
      { "Matching Type", "dns.tlsa.matching_type",
        FT_UINT8, BASE_DEC, VALS(tlsa_matching_type_vals), 0,
        "Specifies how the certificate association is presented", HFILL }},

    { &hf_dns_tlsa_certificate_association_data,
      { "Certificate Association Data", "dns.tlsa.certificate_association_data",
        FT_BYTES, BASE_NONE, NULL, 0,
        "The data refers to the certificate in the association", HFILL }},

    { &hf_dns_tsig_original_id,
      { "Original Id", "dns.tsig.original_id",
        FT_UINT16, BASE_DEC, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_tsig_error,
      { "Error", "dns.tsig.error",
        FT_UINT16, BASE_DEC, NULL, 0x0,
        "Expanded RCODE for TSIG", HFILL }},

    { &hf_dns_tsig_fudge,
      { "Fudge", "dns.tsig.fudge",
        FT_UINT16, BASE_DEC, NULL, 0x0,
        "Number of bytes for the MAC", HFILL }},

    { &hf_dns_tsig_mac_size,
      { "MAC Size", "dns.tsig.mac_size",
        FT_UINT16, BASE_DEC, NULL, 0x0,
        "Number of bytes for the MAC", HFILL }},

    { &hf_dns_tsig_other_len,
      { "Other Len", "dns.tsig.other_len",
        FT_UINT16, BASE_DEC, NULL, 0x0,
        "Number of bytes for Other Data", HFILL }},

    { &hf_dns_tsig_mac,
      { "MAC", "dns.tsig.mac",
        FT_NONE, BASE_NONE, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_tsig_other_data,
      { "Other Data", "dns.tsig.other_data",
        FT_BYTES, BASE_NONE, NULL, 0x0,
        NULL, HFILL }},

    { &hf_dns_tsig_algorithm_name,
      { "Algorithm Name", "dns.tsig.algorithm_name",
        FT_STRING, BASE_NONE, NULL, 0x0,
        "Name of algorithm used for the MAC", HFILL }},

    { &hf_dns_response_in,
      { "Response In", "dns.response_in",
        FT_FRAMENUM, BASE_NONE, NULL, 0x0,
        "The response to this DNS query is in this frame", HFILL }},

    { &hf_dns_response_to,
      { "Request In", "dns.response_to",
        FT_FRAMENUM, BASE_NONE, NULL, 0x0,
        "This is a response to the DNS query in this frame", HFILL }},

    { &hf_dns_time,
      { "Time", "dns.time",
        FT_RELATIVE_TIME, BASE_NONE, NULL, 0x0,
        "The time between the Query and the Response", HFILL }},

    { &hf_dns_count_add_rr,
      { "Additional RRs", "dns.count.add_rr",
        FT_UINT16, BASE_DEC, NULL, 0x0,
        "Number of additional records in packet", HFILL }},

    { &hf_dns_sshfp_fingerprint,
      { "Fingerprint", "dns.sshfp.fingerprint",
        FT_BYTES, BASE_NONE, NULL, 0,
        NULL, HFILL }},

    { &hf_dns_hip_hit,
      { "Host Identity Tag", "dns.hip.hit",
        FT_BYTES, BASE_NONE, NULL, 0,
        NULL, HFILL }},

    { &hf_dns_hip_pk,
      { "HIP Public Key", "dns.hip.pk",
        FT_BYTES, BASE_NONE, NULL, 0,
        NULL, HFILL }},

    { &hf_dns_dhcid_rdata,
      { "DHCID Data", "dns.dhcid.rdata",
        FT_BYTES, BASE_NONE, NULL, 0,
        NULL, HFILL }},

    { &hf_dns_apl_coded_prefix,
      { "Prefix Length", "dns.apl.coded.prefix",
        FT_UINT8, BASE_DEC, NULL, 0,
        NULL, HFILL }},

    { &hf_dns_apl_negation,
      { "Negation Flag", "dns.apl.negation",
        FT_BOOLEAN, 8, TFS(&tfs_dns_apl_negation), DNS_APL_NEGATION,
        NULL, HFILL }},

    { &hf_dns_apl_afdlength,
      { "Address Length, in octets","dns.apl.afdlength",
        FT_UINT8, BASE_DEC, NULL, DNS_APL_AFDLENGTH,
        NULL, HFILL }},

    { &hf_dns_nsap_rdata,
      { "NSAP Data", "dns.nsap.rdata",
        FT_BYTES, BASE_NONE, NULL, 0,
        NULL, HFILL }}

  };
  static gint *ett[] = {
    &ett_dns,
    &ett_dns_qd,
    &ett_dns_rr,
    &ett_dns_qry,
    &ett_dns_ans,
    &ett_dns_flags,
    &ett_dns_opts,
    &ett_nsec3_flags,
    &ett_t_key_flags,
    &ett_t_key,
    &ett_dns_mac,
  };
  module_t *dns_module;

  proto_dns = proto_register_protocol("Domain Name Service", "DNS", "dns");
  proto_register_field_array(proto_dns, hf, array_length(hf));
  proto_register_subtree_array(ett, array_length(ett));

  /* Set default ports */
  range_convert_str(&global_dns_tcp_port_range, DEFAULT_DNS_PORT_RANGE, MAX_TCP_PORT);
  range_convert_str(&global_dns_udp_port_range, DEFAULT_DNS_PORT_RANGE, MAX_UDP_PORT);

  dns_module = prefs_register_protocol(proto_dns, proto_reg_handoff_dns);

  prefs_register_range_preference(dns_module, "tcp.ports", "DNS TCP ports",
                                  "TCP ports to be decoded as DNS (default: "
                                  DEFAULT_DNS_PORT_RANGE ")",
                                  &global_dns_tcp_port_range, MAX_TCP_PORT);

  prefs_register_range_preference(dns_module, "udp.ports", "DNS UDP Ports",
                                  "UDP ports to be decoded as DNS (default: "
                                  DEFAULT_DNS_PORT_RANGE ")",
                                  &global_dns_udp_port_range, MAX_UDP_PORT);

  prefs_register_bool_preference(dns_module, "desegment_dns_messages",
    "Reassemble DNS messages spanning multiple TCP segments",
    "Whether the DNS dissector should reassemble messages spanning multiple TCP segments."
    " To use this option, you must also enable \"Allow subdissectors to reassemble TCP streams\" in the TCP protocol settings.",
    &dns_desegment);

  dns_tsig_dissector_table = register_dissector_table("dns.tsig.mac", "DNS TSIG MAC Dissectors", FT_STRING, BASE_NONE);
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
