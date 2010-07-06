/*************************************************************************
This file is part of libprolog

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



#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <prolog/list.h>
#include <prolog/set.h>


static LIST_HOOK(sets);


/********************
 * set_create
 ********************/
set_t *
set_create(char *name, char **initial_items)
{
    set_t *set;
    int    i;

    if (set_lookup(name) != NULL)
        return NULL;
    
    if ((set = malloc(sizeof(*set))) == NULL)
        goto fail;
    memset(set, 0, sizeof(*set));

    list_hook_init(&set->hook);

    if ((set->name = strdup(name)) == NULL)
        goto fail;
    
    if (initial_items != NULL)
        for (i = 0; initial_items[i]; i++)
            if (set_insert(set, initial_items[i]))
                goto fail;

    list_add_tail(&set->hook, &sets);
    return set;
    
 fail:
    if (set) {
        if (set->name)
            free(set->name);
        
        free(set);
    }
    return NULL;
}


/********************
 * set_destroy
 ********************/
void
set_destroy(set_t *set)
{
    if (set == NULL)
        return;
    
    list_delete(&set->hook);

    if (set->name)
        free(set->name);
    if (set->items) {
        set_reset(set);
        free(set->items);
    }
    
    free(set);
}


/********************
 * set_lookup
 ********************/
set_t *
set_lookup(char *name)
{
    list_hook_t *p;
    set_t       *set;

    list_iter_forw(p, &sets) {
        set = list_entry(p, set_t, hook);
        if (!strcmp(set->name, name))
            return set;
    }

    return NULL;
}


/********************
 * set_insert
 ********************/
int
set_insert(set_t *set, char *item)
{
#define CHUNK 8
    char **p;

    if (set_member(set, item))
        return 0;                                  /* hmm... EEXIST ? */
    
    if (set->items == NULL) {
        if ((set->items = malloc(CHUNK * sizeof(*set->items))) == NULL)
            return ENOMEM;
        memset(set->items, 0, CHUNK * sizeof(*set->items));
        set->nslot = CHUNK;
        set->nitem = 0;
    }
    else if (set->nitem >= set->nslot) {
        if ((p = realloc(set->items, (set->nslot+CHUNK) * sizeof(*p))) == NULL)
            return ENOMEM;
        memset(p + set->nslot, 0, CHUNK * sizeof(*p));
        set->nslot += CHUNK;
        set->items  = p;
    }
    
    if ((set->items[set->nitem] = strdup(item)) == NULL)
        return ENOMEM;

    set->nitem++;
    return 0;
}


/********************
 * set_delete
 ********************/
int
set_delete(set_t *set, char *item)
{
    int slot;

    for (slot = 0; slot < set->nitem; slot++) {
        if (set->items[slot] && !strcmp(set->items[slot], item)) {
            free(set->items[slot]);
            set->items[slot] = NULL;
            break;
        }
    }
    
    if (slot >= set->nitem)
        return ENOENT;
    
    set->nitem--;
    if (slot != set->nitem)               /* if not last, replace with last */
        set->items[slot] = set->items[set->nitem];
    
    return 0;
}


/********************
 * set_reset
 ********************/
void
set_reset(set_t *set)
{
    int slot;

    for (slot = 0; slot < set->nitem; slot++) {
        if (set->items[slot]) {
            free(set->items[slot]);
            set->items[slot] = NULL;
        }
    }
    set->nitem = 0;
}


/********************
 * set_member
 ********************/
int
set_member(set_t *set, char *item)
{
    int slot;

    for (slot = 0; slot < set->nitem; slot++) {
        if (set->items[slot] && !strcmp(set->items[slot], item))
            return 1;
    }
    
    return 0;
}


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
*/

