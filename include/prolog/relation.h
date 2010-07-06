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


#ifndef POLICY_RELATION_H
#define POLICY_RELATION_H

#include "list.h"


/*
 * Notes:
 *   Sets and relations operate on items. For generality and language
 *   independence we want items to be strings. For speed, however, we'd
 *   like to define the actual operations on integers. We might want to
 *   use something similar to X atoms, where we intern items by name and
 *   later refer to them by the unique integer ID we get back.
 *
 *   We could have it in the APIs without actually needing to implement it
 *   at the beginning by defining dummy typedef and macros like this...
 */

typedef char *item_t;

/*
item_t item_intern(char *item);
char * item_resolv(item_t item);
*/

#define item_intern(name) ((item_t)name)
#define item_lookup(item) ((char *)item)


typedef struct relation_s {
    char           *name;
    list_hook_t     hook;
    int             arity;
    char          **items;
    int           **relations;
    int             nrelation;
    int             nslot;
} relation_t;


relation_t *relation_create(char *name, int arity, char ***initial_items);
void        relation_destroy(relation_t *r);
relation_t *relation_lookup(char *name);
int         relation_insert(relation_t *r, char **items);
int         relation_delete(relation_t *r, char **items);
void        relation_reset(relation_t *r);
int         relation_member(relation_t *r, char **items);
void        relation_dump(relation_t *r);








#endif /* POLICY_RELATION_H */


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
*/

