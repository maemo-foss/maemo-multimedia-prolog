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



#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <check.h>

#include <prolog/prolog.h>

#define PL_DUMMY_FILE "./.a-test-file.pl"
#define PL_ERROR_FILE "./syntax-error.pl"
#define PL_OK_FILE    "./syntax-ok.pl"


START_TEST(missing_init)
{
    fail_unless(!prolog_load_file("whatever.pl"),
                "prolog_load_file before prolog_init should return FALSE");
}
END_TEST


START_TEST(multiple_init)
{
    fail_unless(prolog_init("check-libprolog", 0, 0, 0, 0, NULL) == 0,
                "prolog_init failed");
    fail_unless(prolog_init("check-libprolog", 0, 0, 0, 0, NULL) != 0,
                "Calling prolog_init for the 2nd time should fail");
}
END_TEST


START_TEST(non_existing)
{
    fail_unless(prolog_init("check-libprolog", 0, 0, 0, 0, NULL) == 0,
                "prolog_init failed");
    unlink(PL_DUMMY_FILE);
    fail_unless(!prolog_load_file(PL_DUMMY_FILE),
                "prolog_load_file should fail for non-existing files");
}
END_TEST


START_TEST(non_readable)
{
    int fd;

    fail_unless(unlink(PL_DUMMY_FILE) == 0 || errno == ENOENT);
    fail_unless((fd = creat(PL_DUMMY_FILE, O_WRONLY)) >= 0);

    fail_unless(prolog_init("check-libprolog", 0, 0, 0, 0, NULL) == 0,
                "prolog_init failed");
    fail_unless(!prolog_load_file(PL_DUMMY_FILE),
                "prolog_load_file should fail for non-readable files");

    if (fd >= 0)                             /* make coverity happy */
        close(fd);
    unlink(PL_DUMMY_FILE);
}
END_TEST


START_TEST(syntax_error)
{
    fail_unless(prolog_init("check-libprolog", 0, 0, 0, 0, NULL) == 0,
                "prolog_init failed");
    fail_unless(!prolog_load_file(PL_ERROR_FILE),
                "prolog_load_file should fail for syntax errors");
}
END_TEST


START_TEST(syntax_ok)
{
    fail_unless(prolog_init("check-libprolog", 0, 0, 0, 0, NULL) == 0,
                "prolog_init failed");
    fail_unless(prolog_load_file(PL_OK_FILE),
                "prolog_load_file failed for %s", PL_OK_FILE);
}
END_TEST


void
chkprolog_init_tests(Suite *suite)
{
    TCase *tc;

    tc = tcase_create("initalization");
    tcase_add_test(tc, missing_init);
    tcase_add_test(tc, multiple_init);
    suite_add_tcase(suite, tc);

    tc = tcase_create("loading");
    tcase_add_test(tc, non_existing);
    tcase_add_test(tc, non_readable);
    tcase_add_test(tc, syntax_error);
    tcase_add_test(tc, syntax_ok);
    suite_add_tcase(suite, tc);    
}


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


