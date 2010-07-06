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
#include <string.h>
#include <errno.h>

#include <SWI-Stream.h>
#include <SWI-Prolog.h>

#include <prolog/prolog.h>

#include "libprolog.h"



/********************
 * Sread_shell
 ********************/
static ssize_t
Sread_shell(void *handle, char *buf, size_t bufsize)
{
    int fd = (int)handle;
    int n;


    /*
     * read in the next line of input
     *
     * Notes: This is vastly oversimplified based on the assumption that
     *        we are reading from a line-oriented medium (console or tty).
     *        Hence we only convert the last (assumed to be the only)
     *        CRLF to LF, check for a few special cases (quit, ';') and
     *        otherwise return the input read.
     */

    if ((n = read(fd, buf, bufsize)) > 0) {
        if (n < (int)bufsize)
            buf[n] = '\0';

        if (n >= 2 && buf[n-1] == '\n' && buf[n-2] == '\r') {
            buf[n-2] = '\n';
            buf[--n] = '\0';
        }
        
        if (!strcmp(buf, SHELL_QUIT_COMMAND"\n")) {
            *buf = '\0';
            n    = 0;
        }
    }
    
    /* disgusting kludge to get looping through multiple solutions working */
    if (n == 2 && buf[0] == SHELL_MORE_COMMAND && buf[1] == '\n') {
        buf[n] = '\0';
        n = 1;
    }

    return (ssize_t)n;
}


/********************
 * Sclose_shell
 ********************/
static int
Sclose_shell(void *handle)
{
    (void)handle;

    return 0;
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
PROLOG_API int
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


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

