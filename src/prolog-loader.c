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
#include <errno.h>

#include <SWI-Stream.h>
#include <SWI-Prolog.h>

#include <prolog/prolog.h>

#include "libprolog.h"


static int libprolog_loading;      /* non-zero if we're loading a file */
static int libprolog_errors;       /* number of prolog errors encountered */



/*****************************************************************************
 *                      *** loading and error tracking ***                   *
 *****************************************************************************/

void
libprolog_clear_errors(void)
{
    libprolog_errors = 0;
}


int
libprolog_has_errors(void)
{
    return libprolog_errors;
}


void
libprolog_mark_error(void)
{
    libprolog_errors++;
}


void
libprolog_load_start(void)
{
    libprolog_loading++;
}


void
libprolog_load_done(void)
{
    libprolog_loading--;
}



/*****************************************************************************
 *                    *** ruleset & extension loading ***                    *
 *****************************************************************************/

/*************************
 * libprolog_load_file
 *************************/
int
libprolog_load_file(char *path, int extension)
{
    char        *loader = extension ? "load_foreign_library" : "consult";
    predicate_t  pr_loader;
    fid_t        frame;
    qid_t        qid;
    term_t       pl_path;
    int          success;


    /*
     * load the given file (native prolog or foreign library)
     *
     * Notes: 
     *     The prolog predicate consult/1 does not seem to fail or raise an
     *     exception upon errors. It merely produces an error message and
     *     tries to continue or gives up processing the input file. In either
     *     case it succeeds (ie. the goal consult(path) is always proven in
     *     the prolog sense).
     *
     *     This default behaviour is not acceptable for us. As a library we
     *     want to let our caller know whether loading was successful or not.
     *     Otherwise it would be impossible to write even remotely reliable
     *     applications using this library.
     *
     *     To detect errors we have special prolog glue code that hooks into
     *     SWI Prologs user:message_hook and lets us know about errors
     *     (libprolog:mark_error) if loading is active (libprolog:loading).
     *     Currently the glue code prints an error message but it would be
     *     fairly easy to collect the errors here and let our caller print
     *     them if needed. For the time being this glue code lives in policy.pl
     *     but will eventually be separated out (to libprolog.pl ?).
     */


    libprolog_clear_errors();
    libprolog_load_start();
    
    frame = PL_open_foreign_frame();
    
    pr_loader = PL_predicate(loader, 1, NULL);
    pl_path   = PL_new_term_ref();
    PL_put_atom_chars(pl_path, path);
    
    qid     = PL_open_query(NULL, NORMAL_QUERY_FLAGS, pr_loader, pl_path);
    success = PL_next_solution(qid);
    if (PL_exception(qid)) {
#if 0
        char **exception = collect_exception(qid, &exception);
        libprolog_dump_exception(exception);
#endif
        success = FALSE;
    }
    PL_close_query(qid);

    PL_discard_foreign_frame(frame);
    
    libprolog_load_done();
    
    if (libprolog_has_errors())
        return FALSE;
    else
        return success;
}


/*************************
 * prolog_load_file
 *************************/
PROLOG_API int
prolog_load_file(char *path)
{
    if (!libprolog_initialized())
        return FALSE;
    else
        return libprolog_load_file(path, FALSE);
}


/*************************
 * prolog_load_extension
 *************************/
PROLOG_API int
prolog_load_extension(char *path)
{
    if (!libprolog_initialized())
        return FALSE;
    else
        return libprolog_load_file(path, TRUE);
}


/*****************************************************************************
 *                         *** foreign predicates ***                        *
 *****************************************************************************/


/********************
 * libpl_loading
 ********************/
foreign_t
libpl_loading(term_t noargs, int arity, void *context)
{
    (void)noargs;
    (void)arity;
    (void)context;

    if (libprolog_loading > 0)
        PL_succeed;
    else
        PL_fail;
}


/********************
 * libpl_mark_error
 ********************/
foreign_t
libpl_mark_error(term_t noargs, int arity, void *context)
{
    (void)noargs;
    (void)arity;
    (void)context;

    libprolog_mark_error();
    PL_succeed;
}


/********************
 * libpl_clear_errors
 ********************/
foreign_t
libpl_clear_errors(term_t noargs, int arity, void *context)
{
    (void)noargs;
    (void)arity;
    (void)context;
    libprolog_clear_errors();
    PL_succeed;
}


/********************
 * libpl_has_errors
 ********************/
foreign_t
libpl_has_errors(term_t noargs, int arity, void *context)
{
    (void)noargs;
    (void)arity;
    (void)context;

    if (libprolog_has_errors())
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

