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
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sched.h>

#include <glib.h>
#include <gmodule.h>

#include <prolog/prolog.h>

#include <ohm/ohm-plugin.h>
#include <ohm/ohm-plugin-debug.h>
#include <ohm/ohm-plugin-log.h>

#define PLUGIN_NAME    "rule_engine"
#define PLUGIN_VERSION "0.0.2"

#define DEFAULT_STACK  16                            /* 16k stacks */
#define NO_RULE        (-1)                          /* unknown rule */
#define ALL_RULES      "all"
#define MAX_NAME       64

#define PRIO_BOOST() do { if (prio_boost) prio_boost(); } while (0)
#define PRIO_RELAX() do { if (prio_relax) prio_relax(); } while (0)

static int    discover_predicates(void);
static void   free_predicates(void);

static char **get_extensions(const char *param);
static char **get_rules     (const char *param);
static int    get_stack     (const char *param);
static int    setup         (char **extensions, char **files, int stack);

static prolog_predicate_t *predicates;
static int                 npredicate; 
static int                 busy;

/* debug flags */
static int DBG_RULE;

OHM_DEBUG_PLUGIN(rule_engine,
     OHM_DEBUG_FLAG("rules", "rule processing", &DBG_RULE));

OHM_IMPORTABLE(int, prio_boost, (void));
OHM_IMPORTABLE(int, prio_relax, (void));


/*****************************************************************************
 *                       *** initialization & cleanup ***                    *
 *****************************************************************************/

/********************
 * logger
 ********************/
static void
logger(prolog_log_level_t level, const char *format, va_list ap)
{
    OhmLogLevel l;

    switch (level) {
    case PROLOG_LOG_FATAL:    
    case PROLOG_LOG_ERROR:   l = OHM_LOG_ERROR;   break;
    case PROLOG_LOG_WARNING: l = OHM_LOG_WARNING; break;
    case PROLOG_LOG_NOTICE:
    case PROLOG_LOG_INFO:    l = OHM_LOG_INFO;    break;
    default:                                    return;
    }

    ohm_logv(l, format, ap);
}


/**
 * plugin_init:
 **/
static void
plugin_init(OhmPlugin *plugin)
{
    const char *param_extensions = ohm_plugin_get_param(plugin, "extensions");
    const char *param_rules      = ohm_plugin_get_param(plugin, "rules");
    const char *param_stack      = ohm_plugin_get_param(plugin, "stacksize");
    const char *param_priorize   = ohm_plugin_get_param(plugin, "priorize");

    char **extensions;
    char **rules;
    int    stack;
    char *boost_sig, *relax_sig;
    void *boostptr, *relaxptr;
    
    prolog_set_logger(logger);
    
    if (!OHM_DEBUG_INIT(rule_engine))
        OHM_WARNING("rule engine failed to initialize debugging");
    
    
    /*
     * get configuration and set ourselves up
     */
    
    extensions = get_extensions(param_extensions);
    rules      = get_rules(param_rules);
    stack      = get_stack(param_stack);
    
    if (rules != NULL)
        if (setup(extensions, rules, stack) != 0)
            exit(1);
    
    
    /*
     * see if someone provides scheduling priority control methods
     */

    if (param_priorize == NULL ||
        (strcasecmp(param_priorize, "yes") && strcasecmp(param_priorize, "on")))
        return;

    
    boost_sig = (char *)prio_boost_SIGNATURE;
    relax_sig = (char *)prio_relax_SIGNATURE;
    ohm_module_find_method("prio_boost", &boost_sig, &boostptr);
    ohm_module_find_method("prio_relax", &boost_sig, &relaxptr);
    prio_boost = (int (*)(void))boostptr;
    prio_relax = (int (*)(void))relaxptr;
    
    if (prio_boost || prio_relax) {
        if (prio_boost == NULL || prio_relax == NULL) {
            OHM_WARNING("Missing priority boost/relax method, disabling.");
            prio_boost = NULL;
            prio_relax = NULL;
        }
        else
            OHM_INFO("rule-engine: priority control available.");
    }
    else
        OHM_INFO("rule-engine: no priority control.");
}


/**
 * plugin_exit:
 **/
static void
plugin_exit(OhmPlugin *plugin)
{
    free_predicates();
    prolog_exit();

    (void)plugin;
}


/*****************************************************************************
 *                           *** exported methods ***                        *
 *****************************************************************************/

/********************
 * setup_rules
 ********************/
OHM_EXPORTABLE(int, setup_rules, (char **extensions, char **files))
{
    return setup(extensions, files, DEFAULT_STACK);
}


/********************
 * find_rule
 ********************/
OHM_EXPORTABLE(int, find_rule, (char *name, int arity))
{
    prolog_predicate_t *p;
    int                 i;

    if (discover_predicates() != 0)
        return NO_RULE;
    
    for (i = 0, p = predicates; p->name != NULL; i++, p++)
        if ((arity < 0 || p->arity == arity) && !strcmp(p->name, name)) {
            OHM_DEBUG(DBG_RULE, "rule %s/%d: #%d", name, arity, i);
            return i;
        }
    
    OHM_DEBUG(DBG_RULE, "could not find rule %s/%d", name, arity);

    return NO_RULE;
}


/********************
 * eval_rule
 ********************/
OHM_EXPORTABLE(int, eval_rule, (int rule, void *retval, void **args, int narg))
{
    prolog_predicate_t *p;
    int                 status;

    if (rule < 0 || rule >= npredicate) {
        OHM_ERROR("rule-engine: cannot evaluate non-existing rule #%d", rule);
        return ENOENT;
    }
    
    p = predicates + rule;

    OHM_DEBUG(DBG_RULE, "invoking rule #%d (%s/%d)", rule, p->name, p->arity);
    
    if (prio_boost)
        prio_boost();
    status = prolog_acall(p, retval, args, narg);
    if (prio_relax)
        prio_relax();

    return status;
}


/********************
 * free_result
 ********************/
OHM_EXPORTABLE(void, free_result, (void *retval))
{
    char ***results = (char ***)retval;

    prolog_free_results(results);
}


/********************
 * dump_result
 ********************/
OHM_EXPORTABLE(void, dump_result, (void *retval))
{
    char ***results = (char ***)retval;

    prolog_dump_results(results);
}


/********************
 * shell
 ********************/
OHM_EXPORTABLE(void, prompt, (void))
{
    prolog_shell(fileno(stdin));
}


/********************
 * trace
 ********************/
OHM_EXPORTABLE(int, trace, (char *command))
{
    return prolog_trace_set(command);
}


/********************
 * statistics
 ********************/
OHM_EXPORTABLE(void, statistics, (char *command))
{
    prolog_predicate_t *pred;
    char                name[MAX_NAME], *slash;
    int                 arity, n, i;
    size_t              size;
    double              sys, usr, avg, total;

    if (command == NULL || !command[0] || !strcmp(command, ALL_RULES)) {
        total = 0.0;
        for (pred = predicates; pred->name; pred++) {
            prolog_statistics(pred, &n, &sys, &usr, &avg);
            OHM_INFO("%s/%d: %d calls, %.3f ms avg, %.2f total (%.2fu, %.2fs)",
                     pred->name, pred->arity, n, avg, usr+sys, usr, sys);
            total += usr+sys;
        }
        OHM_INFO("grand total: %.2f ms", total);
    }
    else {
        if ((slash = strchr(command, '/')) != NULL) {
            if ((size = (int)(slash - command)) > (sizeof(name) - 1))
                size = sizeof(name) - 1;
            strncpy(name, command, size);
            name[size] = '\0';
            arity = strtoul(slash + 1, NULL, 10);
        }
        else {
            size = sizeof(name) - 1;
            strncpy(name, command, size);
            name[size] = '\0';
            arity = -1;
        }
        if ((i = find_rule(name, arity)) == NO_RULE)
            OHM_INFO("Predicate %s/%d does not exist.", name, arity);
        else {
            pred = predicates + i;
            prolog_statistics(pred, &n, NULL, NULL, &avg);
            OHM_INFO("%s/%d: %d calls, average speed: %.3f msec/call",
                     pred->name, pred->arity, n, avg);
        }
    }
}


/********************
 * setup
 ********************/
static int
setup(char **extensions, char **files, int stack)
{
    int   i;
    char *p, *boot, path[PATH_MAX];

    if (busy)
        return EBUSY;
    
    boot = NULL;
    if (files != NULL) {
        if ((p = strrchr(files[0], '.')) != NULL) {
            if (!strcmp(p + 1, "plc")) {
                boot = files[0];
                files++;
            }
        }
        else {
            struct stat st;
            
            path[sizeof(path) - 1] = '\0';
            snprintf(path, sizeof(path) - 1, "%s.plc", files[0]);
            if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
                OHM_INFO("rule-engine: using precompiled rules %s...", path);
                boot = path;
                files++;
            }
        }
    }

    if (prolog_init(PLUGIN_NAME, stack,stack,stack,stack, boot) != 0) {
        OHM_ERROR("%s: failed to initialize prolog library", __FUNCTION__);
        exit(1);
    }

    busy = TRUE;
    
    for (p = extensions[i=0]; p != NULL; p = extensions[++i]) {
        OHM_INFO("rule-engine: loading extension %s...", p);
        if (!prolog_load_extension(p)) {
            OHM_ERROR("%s: failed to load extension \"%s\"", __FUNCTION__, p);
            exit(1);
        }
    }

    for (p = files[i=0]; p != NULL; p = files[++i]) {
        OHM_INFO("rule-engine: loading rule file %s...", p);
        if (!prolog_load_file(p)) {
            OHM_ERROR("%s: failed to load file \"%s\"", __FUNCTION__, p);
            exit(1);
        }
    }

    return discover_predicates();
}


/*****************************************************************************
 *                        *** miscallaneous routines ***                     *
 *****************************************************************************/

/********************
 * discover_predicates
 ********************/
static int
discover_predicates(void)
{
    prolog_predicate_t *undefined, *p;
    int                 err;
    
    if (predicates != NULL)
        return 0;
    
    if (!busy) {                 /* avoid doing anything if we're not set up */
        OHM_ERROR("rule-engine: not set up, cannot discover rules");
        return EAGAIN;
    }
    
    if ((err = prolog_rules(&predicates, &undefined)) != 0) {
        OHM_ERROR("rule-engine: failed to find exported rules");
        return err;
    }
    
    for (p = predicates; p->name != NULL; p++) {
        OHM_INFO("rule-engine: exported rule %s:%s/%d",
                 p->module, p->name,p->arity);
        npredicate++;
    }

    if (undefined) {
        for (p = undefined; p->name != NULL; p++)
            OHM_ERROR("rule-engine: undefined rule %s:%s/%d",
                     p->module, p->name,p->arity);
        err = ENOENT;
        prolog_free_predicates(undefined);
        undefined = NULL;
    }

    if (err != 0)
        exit(err);
    
    return 0;
}


/********************
 * free_predicates
 ********************/
static void
free_predicates(void)
{
    if (predicates) {
        prolog_free_predicates(predicates);
        predicates = NULL;
        npredicate = 0;
    }
}


/********************
 * get_extensions
 ********************/
static char **
get_extensions(const char *param)
{
    static char *extensions[2];

    if (param == NULL) {
        OHM_INFO("rule-engine: no extensions configured");
        return NULL;
    }

    OHM_INFO("rule-engine: using prolog extensions: %s", param);
    

    /* XXX TODO
     * implement parsing param to potentially multiple extensions
     */

    extensions[0] = (char *)param;
    extensions[1] = NULL;
    return &extensions[0];
}


/********************
 * get_rules
 ********************/
static char **
get_rules(const char *param)
{
    static char *rules[2];

    if (param == NULL) {
        OHM_INFO("rule-engine: no rules configured");
        return NULL;
    }

    /* XXX TODO
     * implement parsing param to potentially multiple rule files
     */

    OHM_INFO("rule-engine: using prolog rules: %s", param);

    rules[0] = (char *)param;
    rules[1] = NULL;
    return &rules[0];
}


/********************
 * get_stack
 ********************/
static int
get_stack(const char *param)
{
    int   size;
    char *end;

    if (param == NULL || *param == '\0') {
        OHM_INFO("rule-engine: no stacksize configured");
        size = DEFAULT_STACK;
    }
    else {
        size = (int)strtol(param, &end, 10);
    
        if (end && *end != '\0') {
            OHM_ERROR("%s: invalid stack size '%s'", PLUGIN_NAME, param);
            exit(1);
        }
    
        if (!size)
            size = DEFAULT_STACK;
    }
    
    OHM_INFO("rule-engine: using stack size %dk", size);

    return size;
}


OHM_PLUGIN_DESCRIPTION(PLUGIN_NAME, PLUGIN_VERSION,
                       "krisztian.litkey@nokia.com",
                       OHM_LICENSE_NON_FREE,
                       plugin_init,
                       plugin_exit,
                       NULL);

OHM_PLUGIN_PROVIDES_METHODS(rule_engine, 8,
    OHM_EXPORT(setup_rules, "setup"),

    OHM_EXPORT(find_rule,   "find"),
    OHM_EXPORT(eval_rule  , "eval"),
    OHM_EXPORT(free_result, "free"),
    OHM_EXPORT(dump_result, "dump"),
    OHM_EXPORT(prompt     , "prompt"),
    OHM_EXPORT(trace      , "trace"),
    OHM_EXPORT(statistics , "statistics")
);

                            

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

