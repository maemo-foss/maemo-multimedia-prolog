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
#include <string.h>
#include <errno.h>

#include <glib.h>
#include <glib-object.h>

#include <prolog/prolog.h>

#include "libprolog.h"


#define HAS_WILDCARDS(p) (strchr((p), WILDCARD_ALL[0]))


/*
 * predicate tracing modes
 */

enum {
    PRED_TRACE_NONE       = 0x00,       /* not traced */
    PRED_TRACE_SHALLOW    = 0x01,       /* trace predicate */
    PRED_TRACE_TRANSITIVE = 0x02,       /* trace predicate and its clauses */
    PRED_TRACE_SUPPRESS,                /* always suppress this predicate */
};


/*
 * predicate port tracing modes (= level of detail)
 */

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

typedef struct {
    char   *module;
    char   *name;
    char   *arity;
    void   *data;
} foreach_t;


static void  predicate_trace_free(gpointer data);
static void  predicate_trace_clear(char *pred);
static pred_trace_t *predicate_trace_get(char *pred);
static void  predicate_trace_set(char *pred, char *cmd);
static int   predicate_trace_set_matching(char *pattern, char *action);



static int         trace_enabled;       /* global trace enable/disable flag */
static int         trace_all;           /* trace all predicates */
static int         trace_transitive;    /* transitive tracing in effect */
static int         trace_indent;        /* indentation level per depth */
static GHashTable *trace_flags;         /* per-predicate trace flags */








/*****************************************************************************
 *                        *** rule/predicate tracing ***                     *
 *****************************************************************************/


/********************
 * libprolog_tracing
 ********************/
int
libprolog_tracing(void)
{
    return trace_enabled;
}


/********************
 * libprolog_trace_init
 ********************/
int
libprolog_trace_init(void)
{
    trace_enabled    = FALSE;
    trace_all        = FALSE;
    trace_transitive = 0;
    trace_indent     = 2;
    trace_flags = g_hash_table_new_full(g_str_hash, g_str_equal,
                                        free, predicate_trace_free);
    prolog_trace_set(PRED_DEFAULT" on, off");
    
    return trace_flags != NULL ? 0 : ENOMEM;
}


/********************
 * libprolog_trace_exit
 ********************/
void
libprolog_trace_exit(void)
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

    if (pt != NULL) {
        if (pt->trace == PRED_TRACE_TRANSITIVE)
            trace_transitive--;
        free(pt);
    }
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
            PROLOG_ERROR("Invalid command \"%s %s\".", pred, cmd);
            return;
        }
    }
    
    if (!strcmp(cmd, COMMAND_CLEAR)) {
        if (!strcmp(pred, PRED_DEFAULT))
            PROLOG_ERROR("Cannot delete default predicate trace settings.");
        else
            predicate_trace_clear(pred);
        return;
    }
    
    if ((pt = predicate_trace_get(pred)) == NULL) {
        if (ALLOC_OBJ(pt) == NULL) {
            PROLOG_ERROR("Failed to allocate memory for predicate tracing.");
            return;
        }
        pt->call   = PRED_PORT_DETAILED;
        pt->redo   = PRED_PORT_DETAILED;
        pt->proven = PRED_PORT_SHORT;
        pt->failed = PRED_PORT_SHORT;
        g_hash_table_insert(trace_flags, strdup(pred), pt);
    }

    if (pt->trace != PRED_TRACE_TRANSITIVE)
        trace_transitive--;
    
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
        trace_transitive++;
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
        PROLOG_ERROR("Invalid command \"%s\".", port);
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
        PROLOG_ERROR("Invalid command \"%s %s\".", port, cmd);
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
        PROLOG_ERROR("Invalid command \"%s %s\".", port, cmd);
        return;
    }
}


/********************
 * trace_set_matching
 ********************/
static gboolean
trace_set_matching(gpointer key, gpointer value, gpointer data)
{
    char         *predicate = (char *)key;
    foreach_t    *fe        = (foreach_t *)data;
    char         *pm, *pn, *pa, *action, *m, *n, *a;

    (void)value;

#define MATCHES(p, s, l, t)                                       \
    ((*(p) == '%' && *(p+1) == '\0') || (!strncmp(p, s, l) && s[l] == t))

    if (predicate == NULL || fe == NULL)
        return FALSE;
    
    pm     = fe->module;
    pn     = fe->name;
    pa     = fe->arity;
    action = fe->data;
    
    m = predicate;
    if ((n = strchr(m, ':')) == NULL)
        n = ":";
    if ((a = strchr(n, '/')) == NULL)
        a = "/";
    
    n++;
    a++;


    /*
     * Notes: This is a bit stupid, we already have a pointer to pt, yet
     *        we call predicate_trace_set which looks it up again. This
     *        is not time-critical, so we don't care for now...
     */

    if (MATCHES(pm, m, strlen(pm), ':') &&
        MATCHES(pn, n, strlen(pn), '/') &&
        MATCHES(pa, a, strlen(pa), '\0')) {
        PROLOG_INFO("  %s %s", predicate, action);
        if (!strcmp(action, COMMAND_CLEAR))
            return TRUE;    /* foreach_remove will remove the entry */
        else
            predicate_trace_set(predicate, action);
    }
        
    return FALSE;
}


/********************
 * predicate_trace_set_matching
 ********************/
static int
predicate_trace_set_matching(char *pattern, char *action)
{
    foreach_t  fe;
    char       buf[256];
    char      *module, *name, *arity, defname[] = ":%", defarity[] = "/%";

    strncpy(buf, pattern, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    
    module = buf;
    if ((name = strchr(module, ':')) == NULL) {
        name = defname;
        arity = strchr(module, '/');
    }
    else
        arity = strchr(name, '/');
    if (arity == NULL)
        arity = defarity;
    
    *name++ = '\0';
    *arity++ = '\0';

    fe.module = module;
    fe.name   = name;
    fe.arity  = arity;
    fe.data   = action;
    
    if (!strcmp(action, COMMAND_CLEAR))
        g_hash_table_foreach_remove(trace_flags, trace_set_matching, &fe);
    else
        g_hash_table_foreach(trace_flags, (GHFunc)trace_set_matching, &fe);
    return 0;
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
 * predicate_trace_show
 ********************/
static void
predicate_trace_show(char *pred, pred_trace_t *pt, void *detailed)
{
    int on = 0;
    
    if (pt == NULL && (pt = predicate_trace_get(pred)) == NULL)
        return;
    
    PROLOG_INFO("  predicate %s: ", pred);
    switch (pt->trace) {
    case PRED_TRACE_NONE:       PROLOG_INFO("off");                     break;
    case PRED_TRACE_SHALLOW:    PROLOG_INFO("on"); on = 1;              break;
    case PRED_TRACE_TRANSITIVE: PROLOG_INFO("on (transitive)"); on = 1; break;
    case PRED_TRACE_SUPPRESS:   PROLOG_INFO("suppressed");              break;
    default:                    PROLOG_INFO("unknown (%d)", pt->trace); break;
    }

    if (!on && !detailed)
        return;
    
    PROLOG_INFO("      predicate call: %s",
                pt->call == PRED_PORT_SUPPRESS ? "suppress" :
                (pt->call == PRED_PORT_SHORT ? "short" : "detailed"));
    PROLOG_INFO("      predicate redo: %s",
                pt->redo == PRED_PORT_SUPPRESS ? "suppress" :
                (pt->redo == PRED_PORT_SHORT ? "short" : "detailed"));
    PROLOG_INFO("    predicate proven: %s",
                pt->proven == PRED_PORT_SUPPRESS ? "suppress" :
                (pt->proven == PRED_PORT_SHORT ? "short" : "detailed"));
    PROLOG_INFO("    predicate failed: %s",
                pt->failed == PRED_PORT_SUPPRESS ? "suppress" :
                (pt->failed == PRED_PORT_SHORT ? "short" : "detailed"));
}


/********************
 * prolog_trace_set
 ********************/
PROLOG_API int
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
            PROLOG_INFO("rule/predicate tracing enabled");
        }
        else if (!strcmp(command, COMMAND_DISABLE)) {
            trace_enabled = FALSE;
            PROLOG_INFO("rule/predicate tracing disabled");
        }
        else if (!strcmp(command, COMMAND_RESET)) {
            predicate_trace_reset();
            PROLOG_INFO("rule/predicate tracing reset");
        }
        else if (!strcmp(command, COMMAND_SHOW)) {
            prolog_trace_show(NULL);
        }
        else if (!strncmp(command, COMMAND_SHOW, sizeof(COMMAND_SHOW) - 1)) {
            prolog_trace_show(command + sizeof(COMMAND_SHOW));
        }
        else if (!strncmp(command, COMMAND_INDENT, sizeof(COMMAND_INDENT)-1)) {
            indent = strtoul(command + sizeof(COMMAND_INDENT) - 1, NULL, 10);
            trace_indent = (indent >= 0 && indent < 8) ? indent : 0;
        }
        else {
            char *predicate, *actions, *a, *n;
            
            predicate = command;
            if ((actions = strchr(predicate, ' ')) == NULL)
                return EINVAL;
            *actions++ = '\0';

            a = actions;
            n = strchr(a, ',');
            while (a != NULL) {
                if (n != NULL)
                    *n++ = '\0';
                while (*a == ' ' || *a == '\t')
                    a++;
                if (HAS_WILDCARDS(predicate)) {
                    PROLOG_INFO("  %s predicates matching %s", a, predicate);
                    predicate_trace_set_matching(predicate, a);
                }
                else {
                    PROLOG_INFO("  %s predicate %s", a, predicate);
                    predicate_trace_set(predicate, a);
                }
                
                a = n;
                n = a ? strchr(a, ',') : NULL;
            }
        }
    }

    return 0;
}


/********************
 * prolog_trace_show
 ********************/
PROLOG_API void
prolog_trace_show(char *predicate)
{
    if (trace_flags == NULL)
        return;
    
    if (!predicate || !*predicate || !strcmp(predicate, WILDCARD_ALL)) {
        PROLOG_INFO("Rule/predicate trace settings:");
        PROLOG_INFO("  tracing %s", trace_enabled ? "enabled" : "disabled");
        PROLOG_INFO("  forced tracing of all predicates %s",
                    trace_all ? "on" : "off");
        PROLOG_INFO("  trace indentation %d / level", trace_indent);
        g_hash_table_foreach(trace_flags,
                             (GHFunc)predicate_trace_show, NULL);
    }
    else
        predicate_trace_show(predicate, NULL, (void *)0x1);
}


/*****************************************************************************
 *                         *** foreign predicates ***                        *
 *****************************************************************************/


/********************
 * libpl_trace_pred
 ********************/
foreign_t
libpl_trace_pred(term_t pl_args, int arity, void *context)
{
    char          *pred, *state;
    pred_trace_t  *pt;
    int            flags;

    (void)context;

    if (arity != 1 && arity != 2)
        PL_fail;
    
    if (!PL_get_chars(pl_args, &pred, CVT_WRITE|BUF_DISCARDABLE))
        PL_fail;
    
    pt = predicate_trace_get(pred);

    /* no explicit, global or transitive tracing in effect, reject */
    if (pt == NULL && !trace_all && trace_transitive <= 0)
        PL_fail;
    
    flags = pt ? pt->trace : PRED_TRACE_NONE;

    /* explicit suppress, reject */
    if (flags == PRED_TRACE_SUPPRESS)
        PL_fail;
    
    /* explicit, global or transitive tracing in effect */
    if (flags == PRED_TRACE_SHALLOW || flags == PRED_TRACE_TRANSITIVE ||
        (flags == PRED_TRACE_NONE && (trace_all || trace_transitive > 0))) {
        if (arity == 1)              /* trace_predicate(predicate) */
            PL_succeed;
        
        /* (arity == 2)                 trace_predicate(predicate, Setting) */
        switch (flags) {
        case PRED_TRACE_NONE:       state = COMMAND_OFF;        break;
        case PRED_TRACE_SHALLOW:    state = COMMAND_ON;         break;
        case PRED_TRACE_TRANSITIVE: state = COMMAND_TRANSITIVE; break;
#ifdef LET_COVERITY_BE_UNHAPPY
        case PRED_TRACE_SUPPRESS:   state = COMMAND_SUPPRESS;   break;
        default:                    state = "unknown";          break;
#endif
        }
        if (PL_unify_atom(pl_args + 1, PL_new_atom(state)))
            PL_succeed;
        else
            PL_fail;
    }
        
    PL_fail;
}


/********************
 * libpl_trace_config
 ********************/
foreign_t
libpl_trace_config(term_t pl_args, int arity, void *context)
{
    static atom_t  call = 0, redo = 0, proven = 0, failed = 0;
    atom_t         pl_port;
    pred_trace_t  *pt;
    char          *pred, *port, *format;
    int            type;

    (void)context;

    if (arity != 3)
        PL_fail;
    
    if (!PL_get_chars(pl_args, &pred, CVT_WRITE|BUF_DISCARDABLE))
        PL_fail;
    
    if ((pt = predicate_trace_get(pred)) == NULL &&
        (pt = predicate_trace_get(PRED_DEFAULT)) == NULL) {
        if (PL_unify_atom(pl_args + 2, PL_new_atom(COMMAND_SHORT)))
            PL_succeed;
        else
            PL_fail;
    }
    
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
    case PRED_PORT_SUPPRESS: format = COMMAND_SUPPRESS; break;
    case PRED_PORT_DETAILED: format = COMMAND_DETAILED; break;
    case PRED_PORT_SHORT:    format = COMMAND_SHORT;    break;
    default:                 format = COMMAND_SHORT;    break;
    }
    
    if (PL_unify_atom(pl_args + 2, PL_new_atom(format)))
        PL_succeed;
    else
        PL_fail;
}





/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

