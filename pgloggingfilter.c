/*
 * Logging filters for PostgreSQL.
 *
 * Copyright (c) 2014, Oskari Saarenmaa <os@ohmu.fi>
 * All rights reserved.
 *
 * This file is under the Apache License, Version 2.0.
 * See the file `LICENSE` for details.
 *
 */

#include "postgres.h"
#include "utils/builtins.h"
#include "utils/guc.h"

PG_MODULE_MAGIC;

void _PG_init(void);
void _PG_fini(void);

/* generated perfect hash function for sqlstates */
#include "sqlstatehashfunc.c"

/* the actual hash table */
typedef struct SqlstateLogFilterStruct
{
  int loglevel;
  int stmt_loglevel;
} SqlstateLogFilter;

static SqlstateLogFilter filter_by_sqlstate_hash[HASH_SQLSTATE_MODULO];
static char *str_log_min_messages_by_sqlstate;
static char *str_log_min_error_statement_by_sqlstate;
static emit_log_hook_type prev_emit_log_hook;

static bool check_filter_by_sqlstate(char **newval, void **extra, GucSource source);
static void assign_filter_by_sqlstate_msgs(const char *newval, void *extra);
static void assign_filter_by_sqlstate_stmt(const char *newval, void *extra);
static void pglf_log_hook(ErrorData *edata);

/* Copied from src/backend/utils/misc/guc.c */
static const struct config_enum_entry server_message_level_options[] = {
  {"debug", DEBUG2, true},
  {"debug5", DEBUG5, false},
  {"debug4", DEBUG4, false},
  {"debug3", DEBUG3, false},
  {"debug2", DEBUG2, false},
  {"debug1", DEBUG1, false},
  {"info", INFO, false},
  {"notice", NOTICE, false},
  {"warning", WARNING, false},
  {"error", ERROR, false},
  {"log", LOG, false},
  {"fatal", FATAL, false},
  {"panic", PANIC, false},
  {NULL, 0, false}
};

void _PG_init(void)
{
  int i;
  for (i=0; i<HASH_SQLSTATE_MODULO; i++)
    {
      filter_by_sqlstate_hash[i].loglevel = -1;
      filter_by_sqlstate_hash[i].stmt_loglevel = -1;
    }
  DefineCustomStringVariable("pgloggingfilter.log_min_messages_by_sqlstate",
                             "XXX Comma-separated list of sqlstate log levels.",
                             "XXX",
                             &str_log_min_messages_by_sqlstate,
                             NULL,
                             PGC_SUSET,
                             GUC_LIST_INPUT,
                             check_filter_by_sqlstate,
                             assign_filter_by_sqlstate_msgs,
                             NULL);
  DefineCustomStringVariable("pgloggingfilter.log_min_error_statement_by_sqlstate",
                             "XXX Comma-separated list of sqlstate log levels.",
                             "XXX",
                             &str_log_min_error_statement_by_sqlstate,
                             NULL,
                             PGC_SUSET,
                             GUC_LIST_INPUT,
                             check_filter_by_sqlstate,
                             assign_filter_by_sqlstate_stmt,
                             NULL);
  prev_emit_log_hook = emit_log_hook;
  emit_log_hook = pglf_log_hook;
}

/*
 * This is called when we're being unloaded from a process. Note that this
 * only happens when we're being replaced by a LOAD (it doesn't happen on
 * process exit), so we can't depend on it being called.
 */
void _PG_fini(void)
{
  if (emit_log_hook == pglf_log_hook)
    {
      emit_log_hook = prev_emit_log_hook;
      prev_emit_log_hook = NULL;
    }
}

static void
pglf_log_hook(ErrorData *edata)
{
  SqlstateLogFilter *filter;

  if (!edata->output_to_server)
    return;  /* nothing we can do about this */
  filter = &filter_by_sqlstate_hash[hash_sqlstate(edata->sqlerrcode)];
  if (filter->loglevel != -1 && edata->elevel < filter->loglevel)
    edata->output_to_server = false;  /* suppress */
  if (filter->stmt_loglevel != -1 && edata->elevel < filter->stmt_loglevel)
    edata->hide_stmt = true;
}

/*
 * check_filter_by_sqlstate - validate sqlstate:errorlevel
 * lists and create a (perfect) hash table from them.
 */
static bool
check_filter_by_sqlstate(char **newval, void **extra, GucSource source)
{
  char *rawstring;
  List *elemlist;
  ListCell *l;
  int i;
  int *new_table;
  bool success = true;

  if (!*newval)
    {
      *extra = NULL;
      return true;
    }

  /* Need a modifiable copy of string */
  rawstring = pstrdup(*newval);

  /* Parse string into list of identifiers */
  if (!SplitIdentifierString(rawstring, ',', &elemlist))
    {
      /* syntax error in list */
      GUC_check_errdetail("List syntax is invalid.");
      pfree(rawstring);
      list_free(elemlist);
      return false;
    }

  /* GUC wants malloced results so allocate a new mapping */
  new_table = (int *) malloc(sizeof(int) * HASH_SQLSTATE_MODULO);
  if (!new_table)
    {
      pfree(rawstring);
      list_free(elemlist);
      return false;
    }
  for (i=0; i<HASH_SQLSTATE_MODULO; i++)
    new_table[i] = -1;

  /* validate list and insert the results in the hash table */
  foreach (l, elemlist)
    {
      const struct config_enum_entry *enum_entry;
      char *tok = lfirst(l), *level_str = strchr(tok, ':');
      int sqlstate, loglevel = -1;

      if (level_str != NULL && (level_str - tok) == 5)
        {
          for (enum_entry = server_message_level_options;
               enum_entry && enum_entry->name;
               enum_entry++)
            {
              if (pg_strcasecmp(enum_entry->name, level_str + 1) == 0)
                {
                  loglevel = enum_entry->val;
                  break;
                }
            }
        }

      if (loglevel < 0)
        {
          GUC_check_errdetail("Invalid sqlstate error definition: \"%s\".", tok);
          success = false;
          break;
        }
      sqlstate = MAKE_SQLSTATE(pg_ascii_toupper(tok[0]), pg_ascii_toupper(tok[1]),
                               pg_ascii_toupper(tok[2]), pg_ascii_toupper(tok[3]),
                               pg_ascii_toupper(tok[4]));
      new_table[hash_sqlstate(sqlstate)] = loglevel;
    }

  pfree(rawstring);
  list_free(elemlist);

  if (!success)
    {
      free(new_table);
      return false;
    }

  *extra = new_table;
  return true;
}

/*
 * assign_filter_by_sqlstate_msgs - take the result generated by
 * check_filter_by_sqlstate into use for messages.
 * 'extra' contains the new hash table.
 */
static void
assign_filter_by_sqlstate_msgs(const char *newval, void *extra)
{
  unsigned int *new_table = (unsigned int *) extra;
  int i;

  for (i=0; i<HASH_SQLSTATE_MODULO; i++)
    filter_by_sqlstate_hash[i].loglevel = new_table ? new_table[i] : -1;
}

/*
 * assign_filter_by_sqlstate_stmt - take the result generated by
 * check_filter_by_sqlstate into use for statements.
 * 'extra' contains the new hash table.
 */
static void
assign_filter_by_sqlstate_stmt(const char *newval, void *extra)
{
  unsigned int *new_table = (unsigned int *) extra;
  int i;

  for (i=0; i<HASH_SQLSTATE_MODULO; i++)
    filter_by_sqlstate_hash[i].stmt_loglevel = new_table ? new_table[i] : -1;
}
