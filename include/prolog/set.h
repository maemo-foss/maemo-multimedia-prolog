/*************************************************************************
This file is part of libtrace

Copyright (C) 2010 Nokia Corporation.

This library is free software; you can redistribute
it and/or modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation
version 2.1 of the License.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
USA.
*************************************************************************/


#ifndef POLICY_SET_H
#define POLICY_SET_H

#include "list.h"

typedef struct set_s {
    char         *name;                             /* name of this set */
    list_hook_t   hook;                             /* hook to more sets */
    char        **items;                            /* items in the set */
    int           nitem;                            /* number of items */
    int           nslot;                            /* number of slots */
} set_t;

set_t *set_create (char *name, char **initial_items);
void   set_destroy(set_t *set);

set_t *set_lookup(char *name);
int    set_insert(set_t *set, char *item);
int    set_delete(set_t *set, char *item);
void   set_reset (set_t *set);
int    set_member(set_t *set, char *item);



#endif /* POLICY_SET_H */




/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
*/

