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

#include <prolog/relation.h>

typedef struct {
    relation_t *r;
    int         idx;
} context_t;


/*************************
 * list_length
 *************************/
static int
list_length(term_t pl_list)
{
    predicate_t pr_length;
    term_t      pl_args, pl_length;
    int         length;

    pr_length = PL_predicate("length", 2, NULL);    
    pl_args   = PL_new_term_refs(2);
    pl_length = pl_args + 1;

    if (!PL_unify(pl_args, pl_list) || 
        !PL_call_predicate(NULL, PL_Q_NORMAL, pr_length, pl_args))
        length = -1;
    else
        PL_get_integer(pl_length, &length);
    
    return length;
}


/********************
 * list_new
 ********************/
static term_t
list_new(int arity)
{
    term_t list = PL_new_term_ref();
    term_t var;
    
    PL_put_nil(list);
    while (arity-- > 0) {
        var = PL_new_term_ref();
        PL_cons_list(list, var, list);
    }
    
    return list;
}


/********************
 * list_of_relation
 ********************/
static term_t
list_of_relation(relation_t *r, int i)
{
    int    n    = r->arity;
    term_t list = PL_new_term_ref();
    term_t item = PL_new_term_ref();
    
    PL_put_nil(list);
    while (n-- > 0) {
        PL_put_atom_chars(item, r->items[r->relations[i][n]]);
        PL_cons_list(list, item, list);
    }
    
    return list;
}


/********************
 * pl_related
 ********************/
static foreign_t
pl_related(term_t pl_name, term_t pl_list, control_t handle)
{
    context_t  *ctx;
    relation_t *r;
    char       *name;
    int         arity, idx;
    fid_t       frame;
    term_t      pl_items;
    
    switch (PL_foreign_control(handle)) {
    case PL_FIRST_CALL:
        if (!PL_is_atom(pl_name))
            PL_fail;

        PL_get_atom_chars(pl_name, &name);
        if ((r = relation_lookup(name)) == NULL)
            PL_fail;

        if (!PL_is_list(pl_list) || (arity = list_length(pl_list)) != r->arity)
            PL_fail;

        if ((ctx = malloc(sizeof(*ctx))) == NULL)
            PL_fail;
        memset(ctx, 0, sizeof(*ctx));
        ctx->r   = r;
        ctx->idx = 0;
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

    r = ctx->r;
    frame = PL_open_foreign_frame();
    while ((idx = ctx->idx++) < ctx->r->nrelation) {
        pl_items = list_of_relation(r, idx);
        if (PL_unify(pl_list, pl_items)) {
            PL_close_foreign_frame(frame);
            PL_retry_address(ctx);
        }
        PL_rewind_foreign_frame(frame);
    }
    PL_close_foreign_frame(frame);
    
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
    PL_register_foreign("related", 2, pl_related, PL_FA_NONDETERMINISTIC);
    PL_register_foreign("in_relation", 2, pl_related, PL_FA_NONDETERMINISTIC);
}

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

