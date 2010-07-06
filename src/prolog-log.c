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

#include "prolog/prolog.h"
#include "libprolog.h"

static void (*logger)(prolog_log_level_t level, const char *format, va_list ap);


/********************
 * prolog_log
 ********************/
void
prolog_log(prolog_log_level_t level, const char *format, ...)
{
    const char *prefix;
    FILE       *out;
    va_list     ap;

    va_start(ap, format);

    if (logger != NULL)
        logger(level, format, ap);
    else {
        switch (level) {
        case PROLOG_LOG_FATAL:   out = stderr; prefix = "FATAL";   break;
        case PROLOG_LOG_ERROR:   out = stderr; prefix = "ERROR";   break;
        case PROLOG_LOG_WARNING: out = stderr; prefix = "WARNING"; break;
        case PROLOG_LOG_NOTICE:  out = stdout; prefix = "NOTICE";  break;
        case PROLOG_LOG_INFO:    out = stdout; prefix = "INFO";    break;
        default:                                                   return;
        }

        /*
         * Hmm... if we'd care about threaded apps maybe we should prepare the
         * message in a buffer then write(2) it to fileno(out) for atomicity.
         */

        fprintf(out , "[%s] ", prefix);
        vfprintf(out, format , ap);
        fprintf(out, "\n");
    }
    
    va_end(ap);
}


/********************
 * prolog_set_logger
 ********************/
PROLOG_API void
prolog_set_logger(void (*app_logger)(prolog_log_level_t, const char *, va_list))
{
    logger = (void (*)(prolog_log_level_t, const char *, va_list))app_logger;
}







/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

