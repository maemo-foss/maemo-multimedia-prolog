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

#include <SWI-Prolog.h>

#include <prolog/set.h>


typedef struct {
    set_t *set;
    int    idx;
} context_t;



/********************
 * pl_set_destroy
 ********************/
static foreign_t
pl_set_destroy(term_t pl_name)
{
    set_t *set;
    char  *name;
        
    if (!PL_is_atom(pl_name))
        PL_fail;
    
    PL_get_atom_chars(pl_name, &name);
    
    if ((set = set_lookup(name)) == NULL)
        PL_fail;
        
    set_destroy(set);
    PL_succeed;
}


/********************
 * pl_set_insert
 ********************/
static foreign_t
pl_set_insert(term_t pl_name, term_t pl_item)
{
    set_t *set;
    char  *name, *item;
        
    if (!PL_is_atom(pl_name) || !PL_is_atom(pl_item))
        PL_fail;
    
    PL_get_atom_chars(pl_name, &name);
    PL_get_atom_chars(pl_item, &item);
    
    if ((set = set_lookup(name)) == NULL || set_insert(set, item))
        PL_fail;
    else
        PL_succeed;
}


/********************
 * pl_set_delete
 ********************/
static foreign_t
pl_set_delete(term_t pl_name, term_t pl_item)
{
    set_t *set;
    char  *name, *item;
        
    if (!PL_is_atom(pl_name) || !PL_is_atom(pl_item))
        PL_fail;
    
    PL_get_atom_chars(pl_name, &name);
    PL_get_atom_chars(pl_item, &item);
    
    if ((set = set_lookup(name)) == NULL || set_delete(set, item))
        PL_fail;
    else
        PL_succeed;
}


/********************
 * pl_set_reset
 ********************/
static foreign_t
pl_set_reset(term_t pl_name)
{
    set_t     *set;
    char      *name;
        
    if (!PL_is_atom(pl_name))
        PL_fail;
    
    PL_get_atom_chars(pl_name, &name);
    
    if ((set = set_lookup(name)) == NULL)
        PL_fail;
   
    set_reset(set);
    PL_succeed;
}


/********************
 * pl_set_member
 ********************/
static foreign_t
pl_set_member(term_t pl_name, term_t pl_item, control_t handle)
{
    context_t *ctx;
    set_t     *set;
    char      *name;
    int        idx;
    term_t     pl_member;
    
    switch (PL_foreign_control(handle)) {
    case PL_FIRST_CALL:
        if (!PL_is_atom(pl_name))
            PL_fail;

        PL_get_atom_chars(pl_name, &name);
        if ((set = set_lookup(name)) == NULL)
            PL_fail;

        if ((ctx = malloc(sizeof(*ctx))) == NULL)
            PL_fail;
        memset(ctx, 0, sizeof(*ctx));
        ctx->set = set;
        break;
        
    case PL_REDO:
        ctx = PL_foreign_context_address(handle);
        break;
        
    case PL_CUTTED:
        ctx = PL_foreign_context_address(handle);
        goto nomore;

    default:
        PL_fail;
    }

    set = ctx->set;
    pl_member = PL_new_term_ref();
    while ((idx = ctx->idx++) < set->nitem) {
        PL_put_atom_chars(pl_member, set->items[idx]);
        if (PL_unify(pl_item, pl_member)) {
            PL_retry_address(ctx);
        }
    }
    
 nomore:
    free(ctx);
    PL_fail;
}


/********************
 * install
 ********************/
install_t
install(void)
{
#if 0
    PL_register_foreign("set_create" , 2, pl_set_create , 0);
#endif
    PL_register_foreign("set_destroy", 1, pl_set_destroy, 0);
    PL_register_foreign("set_insert" , 2, pl_set_insert , 0);
    PL_register_foreign("set_delete" , 2, pl_set_delete , 0);
    PL_register_foreign("set_reset"  , 1, pl_set_reset  , 0);
    PL_register_foreign("set_member" , 2, pl_set_member,PL_FA_NONDETERMINISTIC);
}



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
*/

