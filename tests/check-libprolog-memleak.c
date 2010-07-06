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
#include <stdlib.h>
#include <string.h>
#include <prolog/prolog.h>
#include <check.h>

#include "check-libprolog.h"


#include "check-libprolog.h"

#define PL_PREDTEST_FILE "./predtest.pl"


static prolog_predicate_t *predicates;
static prolog_predicate_t *undefined;


static void *
catch_malloc(size_t size, const char *file, int line, const char *func)
{
    void *caller = __builtin_return_address(0);

    printf("malloc %u bytes at %s@%s:%d (%p)\n", size, func, file, line,
           caller);
    return malloc(size);
}


static void *
catch_realloc(void *ptr, size_t size,
              const char *file, int line, const char *func)
{
    void *caller = __builtin_return_address(0);
    
    printf("realloc %p to %u bytes at %s@%s:%d (%p)\n", ptr, size,
           func, file, line, caller);
    return realloc(ptr, size);
}


static void
catch_free(void *ptr, const char *file, int line, const char *func)
{
    void *caller = __builtin_return_address(0);
    
    printf("free %p at %s@%s:%d (%p)\n", ptr, func, file, line, caller);
    free(ptr);
}


static void
setup(void)
{
    prolog_allocator_t allocator = {
        .malloc  = catch_malloc,
        .realloc = catch_realloc,
        .free    = catch_free
    };

    if (prolog_set_allocator(&allocator) != 0) {
        fprintf(stderr, "Unable to set custom libprolog memory allocator.\n");
        exit(1);
    }

    fail_unless(prolog_init("check-libprolog", 0, 0, 0, 0, NULL) == 0);
    fail_unless(prolog_load_file(PL_PREDTEST_FILE));
    fail_unless(prolog_rules(&predicates, &undefined) == 0);
}


static void
teardown(void)
{
    prolog_free_predicates(predicates);
    prolog_free_predicates(undefined);
}


static prolog_predicate_t *
find_predicate(prolog_predicate_t *predlist,
               const char *module, const char *name, int arity)
{
    prolog_predicate_t *p;

    if (predlist == NULL)
        return NULL;
    
    for (p = predlist; p->name != NULL; p++) {
        if (p->arity == arity &&
            (module == NULL || !strcmp(module, p->module)) &&
            !strcmp(p->name, name))
            return p;
    }
    
    return NULL;
}


START_TEST(defined_predicates)
{
    fail_unless(predicates != NULL);
    fail_unless(find_predicate(predicates,
                               "predicates", "success", 1) != NULL);
    fail_unless(find_predicate(predicates,
                               "predicates", "failure", 1) != NULL);
    fail_unless(find_predicate(predicates,
                               "predicates", "exception", 1) != NULL);
    fail_unless(find_predicate(predicates,
                               "predicates", "echo", 2) != NULL);
}
END_TEST


START_TEST(undefined_predicates)
{
    fail_unless(undefined != NULL);
    fail_unless(find_predicate(undefined,
                               "predicates", "undefined", 1) != NULL);
}
END_TEST


START_TEST(predicate_success)
{
    prolog_predicate_t   *pred;
    char               ***result;

    pred = find_predicate(predicates, "predicates", "success", 1);
    fail_unless(pred != NULL, "Failed to find predicates:success/1.");

    result = NULL;
    fail_unless(prolog_acall(pred, &result, NULL, 0) == TRUE);
    fail_unless(result != NULL);
    
    prolog_dump_results(result);
    prolog_free_results(result);
}
END_TEST


START_TEST(predicate_failure)
{
    prolog_predicate_t   *pred;
    char               ***result;

    pred = find_predicate(predicates, "predicates", "failure", 1);
    fail_unless(pred != NULL, "Failed to find predicates:failure/1.");

    result = NULL;
    fail_unless(prolog_acall(pred, &result, NULL, 0) == FALSE);
    fail_unless(result == NULL);
}
END_TEST


START_TEST(predicate_exception)
{
    prolog_predicate_t   *pred;
    char               ***result;

    pred = find_predicate(predicates, "predicates", "exception", 1);
    fail_unless(pred != NULL, "Failed to find predicates:exception/1.");

    result = NULL;
    fail_unless(prolog_acall(pred, &result, NULL, 0) < 0);
    fail_unless(result != NULL);

    prolog_dump_results(result);
    prolog_free_results(result);
}
END_TEST


START_TEST(integer_argument)
{
    prolog_predicate_t   *pred;
    void                 *args[] = { (void *)'i', (void *)3141 };
    int                   narg = 1;
    char               ***result;

    pred = find_predicate(predicates, "predicates", "echo", 2);
    fail_unless(pred != NULL, "Failed to find predicates:echo/2.");

    result = NULL;
    fail_unless(prolog_acall(pred, &result, args, narg) > 0);
    fail_unless(result != NULL);
    fail_unless(result[0] != NULL && 
                (int)result[0][4] == 'i' && (int)result[0][5] == 3141);
    
    prolog_dump_results(result);
    prolog_free_results(result);
}
END_TEST


START_TEST(double_argument)
{
    prolog_predicate_t   *pred;
    double                pi = 3.141;
    void                 *args[] = { (void *)'d', (void *)&pi };
    int                   narg = 1;
    char               ***result;

    pred = find_predicate(predicates, "predicates", "echo", 2);
    fail_unless(pred != NULL, "Failed to find predicates:echo/2.");

    result = NULL;
    fail_unless(prolog_acall(pred, &result, args, narg) > 0);
    fail_unless(result != NULL);
    fail_unless(result[0] != NULL && 
                (int)result[0][4] == 'd' &&
                *(double *)result[0][5] == pi);


    prolog_dump_results(result);
    prolog_free_results(result);
}
END_TEST


START_TEST(string_argument)
{
    prolog_predicate_t   *pred;
    void                 *args[] = { (void *)'s', (void *)"3.141" };
    int                   narg = 1;
    char               ***result;

    pred = find_predicate(predicates, "predicates", "echo", 2);
    fail_unless(pred != NULL, "Failed to find predicates:echo/2.");

    result = NULL;
    fail_unless(prolog_acall(pred, &result, args, narg) > 0);
    fail_unless(result != NULL);
    fail_unless(result[0] != NULL && 
                (int)result[0][4] == 's' &&
                !strcmp((char *)result[0][5], "3.141"));

    prolog_dump_results(result);
    prolog_free_results(result);
}
END_TEST





void
chkprolog_leak_tests(Suite *suite)
{
    TCase *tc;

    tc = tcase_create("memoryleak");
    tcase_add_checked_fixture(tc, setup, teardown);
    
    tcase_add_test(tc, defined_predicates);
    tcase_add_test(tc, undefined_predicates);
    tcase_add_test(tc, predicate_success);
    tcase_add_test(tc, predicate_failure);
    tcase_add_test(tc, predicate_exception);

    tcase_add_test(tc, integer_argument);
    tcase_add_test(tc, double_argument);
    tcase_add_test(tc, string_argument);

    suite_add_tcase(suite, tc);
}






/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


