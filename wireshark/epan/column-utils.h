/* column-utils.h
 * Definitions for column utility structures and routines
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

#ifndef __COLUMN_UTILS_H__
#define __COLUMN_UTILS_H__

#include <glib.h>

#include "column_info.h"
#include "packet_info.h"
#include <epan/epan.h>
#include "ws_symbol_export.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** @file
 *  Helper routines for column utility structures and routines.
 */

/** Allocate all the data structures for constructing column data, given
 * the number of columns.
 *
 * Internal, don't use this in dissectors!
 */
WS_DLL_PUBLIC void	col_setup(column_info *cinfo, const gint num_cols);

/** Cleanup all the data structures for constructing column data;
 * undoes the alocations that col_setup() does.
 *
 * Internal, don't use this in dissectors!
 */
WS_DLL_PUBLIC void	col_cleanup(column_info *cinfo);

/** Initialize the data structures for constructing column data.
 *
 * Internal, don't use this in dissectors!
 */
extern void	col_init(column_info *cinfo);

/** Fill in all columns of the given packet which are based on values from frame_data.
 *
 * Internal, don't use this in dissectors!
 */
WS_DLL_PUBLIC void col_fill_in_frame_data(const frame_data *fd, column_info *cinfo, const gint col, gboolean const fill_col_exprs);

/** Fill in all columns of the given packet.
 *
 * Internal, don't use this in dissectors!
 */
WS_DLL_PUBLIC void	col_fill_in(packet_info *pinfo, const gboolean fill_col_exprs, const gboolean fill_fd_colums);

/** Fill in columns if we got an error reading the packet.
 * We set most columns to "???", and set the Info column to an error
 * message.
 *
 * Internal, don't use this in dissectors!
 */
WS_DLL_PUBLIC void	col_fill_in_error(column_info *cinfo, frame_data *fdata, const gboolean fill_col_exprs, const gboolean fill_fd_colums);

/* Utility routines used by packet*.c */

/** Are the columns writable?
 *
 * @param cinfo the current packet row
 * @return TRUE if it's writable, FALSE if not
 */
WS_DLL_PUBLIC gboolean	col_get_writable(column_info *cinfo);

/** Set the columns writable.
 *
 * @param cinfo the current packet row
 * @param writable TRUE if it's writable, FALSE if not
 */
WS_DLL_PUBLIC void	col_set_writable(column_info *cinfo, const gboolean writable);

/** 
 * Checks if the given column can be filled with data.
 *
 * @param cinfo the current packet row
 * @param col the column to use, e.g. COL_INFO
 *
 * @deprecated Not needed in new code the check is done in 
 * in the column function calls.
 */
WS_DLL_PUBLIC gint	check_col(column_info *cinfo, const gint col);

/** Sets a fence for the current column content,
 * so this content won't be affected by further col_... function calls.
 *
 * This can be useful if a protocol is more than once in a single packet,
 * e.g. multiple HTTP calls in a single TCP packet.
 *
 * @param cinfo the current packet row
 * @param col the column to use, e.g. COL_INFO
 */
WS_DLL_PUBLIC void	col_set_fence(column_info *cinfo, const gint col);

/** Gets the text of a column element.
 *
 * @param cinfo the current packet row
 * @param col the column to use, e.g. COL_INFO
 *
 * @return the text string
 */
extern const gchar *col_get_text(column_info *cinfo, const gint col);

/** Clears the text of a column element.
 *
 * @param cinfo the current packet row
 * @param col the column to use, e.g. COL_INFO
 */
WS_DLL_PUBLIC void	col_clear(column_info *cinfo, const gint col);

/** Set (replace) the text of a column element, the text won't be copied.
 *
 * Usually used to set const strings!
 *
 * @param cinfo the current packet row
 * @param col the column to use, e.g. COL_INFO
 * @param str the string to set
 */
WS_DLL_PUBLIC void	col_set_str(column_info *cinfo, const gint col, const gchar * str);

/** Add (replace) the text of a column element, the text will be copied.
 *
 * @param cinfo the current packet row
 * @param col the column to use, e.g. COL_INFO
 * @param str the string to add
 */
WS_DLL_PUBLIC void	col_add_str(column_info *cinfo, const gint col, const gchar *str);

/** Add (replace) the text of a column element, the text will be formatted and copied.
 *
 * Same function as col_add_str() but using a printf-like format string.
 *
 * @param cinfo the current packet row
 * @param col the column to use, e.g. COL_INFO
 * @param format the format string
 * @param ... the variable number of parameters
 */
WS_DLL_PUBLIC void	col_add_fstr(column_info *cinfo, const gint col, const gchar *format, ...)
    G_GNUC_PRINTF(3, 4);

/** For internal Wireshark use only.  Not to be called from dissectors. */
void col_custom_set_edt(epan_dissect_t *edt, column_info *cinfo);

/** For internal Wireshark use only.  Not to be called from dissectors. */
WS_DLL_PUBLIC
void col_custom_prime_edt(epan_dissect_t *edt, column_info *cinfo);

/** For internal Wireshark use only.  Not to be called from dissectors. */
WS_DLL_PUBLIC
gboolean have_custom_cols(column_info *cinfo);
/** For internal Wireshark use only.  Not to be called from dissectors. */
WS_DLL_PUBLIC
gboolean col_has_time_fmt(column_info *cinfo, const gint col);
/** For internal Wireshark use only.  Not to be called from dissectors. */
WS_DLL_PUBLIC
gboolean col_based_on_frame_data(column_info *cinfo, const gint col);

/** Append the given text to a column element, the text will be copied.
 *
 * @param cinfo the current packet row
 * @param col the column to use, e.g. COL_INFO
 * @param str the string to append
 */
WS_DLL_PUBLIC void	col_append_str(column_info *cinfo, const gint col, const gchar *str);

/** Append the given text to a column element, the text will be formatted and copied.
 *
 * Same function as col_append_str() but using a printf-like format string.
 *
 * @param cinfo the current packet row
 * @param col the column to use, e.g. COL_INFO
 * @param format the format string
 * @param ... the variable number of parameters
 */
WS_DLL_PUBLIC void	col_append_fstr(column_info *cinfo, const gint col, const gchar *format, ...)
    G_GNUC_PRINTF(3, 4);

/** Prepend the given text to a column element, the text will be formatted and copied.
 *
 * @param cinfo the current packet row
 * @param col the column to use, e.g. COL_INFO
 * @param format the format string
 * @param ... the variable number of parameters
 */
WS_DLL_PUBLIC void	col_prepend_fstr(column_info *cinfo, const gint col, const gchar *format, ...)
    G_GNUC_PRINTF(3, 4);

/**Prepend the given text to a column element, the text will be formatted and copied.
 * This function is similar to col_prepend_fstr() but this function will
 * unconditionally set a fence to the end of the prepended data even if there
 * were no fence before.
 * The col_prepend_fstr() will only prepend the data before the fence IF
 * there is already a fence created. This function will create a fence in case
 * it does not yet exist.
 */
WS_DLL_PUBLIC void	col_prepend_fence_fstr(column_info *cinfo, const gint col, const gchar *format, ...)
    G_GNUC_PRINTF(3, 4);

/** Append the given text (prepended by a separator) to a column element.
 *
 * Much like col_append_str() but will prepend the given separator if the column isn't empty.
 *
 * @param cinfo the current packet row
 * @param col the column to use, e.g. COL_INFO
 * @param sep the separator string or NULL for default: ", "
 * @param str the string to append
 */
WS_DLL_PUBLIC void	col_append_sep_str(column_info *cinfo, const gint col, const gchar *sep,
		const gchar *str);

/** Append the given text (prepended by a separator) to a column element.
 *
 * Much like col_append_fstr() but will prepend the given separator if the column isn't empty.
 *
 * @param cinfo the current packet row
 * @param col the column to use, e.g. COL_INFO
 * @param sep the separator string or NULL for default: ", "
 * @param format the format string
 * @param ... the variable number of parameters
 */
WS_DLL_PUBLIC void	col_append_sep_fstr(column_info *cinfo, const gint col, const gchar *sep,
		const gchar *format, ...)
    G_GNUC_PRINTF(4, 5);

/** Set the given (relative) time to a column element.
 *
 * Used by multiple dissectors to set the time in the column
 * COL_DELTA_CONV_TIME
 *
 * @param cinfo		the current packet row
 * @param col		the column to use, e.g. COL_INFO
 * @param ts		the time to set in the column
 * @param fieldname	the fieldname to use for creating a filter (when
 *			  applying/preparing/copying as filter)
 */
WS_DLL_PUBLIC void 	col_set_time(column_info *cinfo, const int col,
			const nstime_t *ts, const char *fieldname);

WS_DLL_PUBLIC void set_fd_time(frame_data *fd, gchar *buf);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __COLUMN_UTILS_H__ */
