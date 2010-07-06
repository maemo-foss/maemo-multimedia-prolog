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


#include <stdio.h>
#include <stdlib.h>

#include <glib.h>
#include <glib-object.h>

#include <prolog/factmap.h>

#define ACCESSORIES   "accessories"
#define ACCESSORY_KEY "com.nokia.policy.accessories"
#define DEVICE        "device"
#define STATE         "state"

#define fatal(ec, fmt, args...) do {                    \
        printf("fatal error: "fmt"\n", ## args);        \
        exit(ec);                                       \
    } while (0)



int
main(int argc, char *argv[])
{
    char *accessories[] = { "ihf", "earpiece", "microphone", NULL }, **acc;

    OhmFactStore     *store;
    OhmFact          *fact;
    OhmFactStoreView *view;
#ifdef NEW_OHMFACT
    OhmFactStoreSimpleView *sv;
    GValue                 *dev, *state, *d;
#else
    GValue                 dev, state, *d;
#endif
    OhmPatternMatch  *pm;
    OhmPattern       *pattern;
    GSList           *matches, *l;

    factmap_t        *map;
    char             *fields[] = { "device", "state", NULL };

    g_type_init();

    if ((store = ohm_fact_store_new()) == NULL)
        fatal(1, "failed to create fact store");

    if ((view = ohm_fact_store_new_view(store, NULL)) == NULL)
        fatal(2, "failed to create view");
  
    if ((map = factmap_create(store, ACCESSORIES, ACCESSORY_KEY, fields,
                              NULL, NULL)) == NULL)
        fatal(3, "failed to create factmap for %s", ACCESSORIES);  

    factmap_dump(map);

    pattern = ohm_pattern_new(ACCESSORY_KEY);

#ifdef NEW_OHMFACT
    ohm_fact_store_view_add(map->view, OHM_STRUCTURE(pattern));
    pattern = ohm_pattern_new(ACCESSORY_KEY);
    ohm_fact_store_view_add(view, OHM_STRUCTURE(pattern));
#else
    l = g_slist_prepend(NULL, pattern);
    ohm_fact_store_view_set_interested(view, l);
#endif  

    for (acc = accessories; *acc; acc++) {
        fact  = ohm_fact_new(ACCESSORY_KEY);
        dev   = ohm_value_from_string(*acc);
        state = ohm_value_from_int(1);

        if (!ohm_fact_store_insert(store, fact))
            fatal(2, "failed to add device %s to fact store", *acc);

#ifdef NEW_OHMFACT
        ohm_fact_set(fact, DEVICE, dev);
        ohm_fact_set(fact, STATE, state);
#else
        ohm_fact_set(fact, DEVICE, &dev);
        ohm_fact_set(fact, STATE, &state);
#endif
        factmap_update(map);
        factmap_dump(map);
    }
  
    for (l = ohm_fact_store_get_facts_by_name(store, ACCESSORY_KEY);
         l != NULL;
         l = g_slist_next(l)) {
        fact = (OhmFact *)l->data;
        d    = ohm_fact_get(fact, DEVICE);
    
        if (d == NULL)
            fatal(3, "failed to retrieve field %s for fact %p", DEVICE, fact);

        printf("got accessory %s\n", g_value_get_string(d));
    }

#ifdef NEW_OHMFACT
    sv = OHM_FACT_STORE_SIMPLE_VIEW(view);
    matches = ohm_fact_store_change_set_get_matches(sv->change_set);
#else
    matches = ohm_fact_store_change_set_get_matches(view->change_set);
#endif
    for (l = matches; l != NULL; l = g_slist_next(l)) {
        if (!OHM_PATTERN_IS_MATCH(l->data))
            fatal(4, "unexpected object type in changeset");
        pm = OHM_PATTERN_MATCH(l->data);
        printf("%s\n", ohm_pattern_match_to_string(pm));
    }

    if (matches == NULL) {
        printf("view had empty changeset\n");
    }


    return 0;
}




/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
*/

