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


#ifndef __PROLOG_H__
#define __PROLOG_H__

#include <sys/time.h>

#include <SWI-Stream.h>
#include <SWI-Prolog.h>


/*
 * prolog interpreter configuration
 */

typedef struct {
    char  *argv0;
    char  *ruleset;
    char **extensions;
    int    local_stack;
    int    global_stack;
    int    tail_stack;
    int    argument_stack;
} prolog_config_t;


/*
 * an 'exported' prolog predicate
 */

typedef struct {
    char           *module;                  /* module name */
    char           *name;                    /* exported predicate name */
    int             arity;                   /* exported predicate arity */
    predicate_t     predicate;               /* prolog predicate handle */
    /* runtime statistics */
    struct timeval  usr;                     /* time spent in user space */
    struct timeval  sys;                     /* time spent in kernel space */
    int             calls;                   /* number of invocations */
} prolog_predicate_t;


/*
 * overridable memory allocator entry points
 */

typedef struct {
    void *(*malloc)(size_t, const char *, int, const char *);
    void *(*realloc)(void *, size_t, const char *, int, const char *);
    void  (*free)(void *, const char *, int, const char *);
} prolog_allocator_t;


/*
 * logging
 */

typedef enum {
    PROLOG_LOG_FATAL   = 0,
    PROLOG_LOG_ERROR   = 1,
    PROLOG_LOG_WARNING = 2,
    PROLOG_LOG_NOTICE  = 3,
    PROLOG_LOG_INFO    = 4,
} prolog_log_level_t;


int  prolog_init(char *, int, int, int, int, char *);
void prolog_exit(void);
int  prolog_set_helper(const char *path);
int  prolog_set_allocator(prolog_allocator_t *allocator);

void prolog_set_logger(void (*app_logger)(prolog_log_level_t, const char *,
                                          va_list));

int  prolog_load_extension(char *path);
int  prolog_load_file     (char *path);

prolog_predicate_t *prolog_predicates(char *query);
prolog_predicate_t *prolog_undefined (void);

int prolog_rules     (prolog_predicate_t **rules, prolog_predicate_t **undef);
int prolog_statistics(prolog_predicate_t *pred,
                      int *invocations, double *sys, double *usr, double *avg);


int     prolog_call      (prolog_predicate_t *p, void *ret, ...);
int     prolog_acall     (prolog_predicate_t *p, void *retval,
                          void **args, int narg);
int     prolog_vcall     (prolog_predicate_t *p, void *ret,
                          va_list ap);
#define prolog_callarr prolog_acall

int     prolog_trace_set(char *commands);
void    prolog_trace_show(char *predicate);

void    prolog_free_predicates(prolog_predicate_t *predicates);

void prolog_free_results(char ***results);
void prolog_dump_results(char ***results);

void prolog_free_exception(char ***exception);
void prolog_dump_exception(char ***exception);

void prolog_free_objects(char ***objects);
void prolog_dump_objects(char ***objects);

int prolog_shell(int in);


#endif /* __PROLOG_H__ */


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
*/

