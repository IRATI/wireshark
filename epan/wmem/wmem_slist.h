/* wmem_slist.h
 * Definitions for the Wireshark Memory Manager Singly-Linked List
 * Copyright 2012, Evan Huus <eapache@gmail.com>
 *
 * $Id$
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __WMEM_SLIST_H__
#define __WMEM_SLIST_H__

#include <string.h>
#include <glib.h>

#include "wmem_core.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct _wmem_slist_t;
struct _wmem_slist_frame_t;

typedef struct _wmem_slist_t       wmem_slist_t;
typedef struct _wmem_slist_frame_t wmem_slist_frame_t;

WS_DLL_PUBLIC
guint
wmem_slist_count(const wmem_slist_t *slist);

WS_DLL_PUBLIC
wmem_slist_frame_t *
wmem_slist_front(const wmem_slist_t *slist);

WS_DLL_PUBLIC
wmem_slist_frame_t *
wmem_slist_frame_next(const wmem_slist_frame_t *frame);

WS_DLL_PUBLIC
void *
wmem_slist_frame_data(const wmem_slist_frame_t *frame);

WS_DLL_PUBLIC
void
wmem_slist_remove(wmem_slist_t *slist, void *data);

WS_DLL_PUBLIC
void
wmem_slist_prepend(wmem_slist_t *slist, void *data);

WS_DLL_PUBLIC
wmem_slist_t *
wmem_slist_new(wmem_allocator_t *allocator);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __WMEM_SLIST_H__ */

/*
 * Editor modelines  -  http://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 4
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * vi: set shiftwidth=4 tabstop=8 expandtab:
 * :indentSize=4:tabSize=8:noTabs=true:
 */
