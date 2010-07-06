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


#ifndef __LIBPROLOG_H__
#define __LIBPROLOG_H__

/*
 * prolog glue path
 */

#define LIBPROLOG_SO "libprolog.so"

#ifndef LIBPROLOG_HELPER
#  define LIBPROLOG_HELPER "/usr/share/libprolog/libprolog.pl"
#endif



/*
 * prolog shell commands
 */

#define SHELL_QUIT_COMMAND "quit"
#define SHELL_MORE_COMMAND ';'


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
#define PRED_DEFAULT       "default"
#define WILDCARD_ANY       "*"
#define WILDCARD_ALL       "%"


/*
 * prolog query flags for normal and tracing evaluation
 */

#define NORMAL_QUERY_FLAGS (PL_Q_NORMAL | PL_Q_NODEBUG | PL_Q_CATCH_EXCEPTION)
#define TRACE_QUERY_FLAGS  (PL_Q_NORMAL | PL_Q_CATCH_EXCEPTION)


/*
 * predicates to export rules
 */

#define PRED_EXPORTED "exported"                      /* old interface */
#define PRED_RULES    "rules"                         /* new interface */


/*
 * prolog predicate result types
 */

enum {
    RESULT_UNKNOWN = 0,
    RESULT_OBJECTS,
    RESULT_EXCEPTION,
};



/*
 * macros for emitting messages from within the library
 */

#define PROLOG_ERROR(fmt, args...)   \
    prolog_log(PROLOG_LOG_ERROR, fmt"\n" , ## args)
#define PROLOG_WARNING(fmt, args...) \
    prolog_log(PROLOG_LOG_WARNING, fmt"\n" , ## args)
#define PROLOG_INFO(fmt, args...)    \
    prolog_log(PROLOG_LOG_INFO, fmt"\n" , ## args)



/*
 * macros for memory allocation
 */

#include "libprolog-allocator.h"


/*
 * visibility-hiding
 *
 * If we have visibility-hiding support hide global symbols by default and
 * export only the ones tagged with PROLOG_API.
 */

#if __GNUC__
#  if HAVE_VISIBILITY_SUPPORT
#    pragma GCC visibility push(hidden)
#    define PROLOG_API __attribute__ ((visibility("default")))
#  else
#    define PROLOG_API
#  endif
#else
#  define PROLOG_API
#endif



/*
 * prototypes
 */


/* prolog-lib.c */
int libprolog_initialized(void);


/* prolog-trace.c */
int  libprolog_trace_init(void);
void libprolog_trace_exit(void);
int  libprolog_tracing   (void);

foreign_t libpl_trace_pred  (term_t pl_args, int arity, void *context);
foreign_t libpl_trace_config(term_t pl_args, int arity, void *context);


/* prolog-loader.c */
int  libprolog_load_file(char *path, int extension);
void libprolog_clear_errors(void);
int  libprolog_has_errors(void);
void libprolog_mark_error(void);

foreign_t libpl_loading(term_t noargs, int arity, void *context);
foreign_t libpl_mark_error(term_t noargs, int arity, void *context);
foreign_t libpl_clear_errors(term_t noargs, int arity, void *context);
foreign_t libpl_has_errors(term_t noargs, int arity, void *context);

/* prolog-predicate.c */
void libprolog_free_predicates(void);

/* prolog-object.c */
int libprolog_collect_result(term_t pl_retval, void *retval);
int libprolog_collect_exception(qid_t qid, void *retval);


/* prolog-utils.c */
int    swi_list_length(term_t pl_list);
term_t swi_list_new(char **items, int n, term_t result);
term_t swi_list_prepend(term_t list, term_t item);
int    swi_list_walk(term_t list,
		     int (*callback)(term_t item, int i, void *data),
		     void *data);

int swi_set_trace(int state);


/* prolog-log.c */
void prolog_log(prolog_log_level_t level, const char *format, ...);



#endif /* __LIBPROLOG_H__ */
