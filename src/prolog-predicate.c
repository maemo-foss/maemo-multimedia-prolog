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
#include <sys/resource.h>

#include <SWI-Stream.h>
#include <SWI-Prolog.h>

#include <prolog/prolog.h>

#include "libprolog.h"


static prolog_predicate_t *lib_predicates = NULL;
static prolog_predicate_t *lib_undefined  = NULL;


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
 * prolog_rules
 ********************/
PROLOG_API int
prolog_rules(prolog_predicate_t **rules, prolog_predicate_t **undef)
{
    predicate_t pr_rules = PL_predicate(PRED_RULES, 2, NULL);
    fid_t       frame;
    term_t      pl_args;
    int         err, nrule, nundef;

    if (!libprolog_initialized())
        return ENOMEDIUM;

    if (rules == NULL || undef == NULL)
        return EINVAL;


    /*
     * Notes:
     *
     *     With the original prolog_{rules,*call,statistics,free_predicates}
     *     semantics it was not possible to discover predicates more than once
     *     and get reasonable statistics. Statistics were collected on a per
     *     prolog_predicate_t array basis. This made it difficult to query
     *     statistics without running the predicate evaluation. To work around
     *     this limitation without breaking the API we now cache the result
     *     of predicate lookup internally. As a side-effect of this freeing
     *     the predicates happens now also internally during library cleanup.
     *     Hence prolog_free_predicates is a no-op when called from outside
     *     the library.
     */


    /* return the cached result if we have any */
    if (lib_predicates != NULL || lib_undefined != NULL) {
        *rules = lib_predicates;
        *undef = lib_undefined;

        return 0;
    }
    
    *rules = NULL;
    *undef = NULL;
    
    frame   = PL_open_foreign_frame();
    pl_args = PL_new_term_refs(2);

    if (!PL_call_predicate(NULL, NORMAL_QUERY_FLAGS, pr_rules, pl_args))
        return ENOENT;
    
    if ((nrule = swi_list_length(pl_args)) <= 0)
        return ENOENT;

    if ((nundef = swi_list_length(pl_args + 1)) < 0)
        return EINVAL;
    
    if ((*rules = ALLOC_ARRAY(prolog_predicate_t, nrule + 1)) == NULL) {
        err = ENOMEM;
        goto fail;
    }
 
    if ((err = swi_list_walk(pl_args, collect_exported, *rules)) != 0)
        goto fail;

    if (nundef > 0) {
        if ((*undef = ALLOC_ARRAY(prolog_predicate_t, nundef + 1)) == NULL) {
            err = ENOMEM;
            goto fail;
        }
        if ((err = swi_list_walk(pl_args+1, collect_exported, *undef)) != 0)
            goto fail;
    }
    
    PL_discard_foreign_frame(frame);


    /* cache the result of the lookup internally */
    if (lib_predicates == NULL)
        lib_predicates = *rules;
    if (lib_undefined == NULL)
        lib_undefined = *undef;
    
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
PROLOG_API void
prolog_free_predicates(prolog_predicate_t *predicates)
{
    prolog_predicate_t *p;
    
    if (predicates == NULL)
        return;
    
    /* don't free cached results */
    if (predicates == lib_predicates || predicates == lib_undefined)
        return;

    for (p = predicates; p->name != NULL; p++) {
        FREE(p->module);
        FREE(p->name);
    }

    FREE(predicates);
}


/********************
 * libprolog_free_predicates
 ********************/
void
libprolog_free_predicates(void)
{
    prolog_predicate_t *predicates = lib_predicates;
    prolog_predicate_t *undefined  = lib_undefined;

    lib_predicates = NULL;
    lib_undefined  = NULL;
    
    prolog_free_predicates(predicates);
    prolog_free_predicates(undefined);
}


/********************
 * prolog_statistics
 ********************/
PROLOG_API int
prolog_statistics(prolog_predicate_t *pred,
                  int *invocations, double *sys, double *usr, double *avg)
{
    if (invocations != NULL)
        *invocations = pred->calls;
    
    if (sys != NULL)
        *sys = pred->sys.tv_sec * 1000.0 + pred->sys.tv_usec / 1000.0;
    
    if (usr != NULL)
        *usr = pred->usr.tv_sec * 1000.0 + pred->usr.tv_usec / 1000.0;
    
    if (avg != NULL) {
        if (!pred->calls)
            *avg = 0.0;
        else {
            *avg  = (pred->sys.tv_sec  + pred->usr.tv_sec)  * 1000.0;
            *avg += (pred->sys.tv_usec + pred->usr.tv_usec) / 1000.0;
            *avg /= pred->calls;
        }
    }
    
    return 0;
}


static inline struct timeval *
timeval_sub(struct timeval *a, struct timeval *b, struct timeval *diff)
{
    diff->tv_sec = a->tv_sec - b->tv_sec;
    if (a->tv_usec < b->tv_usec) {
        diff->tv_sec--;
        diff->tv_usec = 1000000 - b->tv_usec + a->tv_usec;
    }
    else
        diff->tv_usec = a->tv_usec - b->tv_usec;

    return diff;
}


static inline struct timeval *
timeval_add(struct timeval *a, struct timeval *b, struct timeval *sum)
{
    sum->tv_sec  = a->tv_sec  + b->tv_sec;
    sum->tv_usec = a->tv_usec + b->tv_usec;
    
    if (sum->tv_usec >= 1000000) {
        sum->tv_sec++;
        sum->tv_usec -= 1000000;
    }
    
    return sum;
}


static int
eval_predicate(int flags, prolog_predicate_t *pred, void *retval, term_t args)
{
    struct rusage start, diff;
    qid_t         qid;
    term_t        pl_retval = args + pred->arity - 1;
    int           status;

    qid    = PL_open_query(NULL, flags, pred->predicate, args);
    getrusage(RUSAGE_SELF, &start);
    status = PL_next_solution(qid);
    getrusage(RUSAGE_SELF, &diff);

    if (!status)
        status = libprolog_collect_exception(qid, retval);
    else
        status = libprolog_collect_result(pl_retval, retval);
    PL_close_query(qid);

    if (status > 0) {
        timeval_sub(&diff.ru_utime, &start.ru_utime, &diff.ru_utime);
        timeval_sub(&diff.ru_stime, &start.ru_stime, &diff.ru_stime);
        timeval_add(&pred->usr, &diff.ru_utime, &pred->usr);
        timeval_add(&pred->sys, &diff.ru_stime, &pred->sys);
        pred->calls++;
    }

    return status;
}



/********************
 * prolog_acall
 ********************/
PROLOG_API int
prolog_acall(prolog_predicate_t *pred, void *retval, void **args, int narg)
{
    fid_t   frame;
    term_t  pl_args;
    int     i, a, type, flags, status;
    
    if (narg < pred->arity - 1)
        return FALSE;
    else if (narg > pred->arity - 1) {
        PROLOG_WARNING("%s: ignoring extra %d parameter%s to %s",
                       __FUNCTION__,
                       narg-pred->arity-1, narg-pred->arity-1 > 1 ? "s" : "",
                       pred->name);
    }
    
    frame   = PL_open_foreign_frame();
    pl_args = PL_new_term_refs(pred->arity);

    for (i = 0, a = 0; i < pred->arity - 1; i++) {
        type  = (int)args[a++];
        switch (type) {
        case 's': PL_put_atom_chars(pl_args + i, (char *)args[a++]); break;
        case 'i': PL_put_integer(pl_args + i, (int)args[a++]);       break;
        case 'd': PL_put_float(pl_args + i, *(double *)args[a++]);   break;
        default:
            PROLOG_ERROR("%s: invalid prolog argument type 0x%x",
                         __FUNCTION__, type);
            status = -EINVAL;
            goto out;
        }
    }

    if (libprolog_tracing()) {
        swi_set_trace(TRUE);
	flags = TRACE_QUERY_FLAGS;
    }
    else
        flags = NORMAL_QUERY_FLAGS;

    status = eval_predicate(flags, pred, retval, pl_args);

    if (libprolog_tracing())
        swi_set_trace(FALSE);

 out:
    PL_discard_foreign_frame(frame);
    
    return status;
} 


/********************
 * prolog_call
 ********************/
PROLOG_API int
prolog_call(prolog_predicate_t *pred, void *retval, ...)
{
    va_list  ap;
    fid_t    frame;
    term_t   pl_args;
    char    *arg;
    int      i, status;

    
    frame   = PL_open_foreign_frame();
    pl_args = PL_new_term_refs(pred->arity);

    /*
     * XXX TODO: add argument type processing similar to prolog_acall
     */

    va_start(ap, retval);
    for (i = 0; i < pred->arity - 1; i++) {
        arg = va_arg(ap, char *);
        PL_put_atom_chars(pl_args + i, arg);
    }
    va_end(ap);

    status = eval_predicate(NORMAL_QUERY_FLAGS, pred, retval, pl_args);

    PL_discard_foreign_frame(frame);
    
    return status;
} 


/********************
 * prolog_vcall
 ********************/
PROLOG_API int
prolog_vcall(prolog_predicate_t *pred, void *retval, va_list ap)
{
    fid_t   frame;
    term_t  pl_args;
    char   *arg;
    int     i, status;

    
    frame   = PL_open_foreign_frame();
    pl_args = PL_new_term_refs(pred->arity);

    /*
     * XXX TODO: add argument type processing similar to prolog_acall
     */

    for (i = 0; i < pred->arity - 1; i++) {
        arg = va_arg(ap, char *);
        PL_put_atom_chars(pl_args + i, arg);
    }

    status = eval_predicate(NORMAL_QUERY_FLAGS, pred, retval, pl_args);

    PL_discard_foreign_frame(frame);
    
    return status;
} 




/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

