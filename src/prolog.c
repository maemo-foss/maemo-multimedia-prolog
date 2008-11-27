#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <glib.h>
#include <glib-object.h>

#include <prolog/prolog.h>
#include <SWI-Stream.h>
#include <SWI-Prolog.h>

#ifndef PATH_MAX
#  define PATH_MAX 256
#endif

#define PROLOG_ERROR(fmt, args...) do {                 \
        fprintf(stderr, "[ERROR] "fmt"\n", ## args);    \
    } while (0)

#define PROLOG_WARNING(fmt, args...) do {               \
        fprintf(stderr, "[WARNING] "fmt"\n", ## args);  \
    } while (0)

#define PROLOG_INFO(fmt, args...) do {                  \
        fprintf(stdout, "[WARNING] "fmt"\n", ## args);  \
    } while (0)

#define MARK_ERROR()    do { libprolog_errors++;   } while (0)
#define CLEAR_ERRORS()  do { libprolog_errors = 0; } while (0)
#define HAS_ERRORS()    (libprolog_errors > 0)
#define START_LOADING() do { libprolog_loading++; } while (0)
#define DONE_LOADING()  do { libprolog_loading--; } while (0)
#define IS_LOADING()    (libprolog_loading > 0)

#define PRED_EXPORTED "exported"
#define PRED_RULES    "rules"

#define ALLOC_ARRAY ALLOC_ARR
#define LIBPROLOG     "libprolog.so"
#define OBJECT_NAME   "name"

#define NORMAL_QUERY_FLAGS (PL_Q_NORMAL | PL_Q_NODEBUG | PL_Q_CATCH_EXCEPTION)
#define TRACE_QUERY_FLAGS (PL_Q_NORMAL | PL_Q_CATCH_EXCEPTION)


enum {
    NATIVE  = 0,
    FOREIGN = 1,
};

enum {
    RESULT_UNKNOWN = 0,
    RESULT_ACTIONS,
    RESULT_OBJECTS,
    RESULT_EXCEPTION,
};


static int   load_file(char *path, int foreign);
static char *shlib_path(char *lib, char *buf, size_t size);
static int   collect_exported  (term_t pl_descriptor, int i, void *data);
static int   collect_undefined (term_t pl_descriptor, int i, void *data);
static int   collect_result    (qid_t qid, term_t pl_retval, void *retval);
static int   collect_actions   (term_t item, int i, void *data);
static int   collect_objects   (term_t item, int i, void *data);
static int   collect_exception (qid_t qid, void *retval);



static int  initialized = FALSE;
static char libpl[PATH_MAX];
static char lstack[16], gstack[16], tstack[16], astack[16];

static char *pl_argv[] = {
    NULL,                                      /* argv[0] */
    NULL,                                      /* -x */
    NULL,                                      /* bootfile or argv[0] */
    "-q",                                      /* no startup banners */
    "-nosignals",                              /* no prolog signal handling */
    "-tty",                                    /* no controlling TTY */
#define NUM_FIXED_ARGS 3
    "-L16k",                                   /* local stack size */
    "-G16k",                                   /* global stack size */
    "-T16k",                                   /* trail stack size */
    "-A16k",                                   /* argument stack size */
};


/*
 * load-time error detection
 */

static int libprolog_loading;
static int libprolog_errors;


/*
 * predicate tracing
 */

#define COMMAND_ENABLE     "enable"
#define COMMAND_DISABLE    "disable"
#define COMMAND_INDENT     "indent"
#define COMMAND_DEFAULTS   "defaults"
#define COMMAND_RESET      "reset"
#define COMMAND_CLEAR      "clear"
#define COMMAND_SHOW       "show"
#define COMMAND_ON         "on"
#define COMMAND_OFF        "off"
#define COMMAND_TRANSITIVE "transitive"
#define COMMAND_SUPPRESS   "suppress"
#define COMMAND_DETAILED   "detailed"
#define COMMAND_SHORT      "short"
#define PORT_CALL          "call"
#define PORT_REDO          "redo"
#define PORT_PROVEN        "proven"
#define PORT_FAILED        "failed"
#define PORT_EXIT          "exit"
#define PORT_FAIL          "fail"
#define PORT_ALL           "all"
#define WILDCARD_ANY       "*"

enum {
    PRED_TRACE_NONE       = 0x00,       /* not traced */
    PRED_TRACE_SHALLOW    = 0x01,       /* trace predicate */
    PRED_TRACE_TRANSITIVE = 0x02,       /* trace predicate and its clauses */
    PRED_TRACE_SUPPRESS,                /* always suppress this predicate */
};

enum {
    PRED_PORT_SUPPRESS  = 0x0,          /* suppress this port altogether */
    PRED_PORT_SHORT     = 0x1,          /* short summary at this port */
    PRED_PORT_DETAILED  = 0x2,          /* detailed information at this port */
};

typedef struct {
    int trace;                          /* predicate trace flags */
    int call;                           /* call port flags */
    int redo;                           /* redo port flags */
    int proven;                         /* proven (exit) port flags */
    int failed;                         /* failed (fail) port flags */
} pred_trace_t;

static int   register_predicates (void);
static int   predicate_trace_init(void);

static int   predicate_trace_init(void);
static void  predicate_trace_exit(void);
static void  predicate_trace_free(gpointer data);
static void  predicate_trace_clear(char *pred);
static pred_trace_t *predicate_trace_get(char *pred);
static int   prolog_tracing(int state);


static int         trace_enabled;       /* global trace enable/disable flag */
static int         trace_all;           /* trace all predicates */
static int         trace_transitive;    /* transitive tracing in effect */
static int         trace_indent;        /* indentation level per depth */
static GHashTable *trace_flags;         /* per-predicate trace flags */


/*****************************************************************************
 *                      *** initialization & cleanup ***                     *
 *****************************************************************************/

/*************************
 * prolog_init
 *************************/
int
prolog_init(char *argv0,
            int lsize, int gsize, int tsize, int asize, char *bootfile)
{
    char **argv;
    int    argc, status;

    if (initialized)
        return EBUSY;

    /*
     * Notes:
     *
     * PL_initialise wants to know the path to the binary that has libpl.a
     * linked into. It tries to load the binary as a prolog resource file.
     * If this fails it will practically skip parsing most of the command
     * line, including our options to silence output and limit the size of
     * the various stacks.
     *
     * In our case, libpl.a is linked to libprolog.so and not to the main
     * binary. Hence we first need to find the path to libprolog.so and then
     * pass this in as argv[0] to PL_initialise. Ugly, I know... but necessary
     * unless we link statically.
     *
     *
     * Notes #2:
     *
     * This is unarguably a linux-specific hack. An alternative would be
     * a GLIBC-specific hack using dl_iterate_phdr(3) to iterate directly
     * through the list of shared objects loaded into us. This would have
     * the benefit of not needing to open and parse /proc entries.
     *
     * Notes #3:
     *
     * Stand-alone precompiled prolog files include a copy of the interpreter
     * itself. Using one could be an alternative to avoid this problem
     * altogether.
     */
    
    putenv("SWI_HOME_DIR="PROLOG_HOME);    /* hmm... is this needed now ? */

    snprintf(lstack, sizeof(lstack), "-L%dk", lsize ?: 16);
    snprintf(gstack, sizeof(gstack), "-G%dk", gsize ?: 16);
    snprintf(tstack, sizeof(tstack), "-T%dk", tsize ?: 16);
    snprintf(astack, sizeof(astack), "-A%dk", asize ?: 16);

    if (bootfile != NULL)
        argv = pl_argv;
    else
        argv = pl_argv + 2;                   /* skip { "-x", bootfile } */
    
    argv[argc=0] = shlib_path(LIBPROLOG, libpl, sizeof(libpl));
    
    if (bootfile != NULL) {
        pl_argv[++argc] = "-x";               /* must be argv[1] */
        pl_argv[++argc] = bootfile;           /*     and argv[2] */
    }
    
    argc += NUM_FIXED_ARGS;
    pl_argv[++argc] = lstack;
    pl_argv[++argc] = gstack;
    pl_argv[++argc] = tstack;
    pl_argv[++argc] = astack;

#if 0
    {
        int i;
        for (i = 0; i <= argc; i++)
            printf("*** prolog argv[%d]: \"%s\"\n", i, argv[i]);
    }
#endif

    libprolog_loading = 0;
    CLEAR_ERRORS();

    if ((status = predicate_trace_init()) != 0)
        return status;
    
    if ((status = register_predicates()) != 0)
        return status;
    
    if (!PL_initialise(argc + 1, argv)) {
        PL_cleanup(0);
        return EINVAL;
    }
    
    initialized = TRUE;
    return status;

    (void)argv0;
}


/*************************
 * prolog_exit
 *************************/
void
prolog_exit(void)
{
    if (!initialized)
        return;
    
    if (PL_is_initialised(NULL, NULL))
        PL_cleanup(0);

    predicate_trace_exit();
    initialized = FALSE;
}


/*****************************************************************************
 *                    *** ruleset & extension loading ***                    *
 *****************************************************************************/

/*************************
 * prolog_load_file
 *************************/
int
prolog_load_file(char *path)
{
    return load_file(path, NATIVE);
}


/*************************
 * prolog_load_extension
 *************************/
int
prolog_load_extension(char *path)
{
    return load_file(path, FOREIGN);
}


/*************************
 * load_file
 *************************/
static int
load_file(char *path, int foreign)
{
    char        *loader = foreign ? "load_foreign_library" : "consult";
    predicate_t  pr_load;
    fid_t        frame;
    qid_t        qid;
    char       **exception;
    term_t       pl_path;
    int          success;

    if (!initialized)
        return ENOMEDIUM;
    
    CLEAR_ERRORS();
    START_LOADING();
    
    /*
     * Notes:
     *     We do our best to detect errors while loading files. However
     *     consult does not seem to fail or raise an exception on syntax
     *     errors, it simply prints an error message instead. Hence we
     *     fail to detect errors while loading native prolog files.
     *
     *     Hopefully we can find an acceptable way to detect syntax errors
     *     and don't need to revert to ugly hacks like reading and parsing
     *     our own output for errors.
     */

    frame = PL_open_foreign_frame();

    pr_load = PL_predicate(loader, 1, NULL);
    pl_path = PL_new_term_ref();
    PL_put_atom_chars(pl_path, path);
    
    qid     = PL_open_query(NULL, NORMAL_QUERY_FLAGS, pr_load, pl_path);
    success = PL_next_solution(qid);
    if (PL_exception(qid)) {
        collect_exception(qid, &exception);
        success = FALSE;
    }
    PL_close_query(qid);

    PL_discard_foreign_frame(frame);

    DONE_LOADING();
    
    if (HAS_ERRORS())
        return FALSE;
    else
        return success;
}


/*****************************************************************************
 *                        *** predicate handling ***                         *
 *****************************************************************************/


/********************
 * prolog_predicates
 ********************/
prolog_predicate_t *
prolog_predicates(char *query)
{
    char        *exported    = query ? query : PRED_EXPORTED;
    predicate_t  pr_exported = PL_predicate(exported, 1, NULL);
    fid_t        frame;
    term_t       pl_predlist = PL_new_term_ref();

    prolog_predicate_t *predicates, *undefined, *p;
    int                 npredicate;

    if (!initialized)
        return NULL;
    
    predicates = NULL;

    frame = PL_open_foreign_frame();

    if (!PL_call_predicate(NULL, NORMAL_QUERY_FLAGS, pr_exported,pl_predlist)) {
        /* if we have a new ruleset emulate old interface if we can */
        if (prolog_rules(&predicates, &undefined) != 0)
            goto fail;

        if (undefined == NULL)
            goto success;

        for (p = undefined; p->name != NULL; p++)
            PROLOG_WARNING("undefined predicate %s:%s/%d",
                           p->module, p->name, p->arity);
        prolog_free_predicates(undefined);
        goto fail;
    }
    
    if ((npredicate = prolog_list_length(pl_predlist)) <= 0)
        goto fail;

    if ((predicates = ALLOC_ARRAY(prolog_predicate_t, npredicate + 1)) == NULL)
        goto fail;
    
    if (prolog_walk_list(pl_predlist, collect_exported, predicates))
        goto fail;
    
 success:
    PL_discard_foreign_frame(frame);
    
    return predicates;

 fail:
    PL_discard_foreign_frame(frame);
    prolog_free_predicates(predicates);
    
    return NULL;
}


/********************
 * prolog_undefined
 ********************/
prolog_predicate_t *
prolog_undefined(void)
{
#define CHUNK 8

    predicate_t pr_prop = PL_predicate("predicate_property", 2, NULL);
    term_t      pl_args;
    fid_t       frame;
    qid_t       qid;

    prolog_predicate_t *predicates;
    int                 npredicate;

    npredicate = 0;

    if ((predicates = ALLOC_ARRAY(prolog_predicate_t, CHUNK + 1)) == NULL)
        return NULL;
    
    frame = PL_open_foreign_frame();
    pl_args = PL_new_term_refs(2);

    PL_unify_atom_chars(pl_args + 1, "undefined");
    
    qid = PL_open_query(NULL, NORMAL_QUERY_FLAGS, pr_prop, pl_args);
    while (PL_next_solution(qid)) {
        if (npredicate && ((npredicate & (CHUNK - 1)) == CHUNK - 1)) {
            if (!REALLOC_ARR(predicates, npredicate+1, npredicate+CHUNK+1))
                goto fail;
        }

        if (collect_undefined(pl_args, npredicate, predicates) != 0)
            goto fail;
        
        npredicate++;
    }

    PL_close_query(qid);
    PL_discard_foreign_frame(frame);

    return predicates;

 fail:
    PL_close_query(qid);
    PL_discard_foreign_frame(frame);
    prolog_free_predicates(predicates);
    
    return NULL;
#undef CHUNK
}


/********************
 * prolog_rules
 ********************/
int
prolog_rules(prolog_predicate_t **rules, prolog_predicate_t **undef)
{
    predicate_t pr_rules = PL_predicate(PRED_RULES, 2, NULL);
    fid_t       frame;
    term_t      pl_args;
    int         err, nrule, nundef;

    if (!initialized)
        return ENOMEDIUM;

    if (rules == NULL || undef == NULL)
        return EINVAL;
    
    *rules = NULL;
    *undef = NULL;
    
    frame   = PL_open_foreign_frame();
    pl_args = PL_new_term_refs(2);

    if (!PL_call_predicate(NULL, NORMAL_QUERY_FLAGS, pr_rules, pl_args))
        return ENOENT;
    
    if ((nrule = prolog_list_length(pl_args)) <= 0)
        return ENOENT;

    if ((nundef = prolog_list_length(pl_args + 1)) < 0)
        return EINVAL;
    
    if ((*rules = ALLOC_ARRAY(prolog_predicate_t, nrule + 1)) == NULL) {
        err = ENOMEM;
        goto fail;
    }
 
    if ((err = prolog_walk_list(pl_args, collect_exported, *rules)) != 0)
        goto fail;

    if (nundef > 0) {
        if ((*undef = ALLOC_ARRAY(prolog_predicate_t, nundef + 1)) == NULL) {
            err = ENOMEM;
            goto fail;
        }
        if ((err = prolog_walk_list(pl_args+1, collect_exported, *undef)) != 0)
            goto fail;
    }
    
    PL_discard_foreign_frame(frame);
    
    return 0;

 fail:
    PL_discard_foreign_frame(frame);
    if (rules)
        prolog_free_predicates(*rules);
    if (undef)
        prolog_free_predicates(*undef);
    *rules = *undef = NULL;
    return err;
}


/********************
 * prolog_free_predicates
 ********************/
void
prolog_free_predicates(prolog_predicate_t *predicates)
{
    prolog_predicate_t *p;
    
    if (predicates == NULL)
        return;
    
    for (p = predicates; p->name != NULL; p++) {
        FREE(p->module);
        FREE(p->name);
    }

    FREE(predicates);
}


/********************
 * collect_exported
 ********************/
static int
collect_exported(term_t pl_descriptor, int i, void *data)
{
    prolog_predicate_t *predicate = ((prolog_predicate_t *)data) + i;
    const char         *name;
    int                 arity;
    predicate_t         pr_predicate;
    atom_t              slash_name;
    term_t              pl_module, pl_descr, pl_name, pl_arity;

    /*
     * Notes:
     *     Prolog represents the term bar/3 as /(bar, 3), ie. as
     *     the functor '/' with arity 2 and arguments 'bar' and '3'.
     *     Similarly the term foo:bar/3 is represented by the
     *     compound :(foo, /(bar, 3)).
     */
    
    if (!PL_get_name_arity(pl_descriptor, &slash_name, &arity))
        return EINVAL;
    
    name = PL_atom_chars(slash_name);
    
    if (name[1] != '\0')
        return EINVAL;

    if (name[0] == ':') {
        pl_module = PL_new_term_refs(2);
        pl_descr  = pl_module + 1;
        if (!PL_get_arg(1, pl_descriptor, pl_module) ||
            !PL_get_arg(2, pl_descriptor, pl_descr))
            return EINVAL;
    
        if (!PL_get_chars(pl_module, (char **)&name, CVT_ALL))
            return EINVAL;
        
        if ((predicate->module = STRDUP(name)) == NULL)
            return ENOMEM;
        
        pl_descriptor = pl_descr;
        if (!PL_get_name_arity(pl_descriptor, &slash_name, &arity))
            return EINVAL;
    }

    name = PL_atom_chars(slash_name);
    
    if ((name = PL_atom_chars(slash_name)) == NULL ||
        (name[0] != '/' || name[1] != '\0' || arity != 2))
        return EINVAL;

    pl_name  = PL_new_term_refs(2);
    pl_arity = pl_name + 1;
    
    if (!PL_get_arg(1, pl_descriptor, pl_name) ||
        !PL_get_arg(2, pl_descriptor, pl_arity))
        return EINVAL;
    
    if (!PL_get_chars(pl_name, (char **)&name, CVT_ALL))
        return EINVAL;
    PL_get_integer(pl_arity, &arity);
    
    pr_predicate = PL_predicate(name, arity, predicate->module);
    
    predicate->name      = STRDUP(name);
    predicate->arity     = arity;
    predicate->predicate = pr_predicate;

    if (predicate->name == NULL)
        return ENOMEM;
    
    return 0;
}


/********************
 * collect_undefined
 ********************/
static int
collect_undefined(term_t pl_descriptor, int i, void *data)
{
    prolog_predicate_t *predicate = ((prolog_predicate_t *)data) + i;
    const char         *name;
    int                 arity;
    predicate_t         pr_predicate;
    atom_t              pred_name;
    term_t              pl_module, pl_descr;

    /*
     * Notes:
     *     Prolog represents the term foo:bar(_G1, _G2, ... _Gn) as the
     *     compound term :(foo, bar(_G1, _G2, ..., _Gn)., ie. the functor
     *     ':' with arity 2 and arguments 'foo' and bar(...).
     */
    
    if (!PL_get_name_arity(pl_descriptor, &pred_name, &arity))
        return EINVAL;
    
    name = PL_atom_chars(pred_name);
    
    if (name[0] == ':' && name[1] == '\0') {
        pl_module = PL_new_term_refs(2);
        pl_descr  = pl_module + 1;
        if (!PL_get_arg(1, pl_descriptor, pl_module) ||
            !PL_get_arg(2, pl_descriptor, pl_descr))
            return EINVAL;
    
        if (!PL_get_chars(pl_module, (char **)&name, CVT_ALL))
            return EINVAL;
        
        if ((predicate->module = STRDUP(name)) == NULL)
            return ENOMEM;
        
        pl_descriptor = pl_descr;
        if (!PL_get_name_arity(pl_descriptor, &pred_name, &arity))
            return EINVAL;

        name = PL_atom_chars(pred_name);
    }

    pr_predicate = PL_predicate(name, arity, predicate->module);
    
    predicate->name      = STRDUP(name);
    predicate->arity     = arity;
    predicate->predicate = 0;

    if (predicate->name == NULL)
        return ENOMEM;
    
    return 0;
}


/*****************************************************************************
 *                      *** prolog predicate invocation ***                  *
 *****************************************************************************/

/********************
 * prolog_call
 ********************/
int
prolog_call(prolog_predicate_t *p, void *retval, ...)
{
    va_list  ap;
    fid_t    frame;
    qid_t    qid;
    term_t   pl_args, pl_retval;
    char    *arg;
    int      i, success;

    
    frame = PL_open_foreign_frame();
    
    pl_args   = PL_new_term_refs(p->arity);
    pl_retval = pl_args + p->arity - 1;

    va_start(ap, retval);
    for (i = 0; i < p->arity - 1; i++) {
        arg = va_arg(ap, char *);
        PL_put_atom_chars(pl_args + i, arg);
    }
    va_end(ap);

    qid     = PL_open_query(NULL, NORMAL_QUERY_FLAGS, p->predicate, pl_args);
    success = PL_next_solution(qid);
    if (collect_result(qid, pl_retval, retval) != 0)
        success = FALSE;
    PL_close_query(qid);

    PL_discard_foreign_frame(frame);
    
    return success;
} 


/********************
 * prolog_acall
 ********************/
int
prolog_acall(prolog_predicate_t *p, void *retval, void **args, int narg)
{
    fid_t   frame;
    qid_t   qid;
    term_t  pl_args, pl_retval;
    int     i, a, type, success;
    
    if (narg < p->arity - 1)
        return FALSE;
    else if (narg > p->arity - 1) {
        PROLOG_WARNING("%s: ignoring extra %d parameter%s to %s",
                       __FUNCTION__,
                       narg - p->arity - 1, narg - p->arity - 1 > 1 ? "s" : "",
                       p->name);
    }
    
    frame = PL_open_foreign_frame();
    
    pl_args   = PL_new_term_refs(p->arity);
    pl_retval = pl_args + p->arity - 1;

    for (i = 0, a = 0; i < p->arity - 1; i++) {
        type  = (int)args[a++];
        switch (type) {
        case 's': PL_put_atom_chars(pl_args + i, (char *)args[a++]); break;
        case 'i': PL_put_integer(pl_args + i, (int)args[a++]);       break;
        case 'd': PL_put_float(pl_args + i, *(double *)args[a++]);   break;
        default:
            PROLOG_ERROR("%s: invalid prolog argument type 0x%x",
                         __FUNCTION__, type);
            success = FALSE;
            goto out;
        }
    }

    if (trace_enabled) {
        prolog_tracing(TRUE);
        trace_transitive = 0;
    }
    /*                            NORMAL_QUERY_FLAGS */
    qid     = PL_open_query(NULL, TRACE_QUERY_FLAGS, p->predicate, pl_args);
    success = PL_next_solution(qid);
    if (collect_result(qid, pl_retval, retval) != 0)
        success = FALSE;
    PL_close_query(qid);
    
 out:
    if (trace_enabled) {
        prolog_tracing(FALSE);
        if (trace_transitive != 0)
            printf("\n*** transitive = %d upon return\n", trace_transitive);
        trace_transitive = 0;
    }
    PL_discard_foreign_frame(frame);
    
    return success;
} 


/********************
 * prolog_vcall
 ********************/
int
prolog_vcall(prolog_predicate_t *p, void *retval, va_list ap)
{
    fid_t    frame;
    qid_t    qid;
    term_t   pl_args, pl_retval;
    char    *arg;
    int      i, success;

    
    frame = PL_open_foreign_frame();
    
    pl_args   = PL_new_term_refs(p->arity);
    pl_retval = pl_args + p->arity - 1;

    for (i = 0; i < p->arity - 1; i++) {
        arg = va_arg(ap, char *);
        PL_put_atom_chars(pl_args + i, arg);
    }

    qid     = PL_open_query(NULL, NORMAL_QUERY_FLAGS, p->predicate, pl_args);
    success = PL_next_solution(qid);
    if (collect_result(qid, pl_retval, retval) != 0)
        success = FALSE;
    PL_close_query(qid);
    
    PL_discard_foreign_frame(frame);
    
    return success;
} 


/*****************************************************************************
 *                        *** action/object handling ***                     *
 *****************************************************************************/


/********************
 * prolog_free_results
 ********************/
void
prolog_free_results(char ***results)
{
    int tag;

    if (results == NULL)
        return;

    switch ((tag = (int)results[-1])) {
    case RESULT_ACTIONS:   prolog_free_actions(results);   break;
    case RESULT_OBJECTS:   prolog_free_objects(results);   break;
    case RESULT_EXCEPTION: prolog_free_exception(results); break;
    default:
        PROLOG_WARNING("%s: called with invalid result type %d",
                       __FUNCTION__, tag);
    }
}


/********************
 * prolog_dump_results
 ********************/
void
prolog_dump_results(char ***results)
{
    int tag;

    if (results == NULL)
        return;

    switch ((tag = (int)results[-1])) {
    case RESULT_ACTIONS:   prolog_dump_actions(results);   break;
    case RESULT_OBJECTS:   prolog_dump_objects(results);   break;
    case RESULT_EXCEPTION: prolog_dump_exception(results); break;
    default:
        PROLOG_WARNING("%s: called with invalid result type %d",
                       __FUNCTION__, tag);
    }
}


/********************
 * prolog_free_actions
 ********************/
void
prolog_free_actions(char ***actions)
{
    int a, p;

    if (actions == NULL)
        return;

    if (actions[-1] != (char **)RESULT_ACTIONS) {
        PROLOG_WARNING("%s: called for invalid list (tag: 0x%x)",
                       __FUNCTION__, (int)actions[-1]);
        return;
    }
    
    for (a = 0; actions[a]; a++) {
        for (p = 0; actions[a][p]; p++)
            free(actions[a][p]);
        free(actions[a]);
    }
    free(actions - 1);
}


/********************
 * prolog_dump_actions
 ********************/
void
prolog_dump_actions(char ***actions)
{
    int a, p;

    if (actions == NULL)
        return;

    if (actions[-1] != (char **)RESULT_ACTIONS) {
        PROLOG_WARNING("%s: called for invalid list (tag: 0x%x)",
                       __FUNCTION__, (int)actions[-1]);
        return;
    }
    
    for (a = 0; actions[a]; a++) {
        printf("(%s", actions[a][0]);
        for (p = 1; actions[a][p]; p++)
            printf(", %s", actions[a][p]);
        printf(")\n");
    }
}


/********************
 * prolog_flatten_actions
 ********************/
char *
prolog_flatten_actions(char ***actions)
{
#define CHUNK 128
#define NEED_SPACE(n) do {                                      \
        if (left < n) {                                         \
            p -= (int)flattened;                                \
            if (!REALLOC_ARR(flattened, size, size + CHUNK)) {  \
                errno = ENOMEM;                                 \
                free(flattened);                                \
                return NULL;                                    \
            }                                                   \
            p    += (int)flattened;                             \
            size += CHUNK;                                      \
            left += CHUNK;                                      \
        }                                                       \
    } while (0)

    char **action, *a, *flattened, *p, *t;
    int    i, j, n;
    unsigned int size, left;
    
    if (actions == NULL)
        return NULL;
    
    if (actions[-1] != (char **)RESULT_ACTIONS) {
        PROLOG_WARNING("%s: called for invalid list (tag: 0x%x)",
                       __FUNCTION__, (int)actions[-1]);
        return NULL;
    }
    
    p = flattened = NULL;
    size = left = 0;

    NEED_SPACE(1);
    
    *p++ = '[';
    left--;

    for (i = 0; (action = actions[i]) != NULL; i++) {
        for (j = 0, t = "["; (a = action[j]) != NULL; j++, t = " ") {
            NEED_SPACE(strlen(a) + strlen(t));
            n     = snprintf(p, left, "%s%s", t, a);
            p    += n;
            left -= n;
        }
        NEED_SPACE(1);
        *p++ = ']';
        left--;
    }
    
    NEED_SPACE(2);
    *p++ = ']';
    *p   = '\0';
    
    return flattened;
#undef CHUNK
}


/********************
 * prolog_free_objects
 ********************/
void
prolog_free_objects(char ***objects)
{
    int i, p;

    if (objects == NULL)
        return;

    if (objects[-1] != (char **)RESULT_OBJECTS) {
        PROLOG_WARNING("%s: called for invalid list (tag: 0x%x)",
                       __FUNCTION__, (int)objects[-1]);
        return;
    }
    
    for (i = 0; objects[i]; i++) {
        for (p = 0; objects[i][p]; p += 3) {
            FREE(objects[i][p]);
            switch ((int)objects[i][p+1]) {
            case 's':
            case 'd': FREE(objects[i][p+2]);
                break;
            default:
                break;
            }
        }
        FREE(objects[i]);
    }
    FREE(objects - 1);
}


/********************
 * prolog_dump_objects
 ********************/
void
prolog_dump_objects(char ***objects)
{
    int   i, p, type;
    char *field, *value, *t;

    if (objects == NULL)
        return;

    if (objects[-1] != (char **)RESULT_OBJECTS) {
        PROLOG_WARNING("%s: called for invalid list (tag: 0x%x)",
                       __FUNCTION__, (int)objects[-1]);
        return;
    }

    for (i = 0; objects[i]; i++) {
        if (objects[i][0] != NULL && !strcmp(objects[i][0], "name") &&
            objects[i][2] != NULL) {
            printf("%s: ", objects[i][2]);
            p = 3;
        }
        else
            p = 0;

        printf("{ ");
        t = "";
        while (objects[i][p] != NULL) {
            field = objects[i][p];
            type  = (int)objects[i][p+1];
            value = objects[i][p+2];
            printf("%s%s: ", t, field);
            switch (type) {
            case 's': printf("'%s'", value);          break;
            case 'i': printf("%d", (int)value);       break;
            case 'd': printf("%f", *(double *)value); break;
            default:  printf("<unknown>");            break;
            }
            p += 3;
            t = ", ";
        }
        printf(" }\n");
    }
}


/********************
 * prolog_dump_exception
 ********************/
void
prolog_dump_exception(char ***exception)
{
    if (exception == NULL)
        return;
    
    if (exception[-1] != (char **)RESULT_EXCEPTION) {
        PROLOG_WARNING("%s: called for invalid list (tag: 0x%x)",
                       __FUNCTION__, (int)exception[-1]);
        return;
    }

    printf("prolog exception '%s'\n", (char *)exception[0]);
}


/********************
 * prolog_free_exception
 ********************/
void
prolog_free_exception(char ***exception)
{
    if (exception == NULL)
        return;

    if (exception[-1] != (char **)RESULT_EXCEPTION) {
        PROLOG_WARNING("%s: called for invalid list (tag: 0x%x)",
                       __FUNCTION__, (int)exception[-1]);
        return;
    }

    FREE((char *)exception[0]);
    FREE(exception - 1);
}


/*****************************************************************************
 *                        *** prolog list handling ***                       *
 *****************************************************************************/

/*************************
 * prolog_list_length
 *************************/
int
prolog_list_length(term_t pl_list)
{
    fid_t       frame;
    predicate_t pr_length;
    term_t      pl_args, pl_length;
    int         length;


    frame = PL_open_foreign_frame();

    pr_length = PL_predicate("length", 2, NULL);    
    pl_args   = PL_new_term_refs(2);
    pl_length = pl_args + 1;
    length    = -1;

    if (!PL_unify(pl_args, pl_list) || 
        !PL_call_predicate(NULL, PL_Q_NORMAL, pr_length, pl_args))
        goto out;

    PL_get_integer(pl_length, &length);
    
 out:
    PL_discard_foreign_frame(frame);

    return length;
}


/********************
 * prolog_list_new
 ********************/
term_t
prolog_list_new(char **items, int n, term_t result)
{
    term_t list = PL_new_term_ref();
    term_t item = PL_new_term_ref();

    if (n < 0) {                  /* NULL-terminated list, calculate items */
        n = 0;
        if (items)
            while (items[n])
                n++;
    }

    PL_put_nil(list);
    while (n-- > 0) {
        PL_put_atom_chars(item, items[n]);
        PL_cons_list(list, item, list);
    }
    
    if (result && PL_is_variable(result))
        PL_unify(list, result);
    
    return list;
}


/********************
 * prolog_list_prepend
 ********************/
term_t
prolog_list_prepend(term_t list, term_t item)
{
    PL_cons_list(list, item, list);
    
    return list;
}



/*************************
 * prolog_walk_list
 *************************/
int
prolog_walk_list(term_t list,
                 int (*callback)(term_t item, int i, void *data), void *data)
{
    term_t pl_list, pl_head;
    int    i, err;
    
    pl_list = PL_copy_term_ref(list);
    pl_head = PL_new_term_ref();

    for (i = err = 0; !err && PL_get_list(pl_list, pl_head, pl_list); i++)
        err = callback(pl_head, i, data);
    
    return err;
}



/*****************************************************************************
 *                       *** action/object collectors ***                    *
 *****************************************************************************/


/********************
 * is_action_list
 ********************/
static int
is_action_list(term_t list)
{
    term_t pl_list, pl_head, pl_action;
    
    pl_list   = PL_copy_term_ref(list);
    pl_head   = PL_new_term_refs(2);
    pl_action = pl_head + 1;


    /*
     * Notes:
     *   Action lists are of the form:
     *     [[action1, arg1, arg2, ...], [action2, arg1, arg2, ...], ...]
     *
     *   Object lists are of the form:
     *     [[name1, [field1, value1], [field2, value2]], ...]
     *
     *   Thus we conclude that list is an action list if the second element
     *   of the first element is not a list.
     */

    if (!PL_get_head(pl_list, pl_action))            /* get first action */
        return FALSE;
    
    if (!PL_get_list(pl_action, pl_head, pl_list))   /* get tail of action */
        return FALSE;
    
    if (!PL_get_head(pl_list, pl_head))              /* get second element */
        return FALSE;
    
    return !PL_is_list(pl_head);
}



/********************
 * collect_result
 ********************/
static int
collect_result(qid_t qid, term_t pl_retval, void *retval)
{
    char     *s;
    int       n;

    if (PL_exception(qid) != 0)
        return collect_exception(qid, retval);

    switch (PL_term_type(pl_retval)) {
        
    case PL_INTEGER:
        return !PL_get_integer(pl_retval, (int *)retval);

    case PL_FLOAT:
        return !PL_get_float(pl_retval, (double *)retval);

    case PL_ATOM:
        if (PL_is_list(pl_retval))              /* [] is an atom... */
            goto list;
        if (!PL_get_atom_chars(pl_retval, &s))
            return EIO;
        return (*(char **)retval = strdup(s)) == NULL ? EIO : 0;

    case PL_STRING:
        if (!PL_get_string_chars(pl_retval, &s, (size_t *)&n))
            return EIO;
        return (*(char **)retval = strdup(s)) == NULL ? EIO : 0;
        
    case PL_VARIABLE:
        *(void **)retval = NULL;
        return 0;

    case PL_TERM:
        if (!PL_is_list(pl_retval))
            goto invalid;
    list:
        if ((n = prolog_list_length(pl_retval)) < 0)
            return EIO;

        if (is_action_list(pl_retval)) {
            char ***actions;

            if ((actions = ALLOC_ARRAY(char **, 1 + n + 1)) == NULL)
                return ENOMEM;
            
            *actions++ = (char **)RESULT_ACTIONS;

            if (prolog_walk_list(pl_retval, collect_actions, actions)) {
                prolog_free_actions(actions);
                return EIO;
            }
            
            *(char ****)retval = actions;
            return 0;
        }
        else {
            char ***objects;
            
            if ((objects = ALLOC_ARRAY(char **, 1 + n + 1)) == NULL)
                return ENOMEM;
            
            *objects++ = (char **)RESULT_OBJECTS;

            if (prolog_walk_list(pl_retval, collect_objects, objects)) {
                prolog_free_objects(objects);
                return EIO;
            }
            
            *(char ****)retval = objects;
            return 0;
        }
        
    invalid:
    default:
        PROLOG_WARNING("%s: cannot handle term of type %d", __FUNCTION__,
                       PL_term_type(pl_retval));
        return EINVAL;
    }
}


/********************
 * collect_action
 ********************/
static int
collect_action(term_t pl_param, int i, void *data)
{
    char **action = (char **)data;
    char  *param;

    if (!PL_get_chars(pl_param, &param, CVT_ALL))
        return EINVAL;

    if ((action[i] = strdup(param)) == NULL)
        return ENOMEM;
    
    return 0;
}


/********************
 * collect_actions
 ********************/
static int
collect_actions(term_t item, int i, void *data)
{
    char ***actions = (char ***)data;
    char  **action  = NULL;
    int     length, err;

    if ((length = prolog_list_length(item)) < 0)
        return EINVAL;
    
    if (length > 0) {
        if ((action = ALLOC_ARRAY(char *, length + 1)) == NULL)
            return ENOMEM;
        
        if ((err = prolog_walk_list(item, collect_action, action)) != 0)
            return err;
    }
    
    actions[i] = action;
    return 0;
}


/********************
 * collect_object
 ********************/
static int
collect_object(term_t item, int i, void *data)
{
    char   **object = (char **)data;
    term_t   pl_field, pl_value;
    char    *field, *value, *type;
    double  *d;
    size_t   dummy;

    if (i == 0) {
        if (!PL_get_chars(item, &field, CVT_ALL))
            return EINVAL;
        
        if ((object[0] = STRDUP(OBJECT_NAME)) == NULL ||
            (object[2] = STRDUP(field)) == NULL)
            return ENOMEM;
        object[1] = (char *)'s';
    }
    else {
        pl_field = PL_new_term_refs(2);
        pl_value = pl_field + 1;
        
        if (!PL_get_list(item, pl_field, pl_value))
            return EINVAL;
        if (!PL_get_head(pl_value, pl_value))
            return EINVAL;
        if (!PL_get_chars(pl_field, &field, CVT_ALL))
            return EINVAL;
        
        if ((field = STRDUP(field)) == NULL)
            return ENOMEM;

        switch (PL_term_type(pl_value)) {
        case PL_ATOM:
            if (!PL_get_atom_chars(pl_value, &value))
                return EINVAL;
            value = STRDUP(value);
            type  = (char *)'s';
            break;
        case PL_STRING:
            if (!PL_get_string_chars(pl_value, &value, &dummy))
                return EINVAL;
            value = STRDUP(value);
            type  = (char *)'s';
            break;
        case PL_INTEGER:
            /* XXX TODO: change object to void ** to avoid breaking
               gcc's strict aliasing */
            if (!PL_get_integer(pl_value, (int *)(void *)&value))
                return EINVAL;
            type = (char *)'i';
            break;
        case PL_FLOAT:
            if (ALLOC_OBJ(d) == NULL)
                return ENOMEM;
            if (!PL_get_float(pl_value, d))
                return EINVAL;
            value = (char *)d;
            type  = (char *)'d';
            break;
        default:
            PROLOG_ERROR("%s: invalid prolog type (%d) for object field",
                         __FUNCTION__, PL_term_type(pl_value));
            return EINVAL;
        }
        
        object[3*i  ] = field;
        object[3*i+1] = type;
        object[3*i+2] = value;
    }

    return 0;
}
    

/********************
 * collect_objects
 ********************/
static int
collect_objects(term_t item, int i, void *data)
{
    char ***objects = (char ***)data;
    char  **object  = NULL;
    int      length, err;

    if ((length = prolog_list_length(item)) < 0)
        return EINVAL;
    
    if (length > 0) {
        if ((object = ALLOC_ARRAY(char *, 3 * length + 1)) == NULL)
            return ENOMEM;
        
        if ((err = prolog_walk_list(item, collect_object, object)) != 0)
            return err;
    }

    objects[i] = object;
    return 0;
}


/********************
 * parse_exception
 ********************/
static char *
parse_exception(term_t pl_exception)
{

    /*
     * The documentation says:
     *
     * Built-in predicates generate exceptions using a term
     * error(Formal, Context). The first argument is the `formal'
     * description of the error, specifying the class and generic
     * defined context information. When applicable, the ISO error-term
     * definition is used. The second part describes some additional
     * context to help the programmer while debugging. In its most generic
     * form this is a term of the form context(Name/Arity, Message), where
     * Name/Arity describes the built-in predicate that raised the error,
     * and Message provides an additional description of the error. Any
     * part of this structure may be a variable if no information was present.
     */

#define FAIL(fmt, args...) do { APPEND(fmt, ## args); goto out; } while (0)

#define APPEND(fmt, args...) do {                       \
        n     = snprintf(ep, left, fmt, ## args);       \
        ep   += n;                                      \
        left -= n;                                      \
    } while (0)

    fid_t  frame;
    term_t pl_terms, pl_formal, pl_context, pl_type, pl_kind, pl_what;
    
    char exception[1024], *ep, *s;
    int  size, left, n, arity, i;
    
    
    frame      = PL_open_foreign_frame();
    pl_terms   = PL_new_term_refs(5);

    i = 0;
    pl_formal  = pl_terms + i++;
    pl_context = pl_terms + i++;
    pl_type    = pl_terms + i++;
    pl_kind    = pl_terms + i++;
    pl_what    = pl_terms + i++;
    
    PL_get_arg(1, pl_exception, pl_formal);
    PL_get_arg(2, pl_exception, pl_context);
    
    ep   = exception;
    size = 0;
    left = sizeof(exception) - 1;

    if (PL_is_compound(pl_formal)) {
        /* eg: error(existence_error(procedure, foo/3), context(...)) */
        /* eg: error(type(kind, what), context(...)) */
        PL_get_name_arity(pl_formal, &pl_type, &arity);
        APPEND("%s", PL_atom_chars(pl_type));

        if (arity != 2)
            FAIL(" (unknown details)");
        
        PL_get_arg(1, pl_formal, pl_kind);
        if (!PL_get_chars(pl_kind, &s, CVT_WRITE | BUF_DISCARDABLE))
            FAIL(" (details in unknown format)");
        APPEND(": %s", s);
        
        PL_get_arg(2, pl_formal, pl_what);
        if (PL_is_atomic(pl_what)) {
            PL_get_chars(pl_what, &s, CVT_ALL | BUF_DISCARDABLE);
            APPEND(", %s", s);
        }
        else if (PL_is_compound(pl_what)) {
            PL_get_chars(pl_what, &s, CVT_WRITE | BUF_DISCARDABLE);
            APPEND(", %s", s);
        }
        else
            FAIL(" (details in unknown format)");
    }
    else
        FAIL("unknown prolog exception");
    
 out:
    PL_discard_foreign_frame(frame);

    
    return STRDUP(exception);
}


/********************
 * collect_exception
 ********************/
static int
collect_exception(qid_t qid, void *retval)
{
    char       ***objects;
    term_t        pl_error;
    atom_t        pl_name;
    int           arity;
    const char   *name;
    char         *error;

    if ((pl_error = PL_exception(qid)) == 0) {
        *(char **)retval = NULL;
        return 0;
    }

#if 0
    error = NULL;
    PL_get_chars(pl_error, &error, CVT_WRITE | BUF_DISCARDABLE);
    printf("*** write(exception): \"%s\" ***\n", error ? error : "<unknown>");
#endif

    if (!PL_is_compound(pl_error) ||
        !PL_get_name_arity(pl_error, &pl_name, &arity))
        return EINVAL;
    
    if ((name = PL_atom_chars(pl_name)) == NULL)
        return EINVAL;

    if (arity == 2 && !strcmp(name, "error"))
        error = parse_exception(pl_error);
    else
        error = STRDUP("unknown prolog exception");
    
    printf("*** prolog exception '%s'\n", error);

    if ((objects = ALLOC_ARRAY(char **, 1 + 1 + 1)) == NULL)
        return ENOMEM;

    objects[0] = (char **)RESULT_EXCEPTION;
    objects[1] = (char **)error;
    objects[2] = NULL;

    *(char ****)retval = objects + 1;

    return (objects[1] != NULL ? 0 : ENOMEM);
}


/*****************************************************************************
 *                            *** prolog shell ***                           *
 *****************************************************************************/

/********************
 * Sread_shell
 ********************/
static ssize_t
Sread_shell(void *handle, char *buf, size_t bufsize)
{
#define QUIT_COMMAND "quit"
    int fd = (int)handle;
    int n;

    if ((n = read(fd, buf, bufsize)) > 0) {
        if (n < (int)bufsize)
            buf[n] = '\0';

        /* convert CRLF to LF */
        if (n >= 2 && buf[n-1] == '\n' && buf[n-2] == '\r') {
            buf[n-2] = '\n';
            buf[--n] = '\0';
        }
        
        /* emulate EOF for our quit command */
        if (!strcmp(buf, QUIT_COMMAND"\n")) {
            *buf = '\0';
            n    = 0;
        }
    }
    
    /* disgusting kludge to get looping through multiple solutions working */
    if (n == 2 && buf[0] == ';' && buf[1] == '\n') {
        n = 1;
        buf[n] = '\0';
    }

    return (ssize_t)n;
}


/********************
 * Sclose_shell
 ********************/
static int
Sclose_shell(void *handle)
{
    return 0;
    (void)handle;
}


/********************
 * Scontrol_shell
 ********************/
static int
Scontrol_shell(void *handle, int action, void *arg)
{
    int fd = (int)handle;

    switch (action) {
    case SIO_GETFILENO:   *(int *)arg = fd; return 0;
    case SIO_SETENCODING:                   return 0;
    default:                                return -1;
    }
}


/********************
 * Sopen_shell
 ********************/
static IOSTREAM *
Sopen_shell(int fdin)
{
    static IOFUNCTIONS Sshellfunctions = {
        .read    = Sread_shell,
        .write   = NULL,
        .seek    = NULL,
        .close   = Sclose_shell,
        .control = Scontrol_shell,
        .seek64  = NULL
    };
    
    return Snew((void *)fdin, SIO_INPUT | SIO_ISATTY, &Sshellfunctions);
}


/********************
 * set_shell_IO
 ********************/
static IOSTREAM *
set_shell_IO(IOSTREAM *in)
{
    IOSTREAM    *old_in;
    predicate_t  pr_set_IO;
    fid_t        frame;
    term_t       pl_args;

    pr_set_IO = PL_predicate("set_prolog_IO", 3, NULL);
    
    old_in = Suser_input;
    frame  = PL_open_foreign_frame();

    pl_args = PL_new_term_refs(3);
    if (!PL_unify_stream(pl_args + 0, in) ||
        !PL_unify_stream(pl_args + 1, Suser_output) ||
        !PL_unify_stream(pl_args + 2, Suser_error))
        goto fail;

    if (!PL_call_predicate(NULL, PL_Q_NORMAL, pr_set_IO, pl_args))
        goto fail;

    PL_discard_foreign_frame(frame);
    return old_in;

 fail:
    PL_discard_foreign_frame(frame);
    return NULL;
}


/********************
 * prolog_shell
 ********************/
int
prolog_shell(int in)
{
    IOSTREAM *sin, *old;

    if ((sin = Sopen_shell(in)) == NULL)
        goto out;
    
    if ((old = set_shell_IO(sin)) == NULL)
        goto out;
    
    PL_toplevel();
    set_shell_IO(old);

 out:
    if (sin)
        Sclose(sin);
    
    return 0;
}


/*****************************************************************************
 *                          *** misc, helper routines ***                    *
 *****************************************************************************/



/********************
 * shlib_path
 ********************/
static char *
shlib_path(char *lib, char *buf, size_t bufsize)
{
    FILE  *maps = NULL;
    char  *p;
    int    plen, llen;

    snprintf(buf, bufsize - 1, "/proc/%u/maps", (int)getpid());
    if ((maps = fopen(buf, "r")) == NULL)
        return lib;

    llen = strlen(lib);
    while (fgets(buf, bufsize, maps) != NULL) {
        if ((plen = strlen(buf)) < llen)
            continue;

        if (*(p = buf + plen - 1) == '\n')
            *p-- = '\0', plen--;
        if (plen < llen)
            continue;

        p -= llen - 1;
        
        if (!strcmp(p, lib)) {
            while (p > buf) {
                if (*--p == ' ' || *p == '\t') {
                    fclose(maps);
                    return p + 1;
                }
            }
        }
    }
    
    fclose(maps);
    return lib;
}





/*****************************************************************************
 *                        *** rule/predicate tracing ***                     *
 *****************************************************************************/


/********************
 * prolog_tracing
 ********************/
static int
prolog_tracing(int state)
{
    predicate_t pred = PL_predicate(state ? "trace" : "notrace", 0, NULL);

    return PL_call_predicate(NULL, PL_Q_NORMAL, pred, 0);
}


/********************
 * predicate_trace_init
 ********************/
static int
predicate_trace_init(void)
{
    trace_enabled    = FALSE;
    trace_all        = FALSE;
    trace_transitive = 0;
    trace_indent     = 2;
    trace_flags = g_hash_table_new_full(g_str_hash, g_str_equal,
                                        free, predicate_trace_free);

    return trace_flags != NULL ? 0 : ENOMEM;
}


/********************
 * predicate_trace_exit
 ********************/
static void
predicate_trace_exit(void)
{
    if (trace_flags != NULL) {
        g_hash_table_destroy(trace_flags);
        trace_flags = NULL;
    }
    trace_enabled    = FALSE;
    trace_all        = FALSE;
    trace_transitive = 0;
}


/********************
 * predicate_trace_free
 ********************/
static void
predicate_trace_free(gpointer data)
{
    pred_trace_t *pt = (pred_trace_t *)data;

    if (pt != NULL)
        free(pt);
}


/********************
 * predicate_trace_reset
 ********************/
static void
predicate_trace_reset(void)
{
    if (trace_flags != NULL)
        g_hash_table_remove_all(trace_flags);
    trace_enabled    = FALSE;
    trace_all        = FALSE;
    trace_transitive = 0;
}


/********************
 * predicate_trace_set
 ********************/
static void
predicate_trace_set(char *pred, char *cmd)
{
    pred_trace_t *pt;
    char         *port;
    int           type;

    if (trace_flags == NULL || pred == NULL)
        return;
    
    if (!strcmp(pred, WILDCARD_ANY)) {
        if (!strcmp(cmd, COMMAND_OFF) || !strcmp(cmd, COMMAND_SUPPRESS))
            trace_all = FALSE;
        else if (!strcmp(cmd, COMMAND_ON) || !strcmp(cmd, COMMAND_TRANSITIVE))
            trace_all = TRUE;
        else {
            printf("Invalid command \"%s %s\".\n", pred, cmd);
            return;
        }
    }
    
    if (!strcmp(cmd, COMMAND_CLEAR)) {
        predicate_trace_clear(pred);
        return;
    }
    
    if ((pt = predicate_trace_get(pred)) == NULL) {
        if (ALLOC_OBJ(pt) == NULL) {
            printf("Failed to allocate memory for predicate tracing.\n");
            return;
        }
        pt->call   = PRED_PORT_DETAILED;
        pt->redo   = PRED_PORT_DETAILED;
        pt->proven = PRED_PORT_SHORT;
        pt->failed = PRED_PORT_SHORT;
        g_hash_table_insert(trace_flags, strdup(pred), pt);
    }

    if (!strcmp(cmd, COMMAND_OFF)) {
        pt->trace = PRED_TRACE_NONE;
        return;
    }
    if (!strcmp(cmd, COMMAND_SUPPRESS)) {
        pt->trace = PRED_TRACE_SUPPRESS;
        return;
    }
    if (!strcmp(cmd, COMMAND_ON)) {
        pt->trace = PRED_TRACE_SHALLOW;
        return;
    }
    if (!strcmp(cmd, COMMAND_TRANSITIVE)) {
        pt->trace = PRED_TRACE_TRANSITIVE;
        return;
    }
    if (!strcmp(cmd, COMMAND_DEFAULTS)) {
        pt->trace = PRED_TRACE_SHALLOW;
        pt->call   = PRED_PORT_DETAILED;
        pt->redo   = PRED_PORT_DETAILED;
        pt->proven = PRED_PORT_SHORT;
        pt->failed = PRED_PORT_SHORT;
        return;
    }
    
    port = cmd;
    if ((cmd = strchr(port, ' ')) == NULL) {
        printf("Invalid command \"%s\".\n", port);
        return;
    }
    *cmd++ = '\0';
    
    if (!strcmp(cmd, COMMAND_DETAILED))
        type = PRED_PORT_DETAILED;
    else if (!strcmp(cmd, COMMAND_SHORT))
        type = PRED_PORT_SHORT;
    else if (!strcmp(cmd, COMMAND_SUPPRESS))
        type = PRED_PORT_SUPPRESS;
    else {
        printf("Invalid command \"%s %s\".\n", port, cmd);
        return;
    }

    if (!strcmp(port, PORT_CALL)) {
        pt->call = type;
        return;
    }
    if (!strcmp(port, PORT_REDO)) {
        pt->redo = type;
        return;
    }
    if (!strcmp(port, PORT_PROVEN) || !strcmp(port, PORT_EXIT)) {
        pt->proven = type;
        return;
    }
    if (!strcmp(port, PORT_FAILED) || !strcmp(port, PORT_FAIL)) {
        pt->failed = type;
        return;
    }
    if (!strcmp(port, PORT_ALL)) {
        pt->call = pt->redo = pt->proven = pt->failed = type;
        return;
    }
    else {
        printf("Invalid command \"%s %s\".\n", port, cmd);
        return;
    }
}


/********************
 * predicate_trace_get
 ********************/
static pred_trace_t *
predicate_trace_get(char *pred)
{
    if (trace_flags != NULL)
        return (pred_trace_t *)g_hash_table_lookup(trace_flags, pred);
    else
        return NULL;
}


/********************
 * predicate_trace_clear
 ********************/
static void
predicate_trace_clear(char *pred)
{
    if (trace_flags != NULL)
        g_hash_table_remove(trace_flags, pred);
}


/********************
 * show_flags_for
 ********************/
static void
show_flags_for(gpointer key, gpointer value, gpointer data)
{
    char         *predicate = (char *)key;
    pred_trace_t *pt        = (pred_trace_t *)value;

    printf("  %s: ", predicate);
    printf("    tracing: ");
    switch (pt->trace) {
    case PRED_TRACE_NONE:       printf("off\n");                     break;
    case PRED_TRACE_SHALLOW:    printf("on (non-transitive)\n");     break;
    case PRED_TRACE_TRANSITIVE: printf("on (transitive)\n");         break;
    case PRED_TRACE_SUPPRESS:   printf("suppressed\n");              break;
    default:                    printf("unknown (%d)\n", pt->trace); break;
    }

    printf("    call port: %s\n",
           pt->call == PRED_PORT_SUPPRESS ? "suppress" :
           (pt->call == PRED_PORT_SHORT ? "short" : "detailed"));
    printf("    redo port: %s\n",
           pt->redo == PRED_PORT_SUPPRESS ? "suppress" :
           (pt->redo == PRED_PORT_SHORT ? "short" : "detailed"));
    printf("    proven port: %s\n",
           pt->proven == PRED_PORT_SUPPRESS ? "suppress" :
           (pt->proven == PRED_PORT_SHORT ? "short" : "detailed"));
    printf("    failed port: %s\n",
           pt->failed == PRED_PORT_SUPPRESS ? "suppress" :
           (pt->failed == PRED_PORT_SHORT ? "short" : "detailed"));
    
    (void)data;
}


/********************
 * predicate_trace_show
 ********************/
void
predicate_trace_show(void)
{
    if (trace_flags == NULL)
        return;
    
    g_hash_table_foreach(trace_flags, show_flags_for, NULL);
}


/********************
 * prolog_trace_set
 ********************/
int
prolog_trace_set(char *commands)
{
#define MAX_SIZE 1024

    char command[MAX_SIZE + 1], *p, *q;
    int  l, indent;

    p = commands;
    while (p && *p) {
        while (*p == ' ' || *p == '\t')
            p++;
        for (l = 0, q = command; p && *p && *p != ';' && l < MAX_SIZE; l++)
            *q++ = *p++;
        if (l >= MAX_SIZE)
            return EINVAL;
        if (*p == ';')
            *p++ = '\0';
        *q = '\0';
        while (l > 0 && (q[-1] == ' ' || q[-1] == '\t'))
            *--q = '\0';

        if (!strcmp(command, COMMAND_ENABLE)) {
            trace_enabled = TRUE;
            printf("rule/predicate tracing enabled\n");
        }
        else if (!strcmp(command, COMMAND_DISABLE)) {
            trace_enabled = FALSE;
            printf("rule/predicate tracing disabled\n");
        }
        else if (!strcmp(command, COMMAND_RESET)) {
            predicate_trace_reset();
            printf("rule/predicate tracing reset\n");
        }
        else if (!strcmp(command, COMMAND_SHOW)) {
            prolog_trace_show();
        }
        else if (!strncmp(command, COMMAND_INDENT, sizeof(COMMAND_INDENT)-1)) {
            indent = strtoul(command + sizeof(COMMAND_INDENT) - 1, NULL, 10);
            trace_indent = (indent >= 0 && indent < 8) ? indent : 0;
        }
        else {
            char *predicate, *actions, *a, *next;
            
            predicate = command;
            if ((actions = strchr(predicate, ' ')) == NULL)
                return EINVAL;
            *actions++ = '\0';

            for (a = actions, next = strchr(a, ','); a; a = next) {
                if (next != NULL)
                    *next++ = '\0';
                printf("action %s for predicate %s\n", a, predicate);
                predicate_trace_set(predicate, a);
            }
            
#if 0
            if (!strcmp(action, COMMAND_ON))
                flags = PRED_TRACE_SHALLOW;
            else if (!strcmp(action, COMMAND_OFF))
                flags = PRED_TRACE_NONE;
            else if (!strcmp(action, COMMAND_TRANSITIVE))
                flags = PRED_TRACE_TRANSITIVE;
            else if (!strcmp(action, COMMAND_SUPPRESS))
                flags = PRED_TRACE_SUPPRESS;
            else {
                printf("unknown rule trace action \"%s\"\n", action);
                return EINVAL;
            }
            if (!strcmp(predicate, WILDCARD_ANY)) {
                if (flags == PRED_TRACE_NONE || flags == PRED_TRACE_SUPPRESS) {
                    trace_all = FALSE;
                    printf("global predicate tracing turned off\n");
                }
                else {
                    trace_all = TRUE;
                    printf("global predicate tracing turned on\n");
                }
            }
            else {
                predicate_trace_set(strdup(predicate), flags);
                printf("predicate trace for %s set to %s\n", predicate, action);
            }
#endif
        }
    }

    return 0;
}


/********************
 * prolog_trace_show
 ********************/
void
prolog_trace_show(void)
{
    printf("Rule/predicate trace settings:\n");
    printf("  tracing currently %s\n", trace_enabled ? "enabled" : "disabled");
    printf("  tracing of all predicates %s\n", trace_all ? "on" : "off");
    printf("  trace indentation %d / level\n", trace_indent);
    predicate_trace_show();
}


/********************
 * pl_trace_pred
 ********************/
static foreign_t
pl_trace_pred(term_t pl_args, int arity, void *context)
{
    static atom_t  call = 0, redo = 0, proven = 0, failed = 0;
    atom_t         pl_port;
    char          *pred, *port, *format;
    pred_trace_t  *pt;
    int            flags, type;


    if (arity != 1 && arity != 3)
        PL_fail;
    
    if (!PL_get_chars(pl_args, &pred, CVT_WRITE|BUF_DISCARDABLE))
        PL_fail;
    
    pt = predicate_trace_get(pred);

    /* no entry and no globl or transitive tracing on, reject */
    if (pt == NULL && !trace_all && trace_transitive <= 0)
        PL_fail;
    
    flags = pt ? pt->trace : PRED_TRACE_NONE;

    /* explicit suppress, reject */
    if (flags == PRED_TRACE_SUPPRESS)
        PL_fail;
    
    /* explicit tracing on or global tracing on or transitive tracing on */
    if (flags == PRED_TRACE_SHALLOW || flags == PRED_TRACE_TRANSITIVE ||
        (flags == PRED_TRACE_NONE && (trace_all || trace_transitive > 0))) {
        if (arity == 1)
            PL_succeed;

        if (!PL_is_atom(pl_args + 1))
            PL_fail;
        
        PL_get_atom(pl_args + 1, &pl_port);
        
#define PT_TYPE(pt, port, dflt) type = (pt) ? (pt)->port : PRED_PORT_##dflt
        if      (pl_port == call)   PT_TYPE(pt, call, DETAILED);
        else if (pl_port == redo)   PT_TYPE(pt, redo, DETAILED);
        else if (pl_port == proven) PT_TYPE(pt, proven, SHORT);
        else if (pl_port == failed) PT_TYPE(pt, failed, SHORT);
        else if (!PL_get_atom_chars(pl_args + 1, &port))
            PL_fail;
        else if (!strcmp(port, "call"))   PT_TYPE(pt, call, DETAILED);
        else if (!strcmp(port, "redo"))   PT_TYPE(pt, redo, DETAILED);
        else if (!strcmp(port, "proven")) PT_TYPE(pt, proven, SHORT);
        else if (!strcmp(port, "failed")) PT_TYPE(pt, failed, SHORT);
        else
            PL_fail;
#undef PT_TYPE
        
        switch (type) {
        case PRED_PORT_SUPPRESS: PL_fail;
        case PRED_PORT_DETAILED: format = COMMAND_DETAILED; break;
        case PRED_PORT_SHORT:    format = COMMAND_SHORT;    break;
        default:                 format = COMMAND_SHORT;    break;
        }


        if (PL_unify_atom(pl_args + 2, PL_new_atom(format)))
            PL_succeed;
        else
            PL_fail;
    }
    
    PL_fail;

    (void)context;
}


/********************
 * pl_trace_event
 ********************/
static foreign_t
pl_trace_event(term_t pl_args, int arity, void *context)
{
    static atom_t  call = 0, redo = 0, proven = 0, failed = 0;
    atom_t         pl_event;
    char          *pred, *event = NULL;
    pred_trace_t  *pt;

    
    if (arity < 2 || !PL_get_chars(pl_args, &pred, CVT_WRITE|BUF_DISCARDABLE))
        PL_succeed;

    pt = predicate_trace_get(pred);

    if (pt == NULL || pt->trace != PRED_TRACE_TRANSITIVE)
        PL_succeed;

    if (!PL_is_atom(pl_args + 1))
        PL_succeed;

    PL_get_atom(pl_args + 1, &pl_event);

    if (pl_event == call || pl_event == redo) {
        printf("\n *** event: %s (%d)\n", pl_event == call ? "call" : "redo",
               trace_transitive);
        trace_transitive++;
    }
    else if (pl_event == proven || pl_event == failed) {
        printf("\n *** event: %s (%d)\n",
               pl_event == proven ? "proven" : "failed", trace_transitive);
        trace_transitive--;
    }
    else {
        if (!PL_get_atom_chars(pl_args + 1, &event))
            PL_succeed;

        printf("\n *** event: %s (#%d) (%d)\n", event, pl_event,
               trace_transitive);
        
        if (!strcmp(event, "call")) {
            call = pl_event;
            trace_transitive++;
        }
        else if (!strcmp(event, "redo")) {
            redo = pl_event;
            trace_transitive++;
        }
        else if (!strcmp(event, "proven")) {
            proven = pl_event;
            trace_transitive--;
        }
        else if (!strcmp(event, "failed")) {
            failed = pl_event;
            trace_transitive--;
        }
    }

    if (trace_transitive <= 0)
        trace_transitive = 1;

    PL_succeed;

    (void)context;
}












/*****************************************************************************
 *                      *** built-in foreign predicates ***                  *
 *****************************************************************************/

/********************
 * pl_loading
 ********************/
static foreign_t
pl_loading(term_t noargs, int arity, void *context)
{
    if (libprolog_loading < 0) {
        PROLOG_ERROR("MAJOR BUG: libprolog_loading < 0 (%d)...",
                     libprolog_loading);
    }

    if (IS_LOADING()) {
        PL_succeed;
    }
    else {
        PL_fail;
    }

    (void)noargs;
    (void)arity;
    (void)context;
}


/********************
 * pl_mark_error
 ********************/
static foreign_t
pl_mark_error(term_t noargs, int arity, void *context)
{
    MARK_ERROR();
    PL_succeed;

    (void)noargs;
    (void)arity;
    (void)context;
}


/********************
 * pl_clear_errors
 ********************/
static foreign_t
pl_clear_errors(term_t noargs, int arity, void *context)
{
    CLEAR_ERRORS();
    PL_succeed;

    (void)noargs;
    (void)arity;
    (void)context;
}


/********************
 * pl_has_errors
 ********************/
static foreign_t
pl_has_errors(term_t noargs, int arity, void *context)
{
    if (HAS_ERRORS())
        PL_succeed;
    else
        PL_fail;

    (void)noargs;
    (void)arity;
    (void)context;
}


/********************
 * register_predicates
 ********************/
static int
register_predicates(void)
{
#define NON_TRACEABLE (PL_FA_VARARGS | PL_FA_NOTRACE)
    
    static PL_extension predicates[] = {
        /* predicates for rule/predicate load-time error detection */
        { "loading"        , 0, pl_loading     , NON_TRACEABLE, },
        { "mark_error"     , 0, pl_mark_error  , NON_TRACEABLE, },
        { "clear_errors"   , 0, pl_clear_errors, NON_TRACEABLE, },
        { "has_errors"     , 0, pl_has_errors  , NON_TRACEABLE, },
        /* predicates for rule/predicate tracing */
        { "trace_predicate", 1, pl_trace_pred  , NON_TRACEABLE, },
        { "trace_predicate", 3, pl_trace_pred  , NON_TRACEABLE, },
        { "trace_event"    , 2, pl_trace_event , NON_TRACEABLE, },
        
        { NULL, 0, NULL, 0 },
    };

    PL_register_extensions_in_module("libprolog", predicates);
    return 0;
}




















#if 0

/*****************************************************************************
 *                         *** prolog_exec & friends ***                     *
 *****************************************************************************/


/********************
 * prolog_run
 ********************/
#if 0
int
prolog_run(char *code)
{
    fid_t       frame;
    predicate_t pr_atom2term, pr_goal;
    functor_t   f_goal;
    term_t      pl_lgb, pl_goal, pl_args;
    qid_t       qid;
    int         i, arity, status;

    /*
     * parse code using atom_to_term
     */
    
    if ((pr_atom2term = PL_predicate("atom_to_term", 3, NULL)) == NULL)
        return ENOENT;
    
    frame = PL_open_foreign_frame();

    /* atom_to_term(Line, Goal, Bindings) */
    pl_lgb = PL_new_term_refs(3);
    PL_put_atom_chars(pl_lgb, code);
    
    status = EINVAL;

    if (!PL_call_predicate(NULL, PL_Q_NORMAL, pr_atom2term, pl_lgb)) {
        printf("*** failed to run atom_to_term\n");
        goto out;
    }

    pl_goal = pl_lgb + 1;
    if (!PL_get_functor(pl_goal, &f_goal)) {
        printf("*** failed to get functor for goal\n");
        goto out;
    }

    /* create argument list */
    if ((arity = PL_functor_arity(f_goal)) > 0) {
        pl_args = PL_new_term_refs(arity);
        for (i = 0; i < arity; i++)
            _PL_get_arg(i + 1, pl_goal, pl_args + i);
    }
    else
        pl_args = 0;
    
    pr_goal = PL_pred(f_goal, NULL);

#if 0
    qid = PL_open_query(NULL, PL_Q_NORMAL, pr_goal, pl_args);
    while (PL_next_solution(qid))
        ;
    /* PL_exception(qid); */
    PL_close_query(qid);
#else
    PL_call_predicate(NULL, PL_Q_NORMAL, pr_goal, pl_args);

    {
        pl_result = PL_new_term_ref();
        PL_put_atom_chars(pl_result, "Result");
        
    }

#endif
    
    status = 0;

 out:
    PL_discard_foreign_frame(frame);
    return status;
}

#else

/* THIS_WORKS_SOMEWHAT */

int
prolog_run(char *code)
{
    fid_t       frame;
    predicate_t pr_a2t, pr_expq, pr_xeq, pr_write;
    term_t      pl_lgb, pl_gegbeb, pl_egeb, pl_goal, pl_args;
    int         status;

    
    /* find predicates atom_to_term and expand_query */
    if ((pr_a2t = PL_predicate("atom_to_term", 3, NULL)) == NULL)
        return ENOENT;
    
    if ((pr_expq = PL_predicate("expand_query", 4, "user")) == NULL &&
        (pr_expq = PL_predicate("expand_query", 4, NULL))   == NULL)
        return ENOENT;
    
    pr_xeq   = PL_predicate("$execute", 2, "$toplevel");
    pr_write = PL_predicate("write", 1, NULL);

    frame  = PL_open_foreign_frame();

    /* atom_to_term(Line, Goal, Bindings) */
    pl_lgb = PL_new_term_refs(3);
    PL_put_atom_chars(pl_lgb, code);
    
    status = EINVAL;

    if (!PL_call_predicate(NULL, PL_Q_NORMAL, pr_a2t, pl_lgb)) {
        printf("*** failed to run atom_to_term\n");
        goto out;
    }

    /* expand_query(Goal, ExpandedGoal, Bindings, ExpandedBindings) */
    pl_gegbeb = PL_new_term_refs(4);
    if (!PL_unify(pl_gegbeb + 0, pl_lgb + 1) ||
        !PL_unify(pl_gegbeb + 2, pl_lgb + 2)) {
        printf("*** failed to unify gegbeb\n");
        goto out;
    }

    if (!PL_call_predicate(NULL, PL_Q_NORMAL, pr_expq, pl_gegbeb)) {
        printf("*** expand_query failed\n");
        goto out;
    }

    pl_egeb = PL_new_term_refs(2);
    if (!PL_unify(pl_egeb + 0, pl_gegbeb + 1) ||
        !PL_unify(pl_egeb + 1, pl_gegbeb + 3)) {
        printf("*** failed to unify egeb\n");
        goto out;
    }

    PL_call_predicate(NULL, PL_Q_NORMAL, pr_xeq, pl_egeb);
    PL_call_predicate(NULL, PL_Q_NORMAL, pr_write, pl_egeb + 1);
    
    status = 0;
    
 out:
    PL_discard_foreign_frame(frame);

    return status;
}

#endif

#if 0

int
prolog_run(char *code)
{
    fid_t       frame;
    predicate_t pr_a2t, pr_expq;
    functor_t   xeq;
    term_t      pl_lgb, pl_gegbeb, pl_egeb, pl_goal;
    int         status;

    
    /* find predicates atom_to_term and expand_query */
    if ((pr_a2t = PL_predicate("atom_to_term", 3, NULL)) == NULL)
        return ENOENT;
    
    if ((pr_expq = PL_predicate("expand_query", 4, "user")) == NULL &&
        (pr_expq = PL_predicate("expand_query", 4, NULL))   == NULL)
        return ENOENT;
       
    xeq = PL_new_functor(PL_new_atom("$execute"), 2);
    
    frame = PL_open_foreign_frame();

    /* atom_to_term(Line, Goal, Bindings) */
    pl_lgb = PL_new_term_refs(3);
    PL_put_atom_chars(pl_lgb, code);
    
    status = EINVAL;

    if (!PL_call_predicate(NULL, PL_Q_NORMAL, pr_a2t, pl_lgb)) {
        printf("*** failed to run atom_to_term\n");
        goto out;
    }

    /* expand_query(Goal, ExpandedGoal, Bindings, ExpandedBindings) */
    pl_gegbeb = PL_new_term_refs(4);
    if (!PL_unify(pl_gegbeb + 0, pl_lgb + 1) ||
        !PL_unify(pl_gegbeb + 2, pl_lgb + 2)) {
        printf("*** failed to unify gegbeb\n");
        goto out;
    }

    if (!PL_call_predicate(NULL, PL_Q_NORMAL, pr_expq, pl_gegbeb)) {
        printf("*** expand_query failed\n");
        goto out;
    }

    pl_egeb = PL_new_term_refs(2);
    if (!PL_unify(pl_egeb + 0, pl_gegbeb + 1) ||
        !PL_unify(pl_egeb + 1, pl_gegbeb + 3)) {
        printf("*** failed to unify egeb\n");
        goto out;
    }

    PL_cons_functor(pl_goal, xeq, pl_egeb + 0, pl_egeb + 1);
    if (!PL_call(pl_goal, NULL)) {
        printf("*** $execute failed\n");
        goto out;
    }
        
    status = 0;
    

 out:
    PL_discard_foreign_frame(frame);

    return status;
}
#endif



/********************
 * prolog_exec
 ********************/
int
prolog_exec(char *code)
{
    IOSTREAM    *in, *old;
    predicate_t  pr_set_input, pr_toplevel;
    fid_t        frame;
    int          status;


    if ((pr_toplevel = PL_predicate("$toplevel", 0, NULL)) == NULL)
        return EIO;
    
    if ((in = Sopen_string(NULL, code, strlen(code), "r")) == NULL)
        return errno;

    old         = Suser_input;
    Suser_input = in;

    frame = PL_open_foreign_frame();
    PL_toplevel();
    PL_discard_foreign_frame(frame);
    status = 0;

    Suser_input = old;

#if 0
    if (in)                      /* XXX TODO: is this garbage-collected ? */
        Sclose(in);
#endif
    
    return status;
}

#endif




/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
*/

