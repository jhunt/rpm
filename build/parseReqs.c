/** \ingroup rpmbuild
 * \file build/parseReqs.c
 *  Parse dependency tag from spec file or from auto-dependency generator.
 */

#include "system.h"

#include <ctype.h>
#include <rpm/rpmtypes.h>
#include <rpm/rpmlog.h>
#include "build/rpmbuild_internal.h"
#include "build/rpmbuild_misc.h"
#include "debug.h"


#define	SKIPWHITE(_x)	{while(*(_x) && (risspace(*_x) || *(_x) == ',')) (_x)++;}
#define	SKIPNONWHITE(_x){while(*(_x) &&!(risspace(*_x) || *(_x) == ',')) (_x)++;}

static rpmRC checkSep(const char *s, char c, char **emsg)
{
    const char *sep = strchr(s, c);
    if (sep && strchr(sep + 1, c)) {
	rasprintf(emsg, "Invalid version (double separator '%c'): %s", c, s);
    }
    return RPMRC_OK;
}

static rpmRC checkDep(rpmSpec spec, char *N, char *EVR, char **emsg)
{
    /* 
     * Tokens must begin with alphanumeric, _, or /, but we don't know
     * the spec's encoding so we only check what we can: plain ascii.
     */
    if (isascii(N[0]) && !(risalnum(N[0]) || N[0] == '_' || N[0] == '/')) {
        rasprintf(emsg, _("Dependency tokens must begin with alpha-numeric, '_' or '/'"));
        return RPMRC_FAIL;
    }
    if (EVR) {
        if (N[0] == '/') {
            rasprintf(emsg, _("Versioned file name not permitted"));
            return RPMRC_FAIL;
        }
        if (rpmCharCheck(spec, EVR, ".-_+:%{}~"))
            return RPMRC_FAIL;
        if (checkSep(EVR, '-', emsg) != RPMRC_OK || checkSep(EVR, ':', emsg) != RPMRC_OK)
            return RPMRC_FAIL;
    }
    return RPMRC_OK;
}

struct parseRCPOTRichData {
    rpmSpec spec;
    StringBuf sb;
};

/* Callback for the rich dependency parser. We use this to do check for invalid
 * characters and to build a normailzed version of the dependency */
static rpmRC parseRCPOTRichCB(void *cbdata, rpmrichParseType type,
		const char *n, int nl, const char *e, int el, rpmsenseFlags sense,
		rpmrichOp op, char **emsg) {
    struct parseRCPOTRichData *data = cbdata;
    StringBuf sb = data->sb;
    rpmRC rc = RPMRC_OK;

    if (type == RPMRICH_PARSE_ENTER) {
	appendStringBuf(sb, "(");
    } else if (type == RPMRICH_PARSE_LEAVE) {
	appendStringBuf(sb, ")");
    } else if (type == RPMRICH_PARSE_SIMPLE) {
	char *N = xmalloc(nl + 1);
	char *EVR = NULL;
	rstrlcpy(N, n, nl + 1);
	appendStringBuf(sb, N);
	if (el) {
	    char rel[6], *rp = rel;
	    EVR = xmalloc(el + 1);
	    rstrlcpy(EVR, e, el + 1);
	    *rp++ = ' ';
	    if (sense & RPMSENSE_LESS)
		*rp++ = '<';
	    if (sense & RPMSENSE_GREATER)
		*rp++ = '>';
	    if (sense & RPMSENSE_EQUAL)
		*rp++ = '=';
	    *rp++ = ' ';
	    *rp = 0;
	    appendStringBuf(sb, rel);
	    appendStringBuf(sb, EVR);
	}
	rc = checkDep(data->spec, N, EVR, emsg);
	_free(N);
	_free(EVR);
    } else if (type == RPMRICH_PARSE_OP) {
	appendStringBuf(sb, " ");
	appendStringBuf(sb, rpmrichOpStr(op));
	appendStringBuf(sb, " ");
    }
    return rc;
}

rpmRC parseRCPOT(rpmSpec spec, Package pkg, const char *field, rpmTagVal tagN,
	       int index, rpmsenseFlags tagflags)
{
    const char *r, *re, *v, *ve;
    char *emsg = NULL;
    char * N = NULL, * EVR = NULL;
    rpmTagVal nametag = RPMTAG_NOT_FOUND;
    rpmsenseFlags Flags;
    rpmRC rc = RPMRC_FAIL; /* assume failure */

    switch (tagN) {
    default:
    case RPMTAG_REQUIREFLAGS:
	nametag = RPMTAG_REQUIRENAME;
	tagflags |= RPMSENSE_ANY;
	break;
    case RPMTAG_RECOMMENDFLAGS:
	nametag = RPMTAG_RECOMMENDNAME;
	break;
    case RPMTAG_SUGGESTFLAGS:
	nametag = RPMTAG_SUGGESTNAME;
	break;
    case RPMTAG_SUPPLEMENTFLAGS:
	nametag = RPMTAG_SUPPLEMENTNAME;
	break;
    case RPMTAG_ENHANCEFLAGS:
	nametag = RPMTAG_ENHANCENAME;
	break;
    case RPMTAG_PROVIDEFLAGS:
	nametag = RPMTAG_PROVIDENAME;
	break;
    case RPMTAG_OBSOLETEFLAGS:
	nametag = RPMTAG_OBSOLETENAME;
	break;
    case RPMTAG_CONFLICTFLAGS:
	nametag = RPMTAG_CONFLICTNAME;
	break;
    case RPMTAG_ORDERFLAGS:
	nametag = RPMTAG_ORDERNAME;
	break;
    case RPMTAG_PREREQ:
	/* XXX map legacy PreReq into Requires(pre,preun) */
	nametag = RPMTAG_REQUIRENAME;
	tagflags |= (RPMSENSE_SCRIPT_PRE|RPMSENSE_SCRIPT_PREUN);
	break;
    case RPMTAG_TRIGGERPREIN:
	nametag = RPMTAG_TRIGGERNAME;
	tagflags |= RPMSENSE_TRIGGERPREIN;
	break;
    case RPMTAG_TRIGGERIN:
	nametag = RPMTAG_TRIGGERNAME;
	tagflags |= RPMSENSE_TRIGGERIN;
	break;
    case RPMTAG_TRIGGERPOSTUN:
	nametag = RPMTAG_TRIGGERNAME;
	tagflags |= RPMSENSE_TRIGGERPOSTUN;
	break;
    case RPMTAG_TRIGGERUN:
	nametag = RPMTAG_TRIGGERNAME;
	tagflags |= RPMSENSE_TRIGGERUN;
	break;
    case RPMTAG_BUILDPREREQ:
    case RPMTAG_BUILDREQUIRES:
	nametag = RPMTAG_REQUIRENAME;
	tagflags |= RPMSENSE_ANY;
	break;
    case RPMTAG_BUILDCONFLICTS:
	nametag = RPMTAG_CONFLICTNAME;
	break;
    case RPMTAG_FILETRIGGERIN:
	nametag = RPMTAG_FILETRIGGERNAME;
	tagflags |= RPMSENSE_TRIGGERIN;
	break;
    case RPMTAG_FILETRIGGERUN:
	nametag = RPMTAG_FILETRIGGERNAME;
	tagflags |= RPMSENSE_TRIGGERUN;
	break;
    case RPMTAG_FILETRIGGERPOSTUN:
	nametag = RPMTAG_FILETRIGGERNAME;
	tagflags |= RPMSENSE_TRIGGERPOSTUN;
	break;
    case RPMTAG_TRANSFILETRIGGERIN:
	nametag = RPMTAG_TRANSFILETRIGGERNAME;
	tagflags |= RPMSENSE_TRIGGERIN;
	break;
    case RPMTAG_TRANSFILETRIGGERUN:
	nametag = RPMTAG_TRANSFILETRIGGERNAME;
	tagflags |= RPMSENSE_TRIGGERUN;
	break;
    case RPMTAG_TRANSFILETRIGGERPOSTUN:
	nametag = RPMTAG_TRANSFILETRIGGERNAME;
	tagflags |= RPMSENSE_TRIGGERPOSTUN;
	break;
    }

    for (r = field; *r != '\0'; r = re) {
	SKIPWHITE(r);
	if (*r == '\0')
	    break;

	Flags = (tagflags & ~RPMSENSE_SENSEMASK);

	if (r[0] == '(') {
	    struct parseRCPOTRichData data;
	    if (nametag != RPMTAG_REQUIRENAME && nametag != RPMTAG_CONFLICTNAME &&
			nametag != RPMTAG_RECOMMENDNAME && nametag != RPMTAG_SUPPLEMENTNAME &&
			nametag != RPMTAG_SUGGESTNAME && nametag != RPMTAG_ENHANCENAME) {
		rasprintf(&emsg, _("No rich dependencies allowed for this type"));
		goto exit;
	    }
	    data.spec = spec;
	    data.sb = newStringBuf();
	    if (rpmrichParse(&r, &emsg, parseRCPOTRichCB, &data) != RPMRC_OK) {
		freeStringBuf(data.sb);
		goto exit;
	    }
	    if (addReqProv(pkg, nametag, getStringBuf(data.sb), NULL, Flags | RPMSENSE_RICH, index)) {
		rasprintf(&emsg, _("invalid dependency"));
		freeStringBuf(data.sb);
		goto exit;
	    }
	    freeStringBuf(data.sb);
	    re = r;
	    continue;
	}

	re = r;
	SKIPNONWHITE(re);
	N = xmalloc((re-r) + 1);
	rstrlcpy(N, r, (re-r) + 1);

	/* Parse EVR */
	EVR = NULL;
	v = re;
	SKIPWHITE(v);
	ve = v;
	SKIPNONWHITE(ve);

	re = v;	/* ==> next token (if no EVR found) starts here */

	/* Check for possible logical operator */
	if (ve > v) {
	    rpmsenseFlags sense = rpmParseDSFlags(v, ve - v);
	    if (sense) {
		Flags |= sense;

		/* now parse EVR */
		v = ve;
		SKIPWHITE(v);
		ve = v;
		SKIPNONWHITE(ve);
		if (*v == '\0' || ve == v) {
		    rasprintf(&emsg, _("Version required"));
		    goto exit;
		}
		EVR = xmalloc((ve-v) + 1);
		rstrlcpy(EVR, v, (ve-v) + 1);
		re = ve;	/* ==> next token after EVR string starts here */
	    }
	}

	/* check that dependency is well-formed */
	if (checkDep(spec, N, EVR, &emsg))
	    goto exit;

	if (nametag == RPMTAG_FILETRIGGERNAME ||
	    nametag == RPMTAG_TRANSFILETRIGGERNAME) {
	    if (N[0] != '/') {
		rasprintf(&emsg, _("Only absolute paths are allowed in "
				    "file triggers"));
	    }
	}


	/* Deny more "normal" triggers fired by the same pakage. File triggers are ok */
	if (nametag == RPMTAG_TRIGGERNAME) {
	    rpmds *pdsp = packageDependencies(pkg, nametag);
	    rpmds newds = rpmdsSingle(nametag, N, EVR, Flags);
	    rpmdsInit(*pdsp);
	    while (rpmdsNext(*pdsp) >= 0) {
		if (rpmdsCompare(*pdsp, newds) && (rpmdsFlags(*pdsp) & tagflags )) {
		    rasprintf(&emsg, _("Trigger fired by the same package "
			"is already defined in spec file"));
		    break;
		}
	    }
	    rpmdsFree(newds);
	    if (emsg)
		goto exit;
	}

	if (addReqProv(pkg, nametag, N, EVR, Flags, index)) {
	    rasprintf(&emsg, _("invalid dependency"));
	    goto exit;
	}

	N = _free(N);
	EVR = _free(EVR);

    }
    rc = RPMRC_OK;

exit:
    if (emsg) {
	int lvl = (rc == RPMRC_OK) ? RPMLOG_WARNING : RPMLOG_ERR;
	/* Automatic dependencies don't relate to spec lines */
	if (tagflags & (RPMSENSE_FIND_REQUIRES|RPMSENSE_FIND_PROVIDES)) {
	    rpmlog(lvl, "%s: %s\n", emsg, r);
	} else {
	    rpmlog(lvl, _("line %d: %s: %s\n"),
		   spec->lineNum, emsg, spec->line);
	}
	free(emsg);
    }
    _free(N);
    _free(EVR);

    return rc;
}
