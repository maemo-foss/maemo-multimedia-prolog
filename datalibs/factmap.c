#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <prolog/relation.h>
#include <prolog/ohm-fact.h>
#include <prolog/factmap.h>

#define ALLOC(type) ({                            \
            type   *__ptr;                        \
            size_t  __size = sizeof(type);        \
                                                  \
            if ((__ptr = malloc(__size)) != NULL) \
                memset(__ptr, 0, __size);         \
            __ptr; })

#define ALLOC_OBJ(ptr) ((ptr) = ALLOC(typeof(*ptr)))

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
            if ((ptr) == NULL) {                                        \
                (__ptr) = ALLOC_ARR(typeof(*ptr), n);                   \
                ptr = __ptr;                                            \
            }                                                           \
            else if ((__ptr = realloc(ptr, __size)) != NULL) {          \
                if ((n) > (o))                                          \
                    memset(__ptr + (o), 0, ((n)-(o)) * sizeof(*ptr));   \
                ptr = __ptr;                                            \
            }                                                           \
            __ptr; })
                
#define FREE(obj) do { if (obj) free(obj); } while (0)

#define STRDUP(s) ({                                    \
            char *__s = s;                              \
            __s = ((s) ? strdup(s) : strdup(""));       \
            __s; })

#define DEBUG(fmt, args...) do {                                \
        int __depth = depth;                                    \
        while (__depth-- > 0)                                   \
            printf("    ");                                     \
        printf("[%s] "fmt"\n", __FUNCTION__, ## args);          \
    } while (0)


/********************
 * factmap_create
 ********************/
factmap_t *
factmap_create(OhmFactStore *store, char *name, char *key, char **members,
               int (*filter)(int, char **, void *), void *filter_data)
{
    factmap_t *map;
    int        i, arity;
    char      *p;

    if (store == NULL || name == NULL || key == NULL || members == NULL)
        return NULL;

    for (arity = 0; members[arity] != NULL; arity++)
        ;
    
    if ((map = ALLOC(factmap_t)) == NULL)
        return NULL;

    map->store       = store;
    map->nmember     = arity;
    map->filter      = filter;
    map->filter_data = filter_data;

    if ((map->key = STRDUP(key)) == NULL)
        goto fail;

    if ((map->members = ALLOC_ARR(char *, arity)) == NULL ||
        (map->relation = relation_create(name, arity, NULL)) == NULL)
        goto fail;
    
    for (i = 0; i < arity; i++)
        if ((map->members[i] = STRDUP(members[i])) == NULL)
            goto fail;

    if (factmap_update(map) != 0)
        goto fail;
    
    return map;
    
 fail:
    factmap_destroy(map);
}


/********************
 * factmap_destroy
 ********************/
void
factmap_destroy(factmap_t *map)
{
    int i;
    
    if (map == NULL)
        return;
    
    if (map->key)
        FREE(map->key);
    
    if (map->members) {
        for (i = map->nmember - 1; i >= 0; i--)
            if (map->members[i] != NULL)
                FREE(map->members[i]);
        FREE(map->members);
    }

    if (map->view) {
#ifdef NEW_OHMFACT
        OhmFactStoreSimpleView *sv = OHM_FACT_STORE_SIMPLE_VIEW(map->view);
        ohm_fact_store_change_set_reset(sv->change_set);
#else
        ohm_fact_store_change_set_reset(map->view->change_set);
#endif
        g_object_unref(map->view);
    }

    relation_destroy(map->relation);
    FREE(map);
}


/********************
 * factmap_update
 ********************/
int
factmap_update(factmap_t *map)
{
    OhmFact    *fact;
    OhmPattern *pattern;
    
    GSList  *l, *updates;
    GValue  *value, gstr = {0};
    char    *row[map->nmember];
    int      i;
    char     buf[map->nmember * 32], *p;
    size_t   left, n;
    

    /*
     * if we have a pattern check its view for updates
     */

    if (map->view != NULL) {
#ifdef NEW_OHMFACT
        OhmFactStoreSimpleView *sv = OHM_FACT_STORE_SIMPLE_VIEW(map->view);
        updates = ohm_fact_store_change_set_get_matches(sv->change_set);
#else
        updates = ohm_fact_store_change_set_get_matches(map->view->change_set);
#endif
        if (updates == NULL) {
            printf("***** no updates\n");
            return 0;
        }
    }
    else {
        if ((map->view = ohm_fact_store_new_view(map->store, NULL)) == NULL)
            return EIO;
        if ((pattern = ohm_pattern_new(map->key)) == NULL)
            return EIO;

#ifdef NEW_OHMFACT
        ohm_fact_store_view_add(map->view, OHM_STRUCTURE(pattern));
#else
        if ((l = g_slist_prepend(NULL, pattern)) == NULL) {
            g_object_unref(pattern);
            return EIO;
        }

        ohm_fact_store_view_set_interested(map->view, l);
#endif
    }

    printf("***** has updates\n");
    
    relation_reset(map->relation);
    
    p    = buf;
    left = sizeof(buf) - 1;
    for (l = ohm_fact_store_get_facts_by_name(map->store, map->key);
         l != NULL;
         l = g_slist_next(l)) {
        fact = OHM_FACT(l->data);
        for (i = 0; i < map->nmember; i++) {
            if ((value = ohm_fact_get(fact, map->members[i])) == NULL)
                return EIO;

            if (G_VALUE_HOLDS_STRING(value)) {
                row[i] = (char *)g_value_get_string(value);
                continue;
            }
            
            if (!g_value_type_transformable(G_VALUE_TYPE(value), G_TYPE_STRING))
                return EIO;
            
            g_value_init(&gstr, G_TYPE_STRING);
            if (!g_value_transform(value, &gstr))
                return EIO;
            
            n = snprintf(p, left, "%s", g_value_get_string(&gstr));
            g_value_unset(&gstr);
            
            if (n >= left)
                return EIO;
            
            row[i]  = p;
            p      += n;
        }
        if (map->filter == NULL ||
            map->filter(map->nmember, row, map->filter_data))
            if (relation_insert(map->relation, row))
                return EIO;
    }
    
    if (map->view) {
#ifdef NEW_OHMFACT
        OhmFactStoreSimpleView *sv = OHM_FACT_STORE_SIMPLE_VIEW(map->view);
        ohm_fact_store_change_set_reset(sv->change_set);
#else
        ohm_fact_store_change_set_reset(map->view->change_set);
#endif
    }
    
    return 0;
}


/********************
 * factmap_dump
 ********************/
void
factmap_dump(factmap_t *map)
{
    int   i;
    char *t;
    
    if (map == NULL || map->relation == NULL)
        return;

    printf("Contents of factmap \"%s\" (", map->key ?: "<unknown>");
    for (i = 0, t = ""; i < map->nmember; i++, t = ", ")
        printf("%s%s", t, map->members[i]);
    printf(")\n");
    
    relation_dump(map->relation);
}



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
*/



