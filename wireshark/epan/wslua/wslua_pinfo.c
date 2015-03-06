/*
 * wslua_pinfo.c
 *
 * Wireshark's interface to the Lua Programming Language
 *
 * (c) 2006, Luis E. Garcia Ontanon <luis@ontanon.org>
 * (c) 2008, Balint Reczey <balint.reczey@ericsson.com>
 * (c) 2011, Stig Bjorlykke <stig@bjorlykke.org>
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

/* WSLUA_MODULE Pinfo Obtaining packet information */


#include "wslua.h"

#include <epan/addr_resolv.h>
#include <string.h>


/*
 * Track pointers to wireshark's structures.
 * see comment on wslua_tvb.c
 */

static GPtrArray* outstanding_Pinfo = NULL;
static GPtrArray* outstanding_Column = NULL;
static GPtrArray* outstanding_Columns = NULL;
static GPtrArray* outstanding_PrivateTable = NULL;

CLEAR_OUTSTANDING(Pinfo,expired, TRUE)
CLEAR_OUTSTANDING(Column,expired, TRUE)
CLEAR_OUTSTANDING(Columns,expired, TRUE)
CLEAR_OUTSTANDING(PrivateTable,expired, TRUE)

Pinfo* push_Pinfo(lua_State* L, packet_info* ws_pinfo) {
    Pinfo pinfo = NULL;
    if (ws_pinfo) {
        pinfo = (Pinfo)g_malloc(sizeof(struct _wslua_pinfo));
        pinfo->ws_pinfo = ws_pinfo;
        pinfo->expired = FALSE;
        g_ptr_array_add(outstanding_Pinfo,pinfo);
    }
    return pushPinfo(L,pinfo);
}

#define PUSH_COLUMN(L,c) {g_ptr_array_add(outstanding_Column,c);pushColumn(L,c);}
#define PUSH_COLUMNS(L,c) {g_ptr_array_add(outstanding_Columns,c);pushColumns(L,c);}
#define PUSH_PRIVATE_TABLE(L,c) {g_ptr_array_add(outstanding_PrivateTable,c);pushPrivateTable(L,c);}

WSLUA_CLASS_DEFINE(NSTime,NOP,NOP);
	/* NSTime represents a nstime_t.  This is an object with seconds and nano seconds. */

WSLUA_CONSTRUCTOR NSTime_new(lua_State *L) {
	/* Creates a new NSTime object */
#define WSLUA_OPTARG_NSTime_new_SECONDS 1 /* Seconds */
#define WSLUA_OPTARG_NSTime_new_NSECONDS 2 /* Nano seconds */
    NSTime nstime = (NSTime)g_malloc(sizeof(nstime_t));

    if (!nstime) return 0;

    nstime->secs = (time_t) luaL_optint(L,WSLUA_OPTARG_NSTime_new_SECONDS,0);
    nstime->nsecs = luaL_optint(L,WSLUA_OPTARG_NSTime_new_NSECONDS,0);

    pushNSTime(L,nstime);

    WSLUA_RETURN(1); /* The new NSTime object. */
}

WSLUA_METAMETHOD NSTime__tostring(lua_State* L) {
    NSTime nstime = checkNSTime(L,1);

    if (!nstime) return 0;

    lua_pushstring(L,ep_strdup_printf("%ld.%09d", (long)nstime->secs, nstime->nsecs));

    WSLUA_RETURN(1); /* The string representing the nstime. */
}
WSLUA_METAMETHOD NSTime__add(lua_State* L) { /* Calculates the sum of two NSTimes */
    NSTime time1 = checkNSTime(L,1);
    NSTime time2 = checkNSTime(L,2);
    NSTime time3 = (NSTime)g_malloc (sizeof (nstime_t));

    nstime_sum (time3, time1, time2);
    pushNSTime (L, time3);

    return 1;
}

WSLUA_METAMETHOD NSTime__sub(lua_State* L) { /* Calculates the diff of two NSTimes */
    NSTime time1 = checkNSTime(L,1);
    NSTime time2 = checkNSTime(L,2);
    NSTime time3 = (NSTime)g_malloc (sizeof (nstime_t));

    nstime_delta (time3, time1, time2);
    pushNSTime (L, time3);

    return 1;
}

WSLUA_METAMETHOD NSTime__unm(lua_State* L) { /* Calculates the negative NSTime */
    NSTime time1 = checkNSTime(L,1);
    NSTime time2 = (NSTime)g_malloc (sizeof (nstime_t));

    nstime_set_zero (time2);
    nstime_subtract (time2, time1);
    pushNSTime (L, time2);

    return 1;
}

WSLUA_METAMETHOD NSTime__eq(lua_State* L) { /* Compares two NSTimes */
    NSTime time1 = checkNSTime(L,1);
    NSTime time2 = checkNSTime(L,2);
    gboolean result = FALSE;

    if (!time1 || !time2)
      WSLUA_ERROR(FieldInfo__eq,"Data source must be the same for both fields");

    if (nstime_cmp(time1, time2) == 0)
        result = TRUE;

    lua_pushboolean(L,result);

    return 1;
}

WSLUA_METAMETHOD NSTime__le(lua_State* L) { /* Compares two NSTimes */
    NSTime time1 = checkNSTime(L,1);
    NSTime time2 = checkNSTime(L,2);
    gboolean result = FALSE;

    if (!time1 || !time2)
      WSLUA_ERROR(FieldInfo__eq,"Data source must be the same for both fields");

    if (nstime_cmp(time1, time2) <= 0)
        result = TRUE;

    lua_pushboolean(L,result);

    return 1;
}

WSLUA_METAMETHOD NSTime__lt(lua_State* L) { /* Compares two NSTimes */
    NSTime time1 = checkNSTime(L,1);
    NSTime time2 = checkNSTime(L,2);
    gboolean result = FALSE;

    if (!time1 || !time2)
      WSLUA_ERROR(FieldInfo__eq,"Data source must be the same for both fields");

    if (nstime_cmp(time1, time2) < 0)
        result = TRUE;

    lua_pushboolean(L,result);

    return 1;
}

typedef struct {
    const gchar* name;
    lua_CFunction get;
    lua_CFunction set;
} nstime_actions_t;

static int NSTime_get_secs(lua_State* L) {
    NSTime nstime = toNSTime(L,1);

    lua_pushnumber (L,(lua_Number)(nstime->secs));

    return 1;
}

static int NSTime_set_secs(lua_State* L)
 {
    NSTime nstime = toNSTime(L,1);
    time_t secs = luaL_checkint(L,3);

    nstime->secs = secs;

    return 0;
}

static int NSTime_get_nsecs(lua_State* L) {
    NSTime nstime = toNSTime(L,1);

    lua_pushnumber (L,(lua_Number)(nstime->nsecs));

    return 1;
}

static int NSTime_set_nsecs(lua_State* L) {
    NSTime nstime = toNSTime(L,1);
    int nsecs = luaL_checkint(L,3);

    nstime->nsecs = nsecs;

    return 0;
}

static const nstime_actions_t nstime_actions[] = {
    /* WSLUA_ATTRIBUTE NSTime_secs RW The NSTime seconds */
    {"secs", NSTime_get_secs, NSTime_set_secs},

    /* WSLUA_ATTRIBUTE NSTime_nsecs RW The NSTime nano seconds */
    {"nsecs", NSTime_get_nsecs, NSTime_set_nsecs},

    {NULL,NULL,NULL}
};

static int NSTime__index(lua_State* L) {
    NSTime nstime = checkNSTime(L,1);
    const gchar* name = luaL_checkstring(L,2);
    const nstime_actions_t* pa;

    if (! (nstime && name) ) return 0;

    for (pa = nstime_actions; pa->name; pa++) {
        if ( g_str_equal(name,pa->name) ) {
            if (pa->get) {
                return pa->get(L);
            } else {
                luaL_error(L,"You cannot get the `%s' attribute of a nstime",name);
                return 0;
            }
        }
    }

    luaL_error(L,"A protocol doesn't have a `%s' nstime",name);
    return 0;
}

static int NSTime__newindex(lua_State* L) {
    NSTime nstime = checkNSTime(L,1);
    const gchar* name = luaL_checkstring(L,2);
    const nstime_actions_t* pa;

    if (! (nstime && name) ) return 0;

    for (pa = nstime_actions; pa->name; pa++) {
        if ( g_str_equal(name,pa->name) ) {
            if (pa->set) {
                return pa->set(L);
            } else {
                luaL_error(L,"You cannot set the `%s' attribute of a nstime",name);
                return 0;
            }
        }
    }

    luaL_error(L,"A protocol doesn't have a `%s' nstime",name);
    return 0;
}

/* Gets registered as metamethod automatically by WSLUA_REGISTER_CLASS/META */
static int NSTime__gc(lua_State* L) {
    NSTime nstime = checkNSTime(L,1);

    if (!nstime) return 0;

    g_free (nstime);
    return 0;
}

WSLUA_META NSTime_meta[] = {
    {"__index", NSTime__index},
    {"__newindex", NSTime__newindex},
    {"__tostring", NSTime__tostring},
    {"__add", NSTime__add},
    {"__sub", NSTime__sub},
    {"__unm", NSTime__unm},
    {"__eq", NSTime__eq},
    {"__le", NSTime__le},
    {"__lt", NSTime__lt},
    { NULL, NULL}
};

int NSTime_register(lua_State* L) {
    WSLUA_REGISTER_META(NSTime);

    lua_pushcfunction(L, NSTime_new);
    lua_setglobal(L, "NSTime");

    return 1;
}

WSLUA_CLASS_DEFINE(Address,NOP,NOP); /* Represents an address */

WSLUA_CONSTRUCTOR Address_ip(lua_State* L) {
	/* Creates an Address Object representing an IP address. */

#define WSLUA_ARG_Address_ip_HOSTNAME 1 /* The address or name of the IP host. */
    Address addr = (Address)g_malloc(sizeof(address));
    guint32* ip_addr = (guint32 *)g_malloc(sizeof(guint32));
    const gchar* name = luaL_checkstring(L,WSLUA_ARG_Address_ip_HOSTNAME);

    if (! get_host_ipaddr(name, (guint32*)ip_addr)) {
        *ip_addr = 0;
    }

    SET_ADDRESS(addr, AT_IPv4, 4, ip_addr);
    pushAddress(L,addr);
    WSLUA_RETURN(1); /* The Address object */
}

#if 0
/* TODO */
static int Address_ipv6(lua_State* L) {
    Address addr = g_malloc(sizeof(address));

    SET_ADDRESS(addr, AT_NONE, 4, g_malloc(4));

    pushAddress(L,addr);
    return 1;
}
static int Address_ss7(lua_State* L) {
    Address addr = g_malloc(sizeof(address));

    SET_ADDRESS(addr, AT_NONE, 4, g_malloc(4));

    pushAddress(L,addr);
    return 1;
}
static int Address_eth(lua_State* L) {
    Address addr = g_malloc(sizeof(address));

    SET_ADDRESS(addr, AT_NONE, 4, g_malloc(4));

    pushAddress(L,addr);
    return 1;
}
static int Address_sna(lua_State* L) {
    Address addr = g_malloc(sizeof(address));

    SET_ADDRESS(addr, AT_NONE, 4, g_malloc(4));

    pushAddress(L,addr);
    return 1;
}
static int Address_atalk(lua_State* L) {
    Address addr = g_malloc(sizeof(address));

    SET_ADDRESS(addr, AT_NONE, 4, g_malloc(4));

    pushAddress(L,addr);
    return 1;
}
static int Address_vines(lua_State* L) {
    Address addr = g_malloc(sizeof(address));

    SET_ADDRESS(addr, AT_NONE, 4, g_malloc(4));

    pushAddress(L,addr);
    return 1;
}
static int Address_osi(lua_State* L) {
    Address addr = g_malloc(sizeof(address));

    SET_ADDRESS(addr, AT_NONE, 4, g_malloc(4));

    pushAddress(L,addr);
    return 1;
}
static int Address_arcnet(lua_State* L) {
    Address addr = g_malloc(sizeof(address));

    SET_ADDRESS(addr, AT_NONE, 4, g_malloc(4));

    pushAddress(L,addr);
    return 1;
}
static int Address_fc(lua_State* L) {
    Address addr = g_malloc(sizeof(address));

    SET_ADDRESS(addr, AT_NONE, 4, g_malloc(4));

    pushAddress(L,addr);
    return 1;
}
static int Address_string(lua_State* L) {
    Address addr = g_malloc(sizeof(address));

    SET_ADDRESS(addr, AT_NONE, 4, g_malloc(4));

    pushAddress(L,addr);
    return 1;
}
static int Address_eui64(lua_State* L) {
    Address addr = g_malloc(sizeof(address));

    SET_ADDRESS(addr, AT_NONE, 4, g_malloc(4));

    pushAddress(L,addr);
    return 1;
}
static int Address_uri(lua_State* L) {
    Address addr = g_malloc(sizeof(address));

    SET_ADDRESS(addr, AT_NONE, 4, g_malloc(4));

    pushAddress(L,addr);
    return 1;
}
static int Address_tipc(lua_State* L) {
    Address addr = g_malloc(sizeof(address));

    SET_ADDRESS(addr, AT_NONE, 4, g_malloc(4));

    pushAddress(L,addr);
    return 1;
}
#endif

WSLUA_METHODS Address_methods[] = {
    WSLUA_CLASS_FNREG(Address,ip),
    WSLUA_CLASS_FNREG_ALIAS(Address,ipv4,ip),
#if 0
    WSLUA_CLASS_FNREG(Address,ipv6),
    WSLUA_CLASS_FNREG_ALIAS(Address,ss7pc,ss7),
    WSLUA_CLASS_FNREG(Address,eth),
    WSLUA_CLASS_FNREG(Address,sna},
    WSLUA_CLASS_FNREG(Address,atalk),
    WSLUA_CLASS_FNREG(Address,vines),
    WSLUA_CLASS_FNREG(Address,osi),
    WSLUA_CLASS_FNREG(Address,arcnet),
    WSLUA_CLASS_FNREG(Address,fc),
    WSLUA_CLASS_FNREG(Address,string),
    WSLUA_CLASS_FNREG(Address,eui64),
    WSLUA_CLASS_FNREG(Address,uri),
    WSLUA_CLASS_FNREG(Address,tipc),
#endif
    {0,0}
};

WSLUA_METAMETHOD Address__tostring(lua_State* L) {
    Address addr = checkAddress(L,1);

    lua_pushstring(L,get_addr_name(addr));

    WSLUA_RETURN(1); /* The string representing the address. */
}

/* Gets registered as metamethod automatically by WSLUA_REGISTER_CLASS/META */
static int Address__gc(lua_State* L) {
    Address addr = checkAddress(L,1);

    if (addr) {
        g_free((void*)addr->data);
        g_free((void*)addr);
    }

    return 0;
}

WSLUA_METAMETHOD Address__eq(lua_State* L) { /* Compares two Addresses */
    Address addr1 = checkAddress(L,1);
    Address addr2 = checkAddress(L,2);
    gboolean result = FALSE;

    if (ADDRESSES_EQUAL(addr1, addr2))
        result = TRUE;

    lua_pushboolean(L,result);

    return 1;
}

WSLUA_METAMETHOD Address__le(lua_State* L) { /* Compares two Addresses */
    Address addr1 = checkAddress(L,1);
    Address addr2 = checkAddress(L,2);
    gboolean result = FALSE;

    if (CMP_ADDRESS(addr1, addr2) <= 0)
        result = TRUE;

    lua_pushboolean(L,result);

    return 1;
}

WSLUA_METAMETHOD Address__lt(lua_State* L) { /* Compares two Addresses */
    Address addr1 = checkAddress(L,1);
    Address addr2 = checkAddress(L,2);
    gboolean result = FALSE;

    if (CMP_ADDRESS(addr1, addr2) < 0)
        result = TRUE;

    lua_pushboolean(L,result);

    return 1;
}

WSLUA_META Address_meta[] = {
    {"__tostring", Address__tostring },
    {"__eq",Address__eq},
    {"__le",Address__le},
    {"__lt",Address__lt},
    {0,0}
};


int Address_register(lua_State *L) {
    WSLUA_REGISTER_CLASS(Address);
    return 1;
}


WSLUA_CLASS_DEFINE(Column,FAIL_ON_NULL("expired column"),NOP); /* A Column in the packet list */

struct col_names_t {
    const gchar* name;
    int id;
};

static const struct col_names_t colnames[] = {
    {"number",COL_NUMBER},
    {"abs_time",COL_ABS_TIME},
    {"utc_time",COL_UTC_TIME},
    {"cls_time",COL_CLS_TIME},
    {"rel_time",COL_REL_TIME},
    {"date",COL_ABS_DATE_TIME},
    {"utc_date",COL_UTC_DATE_TIME},
    {"delta_time",COL_DELTA_TIME},
    {"delta_time_displayed",COL_DELTA_TIME_DIS},
    {"src",COL_DEF_SRC},
    {"src_res",COL_RES_SRC},
    {"src_unres",COL_UNRES_SRC},
    {"dl_src",COL_DEF_DL_SRC},
    {"dl_src_res",COL_RES_DL_SRC},
    {"dl_src_unres",COL_UNRES_DL_SRC},
    {"net_src",COL_DEF_NET_SRC},
    {"net_src_res",COL_RES_NET_SRC},
    {"net_src_unres",COL_UNRES_NET_SRC},
    {"dst",COL_DEF_DST},
    {"dst_res",COL_RES_DST},
    {"dst_unres",COL_UNRES_DST},
    {"dl_dst",COL_DEF_DL_DST},
    {"dl_dst_res",COL_RES_DL_DST},
    {"dl_dst_unres",COL_UNRES_DL_DST},
    {"net_dst",COL_DEF_NET_DST},
    {"net_dst_res",COL_RES_NET_DST},
    {"net_dst_unres",COL_UNRES_NET_DST},
    {"src_port",COL_DEF_SRC_PORT},
    {"src_port_res",COL_RES_SRC_PORT},
    {"src_port_unres",COL_UNRES_SRC_PORT},
    {"dst_port",COL_DEF_DST_PORT},
    {"dst_port_res",COL_RES_DST_PORT},
    {"dst_port_unres",COL_UNRES_DST_PORT},
    {"protocol",COL_PROTOCOL},
    {"info",COL_INFO},
    {"packet_len",COL_PACKET_LENGTH},
    {"cumulative_bytes",COL_CUMULATIVE_BYTES},
    {"direction",COL_IF_DIR},
    {"vsan",COL_VSAN},
    {"tx_rate",COL_TX_RATE},
    {"rssi",COL_RSSI},
    {"dce_call",COL_DCE_CALL},
    {NULL,0}
};

static gint col_name_to_id(const gchar* name) {
    const struct col_names_t* cn;
    for(cn = colnames; cn->name; cn++) {
        if (g_str_equal(cn->name,name)) {
            return cn->id;
        }
    }

    return 0;
}

static const gchar*  col_id_to_name(gint id) {
    const struct col_names_t* cn;
    for(cn = colnames; cn->name; cn++) {
        if ( cn->id == id ) {
            return cn->name;
        }
    }
    return NULL;
}


WSLUA_METAMETHOD Column__tostring(lua_State *L) {
    Column c = checkColumn(L,1);
    const gchar* text;

    if (!c) {
        lua_pushstring(L,"(nil)");
    }
    else if (!c->cinfo) {
        text = col_id_to_name(c->col);
        lua_pushfstring(L, "(%s)", text ? text : "unknown");
    }
    else {
        text = col_get_text(c->cinfo, c->col);
        lua_pushstring(L, text ? text : "(nil)");
    }

    WSLUA_RETURN(1); /* The column's string text (in parenthesis if not available) */
}

/* Gets registered as metamethod automatically by WSLUA_REGISTER_CLASS */
static int Column__gc(lua_State* L) {
    Column col = checkColumn(L,1);

    if (!col) return 0;

    if (!col->expired)
        col->expired = TRUE;
    else
        g_free(col);

    return 0;

}

WSLUA_METHOD Column_clear(lua_State *L) {
	/* Clears a Column */
    Column c = checkColumn(L,1);

    if (!(c && c->cinfo)) return 0;

    col_clear(c->cinfo, c->col);

    return 0;
}

WSLUA_METHOD Column_set(lua_State *L) {
	/* Sets the text of a Column */
#define WSLUA_ARG_Column_set_TEXT 2 /* The text to which to set the Column */
    Column c = checkColumn(L,1);
    const gchar* s = luaL_checkstring(L,WSLUA_ARG_Column_set_TEXT);

    if (!(c && c->cinfo))
        return 0;

    if (!s) WSLUA_ARG_ERROR(Column_set,TEXT,"must be a string");

    col_add_str(c->cinfo, c->col, s);

    return 0;
}

WSLUA_METHOD Column_append(lua_State *L) {
	/* Appends text to a Column */
#define WSLUA_ARG_Column_append_TEXT 2 /* The text to append to the Column */
    Column c = checkColumn(L,1);
    const gchar* s = luaL_checkstring(L,WSLUA_ARG_Column_append_TEXT);

    if (!(c && c->cinfo))
        return 0;

    if (!s) WSLUA_ARG_ERROR(Column_append,TEXT,"must be a string");

    col_append_str(c->cinfo, c->col, s);

    return 0;
}

WSLUA_METHOD Column_prepend(lua_State *L) {
	/* Prepends text to a Column */
#define WSLUA_ARG_Column_prepend_TEXT 2 /* The text to prepend to the Column */
    Column c = checkColumn(L,1);
    const gchar* s = luaL_checkstring(L,WSLUA_ARG_Column_prepend_TEXT);

    if (!(c && c->cinfo))
        return 0;

    if (!s) WSLUA_ARG_ERROR(Column_prepend,TEXT,"must be a string");

    if (check_col(c->cinfo, c->col))
        col_prepend_fstr(c->cinfo, c->col, "%s",s);

    return 0;
}

WSLUA_METHOD Column_fence(lua_State *L) {
        /* Sets Column text fence, to prevent overwriting */
    Column c = checkColumn(L,1);

    if (c && c->cinfo)
        col_set_fence(c->cinfo, c->col);

    return 0;
}  


WSLUA_METHODS Column_methods[] = {
    WSLUA_CLASS_FNREG(Column,clear),
    WSLUA_CLASS_FNREG(Column,set),
    WSLUA_CLASS_FNREG(Column,append),
    WSLUA_CLASS_FNREG(Column,prepend),
    WSLUA_CLASS_FNREG_ALIAS(Column,preppend,prepend),
    WSLUA_CLASS_FNREG(Column,fence),
    {0,0}
};


WSLUA_META Column_meta[] = {
    {"__tostring", Column__tostring },
    {0,0}
};


int Column_register(lua_State *L) {
    WSLUA_REGISTER_CLASS(Column);
    return 1;
}






WSLUA_CLASS_DEFINE(Columns,NOP,NOP);
/* The Columns of the packet list. */

WSLUA_METAMETHOD Columns__tostring(lua_State *L) {
    lua_pushstring(L,"Columns");
    WSLUA_RETURN(1);
    /* The string "Columns", no real use, just for debugging purposes. */
}

/* 
 * To document this is very odd - it won't make sense to a person reading the
 * API docs to see this metamethod as a method, but oh well.
 */
WSLUA_METAMETHOD Columns__newindex(lua_State *L) {
	/* Sets the text of a specific column */
#define WSLUA_ARG_Columns__newindex_COLUMN 2 /* The name of the column to set */
#define WSLUA_ARG_Columns__newindex_TEXT 3 /* The text for the column */
    Columns cols = checkColumns(L,1);
    const struct col_names_t* cn;
    const char* colname;
    const char* text;

    if (!cols) return 0;
    if (cols->expired) {
        luaL_error(L,"expired column");
        return 0;
    }

    colname = luaL_checkstring(L,WSLUA_ARG_Columns__newindex_COLUMN);
    text = luaL_checkstring(L,WSLUA_ARG_Columns__newindex_TEXT);

    for(cn = colnames; cn->name; cn++) {
        if( g_str_equal(cn->name,colname) ) {
            col_add_str(cols->cinfo, cn->id, text);
            return 0;
        }
    }

    WSLUA_ARG_ERROR(Columns__newindex,COLUMN,"the column name must be a valid column");
}

WSLUA_METAMETHOD Columns_index(lua_State *L) {
    Columns cols = checkColumns(L,1);
    const struct col_names_t* cn;
    const char* colname = luaL_checkstring(L,2);

    if (!cols) {
        Column c = (Column)g_malloc(sizeof(struct _wslua_col_info));
        c->cinfo = NULL;
        c->col = col_name_to_id(colname);
	c->expired = FALSE;

        PUSH_COLUMN(L,c);
        return 1;
    }


    if (cols->expired) {
        luaL_error(L,"expired column");
        return 0;
    }

    if (!colname) return 0;

    for(cn = colnames; cn->name; cn++) {
        if( g_str_equal(cn->name,colname) ) {
            Column c = (Column)g_malloc(sizeof(struct _wslua_col_info));
            c->cinfo = cols->cinfo;
            c->col = col_name_to_id(colname);
	    c->expired = FALSE;

            PUSH_COLUMN(L,c);
            return 1;
        }
    }

    return 0;
}

/* Gets registered as metamethod automatically by WSLUA_REGISTER_META */
static int Columns__gc(lua_State* L) {
    Columns cols = checkColumns(L,1);

    if (!cols) return 0;

    if (!cols->expired)
        cols->expired = TRUE;
    else
        g_free(cols);

    return 0;

}


static const luaL_Reg Columns_meta[] = {
    {"__tostring", Columns__tostring },
    {"__newindex", Columns__newindex },
    {"__index",  Columns_index},
    { NULL, NULL }
};


int Columns_register(lua_State *L) {
    WSLUA_REGISTER_META(Columns);
    return 1;
}

WSLUA_CLASS_DEFINE(PrivateTable,NOP,NOP);
	/* PrivateTable represents the pinfo->private_table. */

WSLUA_METAMETHOD PrivateTable__tostring(lua_State* L) {
    PrivateTable priv = checkPrivateTable(L,1);
    GString *key_string;
    GList *keys, *key;

    if (!priv) return 0;

    key_string = g_string_new ("");
    keys = g_hash_table_get_keys (priv->table);
    key = g_list_first (keys);
    while (key) {
        key_string = g_string_append (key_string, (const gchar *)key->data);
        key = g_list_next (key);
        if (key) {
            key_string = g_string_append_c (key_string, ',');
        }
    }

    lua_pushstring(L,key_string->str);

    g_string_free (key_string, TRUE);
    g_list_free (keys);

    WSLUA_RETURN(1); /* A string with all keys in the table, mostly for debugging. */
}

static int PrivateTable__index(lua_State* L) {
	/* Gets the text of a specific entry */
    PrivateTable priv = checkPrivateTable(L,1);
    const gchar* name = luaL_checkstring(L,2);
    const gchar* string;

    if (! (priv && name) ) return 0;

    if (priv->expired) {
        luaL_error(L,"expired private_table");
        return 0;
    }

    string = (const gchar *)g_hash_table_lookup (priv->table, (gpointer) name);

    if (string) {
        lua_pushstring(L, string);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

static int PrivateTable__newindex(lua_State* L) {
	/* Sets the text of a specific entry */
    PrivateTable priv = checkPrivateTable(L,1);
    const gchar* name = luaL_checkstring(L,2);
    const gchar* string = NULL;

    if (! (priv && name) ) return 0;

    if (priv->expired) {
        luaL_error(L,"expired private_table");
        return 0;
    }

    if (lua_isstring(L,3)) {
        /* This also catches numbers, which is converted to string */
        string = luaL_checkstring(L,3);
    } else if (lua_isboolean(L,3)) {
        /* We support boolean by setting a empty string if true and NULL if false */
        string = lua_toboolean(L,3) ? "" : NULL;
    } else if (!lua_isnil(L,3)) {
        luaL_error(L,"unsupported type: %s", lua_typename(L,3));
        return 0;
    }

    if (string) {
      g_hash_table_replace (priv->table, (gpointer) ep_strdup(name), (gpointer) ep_strdup(string));
    } else {
      g_hash_table_remove (priv->table, (gpointer) name);
    }

    return 1;
}

/* Gets registered as metamethod automatically by WSLUA_REGISTER_CLASS/META */
static int PrivateTable__gc(lua_State* L) {
    PrivateTable priv = checkPrivateTable(L,1);

    if (!priv) return 0;

    if (!priv->expired) {
        priv->expired = TRUE;
    } else {
        if (priv->is_allocated) {
            g_hash_table_destroy (priv->table);
        }
        g_free(priv);
    }

    return 0;
}

WSLUA_META PrivateTable_meta[] = {
    {"__index", PrivateTable__index},
    {"__newindex", PrivateTable__newindex},
    {"__tostring", PrivateTable__tostring},
    { NULL, NULL}
};

int PrivateTable_register(lua_State* L) {
    WSLUA_REGISTER_META(PrivateTable);
    return 1;
}


WSLUA_CLASS_DEFINE(Pinfo,FAIL_ON_NULL("expired pinfo"),NOP);
/* Packet information */

static int Pinfo_tostring(lua_State *L) { lua_pushstring(L,"a Pinfo"); return 1; }

#define PINFO_GET(name,block) static int name(lua_State *L) {  \
    Pinfo pinfo = checkPinfo(L,1); \
    if (!pinfo) return 0;\
    if (pinfo->expired) { \
        luaL_error(L,"expired_pinfo"); \
        return 0; \
    } \
    block \
    return 1;\
}

#define PINFO_GET_BOOLEAN(name,val) \
    PINFO_GET(name,{lua_pushboolean(L,val);})

#define PINFO_GET_NUMBER(name,val) \
    PINFO_GET(name,{lua_pushnumber(L,(lua_Number)(val));})

#define PINFO_GET_STRING(name,val) \
    PINFO_GET(name, { \
      const gchar* value; \
      value = val; \
      if (value) lua_pushstring(L,(const char*)(value)); else lua_pushnil(L); \
    })

#define PINFO_GET_ADDRESS(name,role) \
    PINFO_GET(name, { \
      Address addr; \
      addr = g_new(address,1); \
      COPY_ADDRESS(addr, &(pinfo->ws_pinfo->role)); \
      pushAddress(L,addr); \
    })

#define PINFO_GET_LIGHTUSERDATA(name, val) \
    PINFO_GET(name,{lua_pushlightuserdata(L, (void *) (val));})

static double
lua_nstime_to_sec(const nstime_t *nstime)
{
    return (((double)nstime->secs) + (((double)nstime->nsecs) / 1000000000.0));
}

static double
lua_delta_nstime_to_sec(const frame_data *fd, const frame_data *prev)
{
	nstime_t del;

	frame_delta_abs_time(fd, prev, &del);
	return lua_nstime_to_sec(&del);
}

PINFO_GET_BOOLEAN(Pinfo_fragmented,pinfo->ws_pinfo->fragmented)
PINFO_GET_BOOLEAN(Pinfo_in_error_pkt,pinfo->ws_pinfo->flags.in_error_pkt)
PINFO_GET_BOOLEAN(Pinfo_visited,pinfo->ws_pinfo->fd->flags.visited)

PINFO_GET_NUMBER(Pinfo_number,pinfo->ws_pinfo->fd->num)
PINFO_GET_NUMBER(Pinfo_len,pinfo->ws_pinfo->fd->pkt_len)
PINFO_GET_NUMBER(Pinfo_caplen,pinfo->ws_pinfo->fd->cap_len)
PINFO_GET_NUMBER(Pinfo_abs_ts,lua_nstime_to_sec(&pinfo->ws_pinfo->fd->abs_ts))
PINFO_GET_NUMBER(Pinfo_rel_ts,lua_nstime_to_sec(&pinfo->ws_pinfo->fd->rel_ts))
PINFO_GET_NUMBER(Pinfo_delta_ts,lua_delta_nstime_to_sec(pinfo->ws_pinfo->fd, pinfo->ws_pinfo->fd->prev_cap))
PINFO_GET_NUMBER(Pinfo_delta_dis_ts,lua_delta_nstime_to_sec(pinfo->ws_pinfo->fd, pinfo->ws_pinfo->fd->prev_dis))
PINFO_GET_NUMBER(Pinfo_ipproto,pinfo->ws_pinfo->ipproto)
PINFO_GET_NUMBER(Pinfo_circuit_id,pinfo->ws_pinfo->circuit_id)
PINFO_GET_NUMBER(Pinfo_desegment_len,pinfo->ws_pinfo->desegment_len)
PINFO_GET_NUMBER(Pinfo_desegment_offset,pinfo->ws_pinfo->desegment_offset)
PINFO_GET_NUMBER(Pinfo_ptype,pinfo->ws_pinfo->ptype)
PINFO_GET_NUMBER(Pinfo_src_port,pinfo->ws_pinfo->srcport)
PINFO_GET_NUMBER(Pinfo_dst_port,pinfo->ws_pinfo->destport)
PINFO_GET_NUMBER(Pinfo_ethertype,pinfo->ws_pinfo->ethertype)
PINFO_GET_NUMBER(Pinfo_match_uint,pinfo->ws_pinfo->match_uint)

PINFO_GET_STRING(Pinfo_curr_proto,pinfo->ws_pinfo->current_proto)
PINFO_GET_STRING(Pinfo_match_string,pinfo->ws_pinfo->match_string)

PINFO_GET_ADDRESS(Pinfo_net_src,net_src)
PINFO_GET_ADDRESS(Pinfo_net_dst,net_dst)
PINFO_GET_ADDRESS(Pinfo_dl_src,dl_src)
PINFO_GET_ADDRESS(Pinfo_dl_dst,dl_dst)
PINFO_GET_ADDRESS(Pinfo_src,src)
PINFO_GET_ADDRESS(Pinfo_dst,dst)

PINFO_GET_LIGHTUSERDATA(Pinfo_private_data, pinfo->ws_pinfo->private_data)

static int Pinfo_match(lua_State *L) {
    Pinfo pinfo = checkPinfo(L,1);

    if (!pinfo) return 0;
    if (pinfo->expired) {
        luaL_error(L,"expired_pinfo");
        return 0;
    }

    if (pinfo->ws_pinfo->match_string) {
        lua_pushstring(L,pinfo->ws_pinfo->match_string);
    } else {
        lua_pushnumber(L,(lua_Number)(pinfo->ws_pinfo->match_uint));
    }

    return 1;
}

static int Pinfo_columns(lua_State *L) {
    Columns cols = NULL;
    Pinfo pinfo = checkPinfo(L,1);
    const gchar* colname = luaL_optstring(L,2,NULL);

    if (pinfo->expired) {
        luaL_error(L,"expired_pinfo");
        return 0;
    }

    cols = (Columns)g_malloc(sizeof(struct _wslua_cols));
    cols->cinfo = pinfo->ws_pinfo->cinfo;
    cols->expired = FALSE;

    if (!colname) {
        PUSH_COLUMNS(L,cols);
    } else {
        lua_settop(L,0);
        PUSH_COLUMNS(L,cols);
        lua_pushstring(L,colname);
        return Columns_index(L);
    }
    return 1;
}

static int Pinfo_private(lua_State *L) {
    PrivateTable priv = NULL;
    Pinfo pinfo = checkPinfo(L,1);
    const gchar* privname = luaL_optstring(L,2,NULL);
    gboolean is_allocated = FALSE;

    if (!pinfo) return 0;

    if (pinfo->expired) {
        luaL_error(L,"expired private_table");
        return 0;
    }

    if (!pinfo->ws_pinfo->private_table) {
        pinfo->ws_pinfo->private_table = g_hash_table_new(g_str_hash,g_str_equal);
        is_allocated = TRUE;
    }

    priv = (PrivateTable)g_malloc(sizeof(struct _wslua_private_table));
    priv->table = pinfo->ws_pinfo->private_table;
    priv->is_allocated = is_allocated;
    priv->expired = FALSE;

    if (!privname) {
        PUSH_PRIVATE_TABLE(L,priv);
    } else {
        lua_settop(L,0);
        PUSH_PRIVATE_TABLE(L,priv);
        lua_pushstring(L,privname);
        return PrivateTable__index(L);
    }
    return 1;
}

typedef enum {
    PARAM_NONE,
    PARAM_ADDR_SRC,
    PARAM_ADDR_DST,
    PARAM_ADDR_DL_SRC,
    PARAM_ADDR_DL_DST,
    PARAM_ADDR_NET_SRC,
    PARAM_ADDR_NET_DST,
    PARAM_PORT_SRC,
    PARAM_PORT_DST,
    PARAM_CIRCUIT_ID,
    PARAM_DESEGMENT_LEN,
    PARAM_DESEGMENT_OFFSET,
    PARAM_PORT_TYPE,
    PARAM_ETHERTYPE
} pinfo_param_type_t;

static int pushnil_param(lua_State* L, packet_info* pinfo _U_, pinfo_param_type_t pt _U_ ) {
    lua_pushnil(L);
    return 1;
}

static int Pinfo_set_addr(lua_State* L, packet_info* pinfo, pinfo_param_type_t pt) {
    const address* from = checkAddress(L,1);
    address* to;

    if (! from ) {
        luaL_error(L,"Not an OK address");
        return 0;
    }

    if (!pinfo) {
        luaL_error(L,"expired_pinfo");
        return 0;
    }

    switch(pt) {
        case PARAM_ADDR_SRC:
            to = &(pinfo->src);
            break;
        case PARAM_ADDR_DST:
            to = &(pinfo->dst);
            break;
        case PARAM_ADDR_DL_SRC:
            to = &(pinfo->dl_src);
            break;
        case PARAM_ADDR_DL_DST:
            to = &(pinfo->dl_dst);
            break;
        case PARAM_ADDR_NET_SRC:
            to = &(pinfo->net_src);
            break;
        case PARAM_ADDR_NET_DST:
            to = &(pinfo->net_dst);
            break;
        default:
            g_assert(!"BUG: A bad parameter");
            return 0;
    }

    COPY_ADDRESS(to,from);
    return 0;
}

static int Pinfo_set_int(lua_State* L, packet_info* pinfo, pinfo_param_type_t pt) {
    gint64 v = luaL_checkint(L,1);

    if (!pinfo) {
        luaL_error(L,"expired_pinfo");
        return 0;
    }

    switch(pt) {
        case PARAM_PORT_SRC:
            pinfo->srcport = (guint32)v;
            return 0;
        case PARAM_PORT_DST:
            pinfo->destport = (guint32)v;
            return 0;
        case PARAM_CIRCUIT_ID:
            pinfo->circuit_id = (guint32)v;
            return 0;
        case PARAM_DESEGMENT_LEN:
            pinfo->desegment_len = (guint32)v;
            return 0;
        case PARAM_DESEGMENT_OFFSET:
            pinfo->desegment_offset = (int)v;
            return 0;
        case PARAM_ETHERTYPE:
            pinfo->ethertype = (guint32)v;
            return 0;
        default:
            g_assert(!"BUG: A bad parameter");
    }

    return 0;
}

typedef struct _pinfo_method_t {
    const gchar* name;
    lua_CFunction get;
    int (*set)(lua_State*, packet_info*, pinfo_param_type_t);
    pinfo_param_type_t param;
} pinfo_method_t;

static int Pinfo_hi(lua_State *L) {
    Pinfo pinfo = checkPinfo(L,1);
    Address addr;

    if (!pinfo) return 0;
    if (pinfo->expired) {
        luaL_error(L,"expired_pinfo");
        return 0;
    }

    addr = (Address)g_malloc(sizeof(address));
    if (CMP_ADDRESS(&(pinfo->ws_pinfo->src), &(pinfo->ws_pinfo->dst) ) >= 0) {
        COPY_ADDRESS(addr, &(pinfo->ws_pinfo->src));
    } else {
        COPY_ADDRESS(addr, &(pinfo->ws_pinfo->dst));
    }

    pushAddress(L,addr);
    return 1;
}

static int Pinfo_lo(lua_State *L) {
    Pinfo pinfo = checkPinfo(L,1);
    Address addr;

    if (!pinfo) return 0;
    if (pinfo->expired) {
        luaL_error(L,"expired_pinfo");
        return 0;
    }

    addr = (Address)g_malloc(sizeof(address));
    if (CMP_ADDRESS(&(pinfo->ws_pinfo->src), &(pinfo->ws_pinfo->dst) ) < 0) {
        COPY_ADDRESS(addr, &(pinfo->ws_pinfo->src));
    } else {
        COPY_ADDRESS(addr, &(pinfo->ws_pinfo->dst));
    }

    pushAddress(L,addr);
    return 1;
}


static const pinfo_method_t Pinfo_methods[] = {

	/* WSLUA_ATTRIBUTE Pinfo_number RO The number of this packet in the current file */
    {"number", Pinfo_number, pushnil_param, PARAM_NONE},

  	/* WSLUA_ATTRIBUTE Pinfo_len  RO The length of the frame */
    {"len", Pinfo_len, pushnil_param, PARAM_NONE },

	/* WSLUA_ATTRIBUTE Pinfo_caplen RO The captured length of the frame */
    {"caplen", Pinfo_caplen, pushnil_param, PARAM_NONE },

	/* WSLUA_ATTRIBUTE Pinfo_abs_ts RO When the packet was captured */
    {"abs_ts",Pinfo_abs_ts, pushnil_param, PARAM_NONE },

	/* WSLUA_ATTRIBUTE Pinfo_rel_ts RO Number of seconds passed since beginning of capture */
    {"rel_ts",Pinfo_rel_ts, pushnil_param, PARAM_NONE },

	/* WSLUA_ATTRIBUTE Pinfo_delta_ts RO Number of seconds passed since the last captured packet */
    {"delta_ts",Pinfo_delta_ts, pushnil_param, PARAM_NONE },

	/* WSLUA_ATTRIBUTE Pinfo_delta_dis_ts RO Number of seconds passed since the last displayed packet */
    {"delta_dis_ts",Pinfo_delta_dis_ts, pushnil_param, PARAM_NONE },

	/* WSLUA_ATTRIBUTE Pinfo_visited RO Whether this packet hass been already visited */
    {"visited",Pinfo_visited, pushnil_param, PARAM_NONE },

	/* WSLUA_ATTRIBUTE Pinfo_src RW Source Address of this Packet */
    {"src", Pinfo_src, Pinfo_set_addr, PARAM_ADDR_SRC },

	/* WSLUA_ATTRIBUTE Pinfo_dst RW Destination Address of this Packet */
    {"dst", Pinfo_dst, Pinfo_set_addr, PARAM_ADDR_DST },

	/* WSLUA_ATTRIBUTE Pinfo_lo RO lower Address of this Packet */
    {"lo", Pinfo_lo, pushnil_param, PARAM_NONE },

	/* WSLUA_ATTRIBUTE Pinfo_hi RW higher Address of this Packet */
    {"hi", Pinfo_hi, pushnil_param, PARAM_NONE },

	/* WSLUA_ATTRIBUTE Pinfo_dl_src RW Data Link Source Address of this Packet */
    {"dl_src", Pinfo_dl_src, Pinfo_set_addr, PARAM_ADDR_DL_SRC },

	/* WSLUA_ATTRIBUTE Pinfo_dl_dst RW Data Link Destination Address of this Packet */
    {"dl_dst", Pinfo_dl_dst, Pinfo_set_addr, PARAM_ADDR_DL_DST },

	/* WSLUA_ATTRIBUTE Pinfo_net_src RW Network Layer Source Address of this Packet */
    {"net_src", Pinfo_net_src, Pinfo_set_addr, PARAM_ADDR_NET_SRC },

	/* WSLUA_ATTRIBUTE Pinfo_net_dst RW Network Layer Destination Address of this Packet */
    {"net_dst", Pinfo_net_dst, Pinfo_set_addr, PARAM_ADDR_NET_DST },

	/* WSLUA_ATTRIBUTE Pinfo_ptype RW Type of Port of .src_port and .dst_port */
    {"port_type", Pinfo_ptype, pushnil_param, PARAM_NONE },

	/* WSLUA_ATTRIBUTE Pinfo_src_port RW Source Port of this Packet */
    {"src_port", Pinfo_src_port, Pinfo_set_int,  PARAM_PORT_SRC },

	/* WSLUA_ATTRIBUTE Pinfo_dst_port RW Source Address of this Packet */
    {"dst_port", Pinfo_dst_port, Pinfo_set_int,  PARAM_PORT_SRC },

	/* WSLUA_ATTRIBUTE Pinfo_ipproto RO IP Protocol id */
    {"ipproto", Pinfo_ipproto, pushnil_param,  PARAM_NONE },

	/* WSLUA_ATTRIBUTE Pinfo_circuit_id RO For circuit based protocols */
    {"circuit_id", Pinfo_circuit_id, Pinfo_set_int, PARAM_CIRCUIT_ID },

	/* WSLUA_ATTRIBUTE Pinfo_match RO Port/Data we are matching */
    {"match", Pinfo_match, pushnil_param, PARAM_NONE },

	/* WSLUA_ATTRIBUTE Pinfo_curr_proto RO Which Protocol are we dissecting */
    {"curr_proto", Pinfo_curr_proto, pushnil_param, PARAM_NONE },

	/* WSLUA_ATTRIBUTE Pinfo_columns RO Accesss to the packet list columns */
    {"columns", Pinfo_columns, pushnil_param, PARAM_NONE },

	/* WSLUA_ATTRIBUTE Pinfo_cols RO Accesss to the packet list columns (equivalent to pinfo.columns) */
    {"cols", Pinfo_columns, pushnil_param, PARAM_NONE },

	/* WSLUA_ATTRIBUTE Pinfo_desegment_len RW Estimated number of additional bytes required for completing the PDU */
    {"desegment_len", Pinfo_desegment_len, Pinfo_set_int,  PARAM_DESEGMENT_LEN },

	/* WSLUA_ATTRIBUTE Pinfo_desegment_offset RW Offset in the tvbuff at which the dissector will continue processing when next called*/
    {"desegment_offset", Pinfo_desegment_offset, Pinfo_set_int,  PARAM_DESEGMENT_OFFSET },

	/* WSLUA_ATTRIBUTE Pinfo_private_data RO Access to private data */
    {"private_data", Pinfo_private_data, pushnil_param, PARAM_NONE},

	/* WSLUA_ATTRIBUTE Pinfo_private RW Access to the private table entries */
    {"private", Pinfo_private, pushnil_param, PARAM_NONE},

	/* WSLUA_ATTRIBUTE Pinfo_ethertype RW Ethernet Type Code, if this is an Ethernet packet */
    {"ethertype", Pinfo_ethertype, Pinfo_set_int, PARAM_ETHERTYPE},

	/* WSLUA_ATTRIBUTE Pinfo_fragmented RO If the protocol is only a fragment */
    {"fragmented", Pinfo_fragmented, pushnil_param, PARAM_NONE},

	/* WSLUA_ATTRIBUTE Pinfo_in_error_pkt RO If we're inside an error packet */
    {"in_error_pkt", Pinfo_in_error_pkt, pushnil_param, PARAM_NONE},

	/* WSLUA_ATTRIBUTE Pinfo_match_uint RO Matched uint for calling subdissector from table */
    {"match_uint", Pinfo_match_uint, pushnil_param, PARAM_NONE },

	/* WSLUA_ATTRIBUTE Pinfo_match_string RO Matched string for calling subdissector from table */
    {"match_string", Pinfo_match_string, pushnil_param, PARAM_NONE },

    {NULL,NULL,NULL,PARAM_NONE}
};


static int pushnil(lua_State* L) {
    lua_pushnil(L);
    return 1;
}

static int Pinfo_index(lua_State* L) {
    Pinfo pinfo = checkPinfo(L,1);
    const gchar* name = luaL_checkstring(L,2);
    lua_CFunction method = pushnil;
    const pinfo_method_t* curr;

    if (! (pinfo && name) ) {
        lua_pushnil(L);
        return 1;
    }
    if (pinfo->expired) {
        luaL_error(L,"expired_pinfo");
        return 0;
    }

    for (curr = Pinfo_methods ; curr->name ; curr++) {
        if (g_str_equal(curr->name,name)) {
            method = curr->get;
            break;
        }
    }

    lua_settop(L,1);
    return method(L);
}

static int Pinfo_setindex(lua_State* L) {
    Pinfo pinfo = checkPinfo(L,1);
    const gchar* name = luaL_checkstring(L,2);
    int (*method)(lua_State*, packet_info* pinfo, pinfo_param_type_t) = pushnil_param;
    const pinfo_method_t* curr;
    pinfo_param_type_t param_type = PARAM_NONE;

    if (! (pinfo && name) ) {
        return 0;
    }
    if (pinfo->expired) {
        luaL_error(L,"expired_pinfo");
        return 0;
    }

    for (curr = Pinfo_methods ; curr->name ; curr++) {
        if (g_str_equal(curr->name,name)) {
            method = curr->set;
            param_type = curr->param;
            break;
        }
    }

    lua_remove(L,1);
    lua_remove(L,1);
    return method(L,pinfo->ws_pinfo,param_type);
}

/* Gets registered as metamethod automatically by WSLUA_REGISTER_CLASS/META */
static int Pinfo__gc(lua_State* L) {
    Pinfo pinfo = checkPinfo(L,1);

    if (!pinfo) return 0;

    if (!pinfo->expired)
        pinfo->expired = TRUE;
    else
        g_free(pinfo);

    return 0;

}

static const luaL_Reg Pinfo_meta[] = {
    {"__index", Pinfo_index},
    {"__newindex",Pinfo_setindex},
    {"__tostring", Pinfo_tostring},
    { NULL, NULL }
};

int Pinfo_register(lua_State* L) {
    WSLUA_REGISTER_META(Pinfo);
    outstanding_Pinfo = g_ptr_array_new();
    outstanding_Column = g_ptr_array_new();
    outstanding_Columns = g_ptr_array_new();
    outstanding_PrivateTable = g_ptr_array_new();
    return 1;
}
