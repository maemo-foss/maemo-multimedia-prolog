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
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <SWI-Stream.h>
#include <SWI-Prolog.h>

#include <prolog/prolog.h>

#include "libprolog.h"

#ifndef PATH_MAX
#  define PATH_MAX 256
#endif

static char libprolog_pl[PATH_MAX] = LIBPROLOG_HELPER;
static char libprolog_so[PATH_MAX];

static int  initialized = FALSE;
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



static char *shlib_path(char *lib, char *buf, size_t size);
static int   register_predicates (void);

prolog_allocator_t __allocator = { NULL, NULL, NULL };


/*****************************************************************************
 *                      *** initialization & cleanup ***                     *
 *****************************************************************************/

/*************************
 * prolog_init
 *************************/
PROLOG_API int
prolog_init(char *argv0,
            int lsize, int gsize, int tsize, int asize, char *bootfile)
{
    char **argv;
    int    argc, status;

    (void)argv0;

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
    
    argv[argc=0] = shlib_path(LIBPROLOG_SO, libprolog_so, sizeof(libprolog_so));
    
    if (bootfile != NULL) {
        pl_argv[++argc] = "-x";               /* must be argv[1] */
        pl_argv[++argc] = bootfile;           /*     and argv[2] */
    }
    
    argc += NUM_FIXED_ARGS;
    pl_argv[++argc] = lstack;
    pl_argv[++argc] = gstack;
    pl_argv[++argc] = tstack;
    pl_argv[++argc] = astack;

    libprolog_clear_errors();

    if ((status = libprolog_trace_init()) != 0)
        return status;
    
    if ((status = register_predicates()) != 0)
        return status;
    
    if (!PL_initialise(argc + 1, argv)) {
        PL_cleanup(0);
        return EINVAL;
    }

    /* libprolog.pl assumed to be part of boot file, otherwise load it */
    if (bootfile == NULL) {
        if (!libprolog_load_file(libprolog_pl, FALSE)) {
            PL_cleanup(0);
            return EINVAL;
        }
    }
    
    initialized = TRUE;
    return status;
}


/*************************
 * prolog_exit
 *************************/
PROLOG_API void
prolog_exit(void)
{
    if (!initialized)
        return;
    
    if (PL_is_initialised(NULL, NULL))
        PL_cleanup(0);
    
    libprolog_free_predicates();

    libprolog_trace_exit();
    initialized = FALSE;
}


/********************
 * prolog_set_helper
 ********************/
PROLOG_API int
prolog_set_helper(const char *path)
{
    struct stat st;

    if (initialized)
        return EBUSY;
    
    strncpy(libprolog_pl, path, sizeof(libprolog_pl));
    libprolog_pl[sizeof(libprolog_pl) - 1] = '\0';

    if (stat(libprolog_pl, &st) != 0)
        return errno;
    else
        return 0;
}


/********************
 * prolog_set_allocator
 ********************/
PROLOG_API int
prolog_set_allocator(prolog_allocator_t *allocator)
{
    if (initialized)
        return EBUSY;
    
    if ((__allocator.malloc && allocator->malloc != __allocator.malloc) ||
        (__allocator.realloc && allocator->realloc != __allocator.realloc) ||
        (__allocator.free && allocator->free != __allocator.free))
        return EINVAL;
    
    memcpy(&__allocator, allocator, sizeof(__allocator));
    return 0;
}


/********************
 * libprolog_initialized
 ********************/
int
libprolog_initialized(void)
{
    return initialized;
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
        { "loading"        , 0, libpl_loading     , NON_TRACEABLE, },
        { "mark_error"     , 0, libpl_mark_error  , NON_TRACEABLE, },
        { "clear_errors"   , 0, libpl_clear_errors, NON_TRACEABLE, },
        { "has_errors"     , 0, libpl_has_errors  , NON_TRACEABLE, },
        /* predicates for rule/predicate tracing */
        { "trace_predicate", 1, libpl_trace_pred  , NON_TRACEABLE, },
        { "trace_predicate", 2, libpl_trace_pred  , NON_TRACEABLE, },
        { "trace_config"   , 3, libpl_trace_config, NON_TRACEABLE, },
        
        { NULL, 0, NULL, 0 },
    };

    PL_register_extensions_in_module("libprolog", predicates);

    return 0;
}


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





/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
*/

