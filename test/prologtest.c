#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <prolog/prolog.h>

#define fatal(ec, fmt, args...) do {                    \
        printf("fatal error: "fmt"\n", ## args);        \
        exit(ec);                                       \
} while (0)

#define TESTPL "./libtest.pl"


int
main(int argc, char *argv[])
{
    char *default_files[] = {
        "prolog/hwconfig",
        "prolog/devconfig",
        "prolog/interface",
        "prolog/profile",
        "prolog/audio",
        "prolog/test"
    };
    char               **files;
    int                  nfile = sizeof(default_files)/sizeof(default_files[0]);

    prolog_predicate_t   *predicates, *p, *set_routes;
    char               ***actions;
    int                   i;

    /* initialize our prolog library */
    if (prolog_init("test", 0, 0, 0, 0) != 0)
        fatal(1, "failed to initialize prolog library");

    if (argc <= 1)
        files = default_files;
    else {
        files = argv + 1;
        nfile = argc - 1;
    }
        
    /* load our test files */
    for (i = 0; i < nfile; i++) {
        printf("loading file %s...\n", files[i]);
        if (prolog_load_file(files[i]))
            fatal(2, "failed to load %s", files[i]);
    }
    
    if ((predicates = prolog_predicates(NULL)) == NULL)
        fatal(3, "failed to get exported predicates from prolog");

    set_routes = NULL;
    for (p = predicates; p->name; p++) {
        printf("found exported predicate: %s%s%s/%d (0x%x)\n",
               p->module ?: "", p->module ? ":" : "",
               p->name, p->arity,
               (unsigned int)p->predicate);
        if (!strcmp(p->name, "set_routes") && p->arity == 1)
            set_routes = p;
    }

    if (set_routes == NULL)
        fatal(4, "failed to find exported predicate \"set_routes\"");
    
    printf("invoking prolog predicate %s\n", set_routes->name);
    if (!prolog_call(set_routes, &actions))
        fatal(5, "failed to invoke exported predicate %s", set_routes->name);
    
    prolog_dump_actions(actions);

    /* clean up our prolog library */
    prolog_exit();
    
    return 0;
}




/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
*/



