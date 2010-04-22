/** \ingroup rpmdb
 * \file lib/dbconfig.c
 */

#include "system.h"

#include <popt.h>

#include <rpm/rpmtypes.h>
#include <rpm/rpmmacro.h>
#include <rpm/rpmstring.h>
#include <rpm/rpmlog.h>
#include <rpm/argv.h>
#include "lib/rpmdb_internal.h"
#include "debug.h"

static struct _dbiIndex staticdbi;

/** \ingroup dbi
 */
static const struct poptOption rdbOptions[] = {
 /* XXX DB_CXX_NO_EXCEPTIONS */

 { "cdb",	0,POPT_BIT_SET,	&staticdbi.dbi_eflags, DB_INIT_CDB,
	NULL, NULL },
 { "lock",	0,POPT_BIT_SET,	&staticdbi.dbi_eflags, DB_INIT_LOCK,
	NULL, NULL },
 { "log",	0,POPT_BIT_SET,	&staticdbi.dbi_eflags, DB_INIT_LOG,
	NULL, NULL },
 { "txn",	0,POPT_BIT_SET,	&staticdbi.dbi_eflags, DB_INIT_TXN,
	NULL, NULL },
 { "recover",	0,POPT_BIT_SET,	&staticdbi.dbi_eflags, DB_RECOVER,
	NULL, NULL },
 { "recover_fatal", 0,POPT_BIT_SET,	&staticdbi.dbi_eflags, DB_RECOVER_FATAL,
	NULL, NULL },
 { "lockdown",	0,POPT_BIT_SET,	&staticdbi.dbi_eflags, DB_LOCKDOWN,
	NULL, NULL },
 { "private",	0,POPT_BIT_SET,	&staticdbi.dbi_eflags, DB_PRIVATE,
	NULL, NULL },

 { "nommap",	0,POPT_BIT_SET,	&staticdbi.dbi_oflags, DB_NOMMAP,
	NULL, NULL },

 { "btree",	0,POPT_ARG_VAL,		&staticdbi.dbi_dbtype, DB_BTREE,
	NULL, NULL },
 { "hash", 	0,POPT_ARG_VAL,		&staticdbi.dbi_dbtype, DB_HASH,
	NULL, NULL },
 { "unknown",	0,POPT_ARG_VAL,		&staticdbi.dbi_dbtype, DB_UNKNOWN,
	NULL, NULL },

 { "verify",	0,POPT_ARG_NONE,	&staticdbi.dbi_verify_on_close, 0,
	NULL, NULL },
 { "nofsync",	0,POPT_ARG_NONE,	&staticdbi.dbi_no_fsync, 0,
	NULL, NULL },
 { "nodbsync",	0,POPT_ARG_NONE,	&staticdbi.dbi_no_dbsync, 0,
	NULL, NULL },
 { "lockdbfd",	0,POPT_ARG_NONE,	&staticdbi.dbi_lockdbfd, 0,
	NULL, NULL },

 { "cachesize",	0,POPT_ARG_INT,		&staticdbi.dbi_cachesize, 0,
	NULL, NULL },

 { "deadlock",	0,POPT_BIT_SET,	&staticdbi.dbi_verbose, DB_VERB_DEADLOCK,
	NULL, NULL },
 { "recovery",	0,POPT_BIT_SET,	&staticdbi.dbi_verbose, DB_VERB_RECOVERY,
	NULL, NULL },
 { "waitsfor",	0,POPT_BIT_SET,	&staticdbi.dbi_verbose, DB_VERB_WAITSFOR,
	NULL, NULL },
 { "verbose",	0,POPT_ARG_VAL,		&staticdbi.dbi_verbose, -1,
	NULL, NULL },

 { "mmapsize", 0,POPT_ARG_INT,		&staticdbi.dbi_mmapsize, 0,
	NULL, NULL },
 { "mp_mmapsize", 0,POPT_ARG_INT,	&staticdbi.dbi_mmapsize, 0,
	NULL, NULL },
 { "mp_size",	0,POPT_ARG_INT,		&staticdbi.dbi_cachesize, 0,
	NULL, NULL },
 { "pagesize",	0,POPT_ARG_INT,		&staticdbi.dbi_pagesize, 0,
	NULL, NULL },

    POPT_TABLEEND
};

dbiIndex dbiFree(dbiIndex dbi)
{
    if (dbi) {
	dbi = _free(dbi);
    }
    return dbi;
}

/** @todo Set a reasonable "last gasp" default db config. */
static const char * const dbi_config_default =
    "hash:cdb:verbose";

dbiIndex dbiNew(rpmdb rpmdb, rpmTag rpmtag)
{
    dbiIndex dbi = xcalloc(1, sizeof(*dbi));
    char *dbOpts;

    dbOpts = rpmExpand("%{_dbi_config_", rpmTagGetName(rpmtag), "}", NULL);
    
    if (!(dbOpts && *dbOpts && *dbOpts != '%')) {
	dbOpts = _free(dbOpts);
	dbOpts = rpmExpand("%{_dbi_config}", NULL);
	if (!(dbOpts && *dbOpts && *dbOpts != '%')) {
	    dbOpts = rpmExpand(dbi_config_default, NULL);
	}
    }

    /* Parse the options for the database element(s). */
    if (dbOpts && *dbOpts && *dbOpts != '%') {
	char *o, *oe;
	char *p, *pe;

	memset(&staticdbi, 0, sizeof(staticdbi));
/*=========*/
	for (o = dbOpts; o && *o; o = oe) {
	    const struct poptOption *opt;
	    const char * tok;
	    unsigned int argInfo;

	    /* Skip leading white space. */
	    while (*o && risspace(*o))
		o++;

	    /* Find and terminate next key=value pair. Save next start point. */
	    for (oe = o; oe && *oe; oe++) {
		if (risspace(*oe))
		    break;
		if (oe[0] == ':' && !(oe[1] == '/' && oe[2] == '/'))
		    break;
	    }
	    if (oe && *oe)
		*oe++ = '\0';
	    if (*o == '\0')
		continue;

	    /* Separate key from value, save value start (if any). */
	    for (pe = o; pe && *pe && *pe != '='; pe++)
		{};
	    p = (pe ? *pe++ = '\0', pe : NULL);

	    /* Skip over negation at start of token. */
	    for (tok = o; *tok == '!'; tok++)
		{};

	    /* Find key in option table. */
	    for (opt = rdbOptions; opt->longName != NULL; opt++) {
		if (!rstreq(tok, opt->longName))
		    continue;
		break;
	    }
	    if (opt->longName == NULL) {
		rpmlog(RPMLOG_ERR,
			_("unrecognized db option: \"%s\" ignored.\n"), o);
		continue;
	    }

	    /* Toggle the flags for negated tokens, if necessary. */
	    argInfo = opt->argInfo;
	    if (argInfo == POPT_BIT_SET && *o == '!' && ((tok - o) % 2))
		argInfo = POPT_BIT_CLR;

	    /* Save value in template as appropriate. */
	    switch (argInfo & POPT_ARG_MASK) {

	    case POPT_ARG_NONE:
		(void) poptSaveInt((int *)opt->arg, argInfo, 1L);
		break;
	    case POPT_ARG_VAL:
		(void) poptSaveInt((int *)opt->arg, argInfo, (long)opt->val);
	    	break;
	    case POPT_ARG_STRING:
	    {	char ** t = opt->arg;
		if (t) {
/* FIX: opt->arg annotation in popt.h */
		    *t = _free(*t);
		    *t = xstrdup( (p ? p : "") );
		}
	    }	break;

	    case POPT_ARG_INT:
	    case POPT_ARG_LONG:
	      {	long aLong = strtol(p, &pe, 0);
		if (pe) {
		    if (!rstrncasecmp(pe, "Mb", 2))
			aLong *= 1024 * 1024;
		    else if (!rstrncasecmp(pe, "Kb", 2))
			aLong *= 1024;
		    else if (*pe != '\0') {
			rpmlog(RPMLOG_ERR,
				_("%s has invalid numeric value, skipped\n"),
				opt->longName);
			continue;
		    }
		}

		if ((argInfo & POPT_ARG_MASK) == POPT_ARG_LONG) {
		    if (aLong == LONG_MIN || aLong == LONG_MAX) {
			rpmlog(RPMLOG_ERR,
				_("%s has too large or too small long value, skipped\n"),
				opt->longName);
			continue;
		    }
		    (void) poptSaveLong((long *)opt->arg, argInfo, aLong);
		    break;
		} else {
		    if (aLong > INT_MAX || aLong < INT_MIN) {
			rpmlog(RPMLOG_ERR,
				_("%s has too large or too small integer value, skipped\n"),
				opt->longName);
			continue;
		    }
		    (void) poptSaveInt((int *)opt->arg, argInfo, aLong);
		}
	      }	break;
	    default:
		break;
	    }
	}
/*=========*/
    }

    dbOpts = _free(dbOpts);

    *dbi = staticdbi;	/* structure assignment */
    memset(&staticdbi, 0, sizeof(staticdbi));

    /* FIX: figger lib/dbi refcounts */
    dbi->dbi_rpmdb = rpmdb;
    dbi->dbi_file = rpmTagGetName(rpmtag);
    dbi->dbi_type = (rpmtag == RPMDBI_PACKAGES) ? DBI_PRIMARY : DBI_SECONDARY;
    dbi->dbi_byteswapped = -1;	/* -1 unknown, 0 native order, 1 alien order */
    dbi->dbi_oflags |= DB_CREATE;

    /* XXX FIXME: These all are environment, not per-dbi configuration */
    dbi->dbi_eflags |= (DB_INIT_MPOOL|DB_CREATE);
    /* Throw in some defaults if configuration didn't set any */
    if (!dbi->dbi_mmapsize) dbi->dbi_mmapsize = 16 * 1024 * 1024;
    if (!dbi->dbi_cachesize) dbi->dbi_cachesize = 1 * 1024 * 1024;


    /* FIX: *(rdbOptions->arg) reachable */
    return dbi;
}

char * prDbiOpenFlags(int dbflags, int print_dbenv_flags)
{
    ARGV_t flags = NULL;
    const struct poptOption *opt;
    char *buf;

    for (opt = rdbOptions; opt->longName != NULL; opt++) {
	if (opt->argInfo != POPT_BIT_SET)
	    continue;
	if (print_dbenv_flags) {
	    if (!(opt->arg == &staticdbi.dbi_eflags))
		continue;
	} else {
	    if (!(opt->arg == &staticdbi.dbi_oflags))
		continue;
	}
	if ((dbflags & opt->val) != opt->val)
	    continue;
	argvAdd(&flags, opt->longName);
	dbflags &= ~opt->val;
    }
    if (dbflags) {
	char *df = NULL;
	rasprintf(&df, "0x%x", (unsigned)dbflags);
	argvAdd(&flags, df);
	free(df);
    }
    buf = argvJoin(flags, ":");
    argvFree(flags);
	
    return buf;
}
