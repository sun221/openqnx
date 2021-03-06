/****************************************************************************
 * Copyright (c) 1998 Free Software Foundation, Inc.                        *
 *                                                                          *
 * Permission is hereby granted, free of charge, to any person obtaining a  *
 * copy of this software and associated documentation files (the            *
 * "Software"), to deal in the Software without restriction, including      *
 * without limitation the rights to use, copy, modify, merge, publish,      *
 * distribute, distribute with modifications, sublicense, and/or sell       *
 * copies of the Software, and to permit persons to whom the Software is    *
 * furnished to do so, subject to the following conditions:                 *
 *                                                                          *
 * The above copyright notice and this permission notice shall be included  *
 * in all copies or substantial portions of the Software.                   *
 *                                                                          *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  *
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF               *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.   *
 * IN NO EVENT SHALL THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,   *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR    *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR    *
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.                               *
 *                                                                          *
 * Except as contained in this notice, the name(s) of the above copyright   *
 * holders shall not be used in advertising or otherwise to promote the     *
 * sale, use or other dealings in this Software without prior written       *
 * authorization.                                                           *
 ****************************************************************************/

/****************************************************************************
 *  Author: Zeyd M. Ben-Halim <zmbenhal@netcom.com> 1992,1995               *
 *     and: Eric S. Raymond <esr@snark.thyrsus.com>                         *
 ****************************************************************************/

/*
 *	tic.c --- Main program for terminfo compiler
 *			by Eric S. Raymond
 *
 */

#include <progs.priv.h>

#include <dump_entry.h>
#include <term_entry.h>

MODULE_ID("$Id: tic.c 153052 2008-08-13 01:17:50Z coreos $")

const char *_nc_progname = "tic";

static	FILE	*log_fp;
static	bool	showsummary = FALSE;

static	const	char usage_string[] = "[-h] [-v[n]] [-e names] [-CILNRTcfrsw1] source-file\n";

static void usage(void)
{
	static const char *const tbl[] = {
	"Options:",
	"  -1         format translation output one capability per line",
	"  -C         translate entries to termcap source form",
	"  -I         translate entries to terminfo source form",
	"  -L         translate entries to full terminfo source form",
	"  -N         disable smart defaults for source translation",
	"  -R         restrict translation to given terminfo/termcap version",
	"  -T         remove size-restrictions on compiled description",
	"  -c         check only, validate input without compiling or translating",
	"  -f         format complex strings for readability",
	"  -e<names>  translate/compile only entries named by comma-separated list",
	"  -o<dir>    set output directory for compiled entry writes",
	"  -r         force resolution of all use entries in source translation",
	"  -s         print summary statistics",
	"  -v[n]      set verbosity level",
	"  -w[n]      set format width for translation output",
	"",
	"Parameters:",
	"  <file>     file to translate or compile"
	};
	size_t j;

	printf("Usage: %s %s\n", _nc_progname, usage_string);
	for (j = 0; j < sizeof(tbl)/sizeof(tbl[0]); j++)
		puts(tbl[j]);
	exit(EXIT_FAILURE);
}

static bool immedhook(ENTRY *ep)
/* write out entries with no use capabilities immediately to save storage */
{
#ifndef HAVE_BIG_CORE
    /*
     * This is strictly a core-economy kluge.  The really clean way to handle
     * compilation is to slurp the whole file into core and then do all the
     * name-collision checks and entry writes in one swell foop.  But the
     * terminfo master file is large enough that some core-poor systems swap
     * like crazy when you compile it this way...there have been reports of
     * this process taking *three hours*, rather than the twenty seconds or
     * less typical on my development box.
     *
     * So.  This hook *immediately* writes out the referenced entry if it
     * has no use capabilities.  The compiler main loop refrains from
     * adding the entry to the in-core list when this hook fires.  If some
     * other entry later needs to reference an entry that got written
     * immediately, that's OK; the resolution code will fetch it off disk
     * when it can't find it in core.
     *
     * Name collisions will still be detected, just not as cleanly.  The
     * write_entry() code complains before overwriting an entry that
     * postdates the time of tic's first call to write_entry().  Thus
     * it will complain about overwriting entries newly made during the
     * tic run, but not about overwriting ones that predate it.
     *
     * The reason this is a hook, and not in line with the rest of the
     * compiler code, is that the support for termcap fallback cannot assume
     * it has anywhere to spool out these entries!
     *
     * The _nc_set_type() call here requires a compensating one in
     * _nc_parse_entry().
     *
     * If you define HAVE_BIG_CORE, you'll disable this kluge.  This will
     * make tic a bit faster (because the resolution code won't have to do
     * disk I/O nearly as often).
     */
    if (ep->nuses == 0)
    {
	int	oldline = _nc_curr_line;

	_nc_set_type(_nc_first_name(ep->tterm.term_names));
	_nc_curr_line = ep->startline;
	_nc_write_entry(&ep->tterm);
	_nc_curr_line = oldline;
	free(ep->tterm.str_table);
	return(TRUE);
    }
    else
#endif /* HAVE_BIG_CORE */
	return(FALSE);
}

static void put_translate(int c)
/* emit a comment char, translating terminfo names to termcap names */
{
    static bool in_name = FALSE;
    static char namebuf[132], suffix[132], *sp;

    if (!in_name)
    {
	if (c == '<')
	{
	    in_name = TRUE;
	    sp = namebuf;
	}
	else
	    putchar(c);
    }
    else if (c == '\n' || c == '@')
    {
	*sp++ = '\0';
	(void) putchar('<');
	(void) fputs(namebuf, stdout);
	putchar(c);
	in_name = FALSE;
    }
    else if (c != '>')
	*sp++ = c;
    else		/* ah! candidate name! */
    {
	char	*up;
	NCURSES_CONST char *tp;

	*sp++ = '\0';
	in_name = FALSE;

	suffix[0] = '\0';
	if ((up = strchr(namebuf, '#')) != 0
	 || (up = strchr(namebuf, '=')) != 0
	 || ((up = strchr(namebuf, '@')) != 0 && up[1] == '>'))
	{
	    (void) strcpy(suffix, up);
	    *up = '\0';
	}

	if ((tp = nametrans(namebuf)) != 0)
	{
	    (void) putchar(':');
	    (void) fputs(tp, stdout);
	    (void) fputs(suffix, stdout);
	    (void) putchar(':');
	}
	else
	{
	    /* couldn't find a translation, just dump the name */
	    (void) putchar('<');
	    (void) fputs(namebuf, stdout);
	    (void) fputs(suffix, stdout);
	    (void) putchar('>');
	}

    }
}

/* Returns a string, stripped of leading/trailing whitespace */
static char *stripped(char *src)
{
	while (isspace(*src))
		src++;
	if (*src != '\0') {
		char *dst = strcpy(malloc(strlen(src)+1), src);
		size_t len = strlen(dst);
		while (--len != 0 && isspace(dst[len]))
			dst[len] = '\0';
		return dst;
	}
	return 0;
}

/* Parse the "-e" option-value into a list of names */
static const char **make_namelist(char *src)
{
	const char **dst = 0;

	char *s, *base;
	size_t pass, n, nn;
	char buffer[BUFSIZ];

	if (strchr(src, '/') != 0) {	/* a filename */
		FILE *fp = fopen(src, "r");
		if (fp == 0) {
			perror(src);
			exit(EXIT_FAILURE);
		}
		for (pass = 1; pass <= 2; pass++) {
			nn = 0;
			while (fgets(buffer, sizeof(buffer), fp) != 0) {
				if ((s = stripped(buffer)) != 0) {
					if (dst != 0)
						dst[nn] = s;
					nn++;
				}
			}
			if (pass == 1) {
				dst = (const char **)calloc(nn+1, sizeof(*dst));
				rewind(fp);
			}
		}
		fclose(fp);
	} else {			/* literal list of names */
		for (pass = 1; pass <= 2; pass++) {
			for (n = nn = 0, base = src; ; n++) {
				int mark = src[n];
				if (mark == ',' || mark == '\0') {
					if (pass == 1) {
						nn++;
					} else {
						src[n] = '\0';
						if ((s = stripped(base)) != 0)
							dst[nn++] = s;
						base = &src[n+1];
					}
				}
				if (mark == '\0')
					break;
			}
			if (pass == 1)
				dst = (const char **)calloc(nn+1, sizeof(*dst));
		}
	}
	if (showsummary) {
		fprintf(log_fp, "Entries that will be compiled:\n");
		for (n = 0; dst[n] != 0; n++)
			fprintf(log_fp, "%d:%s\n", n+1, dst[n]);
	}
	return dst;
}

static bool matches(const char **needle, const char *haystack)
/* does entry in needle list match |-separated field in haystack? */
{
	bool code = FALSE;
	size_t n;

	if (needle != 0)
	{
		for (n = 0; needle[n] != 0; n++)
		{
			if (_nc_name_match(haystack, needle[n], "|"))
			{
				code = TRUE;
				break;
			}
		}
	}
	else
		code = TRUE;
	return(code);
}

int main (int argc, char *argv[])
{
int	v_opt = -1, debug_level;
int	smart_defaults = TRUE;
char    *termcap;
ENTRY	*qp;

int	this_opt, last_opt = '?';

int	outform = F_TERMINFO;	/* output format */
int	sortmode = S_TERMINFO;	/* sort_mode */

int	width = 60;
bool	formatted = FALSE;	/* reformat complex strings? */
bool	infodump = FALSE;	/* running as captoinfo? */
bool	capdump = FALSE;	/* running as infotocap? */
bool	forceresolve = FALSE;	/* force resolution */
bool	limited = TRUE;
char	*tversion = (char *)NULL;
const	char	*source_file = "terminfo";
const	char	**namelst = 0;
char	*outdir = (char *)NULL;
bool	check_only = FALSE;

	log_fp = stderr;

	if ((_nc_progname = strrchr(argv[0], '/')) == NULL)
		_nc_progname = argv[0];
	else
		_nc_progname++;

	infodump = (strcmp(_nc_progname, "captoinfo") == 0);
	capdump = (strcmp(_nc_progname, "infotocap") == 0);

	/*
	 * Processing arguments is a little complicated, since someone made a
	 * design decision to allow the numeric values for -w, -v options to
	 * be optional.
	 */
	while ((this_opt = getopt(argc, argv, "0123456789CILNR:TVce:fo:rsvw")) != EOF) {
		if (isdigit(this_opt)) {
			switch (last_opt) {
			case 'v':
				v_opt = (v_opt * 10) + (this_opt - '0');
				break;
			case 'w':
				width = (width * 10) + (this_opt - '0');
				break;
			default:
				if (this_opt != '1')
					usage();
				last_opt = this_opt;
				width = 0;
			}
			continue;
		}
		switch (this_opt) {
		case 'C':
			capdump  = TRUE;
			outform  = F_TERMCAP;
			sortmode = S_TERMCAP;
			break;
		case 'I':
			infodump = TRUE;
			outform  = F_TERMINFO;
			sortmode = S_TERMINFO;
			break;
		case 'L':
			infodump = TRUE;
			outform  = F_VARIABLE;
			sortmode = S_VARIABLE;
			break;
		case 'N':
			smart_defaults = FALSE;
			break;
		case 'R':
			tversion = optarg;
			break;
		case 'T':
			limited = FALSE;
			break;
		case 'V':
			puts(NCURSES_VERSION);
			return EXIT_SUCCESS;
		case 'c':
			check_only = TRUE;
			break;
		case 'e':
			namelst = make_namelist(optarg);
			break;
		case 'f':
			formatted = TRUE;
			break;
		case 'o':
			outdir = optarg;
			break;
		case 'r':
			forceresolve = TRUE;
			break;
		case 's':
			showsummary = TRUE;
			break;
		case 'v':
			v_opt = 0;
			break;
		case 'w':
			width = 0;
			break;
		default:
			usage();
		}
		last_opt = this_opt;
	}

	debug_level = (v_opt > 0) ? v_opt : (v_opt == 0);
	_nc_tracing = (1 << debug_level) - 1;

	if (optind < argc) {
		source_file = argv[optind++];
		if (optind < argc) {
			fprintf (stderr,
				"%s: Too many file names.  Usage:\n\t%s %s",
				_nc_progname,
				_nc_progname,
				usage_string);
			return EXIT_FAILURE;
		}
	} else {
		if (infodump == TRUE) {
			/* captoinfo's no-argument case */
			source_file = "/etc/termcap";
			if ((termcap = getenv("TERMCAP")) != NULL) {
				if (access(termcap, F_OK) == 0) {
					/* file exists */
					source_file = termcap;
				}
			}
		} else {
		/* tic */
			fprintf (stderr,
				"%s: File name needed.  Usage:\n\t%s %s",
				_nc_progname,
				_nc_progname,
				usage_string);
			return EXIT_FAILURE;
		}
	}

	if (freopen(source_file, "r", stdin) == NULL) {
		fprintf (stderr, "%s: Can't open %s\n", _nc_progname, source_file);
		return EXIT_FAILURE;
	}

	if (infodump)
		dump_init(tversion,
			  smart_defaults
				? outform
				: F_LITERAL,
			  sortmode, width, debug_level, formatted);
	else if (capdump)
		dump_init(tversion,
			  outform,
			  sortmode, width, debug_level, FALSE);

	/* parse entries out of the source file */
	_nc_set_source(source_file);
#ifndef HAVE_BIG_CORE
	if (!(check_only || infodump || capdump))
	    _nc_set_writedir(outdir);
#endif /* HAVE_BIG_CORE */
	_nc_read_entry_source(stdin, (char *)NULL,
			      !smart_defaults, FALSE,
			      (check_only || infodump || capdump) ? NULLHOOK : immedhook);

	/* do use resolution */
	if (check_only || (!infodump && !capdump) || forceresolve)
	    if (!_nc_resolve_uses() && !check_only)
		return EXIT_FAILURE;

#ifndef HAVE_BIG_CORE
	/*
	 * Aaargh! immedhook seriously hoses us!
	 *
	 * One problem with immedhook is it means we can't do -e.  Problem
	 * is that we can't guarantee that for each terminal listed, all the
	 * terminals it depends on will have been kept in core for reference
	 * resolution -- in fact it's certain the primitive types at the end
	 * of reference chains *won't* be in core unless they were explicitly
	 * in the select list themselves.
	 */
	if (namelst && (!infodump && !capdump))
	{
	    (void) fprintf(stderr,
			   "Sorry, -e can't be used without -I or -C\n");
	    return EXIT_FAILURE;
	}
#endif /* HAVE_BIG_CORE */

	/* length check */
	if (check_only && (capdump || infodump))
	{
	    for_entry_list(qp)
	    {
		if (matches(namelst, qp->tterm.term_names))
		{
		    int	len = fmt_entry(&qp->tterm, NULL, TRUE, infodump);

		    if (len>(infodump?MAX_TERMINFO_LENGTH:MAX_TERMCAP_LENGTH))
			    (void) fprintf(stderr,
			   "warning: resolved %s entry is %d bytes long\n",
			   _nc_first_name(qp->tterm.term_names),
			   len);
		}
	    }
	}

	/* write or dump all entries */
	if (!check_only)
	{
	    if (!infodump && !capdump)
	    {
		_nc_set_writedir(outdir);
		for_entry_list(qp)
		    if (matches(namelst, qp->tterm.term_names))
		    {
			_nc_set_type(_nc_first_name(qp->tterm.term_names));
			_nc_curr_line = qp->startline;
			_nc_write_entry(&qp->tterm);
		    }
	    }
	    else
	    {
		/* this is in case infotocap() generates warnings */
		_nc_curr_col = _nc_curr_line = -1;

		for_entry_list(qp)
		    if (matches(namelst, qp->tterm.term_names))
		    {
			int	j = qp->cend - qp->cstart;
			int	len = 0;

			/* this is in case infotocap() generates warnings */
			_nc_set_type(_nc_first_name(qp->tterm.term_names));

			(void) fseek(stdin, qp->cstart, SEEK_SET);
			while (j-- )
			    if (infodump)
				(void) putchar(getchar());
			    else
				put_translate(getchar());

			len = dump_entry(&qp->tterm, limited, NULL);
			for (j = 0; j < qp->nuses; j++)
			    len += dump_uses((char *)(qp->uses[j].parent), infodump);
			(void) putchar('\n');
			if (debug_level != 0 && !limited)
			    printf("# length=%d\n", len);
		    }
		if (!namelst)
		{
		    int  c, oldc = '\0';
		    bool in_comment = FALSE;
		    bool trailing_comment = FALSE;

		    (void) fseek(stdin, _nc_tail->cend, SEEK_SET);
		    while ((c = getchar()) != EOF)
		    {
			if (oldc == '\n') {
			    if (c == '#') {
				trailing_comment = TRUE;
				in_comment = TRUE;
			    } else {
				in_comment = FALSE;
			    }
			}
			if (trailing_comment
			 && (in_comment || (oldc == '\n' && c == '\n')))
			    putchar(c);
			oldc = c;
		    }
		}
	    }
	}

	/* Show the directory into which entries were written, and the total
	 * number of entries
	 */
	if (showsummary
	 && (!(check_only || infodump || capdump))) {
		int total = _nc_tic_written();
		if (total != 0)
			fprintf(log_fp, "%d entries written to %s\n",
				total,
				_nc_tic_dir((char *)0));
		else
			fprintf(log_fp, "No entries written\n");
	}
	return(EXIT_SUCCESS);
}
