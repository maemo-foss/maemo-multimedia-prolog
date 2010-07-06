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
#include <prolog/prolog.h>
#include <check.h>

#include "check-libprolog.h"


static SRunner *srunner;

static Suite *
chkprolog_suite(void)
{
    Suite *suite;

    suite = suite_create("libprolog");

    chkprolog_init_tests(suite);
    chkprolog_pred_tests(suite);
    chkprolog_leak_tests(suite);
    
    return suite;
}



SRunner *
chkprolog_srunner(void)
{
    return srunner;
}


int
chkprolog_fork_status(void)
{
    if (srunner)
        return srunner_fork_status(srunner);
    else
        return -1;
}


int
main(int argc, char *argv[])
{
    Suite *suite;
    int    nfailed;

    (void)argc;
    (void)argv;

    if (prolog_set_helper("../src/libprolog.pl") != 0 &&
        prolog_set_helper("../../src/libprolog.pl") != 0) {
        fprintf(stderr, "Unable to find .../src/libprolog.pl");
        exit(1);
    }
    
    suite   = chkprolog_suite();
    srunner = srunner_create(suite);
    
    srunner_run_all(srunner, CK_ENV);
    nfailed = srunner_ntests_failed(srunner);

    srunner_free(srunner);
    
    return nfailed == 0 ? 0 : 1;
}



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


