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
#include <prolog/relation.h>


#define NOID 0xffffffff

#define DONT_ADD 0
#define AUTO_ADD 1

#define ALLOC(type) ({                            \
            type   *__ptr;                        \
            size_t  __size = sizeof(type);        \
                                                  \
            if ((__ptr = malloc(__size)) != NULL) \
                memset(__ptr, 0, __size);         \
            __ptr; })

#define ALLOC_ARR(type, n) ({                     \
            type   *__ptr;                        \
            size_t   __size = (n) * sizeof(type); \
                                                  \
            if ((__ptr = malloc(__size)) != NULL) \
                memset(__ptr, 0, __size);         \
            __ptr; })

#define REALLOC_ARR(ptr, o, n) ({                                       \
            typeof(ptr) __ptr;                                          \
            size_t      __size = sizeof(*ptr) * (n);                    \
                                                                        \
            if ((ptr) == NULL)                                          \
                (ptr) = ALLOC_ARR(typeof(*ptr), n);                     \
            else if ((__ptr = realloc(ptr, __size)) != NULL) {          \
                if ((n) > (o))                                          \
                    memset(__ptr + (o), 0, ((n)-(o)) * sizeof(*ptr));   \
                ptr = __ptr;                                            \
            }                                                           \
            ptr; })
                
#define FREE(obj) do { if (obj) free(obj); } while (0)

#define STRDUP(s) strdup(s)

static int items_to_relation(relation_t *r, char **items,
                             int *relation, int auto_add);
static int item_id(relation_t *r, char *item, int auto_add);






static LIST_HOOK(relations);



/********************
 * relation_create
 ********************/
relation_t *
relation_create(char *name, int arity, char ***initial_items)
{
    relation_t *r;
    int         i;
    
    if (relation_lookup(name) != NULL)
        return NULL;
    
    if ((r = ALLOC(relation_t)) == NULL)
        goto fail;
    
    list_hook_init(&r->hook);
    r->arity = arity;

    if ((r->name = STRDUP(name)) == NULL)
        goto fail;

    if (initial_items != NULL)
        for (i = 0; initial_items[i]; i++) {
            if (relation_insert(r, initial_items[i]))
                goto fail;
        }
    
    list_add_tail(&r->hook, &relations);
    return r;

 fail:
    relation_destroy(r);
    return NULL;
}


/********************
 * relation_destroy
 ********************/
void
relation_destroy(relation_t *r)
{
    int i;
    
    if (r == NULL)
        return;
    
    list_delete(&r->hook);
    
    if (r->name)
        FREE(r->name);
    if (r->items) {
        for (i = 0; r->items[i]; i++)
            FREE(r->items[i]);
        FREE(r->items);
    }
    if (r->relations)
        FREE(r->relations);
    
    FREE(r);
}


/********************
 * relation_lookup
 ********************/
relation_t *
relation_lookup(char *name)
{
    relation_t  *r;
    list_hook_t *p;

    list_iter_forw(p, &relations) {
        r = list_entry(p, relation_t, hook);
        if (!strcmp(r->name, name))
            return r;
    }
    
    return NULL;
}


/********************
 * relation_insert
 ********************/
int
relation_insert(relation_t *r, char **items)
{
    int *relation;
    
    if (relation_member(r, items))
        return 0;                                   /* hmm... EEXIST ? */

    if ((relation = ALLOC_ARR(int, r->arity)) == NULL ||
        items_to_relation(r, items, relation, AUTO_ADD))
        goto fail;
    
    if (r->nrelation >= r->nslot) {
        if (REALLOC_ARR(r->relations, r->nrelation, r->nrelation + 1) == NULL)
            return ENOMEM;
        r->nslot = r->nrelation + 1;
    }
    
    r->relations[r->nrelation] = relation;
    r->nrelation++;
    
    return 0;

 fail:
    FREE(relation);
    return ENOMEM;
}


/********************
 * relation_delete
 ********************/
int
relation_delete(relation_t *r, char **items)
{
    int slot, i;
    
    for (slot = 0; slot < r->nrelation; slot++) {
        for (i = 0; i < r->arity && items[i]; i++)
            if (strcmp(r->items[r->relations[slot][i]], items[i]))
                break;
        if (i == r->arity || !items[i])  /* full or full given match */
            break;
    }

    if (slot >= r->nrelation)
        return ENOENT;

    FREE(r->relations[slot]);
    r->nrelation--;
    
    if (slot != r->nrelation)              /* if not last, replace with last */
        r->relations[slot] = r->relations[r->nrelation];
    
    return 0;
}


/********************
 * relation_reset
 ********************/
void
relation_reset(relation_t *r)
{
    int i;

    r->nrelation = 0;
    if (r->items) {
        for (i = 0; r->items[i]; i++)
            FREE(r->items[i]);
        FREE(r->items);
        r->items = NULL;
    }
    if (r->relations) {
        for (i = 0; i < r->nrelation; i++)
            FREE(r->relations[i]);
        FREE(r->relations);
        r->relations = NULL;
        r->nslot     = 0;
    }
}


/********************
 * relation_member
 ********************/
int
relation_member(relation_t *r, char **items)
{
    int slot, i;
    
    for (slot = 0; slot < r->nrelation; slot++) {
        for (i = 0; i < r->arity; i++)
            if (strcmp(r->items[r->relations[slot][i]], items[i]))
                break;
        if (i == r->arity)
            return 1;
    }

    return 0;
}


/********************
 * relation_dump
 ********************/
void
relation_dump(relation_t *r)
{
    int   slot, i;
    char *t;

    for (slot = 0; slot < r->nrelation; slot++) {
        printf("%s(", r->name);
        t = "";
        for (i = 0; i < r->arity; i++) {
            printf("%s%s", t, r->items[r->relations[slot][i]]);
            t = ", ";
        }
        printf(")\n");
    }
}


/********************
 * items_to_relation
 ********************/
static int
items_to_relation(relation_t *r, char **items, int *relation, int auto_add)
{
    int i, item;

    for (i = 0; i < r->arity; i++) {
        if ((item = item_id(r, items[i], auto_add)) == NOID)
            return ENOENT;
        relation[i] = item;
    }

    return 0;
}


/********************
 * item_id
 ********************/
static int
item_id(relation_t *r, char *item, int auto_add)
{
    int i;
    
    if (r->items == NULL) {
        if (!auto_add)
            return NOID;
        if ((r->items = ALLOC_ARR(char *, 2)) == NULL)
            return NOID;
        i = 0;
    }
    else {
        for (i = 0; r->items[i]; i++)
            if (!strcmp(r->items[i], item))
                return i;
    
        if (!auto_add)
            return NOID;
    
        if (REALLOC_ARR(r->items, i + 1, i + 2) == NULL)
            return NOID;
    }    
     
    if ((r->items[i] = STRDUP(item)) == NULL)
        return NOID;
    
    return i;
}


#ifdef __TEST__

int
main(int argc, char *argv[])
{
#define fatal(ec, fmt, args...) do {              \
        printf("fatal error: "fmt".\n", ## args); \
        exit(ec);                                 \
    } while (0)
    

    char *chair[]  = { "chair" , "room"       , "floor" };
    char *laptop[] = { "laptop", "room"       , "table" };
    char *iriver[] = { "iriver", "study"      , "mousepad" };
    char *tv[]     = { "TV"    , "living room", "TV-stand" };
    char *htpc[]   = { "HTPC"  , "TV-stand"   , "bottom"   };
    char *fridge[] = { "fridge", "kitched"    , "floor"    };
    char *micro[]  = { "micro" , "cupbord"    , "bottom"   };
    char *bed[]    = { "bed"   , "bedroom"    , "floor"    };
    char *pizza[]  = { "pizza" , "fridge"     , "shelf"    };
    char *tub[]    = { "tub"   , "bathroom"   , "floor"    };

    char **initial_items[] = { chair, laptop, iriver, NULL };
    char **more_items[]    = { tv, htpc, fridge, micro, bed, pizza, tub, NULL };
    char ***items;

    relation_t *test;
    int         j, i;


#define check() do {                                                          \
        for (items = initial_items, j = 0; items[j]; j++)                     \
            printf("  chk: (%s,%s,%s): %s\n",                                 \
                   items[j][0], items[j][1], items[j][2],                     \
                   relation_member(test, items[j]) ?  "yes" : "no");          \
        for (items = more_items, j = 0; items[j]; j++)                        \
            printf("  chk: (%s,%s,%s): %s\n",                                 \
                   items[j][0], items[j][1], items[j][2],                     \
                   relation_member(test, items[j]) ? "yes" : "no");           \
    } while (0)


    if ((test = relation_create("in,on_top_of", 3, initial_items)) == NULL)
        fatal(1, "failed to create relation");
    
    for (i = 0; more_items[i]; i++) {
        if (relation_insert(test, more_items[i]))
            fatal(2, "failed to insert items");
        printf("inserted (%s, %s, %s)\n",
               more_items[i][0], more_items[i][1], more_items[i][2]);
        relation_dump(test);
        check();
    }

    for (i = 0; initial_items[i]; i++) {
        if (relation_delete(test, initial_items[i]))
            fatal(2, "failed to delete items");
        printf("deleted (%s, %s, %s)\n",
               initial_items[i][0], initial_items[i][1], initial_items[i][2]);
        relation_dump(test);
        check();
    }

    for (i = 0; more_items[i]; i++) {
        if (relation_delete(test, more_items[i]))
            fatal(2, "failed to delete items");
        printf("deleted (%s, %s, %s)\n",
               more_items[i][0], more_items[i][1], more_items[i][2]);
        relation_dump(test);
        check();
    }

    relation_reset(test);
    relation_destroy(test);
    
    return 0;
}


#endif /* __TEST__ */







/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
*/


