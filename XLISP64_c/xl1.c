/* msstuff.c - operating system specific routines (Win32/Win64 console) */

#include "xlisp.h"

/* prototypes for functions local to this file */
static void onbreak(int sig);
static void xinfo(void);
static void xflush(void);
static int xgetc(void);
static void xputc(int ch);
static int xcheck(void);
static int xgetkeycode(void);
static void curback(int n);
static void showfrom(int from, int extra);
static void insertch(int ch);
static void deletech(void);
static void cursorto(int pos);
static void rubout(void);
static void setline(const char *str);
static void histadd(void);
static void histmove(int dir);
static LVAL makesymbol(int iflag);
static LVAL bquote1(LVAL expr);
static void placeform(LVAL place, LVAL value);
static void setffunction(LVAL fun, LVAL place, LVAL value);
static int keypresent(LVAL key, LVAL list);
static LVAL let(int pflag);
static LVAL flet(LVAL type, int letflag);
static LVAL prog(int pflag);
static LVAL progx(int n);
static LVAL doloop(int pflag);
static void dobindings(LVAL list, LVAL env);
static void doupdates(LVAL list, int pflag);
static void tagbody(void);
static LVAL match(int type, LVAL *pargs);
static LVAL evarg(LVAL *pargs);
static LVAL evmatch(int type, LVAL *pargs);
static XLNORETURN void toofew(LVAL args);
static XLNORETURN void toomany(LVAL args);
static void breakloop(char *hdr, char *cmsg, char *emsg, LVAL arg, int cflag);
static LVAL newnode(int type);
static unsigned char *stralloc(int size);
static void findmem(void);
static void mark(LVAL ptr);
static void sweep(void);
static int addseg(void);
static void stats(void);
static LVAL evform(LVAL form);
static LVAL evalhook(LVAL expr);
static int evpushargs(LVAL fun, LVAL args);
static LVAL evfun(LVAL fun, int argc, LVAL *argv);
static void doenter(LVAL sym, int argc, LVAL *argv);
static void doexit(LVAL sym, LVAL val);
static int member(LVAL x, LVAL list);
static void badarglist(void);
static LVAL printit(int pflag, int tflag);
static LVAL flatsize(int pflag);
static LVAL getstroutput(LVAL stream);
static LVAL xnotimp(void);

/*
 * Operating system interface.
 *
 * The original MS-DOS version used bdos() calls (INT 21h functions 6
 * and 7) for unbuffered console I/O.  Those do not exist on Win32/Win64,
 * so the console is now driven through the Microsoft C runtime <conio.h>
 * routines (_getch, _putch, _kbhit).  When standard input or output is
 * redirected (a pipe or a file) the interpreter falls back to ordinary
 * buffered stdio so that scripts can be piped in, e.g.
 *
 *     xlisp < test.lsp > test.out
 *
 * A non-Windows fallback (plain stdio) is included so the same sources
 * can also be built with gcc/clang on 64-bit Linux or macOS.
 */

#ifdef _WIN32
#include <conio.h>
#include <io.h>
#include <signal.h>
#define os_isatty(fd)	_isatty(fd)
#else
#include <unistd.h>
#include <signal.h>
#define os_isatty(fd)	isatty(fd)
#endif

#define LBSIZE 200

/* external variables */
extern LVAL s_unbound,s_true;
extern FILE *tfp;

/* local variables */
static unsigned char lbuf[LBSIZE];
static int lindex;
static int lcount;
static int lposition;
static long rseed = 1L;
static int console;			/* TRUE if stdin/stdout is an interactive console */
static volatile sig_atomic_t ctrlc;	/* set by the control-c handler */

/* onbreak - control-c (SIGINT) handler */
static void onbreak(int sig)
{
    (void)sig;
    ctrlc = TRUE;
    signal(SIGINT,onbreak);	/* re-arm the handler */
}

/* osinit - initialize */
void osinit(char *banner)
{
#ifdef _WIN32
    console = os_isatty(0) && os_isatty(1);
#elif defined(XL_CONSOLE_TEST)
    console = TRUE;		/* test hook: drive the line editor from stdin */
#else
    console = FALSE;		/* no raw console driver on this host */
#endif
    ctrlc = FALSE;
    signal(SIGINT,onbreak);
    printf("%s\n",banner);
    fflush(stdout);
    lposition = 0;
    lindex = 0;
    lcount = 0;
}

/* osfinish - clean up before returning to the operating system */
void osfinish(void)
{
    fflush(stdout);
}

/* oserror - print an error message */
void oserror(char *msg)
{
    printf("error: %s\n",msg);
    fflush(stdout);
}

/* osrand - return a random number between 0 and n-1 */
int osrand(int n)
{
    long k1;

    /* make sure we don't get stuck at zero */
    if (rseed == 0L) rseed = 1L;

    /* algorithm taken from Dr. Dobbs Journal, November 1985, page 91 */
    /* (Schrage's method - all intermediate values fit in 32 bits) */
    k1 = rseed / 127773L;
    if ((rseed = 16807L * (rseed - k1 * 127773L) - k1 * 2836L) < 0L)
	rseed += 2147483647L;

    /* return a random number between 0 and n-1 */
    return ((int)(rseed % (long)n));
}

/* osaopen - open an ascii file */
FILE *osaopen(char *name, char *mode)
{
    return (fopen(name,mode));
}

/* osbopen - open a binary file */
FILE *osbopen(char *name, char *mode)
{
    char bmode[10];
    strcpy(bmode,mode); strcat(bmode,"b");
    return (fopen(name,bmode));
}

/* osclose - close a file */
int osclose(FILE *fp)
{
    return (fclose(fp));
}

/* osagetc - get a character from an ascii file */
int osagetc(FILE *fp)
{
    return (getc(fp));
}

/* osaputc - put a character to an ascii file */
int osaputc(int ch, FILE *fp)
{
    return (putc(ch,fp));
}

/* osbgetc - get a character from a binary file */
int osbgetc(FILE *fp)
{
    return (getc(fp));
}

/* osbputc - put a character to a binary file */
int osbputc(int ch, FILE *fp)
{
    return (putc(ch,fp));
}

/*
 * Console line editor with command history and cursor movement
 *
 * Lines typed at the console are collected in lbuf[] and handed to the
 * reader one character at a time.  The line can be edited anywhere:
 *
 *     Left / Right  move the cursor one character
 *     Home / End    move the cursor to the start / end of the line
 *     Backspace     delete the character before the cursor
 *     Delete        delete the character under the cursor
 *     Escape        clear the line
 *     Up arrow      recall the previous (older) line from the history
 *     Down arrow    recall the next (newer) line; past the newest line
 *                   the line that was being typed comes back
 *
 * Typed characters are inserted at the cursor.  A tab is stored as the
 * spaces that reach the next tab stop, so every character in lbuf[]
 * takes exactly one screen column.  Every non-blank line entered is
 * remembered in a history of the last HISTMAX lines.
 *
 * The Windows console reports the arrow and editing keys through
 * _getch() as a prefix byte (0 or 0xE0) followed by a scan code; see
 * xgetkeycode().  Moving the cursor left uses oscurback() (xlcons.c),
 * which also works when a long line wraps onto the next screen row.
 */
#define HISTMAX		100		/* number of lines remembered */
#define KEY_EXT		0x100		/* marks an extended (prefixed) key */
#define KEY_HOME	(KEY_EXT | 0x47)
#define KEY_UP		(KEY_EXT | 0x48)
#define KEY_LEFT	(KEY_EXT | 0x4B)
#define KEY_RIGHT	(KEY_EXT | 0x4D)
#define KEY_END		(KEY_EXT | 0x4F)
#define KEY_DOWN	(KEY_EXT | 0x50)
#define KEY_DELETE	(KEY_EXT | 0x53)

static int lcur = 0;		/* cursor position within lbuf[] */
static int lstart = 0;		/* screen column where the line began */
static char *hist[HISTMAX];	/* history lines, oldest first */
static int hcount = 0;		/* number of lines in hist[] */
static int hpos = 0;		/* line being shown (hcount = the new line) */
static char hsave[LBSIZE];	/* the new line while browsing the history */

/* xgetkeycode - read a key, folding the two-byte extended keys into one code */
static int xgetkeycode(void)
{
    int ch = xgetc();
    if (console && (ch == 0x00 || ch == 0xE0))
	return (KEY_EXT | (xgetc() & 0xFF));
    return (ch);
}

/* curback - move the screen cursor n columns to the left */
static void curback(int n)
{
    if (n <= 0)
	return;
    lposition -= n;
    if (oscurback(n))		/* the console API handles wrapped lines */
	return;
    while (--n >= 0)
	xputc('\010');
}

/* showfrom - redraw lbuf[from..lcount-1], blank 'extra' old columns
   after it, then put the cursor back at lcur */
static void showfrom(int from, int extra)
{
    int i;
    for (i = from; i < lcount; ++i)
	xputc(lbuf[i]);
    for (i = 0; i < extra; ++i)
	xputc(' ');
    lposition = lstart + lcount + extra;
    curback(lcount + extra - lcur);
}

/* insertch - insert a character at the cursor */
static void insertch(int ch)
{
    if (lcount >= LBSIZE - 1)		/* leave room for the newline */
	return;
    memmove(&lbuf[lcur + 1],&lbuf[lcur],(size_t)(lcount - lcur));
    lbuf[lcur] = (unsigned char)ch;
    ++lcount;
    ++lcur;
    showfrom(lcur - 1,0);
}

/* deletech - delete the character under the cursor */
static void deletech(void)
{
    if (lcur >= lcount)
	return;
    memmove(&lbuf[lcur],&lbuf[lcur + 1],(size_t)(lcount - lcur - 1));
    --lcount;
    showfrom(lcur,1);
}

/* rubout - delete the character before the cursor */
static void rubout(void)
{
    if (lcur > 0) {
	--lcur;
	curback(1);
	deletech();
    }
}

/* cursorto - move the cursor to position 'pos' in the line */
static void cursorto(int pos)
{
    if (pos < 0) pos = 0;
    if (pos > lcount) pos = lcount;
    if (pos < lcur)
	curback(lcur - pos);
    else
	while (lcur < pos) {		/* reprinting moves the cursor right */
	    xputc(lbuf[lcur]);
	    ++lposition;
	    ++lcur;
	}
    lcur = pos;
}

/* setline - replace the whole edit line with 'str' */
static void setline(const char *str)
{
    int old = lcount;
    cursorto(0);
    lcount = 0;
    while (*str && lcount < LBSIZE - 1)
	lbuf[lcount++] = (unsigned char)*str++;
    lcur = lcount;
    showfrom(0,old > lcount ? old - lcount : 0);
}

/* histadd - remember an entered line (not blank, not a repeat of the last) */
static void histadd(void)
{
    char *p;
    int i;

    /* ignore blank lines */
    for (i = 0; i < lcount && lbuf[i] == ' '; ++i)
	;
    if (i == lcount)
	return;

    /* make a copy of the line */
    if ((p = (char *)malloc((size_t)lcount + 1)) == NULL)
	return;
    memcpy(p,lbuf,(size_t)lcount);
    p[lcount] = '\0';

    /* don't store the same line twice in a row */
    if (hcount && strcmp(hist[hcount-1],p) == 0) {
	free(p);
	return;
    }

    /* drop the oldest line when the list is full */
    if (hcount == HISTMAX) {
	free(hist[0]);
	memmove(&hist[0],&hist[1],(HISTMAX - 1) * sizeof(char *));
	--hcount;
    }
    hist[hcount++] = p;
}

/* histmove - show the previous (dir < 0) or next (dir > 0) history line */
static void histmove(int dir)
{
    int newpos = hpos + dir;
    if (newpos < 0 || newpos > hcount)
	return;				/* nothing further that way */
    if (hpos == hcount) {		/* leaving the new line: keep it */
	memcpy(hsave,lbuf,(size_t)lcount);
	hsave[lcount] = '\0';
    }
    hpos = newpos;
    setline(hpos == hcount ? hsave : hist[hpos]);
}

/* ostgetc - get a character from the terminal */
int ostgetc(void)
{
    int ch;

    /* redirected input: use buffered stdio */
    if (!console) {
	fflush(stdout);
	if ((ch = getc(stdin)) == EOF)
	    return (EOF);
	lposition = (ch == '\n' ? 0 : lposition + 1);
	if (tfp)
	    osaputc(ch,tfp);
	return (ch);
    }

    /* check for a buffered character */
    if (lcount--)
	return (lbuf[lindex++]);

    /* get an input line */
    for (lcount = lcur = 0, lstart = lposition, hpos = hcount; ; )
	switch (ch = xgetkeycode()) {
	case EOF:			/* only possible with a test console */
		xflush();
		return (EOF);
	case '\r':
		cursorto(lcount);	/* the output goes after the whole line */
		histadd();
		hpos = hcount;
		lbuf[lcount++] = '\n';
		xputc('\r'); xputc('\n'); lposition = 0;
		if (tfp)
		    for (lindex = 0; lindex < lcount; ++lindex)
			osaputc(lbuf[lindex],tfp);
		lindex = 0; lcount--;
		return (lbuf[lindex++]);
	case '\010':
	case '\177':
		rubout();
		break;
	case '\033':			/* escape - clear the line */
		setline("");
		hpos = hcount;
		break;
	case KEY_UP:	histmove(-1);		break;
	case KEY_DOWN:	histmove(1);		break;
	case KEY_LEFT:	cursorto(lcur - 1);	break;
	case KEY_RIGHT:	cursorto(lcur + 1);	break;
	case KEY_HOME:	cursorto(0);		break;
	case KEY_END:	cursorto(lcount);	break;
	case KEY_DELETE: deletech();		break;
	case '\032':
		xflush();
		return (EOF);
	case '\t':			/* spaces to the next tab stop */
		do {
		    insertch(' ');
		} while (((lstart + lcur) & 7) && lcount < LBSIZE - 1);
		break;
	default:
		if (ch & KEY_EXT)	/* other extended keys are ignored */
		    break;
		if (ch >= 0x20 && ch < 0x7F)
		    insertch(ch);
		else {
		    xflush();
		    switch (ch) {
		    case '\003':	ctrlc = FALSE;
					xltoplevel();	/* control-c */
		    case '\007':	xlcleanup();	/* control-g */
		    case '\020':	xlcontinue();	/* control-p */
		    case '\032':	return (EOF);	/* control-z */
		    default:		return (ch);
		    }
		}
	}
}

/* ostputc - put a character to the terminal */
void ostputc(int ch)
{
    /* check for control characters */
    oscheck();

    /* output the character */
    if (ch == '\n') {
	if (console) xputc('\r');
	xputc('\n');
	lposition = 0;
    }
    else {
	xputc(ch);
	lposition++;
   }

   /* output the character to the transcript file */
   if (tfp)
	osaputc(ch,tfp);
}

/* osflush - flush the terminal input buffer */
void osflush(void)
{
    lindex = lcount = lposition = 0;
}

/* oscheck - check for control characters during execution */
void oscheck(void)
{
    int ch;

    /* check for a control-c delivered as a signal */
    if (ctrlc) {
	ctrlc = FALSE;
	xflush();
	xltoplevel();
    }

    /* check for a control character typed at the console */
    if ((ch = xcheck()) != 0)
	switch (ch) {
	case '\002':	/* control-b */
	    xflush();
	    xlbreak("BREAK",s_unbound);
	    break;
	case '\003':	/* control-c */
	    xflush();
	    xltoplevel();
	    break;
	case '\024':	/* control-t */
	    xinfo();
	    break;
	}
}

/* xinfo - show information on control-t */
static void xinfo(void)
{
    extern long nfree,total;
    extern int gccalls;
    char buf[80];
    snprintf(buf,sizeof(buf),"\n[ Free: %ld, GC calls: %d, Total: %ld ]",
	     nfree,gccalls,total);
    errputstr(buf);
}

/* xflush - flush the input line buffer and start a new line */
static void xflush(void)
{
    osflush();
    ostputc('\n');
}

/* xgetc - get a character from the terminal without echo */
static int xgetc(void)
{
#ifdef _WIN32
    if (console)
	return (_getch() & 0xFF);
#endif
    fflush(stdout);
    return (getc(stdin));
}

/* xputc - put a character to the terminal */
static void xputc(int ch)
{
#ifdef _WIN32
    if (console) {
	_putch(ch);
	return;
    }
#endif
    putc(ch,stdout);
}

/* xcheck - check for a character (returns 0 if none is waiting) */
static int xcheck(void)
{
#ifdef _WIN32
    if (console && _kbhit())
	return (_getch() & 0xFF);
#endif
    return (0);
}

/* xsystem - execute a system command */
LVAL xsystem(void)
{
    char *cmd;
#ifdef _WIN32
    if ((cmd = getenv("COMSPEC")) == NULL)
	cmd = "cmd.exe";
#else
    if ((cmd = getenv("SHELL")) == NULL)
	cmd = "/bin/sh";
#endif
    if (moreargs())
	cmd = (char *)getstring(xlgastring());
    xllastarg();
    fflush(stdout);
    return (system(cmd) == 0 ? s_true : cvfixnum((FIXTYPE)errno));
}

/* xgetkey - get a key from the keyboard */
LVAL xgetkey(void)
{
    xllastarg();
    return (cvfixnum((FIXTYPE)xgetc()));
}

/* ossymbols - enter os specific symbols */
void ossymbols(void)
{
}
/* xlbfun.c - xlisp basic built-in functions */
/*	Copyright (c) 1985, by David Michael Betz
	All Rights Reserved
	Permission is granted for unrestricted non-commercial use	*/



/* external variables */
extern LVAL xlenv,xlfenv,xldenv,s_true;
extern LVAL s_evalhook,s_applyhook;
extern LVAL s_car,s_cdr,s_nth,s_get,s_svalue,s_splist,s_aref;
extern LVAL s_lambda,s_macro;
extern LVAL s_comma,s_comat;
extern LVAL s_unbound;
extern char gsprefix[];
extern int gsnumber;

/* external routines */

/* forward declarations */

/* xeval - the built-in function 'eval' */
LVAL xeval(void)
{
    LVAL expr;

    /* get the expression to evaluate */
    expr = xlgetarg();
    xllastarg();

    /* evaluate the expression */
    return (xleval(expr));
}

/* xapply - the built-in function 'apply' */
LVAL xapply(void)
{
    LVAL fun,arglist;

    /* get the function and argument list */
    fun = xlgetarg();
    arglist = xlgalist();
    xllastarg();

    /* apply the function to the arguments */
    return (xlapply(pushargs(fun,arglist)));
}

/* xfuncall - the built-in function 'funcall' */
LVAL xfuncall(void)
{
    LVAL *newfp;
    int argc;
    
    /* build a new argument stack frame */
    newfp = xlsp;
    pusharg(cvfixnum((FIXTYPE)(newfp - xlfp)));
    pusharg(xlgetarg());
    pusharg(NIL); /* will be argc */

    /* push each argument */
    for (argc = 0; moreargs(); ++argc)
	pusharg(nextarg());

    /* establish the new stack frame */
    newfp[2] = cvfixnum((FIXTYPE)argc);
    xlfp = newfp;

    /* apply the function to the arguments */
    return (xlapply(argc));
}

/* xmacroexpand - expand a macro call repeatedly */
LVAL xmacroexpand(void)
{
    LVAL form;
    form = xlgetarg();
    xllastarg();
    return (xlexpandmacros(form));
}

/* x1macroexpand - expand a macro call */
LVAL x1macroexpand(void)
{
    LVAL form,fun,args;

    /* protect some pointers */
    xlstkcheck(2);
    xlsave(fun);
    xlsave(args);

    /* get the form */
    form = xlgetarg();
    xllastarg();

    /* expand until the form isn't a macro call */
    if (consp(form)) {
	fun = car(form);		/* get the macro name */
	args = cdr(form);		/* get the arguments */
	if (symbolp(fun) && fboundp(fun)) {
	    fun = xlgetfunction(fun);	/* get the expansion function */
	    macroexpand(fun,args,&form);
	}
    }

    /* restore the stack and return the expansion */
    xlpopn(2);
    return (form);
}

/* xatom - is this an atom? */
LVAL xatom(void)
{
    LVAL arg;
    arg = xlgetarg();
    xllastarg();
    return (atom(arg) ? s_true : NIL);
}

/* xsymbolp - is this an symbol? */
LVAL xsymbolp(void)
{
    LVAL arg;
    arg = xlgetarg();
    xllastarg();
    return (arg == NIL || symbolp(arg) ? s_true : NIL);
}

/* xnumberp - is this a number? */
LVAL xnumberp(void)
{
    LVAL arg;
    arg = xlgetarg();
    xllastarg();
    return (fixp(arg) || floatp(arg) ? s_true : NIL);
}

/* xintegerp - is this an integer? */
LVAL xintegerp(void)
{
    LVAL arg;
    arg = xlgetarg();
    xllastarg();
    return (fixp(arg) ? s_true : NIL);
}

/* xfloatp - is this a float? */
LVAL xfloatp(void)
{
    LVAL arg;
    arg = xlgetarg();
    xllastarg();
    return (floatp(arg) ? s_true : NIL);
}

/* xcharp - is this a character? */
LVAL xcharp(void)
{
    LVAL arg;
    arg = xlgetarg();
    xllastarg();
    return (charp(arg) ? s_true : NIL);
}

/* xstringp - is this a string? */
LVAL xstringp(void)
{
    LVAL arg;
    arg = xlgetarg();
    xllastarg();
    return (stringp(arg) ? s_true : NIL);
}

/* xarrayp - is this an array? */
LVAL xarrayp(void)
{
    LVAL arg;
    arg = xlgetarg();
    xllastarg();
    return (vectorp(arg) ? s_true : NIL);
}

/* xstreamp - is this a stream? */
LVAL xstreamp(void)
{
    LVAL arg;
    arg = xlgetarg();
    xllastarg();
    return (streamp(arg) || ustreamp(arg) ? s_true : NIL);
}

/* xobjectp - is this an object? */
LVAL xobjectp(void)
{
    LVAL arg;
    arg = xlgetarg();
    xllastarg();
    return (objectp(arg) ? s_true : NIL);
}

/* xboundp - is this a value bound to this symbol? */
LVAL xboundp(void)
{
    LVAL sym;
    sym = xlgasymbol();
    xllastarg();
    return (boundp(sym) ? s_true : NIL);
}

/* xfboundp - is this a functional value bound to this symbol? */
LVAL xfboundp(void)
{
    LVAL sym;
    sym = xlgasymbol();
    xllastarg();
    return (fboundp(sym) ? s_true : NIL);
}

/* xnull - is this null? */
LVAL xnull(void)
{
    LVAL arg;
    arg = xlgetarg();
    xllastarg();
    return (null(arg) ? s_true : NIL);
}

/* xlistp - is this a list? */
LVAL xlistp(void)
{
    LVAL arg;
    arg = xlgetarg();
    xllastarg();
    return (listp(arg) ? s_true : NIL);
}

/* xendp - is this the end of a list? */
LVAL xendp(void)
{
    LVAL arg;
    arg = xlgalist();
    xllastarg();
    return (null(arg) ? s_true : NIL);
}

/* xconsp - is this a cons? */
LVAL xconsp(void)
{
    LVAL arg;
    arg = xlgetarg();
    xllastarg();
    return (consp(arg) ? s_true : NIL);
}

/* xeq - are these equal? */
LVAL xeq(void)
{
    LVAL arg1,arg2;

    /* get the two arguments */
    arg1 = xlgetarg();
    arg2 = xlgetarg();
    xllastarg();

    /* compare the arguments */
    return (arg1 == arg2 ? s_true : NIL);
}

/* xeql - are these equal? */
LVAL xeql(void)
{
    LVAL arg1,arg2;

    /* get the two arguments */
    arg1 = xlgetarg();
    arg2 = xlgetarg();
    xllastarg();

    /* compare the arguments */
    return (eql(arg1,arg2) ? s_true : NIL);
}

/* xequal - are these equal? (recursive) */
LVAL xequal(void)
{
    LVAL arg1,arg2;

    /* get the two arguments */
    arg1 = xlgetarg();
    arg2 = xlgetarg();
    xllastarg();

    /* compare the arguments */
    return (equal(arg1,arg2) ? s_true : NIL);
}

/* xset - built-in function set */
LVAL xset(void)
{
    LVAL sym,val;

    /* get the symbol and new value */
    sym = xlgasymbol();
    val = xlgetarg();
    xllastarg();

    /* assign the symbol the value of argument 2 and the return value */
    setvalue(sym,val);

    /* return the result value */
    return (val);
}

/* xgensym - generate a symbol */
LVAL xgensym(void)
{
    char sym[STRMAX+11]; /* enough space for prefix and number */
    LVAL x;

    /* get the prefix or number */
    if (moreargs()) {
	x = xlgetarg();
	switch (ntype(x)) {
	case SYMBOL:
		x = getpname(x);
	case STRING:
		strncpy(gsprefix,getstring(x),STRMAX);
		gsprefix[STRMAX] = '\0';
		break;
	case FIXNUM:
		gsnumber = xlfixint(x);
		break;
	default:
		xlerror("bad argument type",x);
	}
    }
    xllastarg();

    /* create the pname of the new symbol */
    snprintf(sym,sizeof(sym),"%s%d",gsprefix,gsnumber++);

    /* make a symbol with this print name */
    return (xlmakesym(sym));
}

/* xmakesymbol - make a new uninterned symbol */
LVAL xmakesymbol(void)
{
    return (makesymbol(FALSE));
}

/* xintern - make a new interned symbol */
LVAL xintern(void)
{
    return (makesymbol(TRUE));
}

/* makesymbol - make a new symbol */
static LVAL makesymbol(int iflag)
{
    LVAL pname;

    /* get the print name of the symbol to intern */
    pname = xlgastring();
    xllastarg();

    /* make the symbol */
    return (iflag ? xlenter(getstring(pname))
    		  : xlmakesym(getstring(pname)));
}

/* xsymname - get the print name of a symbol */
LVAL xsymname(void)
{
    LVAL sym;

    /* get the symbol */
    sym = xlgasymbol();
    xllastarg();

    /* return the print name */
    return (getpname(sym));
}

/* xsymvalue - get the value of a symbol */
LVAL xsymvalue(void)
{
    LVAL sym,val;

    /* get the symbol */
    sym = xlgasymbol();
    xllastarg();

    /* get the global value */
    while ((val = getvalue(sym)) == s_unbound)
	xlunbound(sym);

    /* return its value */
    return (val);
}

/* xsymfunction - get the functional value of a symbol */
LVAL xsymfunction(void)
{
    LVAL sym,val;

    /* get the symbol */
    sym = xlgasymbol();
    xllastarg();

    /* get the global value */
    while ((val = getfunction(sym)) == s_unbound)
	xlfunbound(sym);

    /* return its value */
    return (val);
}

/* xsymplist - get the property list of a symbol */
LVAL xsymplist(void)
{
    LVAL sym;

    /* get the symbol */
    sym = xlgasymbol();
    xllastarg();

    /* return the property list */
    return (getplist(sym));
}

/* xget - get the value of a property */
LVAL xget(void)
{
    LVAL sym,prp;

    /* get the symbol and property */
    sym = xlgasymbol();
    prp = xlgasymbol();
    xllastarg();

    /* retrieve the property value */
    return (xlgetprop(sym,prp));
}

/* xputprop - set the value of a property */
LVAL xputprop(void)
{
    LVAL sym,val,prp;

    /* get the symbol and property */
    sym = xlgasymbol();
    val = xlgetarg();
    prp = xlgasymbol();
    xllastarg();

    /* set the property value */
    xlputprop(sym,val,prp);

    /* return the value */
    return (val);
}

/* xremprop - remove a property value from a property list */
LVAL xremprop(void)
{
    LVAL sym,prp;

    /* get the symbol and property */
    sym = xlgasymbol();
    prp = xlgasymbol();
    xllastarg();

    /* remove the property */
    xlremprop(sym,prp);

    /* return nil */
    return (NIL);
}

/* xhash - compute the hash value of a string or symbol */
LVAL xhash(void)
{
    unsigned char *str;
    LVAL len,val;
    int n;

    /* get the string and the table length */
    val = xlgetarg();
    len = xlgafixnum(); n = xlfixint(len);
    xllastarg();
    if (n <= 0)
	xlerror("hash table size must be positive",len);

    /* get the string */
    if (symbolp(val))
	str = getstring(getpname(val));
    else if (stringp(val))
	str = getstring(val);
    else
	xlerror("bad argument type",val);

    /* return the hash index */
    return (cvfixnum((FIXTYPE)hash(str,n)));
}

/* xaref - array reference function */
LVAL xaref(void)
{
    LVAL array,index;
    int i;

    /* get the array and the index */
    array = xlgavector();
    index = xlgafixnum(); i = xlfixint(index);
    xllastarg();

    /* range check the index */
    if (i < 0 || i >= getsize(array))
	xlerror("array index out of bounds",index);

    /* return the array element */
    return (getelement(array,i));
}

/* xmkarray - make a new array */
LVAL xmkarray(void)
{
    LVAL size;
    int n;

    /* get the size of the array */
    size = xlgafixnum() ; n = xlfixint(size);
    xllastarg();
    if (n < 0 || n > INT_MAX / (int)sizeof(LVAL))
	xlerror("bad array size",size);

    /* create the array */
    return (newvector(n));
}

/* xvector - make a vector */
LVAL xvector(void)
{
    LVAL val;
    int i;

    /* make the vector */
    val = newvector(xlargc);

    /* store each argument */
    for (i = 0; moreargs(); ++i)
	setelement(val,i,nextarg());
    xllastarg();

    /* return the vector */
    return (val);
}

/* xerror - special form 'error' */
LVAL xerror(void)
{
    LVAL emsg,arg;

    /* get the error message and the argument */
    emsg = xlgastring();
    arg = (moreargs() ? xlgetarg() : s_unbound);
    xllastarg();

    /* signal the error */
    xlerror(getstring(emsg),arg);
}

/* xcerror - special form 'cerror' */
LVAL xcerror(void)
{
    LVAL cmsg,emsg,arg;

    /* get the correction message, the error message, and the argument */
    cmsg = xlgastring();
    emsg = xlgastring();
    arg = (moreargs() ? xlgetarg() : s_unbound);
    xllastarg();

    /* signal the error */
    xlcerror(getstring(cmsg),getstring(emsg),arg);

    /* return nil */
    return (NIL);
}

/* xbreak - special form 'break' */
LVAL xbreak(void)
{
    LVAL emsg,arg;

    /* get the error message */
    emsg = (moreargs() ? xlgastring() : NIL);
    arg = (moreargs() ? xlgetarg() : s_unbound);
    xllastarg();

    /* enter the break loop */
    xlbreak((emsg ? getstring(emsg) : (unsigned char *)"**BREAK**"),arg);

    /* return nil */
    return (NIL);
}

/* xcleanup - special form 'clean-up' */
LVAL xcleanup(void)
{
    xllastarg();
    xlcleanup();
}

/* xtoplevel - special form 'top-level' */
LVAL xtoplevel(void)
{
    xllastarg();
    xltoplevel();
}

/* xcontinue - special form 'continue' */
LVAL xcontinue(void)
{
    xllastarg();
    xlcontinue();
}

/* xevalhook - eval hook function */
LVAL xevalhook(void)
{
    LVAL expr,newehook,newahook,newenv,oldenv,oldfenv,olddenv,val;

    /* protect some pointers */
    xlstkcheck(3);
    xlsave(oldenv);
    xlsave(oldfenv);
    xlsave(newenv);

    /* get the expression, the new hook functions and the environment */
    expr = xlgetarg();
    newehook = xlgetarg();
    newahook = xlgetarg();
    newenv = (moreargs() ? xlgalist() : NIL);
    xllastarg();

    /* bind *evalhook* and *applyhook* to the hook functions */
    olddenv = xldenv;
    xldbind(s_evalhook,newehook);
    xldbind(s_applyhook,newahook);

    /* establish the environment for the hook function */
    if (newenv) {
	oldenv = xlenv;
	oldfenv = xlfenv;
	xlenv = car(newenv);
	xlfenv = cdr(newenv);
    }

    /* evaluate the expression (bypassing *evalhook*) */
    val = xlxeval(expr);

    /* restore the old environment */
    xlunbind(olddenv);
    if (newenv) {
	xlenv = oldenv;
	xlfenv = oldfenv;
    }

    /* restore the stack */
    xlpopn(3);

    /* return the result */
    return (val);
}

/* xlcont - xlisp special forms */
/*	Copyright (c) 1985, by David Michael Betz
	All Rights Reserved
	Permission is granted for unrestricted non-commercial use	*/


/* external variables */
extern LVAL xlenv,xlfenv,xldenv,xlvalue;
extern LVAL s_setf,s_car,s_cdr,s_nth,s_aref,s_get;
extern LVAL s_svalue,s_sfunction,s_splist;
extern LVAL s_lambda,s_macro;
extern LVAL s_comma,s_comat;
extern LVAL s_unbound;
extern LVAL s_true;

/* external routines */

/* forward declarations */

/* dummy node type for a list */
#define LIST	-1

/* xquote - special form 'quote' */
LVAL xquote(void)
{
    LVAL val;
    val = xlgetarg();
    xllastarg();
    return (val);
}

/* xfunction - special form 'function' */
LVAL xfunction(void)
{
    LVAL val;

    /* get the argument */
    val = xlgetarg();
    xllastarg();

    /* create a closure for lambda expressions */
    if (consp(val) && car(val) == s_lambda && consp(cdr(val)))
	val = xlclose(NIL,s_lambda,car(cdr(val)),cdr(cdr(val)),xlenv,xlfenv);

    /* otherwise, get the value of a symbol */
    else if (symbolp(val))
	val = xlgetfunction(val);

    /* otherwise, its an error */
    else
	xlerror("not a function",val);

    /* return the function */
    return (val);
}

/* xbquote - back quote special form */
LVAL xbquote(void)
{
    LVAL expr;

    /* get the expression */
    expr = xlgetarg();
    xllastarg();

    /* fill in the template */
    return (bquote1(expr));
}

/* bquote1 - back quote helper function */
static LVAL bquote1(LVAL expr)
{
    LVAL val,list,last,new;

    /* handle atoms */
    if (atom(expr))
	val = expr;

    /* handle (comma <expr>) */
    else if (car(expr) == s_comma) {
	if (atom(cdr(expr)))
	    xlfail("bad comma expression");
	val = xleval(car(cdr(expr)));
    }

    /* handle ((comma-at <expr>) ... ) */
    else if (consp(car(expr)) && car(car(expr)) == s_comat) {
	xlstkcheck(2);
	xlsave(list);
	xlsave(val);
	if (atom(cdr(car(expr))))
	    xlfail("bad comma-at expression");
	list = xleval(car(cdr(car(expr))));
	for (last = NIL; consp(list); list = cdr(list)) {
	    new = consa(car(list));
	    if (last)
		rplacd(last,new);
	    else
		val = new;
	    last = new;
	}
	if (last)
	    rplacd(last,bquote1(cdr(expr)));
	else
	    val = bquote1(cdr(expr));
	xlpopn(2);
    }

    /* handle any other list */
    else {
	xlsave1(val);
	val = consa(NIL);
	rplaca(val,bquote1(car(expr)));
	rplacd(val,bquote1(cdr(expr)));
	xlpop();
    }

    /* return the result */
    return (val);
}

/* xlambda - special form 'lambda' */
LVAL xlambda(void)
{
    LVAL fargs,arglist,val;

    /* get the formal argument list and function body */
    xlsave1(arglist);
    fargs = xlgalist();
    arglist = makearglist(xlargc,xlargv);

    /* create a new function definition */
    val = xlclose(NIL,s_lambda,fargs,arglist,xlenv,xlfenv);

    /* restore the stack and return the closure */
    xlpop();
    return (val);
}

/* xgetlambda - get the lambda expression associated with a closure */
LVAL xgetlambda(void)
{
    LVAL closure;
    closure = xlgaclosure();
    return (cons(gettype(closure),
                 cons(getlambda(closure),getbody(closure))));
}

/* xsetq - special form 'setq' */
LVAL xsetq(void)
{
    LVAL sym,val;

    /* handle each pair of arguments */
    for (val = NIL; moreargs(); ) {
	sym = xlgasymbol();
	val = xleval(nextarg());
	xlsetvalue(sym,val);
    }

    /* return the result value */
    return (val);
}

/* xpsetq - special form 'psetq' */
LVAL xpsetq(void)
{
    LVAL plist,sym,val;

    /* protect some pointers */
    xlsave1(plist);

    /* handle each pair of arguments */
    for (val = NIL; moreargs(); ) {
	sym = xlgasymbol();
	val = xleval(nextarg());
	plist = cons(cons(sym,val),plist);
    }

    /* do parallel sets */
    for (; plist; plist = cdr(plist))
	xlsetvalue(car(car(plist)),cdr(car(plist)));

    /* restore the stack */
    xlpop();

    /* return the result value */
    return (val);
}

/* xsetf - special form 'setf' */
LVAL xsetf(void)
{
    LVAL place,value;

    /* protect some pointers */
    xlsave1(value);

    /* handle each pair of arguments */
    while (moreargs()) {

	/* get place and value */
	place = xlgetarg();
	value = xleval(nextarg());

	/* expand macros in the place form */
	if (consp(place))
	    place = xlexpandmacros(place);
	
	/* check the place form */
	if (symbolp(place))
	    xlsetvalue(place,value);
	else if (consp(place))
	    placeform(place,value);
	else
	    xlfail("bad place form");
    }

    /* restore the stack */
    xlpop();

    /* return the value */
    return (value);
}

/* placeform - handle a place form other than a symbol */
static void placeform(LVAL place, LVAL value)
{
    LVAL fun,arg1,arg2;
    int i;

    /* check the function name */
    if ((fun = match(SYMBOL,&place)) == s_get) {
	xlstkcheck(2);
	xlsave(arg1);
	xlsave(arg2);
	arg1 = evmatch(SYMBOL,&place);
	arg2 = evmatch(SYMBOL,&place);
	if (place) toomany(place);
	xlputprop(arg1,value,arg2);
	xlpopn(2);
    }
    else if (fun == s_svalue) {
	arg1 = evmatch(SYMBOL,&place);
	if (place) toomany(place);
	setvalue(arg1,value);
    }
    else if (fun == s_sfunction) {
	arg1 = evmatch(SYMBOL,&place);
	if (place) toomany(place);
	setfunction(arg1,value);
    }
    else if (fun == s_splist) {
	arg1 = evmatch(SYMBOL,&place);
	if (place) toomany(place);
	setplist(arg1,value);
    }
    else if (fun == s_car) {
	arg1 = evmatch(CONS,&place);
	if (place) toomany(place);
	rplaca(arg1,value);
    }
    else if (fun == s_cdr) {
	arg1 = evmatch(CONS,&place);
	if (place) toomany(place);
	rplacd(arg1,value);
    }
    else if (fun == s_nth) {
	xlsave1(arg1);
	arg1 = evmatch(FIXNUM,&place);
	arg2 = evmatch(LIST,&place);
	if (place) toomany(place);
	{ FIXTYPE k;
	  for (k = getfixnum(arg1); k > 0 && consp(arg2); --k)
	    arg2 = cdr(arg2); }
	if (consp(arg2))
	    rplaca(arg2,value);
	xlpop();
    }
    else if (fun == s_aref) {
	xlsave1(arg1);
	arg1 = evmatch(VECTOR,&place);
	arg2 = evmatch(FIXNUM,&place); i = xlfixint(arg2);
	if (place) toomany(place);
	if (i < 0 || i >= getsize(arg1))
	    xlerror("index out of range",arg2);
	setelement(arg1,i,value);
	xlpop();
    }
    else if (fun = xlgetprop(fun,s_setf))
	setffunction(fun,place,value);
    else
	xlfail("bad place form");
}

/* setffunction - call a user defined setf function */
static void setffunction(LVAL fun, LVAL place, LVAL value)
{
    LVAL *newfp;
    int argc;

    /* create the new call frame */
    newfp = xlsp;
    pusharg(cvfixnum((FIXTYPE)(newfp - xlfp)));
    pusharg(fun);
    pusharg(NIL);

    /* push the values of all of the place expressions and the new value */
    for (argc = 1; consp(place); place = cdr(place), ++argc)
	pusharg(xleval(car(place)));
    pusharg(value);

    /* insert the argument count and establish the call frame */
    newfp[2] = cvfixnum((FIXTYPE)argc);
    xlfp = newfp;

    /* apply the function */
    xlapply(argc);
}
		       
/* xdefun - special form 'defun' */
LVAL xdefun(void)
{
    LVAL sym,fargs,arglist;

    /* get the function symbol and formal argument list */
    xlsave1(arglist);
    sym = xlgasymbol();
    fargs = xlgalist();
    arglist = makearglist(xlargc,xlargv);

    /* make the symbol point to a new function definition */
    xlsetfunction(sym,xlclose(sym,s_lambda,fargs,arglist,xlenv,xlfenv));

    /* restore the stack and return the function symbol */
    xlpop();
    return (sym);
}

/* xdefmacro - special form 'defmacro' */
LVAL xdefmacro(void)
{
    LVAL sym,fargs,arglist;

    /* get the function symbol and formal argument list */
    xlsave1(arglist);
    sym = xlgasymbol();
    fargs = xlgalist();
    arglist = makearglist(xlargc,xlargv);

    /* make the symbol point to a new function definition */
    xlsetfunction(sym,xlclose(sym,s_macro,fargs,arglist,NIL,NIL));

    /* restore the stack and return the function symbol */
    xlpop();
    return (sym);
}

/* xcond - special form 'cond' */
LVAL xcond(void)
{
    LVAL list,val;

    /* find a predicate that is s_true */
    for (val = NIL; moreargs(); ) {

	/* get the next conditional */
	list = nextarg();

	/* evaluate the predicate part */
	if (consp(list) && (val = xleval(car(list)))) {

	    /* evaluate each expression */
	    for (list = cdr(list); consp(list); list = cdr(list))
		val = xleval(car(list));

	    /* exit the loop */
	    break;
	}
    }

    /* return the value */
    return (val);
}

/* xwhen - special form 'when' */
LVAL xwhen(void)
{
    LVAL val;

    /* check the test expression */
    if (val = xleval(xlgetarg()))
	while (moreargs())
	    val = xleval(nextarg());

    /* return the value */
    return (val);
}

/* xunless - special form 'unless' */
LVAL xunless(void)
{
    LVAL val=NIL;

    /* check the test expression */
    if (xleval(xlgetarg()) == NIL)
	while (moreargs())
	    val = xleval(nextarg());

    /* return the value */
    return (val);
}

/* xcase - special form 'case' */
LVAL xcase(void)
{
    LVAL key,list,cases,val;

    /* protect some pointers */
    xlsave1(key);

    /* get the key expression */
    key = xleval(nextarg());

    /* find a case that matches */
    for (val = NIL; moreargs(); ) {

	/* get the next case clause */
	list = nextarg();

	/* make sure this is a valid clause */
	if (consp(list)) {

	    /* compare the key list against the key */
	    if ((cases = car(list)) == s_true ||
                (listp(cases) && keypresent(key,cases)) ||
                eql(key,cases)) {

		/* evaluate each expression */
		for (list = cdr(list); consp(list); list = cdr(list))
		    val = xleval(car(list));

		/* exit the loop */
		break;
	    }
	}
	else
	    xlerror("bad case clause",list);
    }

    /* restore the stack */
    xlpop();

    /* return the value */
    return (val);
}

/* keypresent - check for the presence of a key in a list */
static int keypresent(LVAL key, LVAL list)
{
    for (; consp(list); list = cdr(list))
	if (eql(car(list),key))
	    return (TRUE);
    return (FALSE);
}

/* xand - special form 'and' */
LVAL xand(void)
{
    LVAL val;

    /* evaluate each argument */
    for (val = s_true; moreargs(); )
	if ((val = xleval(nextarg())) == NIL)
	    break;

    /* return the result value */
    return (val);
}

/* xor - special form 'or' */
LVAL xor(void)
{
    LVAL val;

    /* evaluate each argument */
    for (val = NIL; moreargs(); )
	if ((val = xleval(nextarg())))
	    break;

    /* return the result value */
    return (val);
}

/* xif - special form 'if' */
LVAL xif(void)
{
    LVAL testexpr,thenexpr,elseexpr;

    /* get the test expression, then clause and else clause */
    testexpr = xlgetarg();
    thenexpr = xlgetarg();
    elseexpr = (moreargs() ? xlgetarg() : NIL);
    xllastarg();

    /* evaluate the appropriate clause */
    return (xleval(xleval(testexpr) ? thenexpr : elseexpr));
}

/* xlet - special form 'let' */
LVAL xlet(void)
{
    return (let(TRUE));
}

/* xletstar - special form 'let*' */
LVAL xletstar(void)
{
    return (let(FALSE));
}

/* let - common let routine */
static LVAL let(int pflag)
{
    LVAL newenv,val;

    /* protect some pointers */
    xlsave1(newenv);

    /* create a new environment frame */
    newenv = xlframe(xlenv);

    /* get the list of bindings and bind the symbols */
    if (!pflag) xlenv = newenv;
    dobindings(xlgalist(),newenv);
    if (pflag) xlenv = newenv;

    /* execute the code */
    for (val = NIL; moreargs(); )
	val = xleval(nextarg());

    /* unbind the arguments */
    xlenv = cdr(xlenv);

    /* restore the stack */
    xlpop();

    /* return the result */
    return (val);
}

/* xflet - built-in function 'flet' */
LVAL xflet(void)
{
    return (flet(s_lambda,TRUE));
}

/* xlabels - built-in function 'labels' */
LVAL xlabels(void)
{
    return (flet(s_lambda,FALSE));
}

/* xmacrolet - built-in function 'macrolet' */
LVAL xmacrolet(void)
{
    return (flet(s_macro,TRUE));
}

/* flet - common flet/labels/macrolet routine */
static LVAL flet(LVAL type, int letflag)
{
    LVAL list,bnd,sym,fargs,val;

    /* create a new environment frame */
    xlfenv = xlframe(xlfenv);

    /* bind each symbol in the list of bindings */
    for (list = xlgalist(); consp(list); list = cdr(list)) {

	/* get the next binding */
	bnd = car(list);

	/* get the symbol and the function definition */
	sym = match(SYMBOL,&bnd);
	fargs = match(LIST,&bnd);
	val = xlclose(sym,type,fargs,bnd,xlenv,(letflag?cdr(xlfenv):xlfenv));

	/* bind the value to the symbol */
	xlfbind(sym,val);
    }

    /* execute the code */
    for (val = NIL; moreargs(); )
	val = xleval(nextarg());

    /* unbind the arguments */
    xlfenv = cdr(xlfenv);

    /* return the result */
    return (val);
}

/* xprog - special form 'prog' */
LVAL xprog(void)
{
    return (prog(TRUE));
}

/* xprogstar - special form 'prog*' */
LVAL xprogstar(void)
{
    return (prog(FALSE));
}

/* prog - common prog routine */
static LVAL prog(int pflag)
{
    LVAL newenv,val;
    CONTEXT cntxt;

    /* protect some pointers */
    xlsave1(newenv);

    /* create a new environment frame */
    newenv = xlframe(xlenv);

    /* establish a new execution context */
    xlbegin(&cntxt,CF_RETURN,NIL);
    if (setjmp(cntxt.c_jmpbuf))
	val = xlvalue;
    else {

	/* get the list of bindings and bind the symbols */
	if (!pflag) xlenv = newenv;
	dobindings(xlgalist(),newenv);
	if (pflag) xlenv = newenv;

	/* execute the code */
	tagbody();
	val = NIL;

	/* unbind the arguments */
	xlenv = cdr(xlenv);
    }
    xlend(&cntxt);

    /* restore the stack */
    xlpop();

    /* return the result */
    return (val);
}

/* xgo - special form 'go' */
LVAL xgo(void)
{
    LVAL label;

    /* get the target label */
    label = xlgetarg();
    xllastarg();

    /* transfer to the label */
    xlgo(label);
}

/* xreturn - special form 'return' */
LVAL xreturn(void)
{
    LVAL val;

    /* get the return value */
    val = (moreargs() ? xleval(nextarg()) : NIL);
    xllastarg();

    /* return from the inner most block */
    xlreturn(NIL,val);
}

/* xrtnfrom - special form 'return-from' */
LVAL xrtnfrom(void)
{
    LVAL name,val;

    /* get the return value */
    name = xlgasymbol();
    val = (moreargs() ? xleval(nextarg()) : NIL);
    xllastarg();

    /* return from the inner most block */
    xlreturn(name,val);
}

/* xprog1 - special form 'prog1' */
LVAL xprog1(void)
{
    return (progx(1));
}

/* xprog2 - special form 'prog2' */
LVAL xprog2(void)
{
    return (progx(2));
}

/* progx - common progx code */
static LVAL progx(int n)
{
    LVAL val;

    /* protect some pointers */
    xlsave1(val);

    /* evaluate the first n expressions */
    while (moreargs() && --n >= 0)
	val = xleval(nextarg());

    /* evaluate each remaining argument */
    while (moreargs())
	xleval(nextarg());

    /* restore the stack */
    xlpop();

    /* return the last test expression value */
    return (val);
}

/* xprogn - special form 'progn' */
LVAL xprogn(void)
{
    LVAL val;

    /* evaluate each expression */
    for (val = NIL; moreargs(); )
	val = xleval(nextarg());

    /* return the last test expression value */
    return (val);
}

/* xprogv - special form 'progv' */
LVAL xprogv(void)
{
    LVAL olddenv,vars,vals,val;

    /* protect some pointers */
    xlstkcheck(2);
    xlsave(vars);
    xlsave(vals);

    /* get the list of variables and the list of values */
    vars = xlgalist(); vars = xleval(vars);
    vals = xlgalist(); vals = xleval(vals);

    /* bind the values to the variables */
    for (olddenv = xldenv; consp(vars); vars = cdr(vars)) {
	if (!symbolp(car(vars)))
	    xlerror("expecting a symbol",car(vars));
	if (consp(vals)) {
	    xldbind(car(vars),car(vals));
	    vals = cdr(vals);
	}
	else
	    xldbind(car(vars),s_unbound);
    }

    /* evaluate each expression */
    for (val = NIL; moreargs(); )
	val = xleval(nextarg());

    /* restore the previous environment and the stack */
    xlunbind(olddenv);
    xlpopn(2);

    /* return the last test expression value */
    return (val);
}

/* xloop - special form 'loop' */
LVAL xloop(void)
{
    LVAL *argv,arg,val;
    CONTEXT cntxt;
    int argc;

    /* protect some pointers */
    xlsave1(arg);

    /* establish a new execution context */
    xlbegin(&cntxt,CF_RETURN,NIL);
    if (setjmp(cntxt.c_jmpbuf))
	val = xlvalue;
    else
	for (argv = xlargv, argc = xlargc; ; xlargv = argv, xlargc = argc)
	    while (moreargs()) {
		arg = nextarg();
		if (consp(arg))
		    xleval(arg);
	    }
    xlend(&cntxt);

    /* restore the stack */
    xlpop();

    /* return the result */
    return (val);
}

/* xdo - special form 'do' */
LVAL xdo(void)
{
    return (doloop(TRUE));
}

/* xdostar - special form 'do*' */
LVAL xdostar(void)
{
    return (doloop(FALSE));
}

/* doloop - common do routine */
static LVAL doloop(int pflag)
{
    LVAL newenv,*argv,blist,clist,test,val;
    CONTEXT cntxt;
    int argc;

    /* protect some pointers */
    xlsave1(newenv);

    /* get the list of bindings, the exit test and the result forms */
    blist = xlgalist();
    clist = xlgalist();
    test = (consp(clist) ? car(clist) : NIL);
    argv = xlargv;
    argc = xlargc;

    /* create a new environment frame */
    newenv = xlframe(xlenv);

    /* establish a new execution context */
    xlbegin(&cntxt,CF_RETURN,NIL);
    if (setjmp(cntxt.c_jmpbuf))
	val = xlvalue;
    else {

	/* bind the symbols */
	if (!pflag) xlenv = newenv;
	dobindings(blist,newenv);
	if (pflag) xlenv = newenv;

	/* execute the loop as long as the test is false */
	for (val = NIL; xleval(test) == NIL; doupdates(blist,pflag)) {
	    xlargv = argv;
	    xlargc = argc;
	    tagbody();
	}

	/* evaluate the result expression */
	if (consp(clist))
	    for (clist = cdr(clist); consp(clist); clist = cdr(clist))
		val = xleval(car(clist));

	/* unbind the arguments */
	xlenv = cdr(xlenv);
    }
    xlend(&cntxt);

    /* restore the stack */
    xlpop();

    /* return the result */
    return (val);
}

/* xdolist - special form 'dolist' */
LVAL xdolist(void)
{
    LVAL list,*argv,clist,sym,val;
    CONTEXT cntxt;
    int argc;

    /* protect some pointers */
    xlsave1(list);

    /* get the control list (sym list result-expr) */
    clist = xlgalist();
    sym = match(SYMBOL,&clist);
    list = evmatch(LIST,&clist);
    argv = xlargv;
    argc = xlargc;

    /* initialize the local environment */
    xlenv = xlframe(xlenv);
    xlbind(sym,NIL);

    /* establish a new execution context */
    xlbegin(&cntxt,CF_RETURN,NIL);
    if (setjmp(cntxt.c_jmpbuf))
	val = xlvalue;
    else {

	/* loop through the list */
	for (val = NIL; consp(list); list = cdr(list)) {

	    /* bind the symbol to the next list element */
	    xlsetvalue(sym,car(list));

	    /* execute the loop body */
	    xlargv = argv;
	    xlargc = argc;
	    tagbody();
	}

	/* evaluate the result expression */
	xlsetvalue(sym,NIL);
	val = (consp(clist) ? xleval(car(clist)) : NIL);

	/* unbind the arguments */
	xlenv = cdr(xlenv);
    }
    xlend(&cntxt);

    /* restore the stack */
    xlpop();

    /* return the result */
    return (val);
}

/* xdotimes - special form 'dotimes' */
LVAL xdotimes(void)
{
    LVAL *argv,clist,sym,cnt,val;
    CONTEXT cntxt;
    int argc;
    FIXTYPE n,i;

    /* get the control list (sym list result-expr) */
    clist = xlgalist();
    sym = match(SYMBOL,&clist);
    cnt = evmatch(FIXNUM,&clist); n = getfixnum(cnt);
    argv = xlargv;
    argc = xlargc;

    /* initialize the local environment */
    xlenv = xlframe(xlenv);
    xlbind(sym,NIL);

    /* establish a new execution context */
    xlbegin(&cntxt,CF_RETURN,NIL);
    if (setjmp(cntxt.c_jmpbuf))
	val = xlvalue;
    else {

	/* loop through for each value from zero to n-1 */
	for (val = NIL, i = 0; i < n; ++i) {

	    /* bind the symbol to the next list element */
	    xlsetvalue(sym,cvfixnum((FIXTYPE)i));

	    /* execute the loop body */
	    xlargv = argv;
	    xlargc = argc;
	    tagbody();
	}

	/* evaluate the result expression */
	xlsetvalue(sym,cnt);
	val = (consp(clist) ? xleval(car(clist)) : NIL);

	/* unbind the arguments */
	xlenv = cdr(xlenv);
    }
    xlend(&cntxt);

    /* return the result */
    return (val);
}

/* xblock - special form 'block' */
LVAL xblock(void)
{
    LVAL name,val;
    CONTEXT cntxt;

    /* get the block name */
    name = xlgetarg();
    if (name && !symbolp(name))
	xlbadtype(name);

    /* execute the block */
    xlbegin(&cntxt,CF_RETURN,name);
    if (setjmp(cntxt.c_jmpbuf))
	val = xlvalue;
    else
	for (val = NIL; moreargs(); )
	    val = xleval(nextarg());
    xlend(&cntxt);

    /* return the value of the last expression */
    return (val);
}

/* xtagbody - special form 'tagbody' */
LVAL xtagbody(void)
{
    tagbody();
    return (NIL);
}

/* xcatch - special form 'catch' */
LVAL xcatch(void)
{
    CONTEXT cntxt;
    LVAL tag,val;

    /* protect some pointers */
    xlsave1(tag);

    /* get the tag */
    tag = xleval(nextarg());

    /* establish an execution context */
    xlbegin(&cntxt,CF_THROW,tag);

    /* check for 'throw' */
    if (setjmp(cntxt.c_jmpbuf))
	val = xlvalue;

    /* otherwise, evaluate the remainder of the arguments */
    else {
	for (val = NIL; moreargs(); )
	    val = xleval(nextarg());
    }
    xlend(&cntxt);

    /* restore the stack */
    xlpop();

    /* return the result */
    return (val);
}

/* xthrow - special form 'throw' */
LVAL xthrow(void)
{
    LVAL tag,val;

    /* get the tag and value */
    tag = xleval(nextarg());
    val = (moreargs() ? xleval(nextarg()) : NIL);
    xllastarg();

    /* throw the tag */
    xlthrow(tag,val);
}

/* xunwindprotect - special form 'unwind-protect' */
LVAL xunwindprotect(void)
{
    extern CONTEXT *xltarget;
    extern int xlmask;
    CONTEXT cntxt,* volatile target = NULL;
    volatile int mask = 0;
    int sts;
    LVAL val;

    /* protect some pointers */
    xlsave1(val);

    /* get the expression to protect */
    val = xlgetarg();

    /* evaluate the protected expression */
    xlbegin(&cntxt,CF_UNWIND,NIL);
    if (sts = setjmp(cntxt.c_jmpbuf)) {
	target = xltarget;
	mask = xlmask;
	val = xlvalue;
    }
    else
	val = xleval(val);
    xlend(&cntxt);
	
    /* evaluate the cleanup expressions */
    while (moreargs())
	xleval(nextarg());

    /* if unwinding, continue unwinding */
    if (sts)
	xljump(target,mask,val);

    /* restore the stack */
    xlpop();

    /* return the value of the protected expression */
    return (val);
}

/* xerrset - special form 'errset' */
LVAL xerrset(void)
{
    LVAL expr,flag,val;
    CONTEXT cntxt;

    /* get the expression and the print flag */
    expr = xlgetarg();
    flag = (moreargs() ? xlgetarg() : s_true);
    xllastarg();

    /* establish an execution context */
    xlbegin(&cntxt,CF_ERROR,flag);

    /* check for error */
    if (setjmp(cntxt.c_jmpbuf))
	val = NIL;

    /* otherwise, evaluate the expression */
    else {
	expr = xleval(expr);
	val = consa(expr);
    }
    xlend(&cntxt);

    /* return the result */
    return (val);
}

/* xtrace - special form 'trace' */
LVAL xtrace(void)
{
    LVAL sym,fun,this;

    /* loop through all of the arguments */
    sym = xlenter("*TRACELIST*");
    while (moreargs()) {
	fun = xlgasymbol();

	/* check for the function name already being in the list */
	for (this = getvalue(sym); consp(this); this = cdr(this))
	    if (car(this) == fun)
		break;

	/* add the function name to the list */
	if (null(this))
	    setvalue(sym,cons(fun,getvalue(sym)));
    }
    return (getvalue(sym));
}

/* xuntrace - special form 'untrace' */
LVAL xuntrace(void)
{
    LVAL sym,fun,this,last;

    /* loop through all of the arguments */
    sym = xlenter("*TRACELIST*");
    while (moreargs()) {
	fun = xlgasymbol();

	/* remove the function name from the list */
	last = NIL;
	for (this = getvalue(sym); consp(this); this = cdr(this)) {
	    if (car(this) == fun) {
		if (last)
		    rplacd(last,cdr(this));
		else
		    setvalue(sym,cdr(this));
		break;
	    }
	    last = this;
	}
    }
    return (getvalue(sym));
}

/* dobindings - handle bindings for let/let*, prog/prog*, do/do* */
static void dobindings(LVAL list, LVAL env)
{
    LVAL bnd,sym,val;

    /* protect some pointers */
    xlsave1(val);

    /* bind each symbol in the list of bindings */
    for (; consp(list); list = cdr(list)) {

	/* get the next binding */
	bnd = car(list);

	/* handle a symbol */
	if (symbolp(bnd)) {
	    sym = bnd;
	    val = NIL;
	}

	/* handle a list of the form (symbol expr) */
	else if (consp(bnd)) {
	    sym = match(SYMBOL,&bnd);
	    val = evarg(&bnd);
	}
	else
	    xlfail("bad binding");

	/* bind the value to the symbol */
	xlpbind(sym,val,env);
    }

    /* restore the stack */
    xlpop();
}

/* doupdates - handle updates for do/do* */
static void doupdates(LVAL list, int pflag)
{
    LVAL plist,bnd,sym,val;

    /* protect some pointers */
    xlstkcheck(2);
    xlsave(plist);
    xlsave(val);

    /* bind each symbol in the list of bindings */
    for (; consp(list); list = cdr(list)) {

	/* get the next binding */
	bnd = car(list);

	/* handle a list of the form (symbol expr) */
	if (consp(bnd)) {
	    sym = match(SYMBOL,&bnd);
	    bnd = cdr(bnd);
	    if (bnd) {
		val = evarg(&bnd);
		if (pflag)
		    plist = cons(cons(sym,val),plist);
		else
		    xlsetvalue(sym,val);
	    }
	}
    }

    /* set the values for parallel updates */
    for (; plist; plist = cdr(plist))
	xlsetvalue(car(car(plist)),cdr(car(plist)));

    /* restore the stack */
    xlpopn(2);
}

/* tagbody - execute code within a block and tagbody */
static void tagbody(void)
{
    LVAL *argv,arg;
    CONTEXT cntxt;
    int argc;

    /* establish an execution context */
    xlbegin(&cntxt,CF_GO,NIL);
    argc = xlargc;
    argv = xlargv;

    /* check for a 'go' */
    if (setjmp(cntxt.c_jmpbuf)) {
	cntxt.c_xlargc = argc;
	cntxt.c_xlargv = argv;
    }

    /* execute the body */
    while (moreargs()) {
	arg = nextarg();
	if (consp(arg))
	    xleval(arg);
    }
    xlend(&cntxt);
}

/* match - get an argument and match its type */
static LVAL match(int type, LVAL *pargs)
{
    LVAL arg;

    /* make sure the argument exists */
    if (!consp(*pargs))
	toofew(*pargs);

    /* get the argument value */
    arg = car(*pargs);

    /* move the argument pointer ahead */
    *pargs = cdr(*pargs);

    /* check its type */
    if (type == LIST) {
	if (arg && ntype(arg) != CONS)
	    xlerror("bad argument type",arg);
    }
    else {
	if (arg == NIL || ntype(arg) != type)
	    xlerror("bad argument type",arg);
    }

    /* return the argument */
    return (arg);
}

/* evarg - get the next argument and evaluate it */
static LVAL evarg(LVAL *pargs)
{
    LVAL arg;

    /* protect some pointers */
    xlsave1(arg);

    /* make sure the argument exists */
    if (!consp(*pargs))
	toofew(*pargs);

    /* get the argument value */
    arg = car(*pargs);

    /* move the argument pointer ahead */
    *pargs = cdr(*pargs);

    /* evaluate the argument */
    arg = xleval(arg);

    /* restore the stack */
    xlpop();

    /* return the argument */
    return (arg);
}

/* evmatch - get an evaluated argument and match its type */
static LVAL evmatch(int type, LVAL *pargs)
{
    LVAL arg;

    /* protect some pointers */
    xlsave1(arg);

    /* make sure the argument exists */
    if (!consp(*pargs))
	toofew(*pargs);

    /* get the argument value */
    arg = car(*pargs);

    /* move the argument pointer ahead */
    *pargs = cdr(*pargs);

    /* evaluate the argument */
    arg = xleval(arg);

    /* check its type */
    if (type == LIST) {
	if (arg && ntype(arg) != CONS)
	    xlerror("bad argument type",arg);
    }
    else {
	if (arg == NIL || ntype(arg) != type)
	    xlerror("bad argument type",arg);
    }

    /* restore the stack */
    xlpop();

    /* return the argument */
    return (arg);
}

/* toofew - too few arguments */
static void toofew(LVAL args)
{
    xlerror("too few arguments",args);
}

/* toomany - too many arguments */
static void toomany(LVAL args)
{
    xlerror("too many arguments",args);
}

/* xldebug - xlisp debugging support */
/*	Copyright (c) 1985, by David Michael Betz
	All Rights Reserved
	Permission is granted for unrestricted non-commercial use	*/


/* external variables */
extern int xldebug;
extern int xlsample;
extern LVAL s_debugio,s_unbound;
extern LVAL s_tracenable,s_tlimit,s_breakenable;
extern LVAL s_true;
extern char buf[];

/* external routines */

/* forward declarations */

/* xlabort - xlisp serious error handler */
void xlabort(char *emsg)
{
    xlsignal(emsg,s_unbound);
    xlerrprint("error",NULL,emsg,s_unbound);
    xlbrklevel();
}

/* xlbreak - enter a break loop */
void xlbreak(char *emsg, LVAL arg)
{
    breakloop("break","return from BREAK",emsg,arg,TRUE);
}

/* xlfail - xlisp error handler */
void xlfail(char *emsg)
{
    xlerror(emsg,s_unbound);
}

/* xlerror - handle a fatal error */
void xlerror(char *emsg, LVAL arg)
{
    if (getvalue(s_breakenable) != NIL)
	breakloop("error",NULL,emsg,arg,FALSE);
    else {
	xlsignal(emsg,arg);
	xlerrprint("error",NULL,emsg,arg);
    }
    xlbrklevel();	/* (breakloop can't return when it can't be continued) */
}

/* xlcerror - handle a recoverable error */
void xlcerror(char *cmsg, char *emsg, LVAL arg)
{
    if (getvalue(s_breakenable) != NIL)
	breakloop("error",cmsg,emsg,arg,TRUE);
    else {
	xlsignal(emsg,arg);
	xlerrprint("error",NULL,emsg,arg);
	xlbrklevel();
    }
}

/* xlerrprint - print an error message */
void xlerrprint(char *hdr, char *cmsg, char *emsg, LVAL arg)
{
    /* print the error message (written in pieces: the message can be
       any length, so it must not be formatted into the fixed buf[]) */
    errputstr(hdr);
    errputstr(": ");
    errputstr(emsg);

    /* print the argument */
    if (arg != s_unbound) {
	errputstr(" - ");
	errprint(arg);
    }

    /* no argument, just end the line */
    else
	errputstr("\n");

    /* print the continuation message */
    if (cmsg) {
	errputstr("if continued: ");
	errputstr(cmsg);
	errputstr("\n");
    }
}

/* breakloop - the debug read-eval-print loop */
static void breakloop(char *hdr, char *cmsg, char *emsg, LVAL arg, int cflag)
{
    LVAL expr,val;
    CONTEXT cntxt;
    int type;

    /* print the error message */
    xlerrprint(hdr,cmsg,emsg,arg);

    /* flush the input buffer */
    xlflush();

    /* do the back trace */
    if (getvalue(s_tracenable)) {
	val = getvalue(s_tlimit);
	xlbaktrace(fixp(val) ? xlfixint(val) : -1);
    }

    /* protect some pointers */
    xlsave1(expr);

    /* increment the debug level */
    ++xldebug;

    /* debug command processing loop */
    xlbegin(&cntxt,CF_BRKLEVEL|CF_CLEANUP|CF_CONTINUE,s_true);
    for (type = 0; type == 0; ) {

	/* setup the continue trap */
	if (type = setjmp(cntxt.c_jmpbuf))
	    switch (type) {
	    case CF_CLEANUP:
		continue;
	    case CF_BRKLEVEL:
		type = 0;
		break;
	    case CF_CONTINUE:
		if (cflag) {
		    dbgputstr("[ continue from break loop ]\n");
		    continue;
		}
		else xlabort("this error can't be continued");
	    }

	/* print a prompt */
	snprintf(buf,BUFSIZE,"%d> ",xldebug);
	dbgputstr(buf);

	/* read an expression and check for eof */
	if (!xlread(getvalue(s_debugio),&expr,FALSE)) {
	    type = CF_CLEANUP;
	    break;
	}

	/* save the input expression */
	xlrdsave(expr);

	/* evaluate the expression */
	expr = xleval(expr);

	/* save the result */
	xlevsave(expr);

	/* print it */
	dbgprint(expr);
    }
    xlend(&cntxt);

    /* decrement the debug level */
    --xldebug;

    /* restore the stack */
    xlpop();

    /* check for aborting to the previous level */
    if (type == CF_CLEANUP)
	xlbrklevel();
}

/* baktrace - do a back trace */
void xlbaktrace(int n)
{
    LVAL *fp,*p;
    int argc;
    for (fp = xlfp; (n < 0 || n--) && *fp; fp = fp - (int)getfixnum(*fp)) {
	p = fp + 1;
	errputstr("Function: ");
	errprint(*p++);
	if (argc = (int)getfixnum(*p++))
	    errputstr("Arguments:\n");
	while (--argc >= 0) {
	    errputstr("  ");
	    errprint(*p++);
	}
    }
}

/* xldinit - debug initialization routine */
void xldinit(void)
{
    xlsample = 0;
    xldebug = 0;
}

/* xldmem - xlisp dynamic memory management routines */
/*	Copyright (c) 1985, by David Michael Betz
	All Rights Reserved
	Permission is granted for unrestricted non-commercial use	*/


/* node flags */
#define MARK	1
#define LEFT	2

/* macro to compute the size of a segment */
#define segsize(n) (sizeof(SEGMENT)+((n)-1)*sizeof(struct node))

/* external variables */
extern LVAL obarray,s_gcflag,s_gchook,s_unbound,s_true;
extern LVAL xlenv,xlfenv,xldenv,xlvalue;
extern CONTEXT *xltarget;
extern int xlmask;
extern char buf[];

/* variables local to xldmem.c and xlimage.c */
SEGMENT *segs,*lastseg,*fixseg,*charseg;
int anodes,nsegs,gccalls;
long nnodes,nfree,total;
LVAL fnodes;

/* external procedures */

/* forward declarations */

/*
 * GC stress testing (debug aid).  Build with /DXL_GCSTRESS=n to force a
 * full collection on every n-th node allocation.  A C local holding an
 * LVAL that was not protected with xlsave/xlprotect is then freed
 * almost at once, so missing GC protection shows up in testing instead
 * of as a rare crash in the field.  Leave it undefined for normal use.
 */
#ifdef XL_GCSTRESS
static long gcstresscnt = 0;
#define GCSTRESS()	(++gcstresscnt % (XL_GCSTRESS) == 0)
#else
#define GCSTRESS()	0
#endif

/* cons - construct a new cons node */
LVAL cons(LVAL x, LVAL y)
{
    LVAL nnode;

    /* get a free node */
    if ((nnode = fnodes) == NIL || GCSTRESS()) {
	xlstkcheck(2);
	xlprotect(x);
	xlprotect(y);
	findmem();
	if ((nnode = fnodes) == NIL)
	    xlabort("insufficient node space");
	xlpop();
	xlpop();
    }

    /* unlink the node from the free list */
    fnodes = cdr(nnode);
    --nfree;

    /* initialize the new node */
    nnode->n_type = CONS;
    rplaca(nnode,x);
    rplacd(nnode,y);

    /* return the new node */
    return (nnode);
}

/* cvstring - convert a string to a string node */
LVAL cvstring(char *str)
{
    LVAL val;
    xlsave1(val);
    val = newnode(STRING);
    val->n_strlen = (int)strlen(str) + 1;
    val->n_string = stralloc(getslength(val));
    strcpy(getstring(val),str);
    xlpop();
    return (val);
}

/* newstring - allocate and initialize a new string */
LVAL newstring(int size)
{
    LVAL val;
    xlsave1(val);
    val = newnode(STRING);
    val->n_strlen = size;
    val->n_string = stralloc(getslength(val));
    strcpy(getstring(val),"");
    xlpop();
    return (val);
}

/* cvsymbol - convert a string to a symbol */
LVAL cvsymbol(char *pname)
{
    LVAL val;
    xlsave1(val);
    val = newvector(SYMSIZE);
    val->n_type = SYMBOL;
    setvalue(val,s_unbound);
    setfunction(val,s_unbound);
    setpname(val,cvstring(pname));
    xlpop();
    return (val);
}

/* cvsubr - convert a function to a subr or fsubr */
LVAL cvsubr(LVAL (*fcn)(void), int type, int offset)
{
    LVAL val;
    val = newnode(type);
    val->n_subr = fcn;
    val->n_offset = offset;
    return (val);
}

/* cvfile - convert a file pointer to a stream */
LVAL cvfile(FILE *fp)
{
    LVAL val;
    val = newnode(STREAM);
    setfile(val,fp);
    setsavech(val,'\0');
    return (val);
}

/* cvfixnum - convert an integer to a fixnum node */
LVAL cvfixnum(FIXTYPE n)
{
    LVAL val;
    if (n >= SFIXMIN && n <= SFIXMAX)
	return (&fixseg->sg_nodes[(int)n-SFIXMIN]);
    val = newnode(FIXNUM);
    val->n_fixnum = n;
    return (val);
}

/* cvflonum - convert a floating point number to a flonum node */
LVAL cvflonum(FLOTYPE n)
{
    LVAL val;
    val = newnode(FLONUM);
    val->n_flonum = n;
    return (val);
}

/* cvchar - convert an integer to a character node */
LVAL cvchar(int n)
{
    if (n >= CHARMIN && n <= CHARMAX)
	return (&charseg->sg_nodes[n-CHARMIN]);
    xlerror("character code out of range",cvfixnum((FIXTYPE)n));
}

/* newustream - create a new unnamed stream */
LVAL newustream(void)
{
    LVAL val;
    val = newnode(USTREAM);
    sethead(val,NIL);
    settail(val,NIL);
    return (val);
}

/* newobject - allocate and initialize a new object */
LVAL newobject(LVAL cls, int size)
{
    LVAL val;
    val = newvector(size+1);
    val->n_type = OBJECT;
    setelement(val,0,cls);
    return (val);
}

/* newclosure - allocate and initialize a new closure */
LVAL newclosure(LVAL name, LVAL type, LVAL env, LVAL fenv)
{
    LVAL val;
    val = newvector(CLOSIZE);
    val->n_type = CLOSURE;
    setname(val,name);
    settype(val,type);
    setcenv(val,env);
    setfenv(val,fenv);
    return (val);
}

/* newvector - allocate and initialize a new vector node */
LVAL newvector(int size)
{
    LVAL vect;
    int bsize;
    xlsave1(vect);
    vect = newnode(VECTOR);
    vect->n_vsize = 0;
    if (bsize = size * sizeof(LVAL)) {
	if ((vect->n_vdata = (LVAL *)calloc(1,bsize)) == NULL) {
	    findmem();
	    if ((vect->n_vdata = (LVAL *)calloc(1,bsize)) == NULL)
		xlfail("insufficient vector space");
	}
	vect->n_vsize = size;
	total += (long) bsize;
    }
    xlpop();
    return (vect);
}

/* newnode - allocate a new node */
static LVAL newnode(int type)
{
    LVAL nnode;

    /* get a free node */
    if ((nnode = fnodes) == NIL || GCSTRESS()) {
	findmem();
	if ((nnode = fnodes) == NIL)
	    xlabort("insufficient node space");
    }

    /* unlink the node from the free list */
    fnodes = cdr(nnode);
    nfree -= 1L;

    /* initialize the new node */
    nnode->n_type = type;
    rplacd(nnode,NIL);

    /* return the new node */
    return (nnode);
}

/* stralloc - allocate memory for a string adding a byte for the terminator */
static unsigned char *stralloc(int size)
{
    unsigned char *sptr;

    /* allocate memory for the string copy */
    if ((sptr = (unsigned char *)malloc(size)) == NULL) {
	gc();  
	if ((sptr = (unsigned char *)malloc(size)) == NULL)
	    xlfail("insufficient string space");
    }
    total += (long)size;

    /* return the new string memory */
    return (sptr);
}

/* findmem - find more memory by collecting then expanding */
static void findmem(void)
{
    gc();
    if (nfree < (long)anodes)
	addseg();
}

/* gc - garbage collect (only called here and in xlimage.c) */
void gc(void)
{
    register LVAL **p,*ap,tmp;
    char buf[STRMAX+1];
    LVAL *newfp,fun;

    static int inhook = FALSE;	/* TRUE while *GC-HOOK* is running */

    /* print the start of the gc message (an unbound flag counts as NIL:
       it is unbound during initialization) */
    if (s_gcflag && getvalue(s_gcflag) && getvalue(s_gcflag) != s_unbound) {
	snprintf(buf,sizeof(buf),"[ gc: total %ld, ",nnodes);
	stdputstr(buf);
    }

    /* mark the obarray, the argument list and the current environment */
    if (obarray)
	mark(obarray);
    if (xlenv)
	mark(xlenv);
    if (xlfenv)
	mark(xlfenv);
    if (xldenv)
	mark(xldenv);

    /* mark the evaluation stack */
    for (p = xlstack; p < xlstktop; ++p)
	if (tmp = **p)
	    mark(tmp);

    /* mark the argument stack */
    for (ap = xlargstkbase; ap < xlsp; ++ap)
	if (tmp = *ap)
	    mark(tmp);

    /* sweep memory collecting all unmarked nodes */
    sweep();

    /* count the gc call */
    ++gccalls;

    /* call the *gc-hook* if necessary (not when it is unbound, and not
       again for a collection caused by the hook itself) */
    if (!inhook && s_gchook && (fun = getvalue(s_gchook)) && fun != s_unbound) {
	CONTEXT cntxt;
	inhook = TRUE;
	xlbegin(&cntxt,CF_UNWIND,NIL);	/* clear inhook even if the hook exits */
	if (setjmp(cntxt.c_jmpbuf) == 0) {
	    newfp = xlsp;
	    pusharg(cvfixnum((FIXTYPE)(newfp - xlfp)));
	    pusharg(fun);
	    pusharg(cvfixnum((FIXTYPE)2));
	    pusharg(cvfixnum((FIXTYPE)nnodes));
	    pusharg(cvfixnum((FIXTYPE)nfree));
	    xlfp = newfp;
	    xlapply(2);
	    xlend(&cntxt);
	    inhook = FALSE;
	}
	else {
	    xlend(&cntxt);
	    inhook = FALSE;
	    xljump(xltarget,xlmask,xlvalue);	/* continue the non-local exit */
	}
    }

    /* print the end of the gc message */
    if (s_gcflag && getvalue(s_gcflag) && getvalue(s_gcflag) != s_unbound) {
	snprintf(buf,sizeof(buf),"%ld free ]\n",nfree);
	stdputstr(buf);
    }
}

/* mark - mark all accessible nodes */
static void mark(LVAL ptr)
{
    register LVAL this,prev,tmp;
    int type,i,n;

    /* initialize */
    prev = NIL;
    this = ptr;

    /* mark this list */
    for (;;) {

	/* descend as far as we can */
	while (!(this->n_flags & MARK))

	    /* check cons and unnamed stream nodes (a USTREAM keeps its
	       character list in its car/cdr just like a cons) */
	    if ((type = ntype(this)) == CONS || type == USTREAM) {
		if (tmp = car(this)) {
		    this->n_flags |= MARK|LEFT;
		    rplaca(this,prev);
		}
		else if (tmp = cdr(this)) {
		    this->n_flags |= MARK;
		    rplacd(this,prev);
		}
		else {				/* both sides nil */
		    this->n_flags |= MARK;
		    break;
		}
		prev = this;			/* step down the branch */
		this = tmp;
	    }

	    /* mark other node types */
	    else {
		this->n_flags |= MARK;
		switch (type) {
		case SYMBOL:
		case OBJECT:
		case VECTOR:
		case CLOSURE:
		    for (i = 0, n = getsize(this); --n >= 0; ++i)
			if (tmp = getelement(this,i))
			    mark(tmp);
		    break;
		}
		break;
	    }

	/* backup to a point where we can continue descending */
	for (;;)

	    /* make sure there is a previous node */
	    if (prev) {
		if (prev->n_flags & LEFT) {	/* came from left side */
		    prev->n_flags &= ~LEFT;
		    tmp = car(prev);
		    rplaca(prev,this);
		    if (this = cdr(prev)) {
			rplacd(prev,tmp);			
			break;
		    }
		}
		else {				/* came from right side */
		    tmp = cdr(prev);
		    rplacd(prev,this);
		}
		this = prev;			/* step back up the branch */
		prev = tmp;
	    }

	    /* no previous node, must be done */
	    else
		return;
    }
}

/* sweep - sweep all unmarked nodes and add them to the free list */
static void sweep(void)
{
    SEGMENT *seg;
    LVAL p;
    int n;

    /* empty the free list */
    fnodes = NIL;
    nfree = 0L;

    /* add all unmarked nodes */
    for (seg = segs; seg; seg = seg->sg_next) {
	if (seg == fixseg)	 /* don't sweep the fixnum segment */
	    continue;
	else if (seg == charseg) /* don't sweep the character segment */
	    continue;
	p = &seg->sg_nodes[0];
	for (n = seg->sg_size; --n >= 0; ++p)
	    if (!(p->n_flags & MARK)) {
		switch (ntype(p)) {
		case STRING:
			if (getstring(p) != NULL) {
			    total -= (long)getslength(p);
			    free(getstring(p));
			}
			break;
		case STREAM:
			if (getfile(p))
			    osclose(getfile(p));
			break;
		case SYMBOL:
		case OBJECT:
		case VECTOR:
		case CLOSURE:
			if (p->n_vsize) {
			    total -= (long) (p->n_vsize * sizeof(LVAL));
			    free(p->n_vdata);
			}
			break;
		}
		p->n_type = FREE;
		rplaca(p,NIL);
		rplacd(p,fnodes);
		fnodes = p;
		nfree += 1L;
	    }
	    else
		p->n_flags &= ~MARK;
    }
}

/* addseg - add a segment to the available memory */
static int addseg(void)
{
    SEGMENT *newseg;
    LVAL p;
    int n;

    /* allocate the new segment */
    if (anodes == 0 || (newseg = newsegment(anodes)) == NULL)
	return (FALSE);

    /* add each new node to the free list */
    p = &newseg->sg_nodes[0];
    for (n = anodes; --n >= 0; ++p) {
	rplacd(p,fnodes);
	fnodes = p;
    }

    /* return successfully */
    return (TRUE);
}

/* newsegment - create a new segment (only called here and in xlimage.c) */
SEGMENT *newsegment(int n)
{
    SEGMENT *newseg;

    /* allocate the new segment */
    if ((newseg = (SEGMENT *)calloc(1,segsize(n))) == NULL)
	return (NULL);

    /* initialize the new segment */
    newseg->sg_size = n;
    newseg->sg_next = NULL;
    if (segs)
	lastseg->sg_next = newseg;
    else
	segs = newseg;
    lastseg = newseg;

    /* update the statistics */
    total += (long)segsize(n);
    nnodes += (long)n;
    nfree += (long)n;
    ++nsegs;

    /* return the new segment */
    return (newseg);
}
 
/* stats - print memory statistics */
static void stats(void)
{
    snprintf(buf,BUFSIZE,"Nodes:       %ld\n",nnodes); stdputstr(buf);
    snprintf(buf,BUFSIZE,"Free nodes:  %ld\n",nfree);  stdputstr(buf);
    snprintf(buf,BUFSIZE,"Segments:    %d\n",nsegs);   stdputstr(buf);
    snprintf(buf,BUFSIZE,"Allocate:    %d\n",anodes);  stdputstr(buf);
    snprintf(buf,BUFSIZE,"Total:       %ld\n",total);  stdputstr(buf);
    snprintf(buf,BUFSIZE,"Collections: %d\n",gccalls); stdputstr(buf);
}

/* xgc - xlisp function to force garbage collection */
LVAL xgc(void)
{
    /* make sure there aren't any arguments */
    xllastarg();

    /* garbage collect */
    gc();

    /* return nil */
    return (NIL);
}

/* xexpand - xlisp function to force memory expansion */
LVAL xexpand(void)
{
    LVAL num;
    int n,i;

    /* get the new number to allocate */
    if (moreargs()) {
	num = xlgafixnum();
	n = xlfixint(num);
    }
    else
	n = 1;
    xllastarg();

    /* allocate more segments */
    for (i = 0; i < n; i++)
	if (!addseg())
	    break;

    /* return the number of segments added */
    return (cvfixnum((FIXTYPE)i));
}

/* xalloc - xlisp function to set the number of nodes to allocate */
LVAL xalloc(void)
{
    int n,oldn;
    LVAL num;

    /* get the new number to allocate */
    num = xlgafixnum();
    n = xlfixint(num);

    /* make sure there aren't any more arguments */
    xllastarg();
    if (n < 0 || n > 10000000)
	xlerror("allocation size must be 0 to 10000000 nodes",num);

    /* set the new number of nodes to allocate */
    oldn = anodes;
    anodes = n;

    /* return the old number */
    return (cvfixnum((FIXTYPE)oldn));
}

/* xmem - xlisp function to print memory statistics */
LVAL xmem(void)
{
    /* allow one argument for compatiblity with common lisp */
    if (moreargs()) xlgetarg();
    xllastarg();

    /* print the statistics */
    stats();

    /* return nil */
    return (NIL);
}

#ifdef SAVERESTORE
/* xsave - save the memory image */
LVAL xsave(void)
{
    unsigned char *name;

    /* get the file name, verbose flag and print flag */
    name = getstring(xlgetfname());
    xllastarg();

    /* save the memory image */
    return (xlisave(name) ? s_true : NIL);
}

/* xrestore - restore a saved memory image */
LVAL xrestore(void)
{
    extern jmp_buf top_level;
    unsigned char *name;

    /* get the file name, verbose flag and print flag */
    name = getstring(xlgetfname());
    xllastarg();

    /* restore the saved memory image */
    if (!xlirestore(name))
	return (NIL);

    /* return directly to the top level */
    stdputstr("[ returning to the top level ]\n");
    longjmp(top_level,1);
}
#endif

/* xlminit - initialize the dynamic memory module */
void xlminit(void)
{
    LVAL p;
    int i;

    /* initialize our internal variables */
    segs = lastseg = NULL;
    nnodes = nfree = total = 0L;
    nsegs = gccalls = 0;
    anodes = NNODES;
    fnodes = NIL;

    /* allocate the fixnum segment */
    if ((fixseg = newsegment(SFIXSIZE)) == NULL)
	xlfatal("insufficient memory");

    /* initialize the fixnum segment */
    p = &fixseg->sg_nodes[0];
    for (i = SFIXMIN; i <= SFIXMAX; ++i) {
	p->n_type = FIXNUM;
	p->n_fixnum = i;
	++p;
    }

    /* allocate the character segment */
    if ((charseg = newsegment(CHARSIZE)) == NULL)
	xlfatal("insufficient memory");

    /* initialize the character segment */
    p = &charseg->sg_nodes[0];
    for (i = CHARMIN; i <= CHARMAX; ++i) {
	p->n_type = CHAR;
	p->n_chcode = i;
	++p;
    }

    /* initialize structures that are marked by the collector */
    obarray = xlenv = xlfenv = xldenv = NIL;
    s_gcflag = s_gchook = NIL;

    /* allocate the evaluation stack */
    if ((xlstkbase = (LVAL **)malloc(EDEPTH * sizeof(LVAL *))) == NULL)
	xlfatal("insufficient memory");
    xlstack = xlstktop = xlstkbase + EDEPTH;

    /* allocate the argument stack */
    if ((xlargstkbase = (LVAL *)malloc(ADEPTH * sizeof(LVAL))) == NULL)
	xlfatal("insufficient memory");
    xlargstktop = xlargstkbase + ADEPTH;
    xlfp = xlsp = xlargstkbase;
    *xlsp++ = NIL;
}

/* xleval - xlisp evaluator */
/*	Copyright (c) 1985, by David Michael Betz
	All Rights Reserved
	Permission is granted for unrestricted non-commercial use	*/


/* macro to check for lambda list keywords */
#define iskey(s) ((s) == lk_optional \
               || (s) == lk_rest \
               || (s) == lk_key \
               || (s) == lk_aux \
               || (s) == lk_allow_other_keys)

/* macros to handle tracing */
#define trenter(sym,argc,argv) {if (sym) doenter(sym,argc,argv);}
#define trexit(sym,val) {if (sym) doexit(sym,val);}

/* external variables */
extern LVAL xlenv,xlfenv,xldenv,xlvalue,s_true;
extern LVAL lk_optional,lk_rest,lk_key,lk_aux,lk_allow_other_keys;
extern LVAL s_evalhook,s_applyhook,s_tracelist;
extern LVAL s_lambda,s_macro;
extern LVAL s_unbound;
extern int xlsample;
extern char buf[];

/* forward declarations */

/* xleval - evaluate an xlisp expression (checking for *evalhook*) */
LVAL xleval(LVAL expr)
{
    /* check for control codes */
    if (--xlsample <= 0) {
	xlsample = SAMPLE;
	oscheck();
    }

    /* check for *evalhook* */
    if (getvalue(s_evalhook))
	return (evalhook(expr));

    /* check for nil */
    if (null(expr))
	return (NIL);

    /* dispatch on the node type */
    switch (ntype(expr)) {
    case CONS:
	return (evform(expr));
    case SYMBOL:
	return (xlgetvalue(expr));
    default:
	return (expr);
    }
}

/* xlxeval - evaluate an xlisp expression (bypassing *evalhook*) */
LVAL xlxeval(LVAL expr)
{
    /* check for nil */
    if (null(expr))
	return (NIL);

    /* dispatch on node type */
    switch (ntype(expr)) {
    case CONS:
	return (evform(expr));
    case SYMBOL:
	return (xlgetvalue(expr));
    default:
	return (expr);
    }
}

/* xlapply - apply a function to arguments (already on the stack) */
LVAL xlapply(int argc)
{
    LVAL *oldargv,fun,val;
    int oldargc;
    
    /* get the function */
    fun = xlfp[1];

    /* get the functional value of symbols */
    if (symbolp(fun)) {
	while ((val = getfunction(fun)) == s_unbound)
	    xlfunbound(fun);
	fun = xlfp[1] = val;
    }

    /* check for nil */
    if (null(fun))
	xlerror("bad function",fun);

    /* dispatch on node type */
    switch (ntype(fun)) {
    case SUBR:
	oldargc = xlargc;
	oldargv = xlargv;
	xlargc = argc;
	xlargv = xlfp + 3;
	val = (*getsubr(fun))();
	xlargc = oldargc;
	xlargv = oldargv;
	break;
    case CONS:
	if (!consp(cdr(fun)))
	    xlerror("bad function",fun);
	if (car(fun) == s_lambda)
	    fun = xlclose(NIL,
	                  s_lambda,
	                  car(cdr(fun)),
	                  cdr(cdr(fun)),
	                  xlenv,xlfenv);
	else
	    xlerror("bad function",fun);
	/**** fall through into the next case ****/
    case CLOSURE:
	if (gettype(fun) != s_lambda)
	    xlerror("bad function",fun);
	val = evfun(fun,argc,xlfp+3);
	break;
    default:
	xlerror("bad function",fun);
    }

    /* remove the call frame */
    xlsp = xlfp;
    xlfp = xlfp - (int)getfixnum(*xlfp);

    /* return the function value */
    return (val);
}

/* evform - evaluate a form */
static LVAL evform(LVAL form)
{
    LVAL fun,args,val,type;
    LVAL tracing=NIL;
    LVAL *argv;
    int argc;

    /* protect some pointers */
    xlstkcheck(2);
    xlsave(fun);
    xlsave(args);

    /* get the function and the argument list */
    fun = car(form);
    args = cdr(form);

    /* get the functional value of symbols */
    if (symbolp(fun)) {
	if (getvalue(s_tracelist) && member(fun,getvalue(s_tracelist)))
	    tracing = fun;
	fun = xlgetfunction(fun);
    }

    /* check for nil */
    if (null(fun))
	xlerror("bad function",NIL);

    /* dispatch on node type */
    switch (ntype(fun)) {
    case SUBR:
	argv = xlargv;
	argc = xlargc;
	xlargc = evpushargs(fun,args);
	xlargv = xlfp + 3;
	trenter(tracing,xlargc,xlargv);
	val = (*getsubr(fun))();
	trexit(tracing,val);
	xlsp = xlfp;
	xlfp = xlfp - (int)getfixnum(*xlfp);
	xlargv = argv;
	xlargc = argc;
	break;
    case FSUBR:
	argv = xlargv;
	argc = xlargc;
	xlargc = pushargs(fun,args);
	xlargv = xlfp + 3;
	val = (*getsubr(fun))();
	xlsp = xlfp;
	xlfp = xlfp - (int)getfixnum(*xlfp);
	xlargv = argv;
	xlargc = argc;
	break;
    case CONS:
	if (!consp(cdr(fun)))
	    xlerror("bad function",fun);
	if ((type = car(fun)) == s_lambda)
 	    fun = xlclose(NIL,
 	                  s_lambda,
 	                  car(cdr(fun)),
 	                  cdr(cdr(fun)),
 	                  xlenv,xlfenv);
	else
	    xlerror("bad function",fun);
	/**** fall through into the next case ****/
    case CLOSURE:
	if (gettype(fun) == s_lambda) {
	    argc = evpushargs(fun,args);
	    argv = xlfp + 3;
	    trenter(tracing,argc,argv);
	    val = evfun(fun,argc,argv);
	    trexit(tracing,val);
	    xlsp = xlfp;
	    xlfp = xlfp - (int)getfixnum(*xlfp);
	}
	else {
	    macroexpand(fun,args,&fun);
	    val = xleval(fun);
	}
	break;
    default:
	xlerror("bad function",fun);
    }

    /* restore the stack */
    xlpopn(2);

    /* return the result value */
    return (val);
}

/* xlexpandmacros - expand macros in a form */
LVAL xlexpandmacros(LVAL form)
{
    LVAL fun,args;
    
    /* protect some pointers */
    xlstkcheck(3);
    xlprotect(form);
    xlsave(fun);
    xlsave(args);

    /* expand until the form isn't a macro call */
    while (consp(form)) {
	fun = car(form);		/* get the macro name */
	args = cdr(form);		/* get the arguments */
	if (!symbolp(fun) || !fboundp(fun))
	    break;
	fun = xlgetfunction(fun);	/* get the expansion function */
	if (!macroexpand(fun,args,&form))
	    break;
    }

    /* restore the stack and return the expansion */
    xlpopn(3);
    return (form);
}

/* macroexpand - expand a macro call */
int macroexpand(LVAL fun, LVAL args, LVAL *pval)
{
    LVAL *argv;
    int argc;
    
    /* make sure it's really a macro call */
    if (!closurep(fun) || gettype(fun) != s_macro)
	return (FALSE);
	
    /* call the expansion function */
    argc = pushargs(fun,args);
    argv = xlfp + 3;
    *pval = evfun(fun,argc,argv);
    xlsp = xlfp;
    xlfp = xlfp - (int)getfixnum(*xlfp);
    return (TRUE);
}

/* evalhook - call the evalhook function */
static LVAL evalhook(LVAL expr)
{
    LVAL *newfp,olddenv,val;

    /* create the new call frame */
    newfp = xlsp;
    pusharg(cvfixnum((FIXTYPE)(newfp - xlfp)));
    pusharg(getvalue(s_evalhook));
    pusharg(cvfixnum((FIXTYPE)2));
    pusharg(expr);
    pusharg(cons(xlenv,xlfenv));
    xlfp = newfp;

    /* rebind the hook functions to nil */
    olddenv = xldenv;
    xldbind(s_evalhook,NIL);
    xldbind(s_applyhook,NIL);

    /* call the hook function */
    val = xlapply(2);

    /* unbind the symbols */
    xlunbind(olddenv);

    /* return the value */
    return (val);
}

/* evpushargs - evaluate and push a list of arguments */
static int evpushargs(LVAL fun, LVAL args)
{
    LVAL *newfp;
    int argc;
    
    /* protect the argument list */
    xlprot1(args);

    /* build a new argument stack frame */
    newfp = xlsp;
    pusharg(cvfixnum((FIXTYPE)(newfp - xlfp)));
    pusharg(fun);
    pusharg(NIL); /* will be argc */

    /* evaluate and push each argument */
    for (argc = 0; consp(args); args = cdr(args), ++argc)
	pusharg(xleval(car(args)));

    /* establish the new stack frame */
    newfp[2] = cvfixnum((FIXTYPE)argc);
    xlfp = newfp;
    
    /* restore the stack */
    xlpop();

    /* return the number of arguments */
    return (argc);
}

/* pushargs - push a list of arguments */
int pushargs(LVAL fun, LVAL args)
{
    LVAL *newfp;
    int argc;
    
    /* build a new argument stack frame */
    newfp = xlsp;
    pusharg(cvfixnum((FIXTYPE)(newfp - xlfp)));
    pusharg(fun);
    pusharg(NIL); /* will be argc */

    /* push each argument */
    for (argc = 0; consp(args); args = cdr(args), ++argc)
	pusharg(car(args));

    /* establish the new stack frame */
    newfp[2] = cvfixnum((FIXTYPE)argc);
    xlfp = newfp;

    /* return the number of arguments */
    return (argc);
}

/* makearglist - make a list of the remaining arguments */
LVAL makearglist(int argc, LVAL *argv)
{
    LVAL list,this,last;
    xlsave1(list);
    for (last = NIL; --argc >= 0; last = this) {
	this = cons(*argv++,NIL);
	if (last) rplacd(last,this);
	else list = this;
	last = this;
    }
    xlpop();
    return (list);
}

/* evfun - evaluate a function */
static LVAL evfun(LVAL fun, int argc, LVAL *argv)
{
    LVAL oldenv,oldfenv,cptr,name,val;
    CONTEXT cntxt;

    /* protect some pointers */
    xlstkcheck(3);
    xlsave(oldenv);
    xlsave(oldfenv);
    xlsave(cptr);

    /* create a new environment frame */
    oldenv = xlenv;
    oldfenv = xlfenv;
    xlenv = xlframe(getcenv(fun));
    xlfenv = getfenv(fun);

    /* bind the formal parameters */
    xlabind(fun,argc,argv);

    /* setup the implicit block */
    if (name = getname(fun))
	xlbegin(&cntxt,CF_RETURN,name);

    /* execute the block */
    if (name && setjmp(cntxt.c_jmpbuf))
	val = xlvalue;
    else
	for (val = NIL, cptr = getbody(fun); consp(cptr); cptr = cdr(cptr))
	    val = xleval(car(cptr));

    /* finish the block context */
    if (name)
	xlend(&cntxt);

    /* restore the environment */
    xlenv = oldenv;
    xlfenv = oldfenv;

    /* restore the stack */
    xlpopn(3);

    /* return the result value */
    return (val);
}

/* xlclose - create a function closure */
LVAL xlclose(LVAL name, LVAL type, LVAL fargs, LVAL body, LVAL env, LVAL fenv)
{
    LVAL closure,key,arg,def,svar,new,last;

    /* protect some pointers */
    xlsave1(closure);

    /* create the closure object */
    closure = newclosure(name,type,env,fenv);
    setlambda(closure,fargs);
    setbody(closure,body);

    /* handle each required argument */
    last = NIL;
    while (consp(fargs) && (arg = car(fargs)) && !iskey(arg)) {

	/* make sure the argument is a symbol */
	if (!symbolp(arg))
	    badarglist();

	/* create a new argument list entry */
	new = cons(arg,NIL);

	/* link it into the required argument list */
	if (last)
	    rplacd(last,new);
	else
	    setargs(closure,new);
	last = new;

	/* move the formal argument list pointer ahead */
	fargs = cdr(fargs);
    }

    /* check for the '&optional' keyword */
    if (consp(fargs) && car(fargs) == lk_optional) {
	fargs = cdr(fargs);

	/* handle each optional argument */
	last = NIL;
	while (consp(fargs) && (arg = car(fargs)) && !iskey(arg)) {

	    /* get the default expression and specified-p variable */
	    def = svar = NIL;
	    if (consp(arg)) {
		if (def = cdr(arg))
		    if (consp(def)) {
			if (svar = cdr(def))
			    if (consp(svar)) {
				svar = car(svar);
				if (!symbolp(svar))
				    badarglist();
			    }
			    else
				badarglist();
			def = car(def);
		    }
		    else
			badarglist();
		arg = car(arg);
	    }

	    /* make sure the argument is a symbol */
	    if (!symbolp(arg))
		badarglist();

	    /* create a fully expanded optional expression */
	    new = cons(cons(arg,cons(def,cons(svar,NIL))),NIL);

	    /* link it into the optional argument list */
	    if (last)
		rplacd(last,new);
	    else
		setoargs(closure,new);
	    last = new;
		
	    /* move the formal argument list pointer ahead */
	    fargs = cdr(fargs);
	}
    }

    /* check for the '&rest' keyword */
    if (consp(fargs) && car(fargs) == lk_rest) {
	fargs = cdr(fargs);

	/* get the &rest argument */
	if (consp(fargs) && (arg = car(fargs)) && !iskey(arg) && symbolp(arg))
	    setrest(closure,arg);
	else
	    badarglist();

	/* move the formal argument list pointer ahead */
	fargs = cdr(fargs);
    }

    /* check for the '&key' keyword */
    if (consp(fargs) && car(fargs) == lk_key) {
	fargs = cdr(fargs);

 	/* handle each key argument */
	last = NIL;
	while (consp(fargs) && (arg = car(fargs)) && !iskey(arg)) {

	    /* get the default expression and specified-p variable */
	    def = svar = NIL;
	    if (consp(arg)) {
		if (def = cdr(arg))
		    if (consp(def)) {
			if (svar = cdr(def))
			    if (consp(svar)) {
				svar = car(svar);
				if (!symbolp(svar))
				    badarglist();
			    }
			    else
				badarglist();
			def = car(def);
		    }
		    else
			badarglist();
		arg = car(arg);
	    }

	    /* get the keyword and the variable */
	    if (consp(arg)) {
		key = car(arg);
		if (!symbolp(key))
		    badarglist();
		if (arg = cdr(arg))
		    if (consp(arg))
			arg = car(arg);
		    else
			badarglist();
	    }
	    else if (symbolp(arg)) {
		key = xlkeyword((char *)getstring(getpname(arg)));
	    }

	    /* make sure the argument is a symbol */
	    if (!symbolp(arg))
		badarglist();

	    /* create a fully expanded key expression */
	    new = cons(cons(key,cons(arg,cons(def,cons(svar,NIL)))),NIL);

	    /* link it into the optional argument list */
	    if (last)
		rplacd(last,new);
	    else
		setkargs(closure,new);
	    last = new;

	    /* move the formal argument list pointer ahead */
	    fargs = cdr(fargs);
	}
    }

    /* check for the '&allow-other-keys' keyword */
    if (consp(fargs) && car(fargs) == lk_allow_other_keys)
	fargs = cdr(fargs);	/* this is the default anyway */

    /* check for the '&aux' keyword */
    if (consp(fargs) && car(fargs) == lk_aux) {
	fargs = cdr(fargs);

	/* handle each aux argument */
	last = NIL;
	while (consp(fargs) && (arg = car(fargs)) && !iskey(arg)) {

	    /* get the initial value */
	    def = NIL;
	    if (consp(arg)) {
		if (def = cdr(arg))
		    if (consp(def))
			def = car(def);
		    else
			badarglist();
		arg = car(arg);
	    }

	    /* make sure the argument is a symbol */
	    if (!symbolp(arg))
		badarglist();

	    /* create a fully expanded aux expression */
	    new = cons(cons(arg,cons(def,NIL)),NIL);

	    /* link it into the aux argument list */
	    if (last)
		rplacd(last,new);
	    else
		setaargs(closure,new);
	    last = new;

	    /* move the formal argument list pointer ahead */
	    fargs = cdr(fargs);
	}
    }

    /* make sure this is the end of the formal argument list */
    if (fargs)
	badarglist();

    /* restore the stack */
    xlpop();

    /* return the new closure */
    return (closure);
}

/* xlabind - bind the arguments for a function */
void xlabind(LVAL fun, int argc, LVAL *argv)
{
    LVAL *kargv,fargs,key,arg,def,svar,p;
    int rargc,kargc;
    
    /* protect some pointers */
    xlsave1(def);

    /* bind each required argument */
    for (fargs = getargs(fun); fargs; fargs = cdr(fargs)) {

	/* make sure there is an actual argument */
	if (--argc < 0)
	    xlfail("too few arguments");

	/* bind the formal variable to the argument value */
	xlbind(car(fargs),*argv++);
    }

    /* bind each optional argument */
    for (fargs = getoargs(fun); fargs; fargs = cdr(fargs)) {

	/* get argument, default and specified-p variable */
	p = car(fargs);
	arg = car(p); p = cdr(p);
	def = car(p); p = cdr(p);
	svar = car(p);

	/* bind the formal variable to the argument value */
	if (--argc >= 0) {
	    xlbind(arg,*argv++);
	    if (svar) xlbind(svar,s_true);
	}

	/* bind the formal variable to the default value */
	else {
	    if (def) def = xleval(def);
	    xlbind(arg,def);
	    if (svar) xlbind(svar,NIL);
	}
    }

    /* save the count of the &rest of the argument list */
    rargc = argc;
    
    /* handle '&rest' argument */
    if (arg = getrest(fun)) {
	def = makearglist(argc,argv);
	xlbind(arg,def);
	argc = 0;
    }

    /* handle '&key' arguments */
    if (fargs = getkargs(fun)) {
	for (; fargs; fargs = cdr(fargs)) {

	    /* get keyword, argument, default and specified-p variable */
	    p = car(fargs);
	    key = car(p); p = cdr(p);
	    arg = car(p); p = cdr(p);
	    def = car(p); p = cdr(p);
	    svar = car(p);

	    /* look for the keyword in the actual argument list */
	    for (kargv = argv, kargc = rargc; (kargc -= 2) >= 0; kargv += 2)
		if (*kargv == key)
		    break;

	    /* bind the formal variable to the argument value */
	    if (kargc >= 0) {
		xlbind(arg,*++kargv);
		if (svar) xlbind(svar,s_true);
	    }

	    /* bind the formal variable to the default value */
	    else {
		if (def) def = xleval(def);
		xlbind(arg,def);
		if (svar) xlbind(svar,NIL);
	    }
	}
	argc = 0;
    }

    /* check for the '&aux' keyword */
    for (fargs = getaargs(fun); fargs; fargs = cdr(fargs)) {

	/* get argument and default */
	p = car(fargs);
	arg = car(p); p = cdr(p);
	def = car(p);

	/* bind the auxiliary variable to the initial value */
	if (def) def = xleval(def);
	xlbind(arg,def);
    }

    /* make sure there aren't too many arguments */
    if (argc > 0)
	xlfail("too many arguments");

    /* restore the stack */
    xlpop();
}

/* doenter - print trace information on function entry */
static void doenter(LVAL sym, int argc, LVAL *argv)
{
    extern int xltrcindent;
    int i;
    
    /* indent to the current trace level */
    for (i = 0; i < xltrcindent; ++i)
	trcputstr(" ");
    ++xltrcindent;

    /* display the function call */
    trcputstr("Entering: ");
    trcputstr((char *)getstring(getpname(sym)));
    trcputstr(", Argument list: (");
    while (--argc >= 0) {
	trcprin1(*argv++);
	if (argc) trcputstr(" ");
    }
    trcputstr(")\n");
}

/* doexit - print trace information for function/macro exit */
static void doexit(LVAL sym, LVAL val)
{
    extern int xltrcindent;
    int i;
    
    /* indent to the current trace level */
    --xltrcindent;
    for (i = 0; i < xltrcindent; ++i)
	trcputstr(" ");
    
    /* display the function value */
    trcputstr("Exiting: ");
    trcputstr((char *)getstring(getpname(sym)));
    trcputstr(", Value: ");
    trcprin1(val);
    trcputstr("\n");
}

/* member - is 'x' a member of 'list'? */
static int member(LVAL x, LVAL list)
{
    for (; consp(list); list = cdr(list))
	if (x == car(list))
	    return (TRUE);
    return (FALSE);
}

/* xlunbound - signal an unbound variable error */
void xlunbound(LVAL sym)
{
    xlcerror("try evaluating symbol again","unbound variable",sym);
}

/* xlfunbound - signal an unbound function error */
void xlfunbound(LVAL sym)
{
    xlcerror("try evaluating symbol again","unbound function",sym);
}

/* xlstkoverflow - signal a stack overflow error */
void xlstkoverflow(void)
{
    xlabort("evaluation stack overflow");
}

/* xlargstkoverflow - signal an argument stack overflow error */
void xlargstkoverflow(void)
{
    xlabort("argument stack overflow");
}

/* badarglist - report a bad argument list error */
static void badarglist(void)
{
    xlfail("bad formal argument list");
}

/* xlfio.c - xlisp file i/o */
/*	Copyright (c) 1985, by David Michael Betz
	All Rights Reserved
	Permission is granted for unrestricted non-commercial use	*/


/* external variables */
extern LVAL k_direction,k_input,k_output;
extern LVAL s_stdin,s_stdout,s_true;
extern char buf[];
extern int xlfsize;

/* external routines */

/* forward declarations */

/* xread - read an expression */
LVAL xread(void)
{
    LVAL fptr,eof,rflag,val;

    /* get file pointer and eof value */
    fptr = (moreargs() ? xlgetfile() : getvalue(s_stdin));
    eof = (moreargs() ? xlgetarg() : NIL);
    rflag = (moreargs() ? xlgetarg() : NIL);
    xllastarg();

    /* read an expression */
    if (!xlread(fptr,&val,rflag != NIL))
	val = eof;

    /* return the expression */
    return (val);
}

/* xprint - built-in function 'print' */
LVAL xprint(void)
{
    return (printit(TRUE,TRUE));
}

/* xprin1 - built-in function 'prin1' */
LVAL xprin1(void)
{
    return (printit(TRUE,FALSE));
}

/* xprinc - built-in function princ */
LVAL xprinc(void)
{
    return (printit(FALSE,FALSE));
}

/* xterpri - terminate the current print line */
LVAL xterpri(void)
{
    LVAL fptr;

    /* get file pointer */
    fptr = (moreargs() ? xlgetfile() : getvalue(s_stdout));
    xllastarg();

    /* terminate the print line and return nil */
    xlterpri(fptr);
    return (NIL);
}

/* printit - common print function */
static LVAL printit(int pflag, int tflag)
{
    LVAL fptr,val;

    /* get expression to print and file pointer */
    val = xlgetarg();
    fptr = (moreargs() ? xlgetfile() : getvalue(s_stdout));
    xllastarg();

    /* print the value */
    xlprint(fptr,val,pflag);

    /* terminate the print line if necessary */
    if (tflag)
	xlterpri(fptr);

    /* return the result */
    return (val);
}

/* xflatsize - compute the size of a printed representation using prin1 */
LVAL xflatsize(void)
{
    return (flatsize(TRUE));
}

/* xflatc - compute the size of a printed representation using princ */
LVAL xflatc(void)
{
    return (flatsize(FALSE));
}

/* flatsize - compute the size of a printed expression */
static LVAL flatsize(int pflag)
{
    LVAL val;

    /* get the expression */
    val = xlgetarg();
    xllastarg();

    /* print the value to compute its size */
    xlfsize = 0;
    xlprint(NIL,val,pflag);

    /* return the length of the expression */
    return (cvfixnum((FIXTYPE)xlfsize));
}

/* xopen - open a file */
LVAL xopen(void)
{
    char *name,*mode;
    FILE *fp;
    LVAL dir;

    /* get the file name and direction */
    name = (char *)getstring(xlgetfname());
    if (!xlgetkeyarg(k_direction,&dir))
	dir = k_input;

    /* get the mode */
    if (dir == k_input)
	mode = "r";
    else if (dir == k_output)
	mode = "w";
    else
	xlerror("bad direction",dir);

    /* try to open the file */
    return ((fp = osaopen(name,mode)) ? cvfile(fp) : NIL);
}

/* xclose - close a file */
LVAL xclose(void)
{
    LVAL fptr;

    /* get file pointer */
    fptr = xlgastream();
    xllastarg();

    /* make sure the file exists */
    if (getfile(fptr) == NULL)
	xlfail("file not open");

    /* close the file */
    osclose(getfile(fptr));
    setfile(fptr,NULL);

    /* return nil */
    return (NIL);
}

/* xrdchar - read a character from a file */
LVAL xrdchar(void)
{
    LVAL fptr;
    int ch;

    /* get file pointer */
    fptr = (moreargs() ? xlgetfile() : getvalue(s_stdin));
    xllastarg();

    /* get character and check for eof */
    return ((ch = xlgetc(fptr)) == EOF ? NIL : cvchar(ch));
}

/* xrdbyte - read a byte from a file */
LVAL xrdbyte(void)
{
    LVAL fptr;
    int ch;

    /* get file pointer */
    fptr = (moreargs() ? xlgetfile() : getvalue(s_stdin));
    xllastarg();

    /* get character and check for eof */
    return ((ch = xlgetc(fptr)) == EOF ? NIL : cvfixnum((FIXTYPE)ch));
}

/* xpkchar - peek at a character from a file */
LVAL xpkchar(void)
{
    LVAL flag,fptr;
    int ch;

    /* peek flag and get file pointer */
    flag = (moreargs() ? xlgetarg() : NIL);
    fptr = (moreargs() ? xlgetfile() : getvalue(s_stdin));
    xllastarg();

    /* skip leading white space and get a character */
    if (flag)
	while ((ch = xlpeek(fptr)) != EOF && isspace(ch))
	    xlgetc(fptr);
    else
	ch = xlpeek(fptr);

    /* return the character */
    return (ch == EOF ? NIL : cvchar(ch));
}

/* xwrchar - write a character to a file */
LVAL xwrchar(void)
{
    LVAL fptr,chr;

    /* get the character and file pointer */
    chr = xlgachar();
    fptr = (moreargs() ? xlgetfile() : getvalue(s_stdout));
    xllastarg();

    /* put character to the file */
    xlputc(fptr,getchcode(chr));

    /* return the character */
    return (chr);
}

/* xwrbyte - write a byte to a file */
LVAL xwrbyte(void)
{
    LVAL fptr,chr;

    /* get the byte and file pointer */
    chr = xlgafixnum();
    fptr = (moreargs() ? xlgetfile() : getvalue(s_stdout));
    xllastarg();

    /* put byte to the file */
    xlputc(fptr,xlfixint(chr));

    /* return the character */
    return (chr);
}

/* xreadline - read a line from a file */
LVAL xreadline(void)
{
    unsigned char buf[STRMAX+1],*p,*sptr;
    LVAL fptr,str,newstr;
    int len,blen,ch;

    /* protect some pointers */
    xlsave1(str);

    /* get file pointer */
    fptr = (moreargs() ? xlgetfile() : getvalue(s_stdin));
    xllastarg();

    /* get character and check for eof */
    len = blen = 0; p = buf;
    while ((ch = xlgetc(fptr)) != EOF && ch != '\n') {

	/* check for buffer overflow */
	if (blen >= STRMAX) {
 	    newstr = newstring(len + STRMAX + 1);
	    sptr = getstring(newstr); *sptr = '\0';
	    if (str) strcat(sptr,getstring(str));
	    *p = '\0'; strcat(sptr,buf);
	    p = buf; blen = 0;
	    len += STRMAX;
	    str = newstr;
	}

	/* store the character */
	*p++ = ch; ++blen;
    }

    /* check for end of file */
    if (len == 0 && p == buf && ch == EOF) {
	xlpop();
	return (NIL);
    }

    /* append the last substring */
    if (str == NIL || blen) {
	newstr = newstring(len + blen + 1);
	sptr = getstring(newstr); *sptr = '\0';
	if (str) strcat(sptr,getstring(str));
	*p = '\0'; strcat(sptr,buf);
	str = newstr;
    }

    /* restore the stack */
    xlpop();

    /* return the string */
    return (str);
}


/* xmkstrinput - make a string input stream */
LVAL xmkstrinput(void)
{
    int start,end,len,i;
    unsigned char *str;
    LVAL string,val;

    /* protect the return value */
    xlsave1(val);
    
    /* get the string and length */
    string = xlgastring();
    str = getstring(string);
    len = getslength(string) - 1;

    /* get the starting offset */
    if (moreargs()) {
	val = xlgafixnum();
	start = xlfixint(val);
    }
    else start = 0;

    /* get the ending offset */
    if (moreargs()) {
	val = xlgafixnum();
	end = xlfixint(val);
    }
    else end = len;
    xllastarg();

    /* check the bounds */
    if (start < 0 || start > len)
	xlerror("string index out of bounds",cvfixnum((FIXTYPE)start));
    if (end < 0 || end > len)
	xlerror("string index out of bounds",cvfixnum((FIXTYPE)end));

    /* make the stream */
    val = newustream();

    /* copy the substring into the stream */
    for (i = start; i < end; ++i)
	xlputc(val,str[i]);

    /* restore the stack */
    xlpop();

    /* return the new stream */
    return (val);
}

/* xmkstroutput - make a string output stream */
LVAL xmkstroutput(void)
{
    return (newustream());
}

/* xgetstroutput - get output stream string */
LVAL xgetstroutput(void)
{
    LVAL stream;
    stream = xlgaustream();
    xllastarg();
    return (getstroutput(stream));
}

/* xgetlstoutput - get output stream list */
LVAL xgetlstoutput(void)
{
    LVAL stream,val;

    /* get the stream */
    stream = xlgaustream();
    xllastarg();

    /* get the output character list */
    val = gethead(stream);

    /* empty the character list */
    sethead(stream,NIL);
    settail(stream,NIL);

    /* return the list */
    return (val);
}

/* xformat - formatted output function */
LVAL xformat(void)
{
    unsigned char *fmt;
    LVAL stream,val;
    int ch;

    /* protect some pointers (the string stream made for a NIL
       destination is referenced from nowhere else during printing) */
    xlsave1(val);

    /* get the stream and format string */
    stream = xlgetarg();
    if (stream == NIL)
	val = stream = newustream();
    else {
	if (stream == s_true)
	    stream = getvalue(s_stdout);
	else if (!streamp(stream) && !ustreamp(stream))
	    xlbadtype(stream);
	val = NIL;
    }
    fmt = getstring(xlgastring());

    /* process the format string */
    while (ch = *fmt++)
	if (ch == '~') {
	    switch (*fmt++) {
	    case '\0':
		xlerror("expecting a format directive",cvstring(fmt-1));
	    case 'a': case 'A':
		xlprint(stream,xlgetarg(),FALSE);
		break;
	    case 's': case 'S':
		xlprint(stream,xlgetarg(),TRUE);
		break;
	    case '%':
		xlterpri(stream);
		break;
	    case '~':
		xlputc(stream,'~');
		break;
	    case '\n':
		while (*fmt && *fmt != '\n' && isspace(*fmt))
		    ++fmt;
		break;
	    default:
		xlerror("unknown format directive",cvstring(fmt-1));
	    }
	}
	else
	    xlputc(stream,ch);
        
    /* return the value */
    if (val)
	val = getstroutput(val);
    xlpop();
    return (val);
}

/* getstroutput - get the output stream string (internal) */
static LVAL getstroutput(LVAL stream)
{
    unsigned char *str;
    LVAL next,val;
    int len,ch;

    /* protect the stream while the string is allocated */
    xlprot1(stream);

    /* compute the length of the stream */
    for (len = 0, next = gethead(stream); next != NIL; next = cdr(next))
	++len;

    /* create a new string */
    val = newstring(len + 1);
    
    /* copy the characters into the new string */
    str = getstring(val);
    while ((ch = xlgetc(stream)) != EOF)
	*str++ = ch;
    *str = '\0';

    /* return the string */
    xlpop();
    return (val);
}

/* xlftab.c - xlisp function table */
/*	Copyright (c) 1985, by David Michael Betz
	All Rights Reserved
	Permission is granted for unrestricted non-commercial use	*/

/* external functions */

/* functions specific to xldmem.c */
#ifdef SAVERESTORE
#endif

/* include system dependant definitions */
#include "osdefs.h"

/* SUBR/FSUBR indicator */
#define S	SUBR
#define F	FSUBR

/* forward declarations */

/* the function table */
FUNDEF funtab[] = {

	/* read macro functions */
{	NULL,				S, rmhash		}, /*   0 */
{	NULL,				S, rmquote		}, /*   1 */
{	NULL,				S, rmdquote		}, /*   2 */
{	NULL,				S, rmbquote		}, /*   3 */
{	NULL,				S, rmcomma		}, /*   4 */
{	NULL,				S, rmlpar		}, /*   5 */
{	NULL,				S, rmrpar		}, /*   6 */
{	NULL,				S, rmsemi		}, /*   7 */
{	NULL,				S, xnotimp		}, /*   8 */
{	NULL,				S, xnotimp		}, /*   9 */

	/* methods */
{	NULL,				S, clnew		}, /*  10 */
{	NULL,				S, clisnew		}, /*  11 */
{	NULL,				S, clanswer		}, /*  12 */
{	NULL,				S, obisnew		}, /*  13 */
{	NULL,				S, obclass		}, /*  14 */
{	NULL,				S, obshow		}, /*  15 */
{	NULL,				S, xnotimp		}, /*  16 */
{	NULL,				S, xnotimp		}, /*  17 */
{	NULL,				S, xnotimp		}, /*  18 */
{	NULL,				S, xnotimp		}, /*  19 */

	/* evaluator functions */
{	"EVAL",				S, xeval		}, /*  20 */
{	"APPLY",			S, xapply		}, /*  21 */
{	"FUNCALL",			S, xfuncall		}, /*  22 */
{	"QUOTE",			F, xquote		}, /*  23 */
{	"FUNCTION",			F, xfunction		}, /*  24 */
{	"BACKQUOTE",			F, xbquote		}, /*  25 */
{	"LAMBDA",			F, xlambda		}, /*  26 */

	/* symbol functions */
{	"SET",				S, xset			}, /*  27 */
{	"SETQ",				F, xsetq		}, /*  28 */
{	"SETF",				F, xsetf		}, /*  29 */
{	"DEFUN",			F, xdefun		}, /*  30 */
{	"DEFMACRO",			F, xdefmacro		}, /*  31 */
{	"GENSYM",			S, xgensym		}, /*  32 */
{	"MAKE-SYMBOL",			S, xmakesymbol		}, /*  33 */
{	"INTERN", 			S, xintern		}, /*  34 */
{	"SYMBOL-NAME",			S, xsymname		}, /*  35 */
{	"SYMBOL-VALUE",			S, xsymvalue		}, /*  36 */
{	"SYMBOL-PLIST",			S, xsymplist		}, /*  37 */
{	"GET",				S, xget			}, /*  38 */
{	"PUTPROP", 			S, xputprop		}, /*  39 */
{	"REMPROP",			S, xremprop		}, /*  40 */
{	"HASH",				S, xhash		}, /*  41 */

	/* array functions */
{	"MAKE-ARRAY",			S, xmkarray		}, /*  42 */
{	"AREF",				S, xaref		}, /*  43 */
			
	/* list functions */
{	"CAR",				S, xcar			}, /*  44 */
{	"CDR",				S, xcdr			}, /*  45 */
			
{	"CAAR",				S, xcaar		}, /*  46 */
{	"CADR",				S, xcadr		}, /*  47 */
{	"CDAR",				S, xcdar		}, /*  48 */
{	"CDDR",				S, xcddr		}, /*  49 */

{	"CAAAR",			S, xcaaar		}, /*  50 */
{	"CAADR",			S, xcaadr		}, /*  51 */
{	"CADAR",			S, xcadar		}, /*  52 */
{	"CADDR",			S, xcaddr		}, /*  53 */
{	"CDAAR",			S, xcdaar		}, /*  54 */
{	"CDADR",			S, xcdadr		}, /*  55 */
{	"CDDAR",			S, xcddar		}, /*  56 */
{	"CDDDR",			S, xcdddr		}, /*  57 */

{	"CAAAAR", 			S, xcaaaar		}, /*  58 */
{	"CAAADR",			S, xcaaadr		}, /*  59 */
{	"CAADAR",			S, xcaadar		}, /*  60 */
{	"CAADDR",			S, xcaaddr		}, /*  61 */
{	"CADAAR",		 	S, xcadaar		}, /*  62 */
{	"CADADR",			S, xcadadr		}, /*  63 */
{	"CADDAR",			S, xcaddar		}, /*  64 */
{	"CADDDR",			S, xcadddr		}, /*  65 */
{	"CDAAAR",			S, xcdaaar		}, /*  66 */
{	"CDAADR",			S, xcdaadr		}, /*  67 */
{	"CDADAR",			S, xcdadar		}, /*  68 */
{	"CDADDR",			S, xcdaddr		}, /*  69 */
{	"CDDAAR",			S, xcddaar		}, /*  70 */
{	"CDDADR",			S, xcddadr		}, /*  71 */
{	"CDDDAR",			S, xcdddar		}, /*  72 */
{	"CDDDDR",			S, xcddddr		}, /*  73 */

{	"CONS",				S, xcons		}, /*  74 */
{	"LIST",				S, xlist		}, /*  75 */
{	"APPEND",			S, xappend		}, /*  76 */
{	"REVERSE",			S, xreverse		}, /*  77 */
{	"LAST",				S, xlast		}, /*  78 */
{	"NTH",				S, xnth			}, /*  79 */
{	"NTHCDR",			S, xnthcdr		}, /*  80 */
{	"MEMBER",			S, xmember		}, /*  81 */
{	"ASSOC",			S, xassoc		}, /*  82 */
{	"SUBST", 			S, xsubst		}, /*  83 */
{	"SUBLIS",			S, xsublis		}, /*  84 */
{	"REMOVE",			S, xremove		}, /*  85 */
{	"LENGTH",			S, xlength		}, /*  86 */
{	"MAPC",				S, xmapc		}, /*  87 */
{	"MAPCAR",			S, xmapcar		}, /*  88 */
{	"MAPL",				S, xmapl		}, /*  89 */
{	"MAPLIST",			S, xmaplist		}, /*  90 */
			
	/* destructive list functions */
{	"RPLACA",			S, xrplca		}, /*  91 */
{	"RPLACD",			S, xrplcd		}, /*  92 */
{	"NCONC",			S, xnconc		}, /*  93 */
{	"DELETE",			S, xdelete		}, /*  94 */

	/* predicate functions */
{	"ATOM",				S, xatom		}, /*  95 */
{	"SYMBOLP",			S, xsymbolp		}, /*  96 */
{	"NUMBERP",			S, xnumberp		}, /*  97 */
{	"BOUNDP",			S, xboundp 		}, /*  98 */
{	"NULL",				S, xnull		}, /*  99 */
{	"LISTP",			S, xlistp		}, /* 100 */
{	"CONSP",			S, xconsp		}, /* 101 */
{	"MINUSP",			S, xminusp 		}, /* 102 */
{	"ZEROP",			S, xzerop		}, /* 103 */
{	"PLUSP",			S, xplusp		}, /* 104 */
{	"EVENP",			S, xevenp		}, /* 105 */
{	"ODDP",				S, xoddp		}, /* 106 */
{	"EQ",				S, xeq			}, /* 107 */
{	"EQL",				S, xeql			}, /* 108 */
{	"EQUAL",			S, xequal		}, /* 109 */

	/* special forms */
{	"COND",				F, xcond		}, /* 110 */
{	"CASE",				F, xcase		}, /* 111 */
{	"AND",				F, xand			}, /* 112 */
{	"OR",				F, xor			}, /* 113 */
{	"LET",				F, xlet			}, /* 114 */
{	"LET*",				F, xletstar		}, /* 115 */
{	"IF",				F, xif			}, /* 116 */
{	"PROG",				F, xprog		}, /* 117 */
{	"PROG*",			F, xprogstar		}, /* 118 */
{	"PROG1",			F, xprog1		}, /* 119 */
{	"PROG2",			F, xprog2		}, /* 120 */
{	"PROGN",			F, xprogn		}, /* 121 */
{	"GO",				F, xgo			}, /* 122 */
{	"RETURN",			F, xreturn  		}, /* 123 */
{	"DO",				F, xdo			}, /* 124 */
{	"DO*",				F, xdostar  		}, /* 125 */
{	"DOLIST",			F, xdolist  		}, /* 126 */
{	"DOTIMES",			F, xdotimes		}, /* 127 */
{	"CATCH",			F, xcatch		}, /* 128 */
{	"THROW",			F, xthrow		}, /* 129 */
	
	/* debugging and error handling functions */
{	"ERROR",			S, xerror		}, /* 130 */
{	"CERROR",			S, xcerror  		}, /* 131 */
{	"BREAK",			S, xbreak		}, /* 132 */
{	"CLEAN-UP",			S, xcleanup		}, /* 133 */
{	"TOP-LEVEL",			S, xtoplevel		}, /* 134 */
{	"CONTINUE",			S, xcontinue		}, /* 135 */
{	"ERRSET", 			F, xerrset  		}, /* 136 */
{	"BAKTRACE",			S, xbaktrace		}, /* 137 */
{	"EVALHOOK",			S, xevalhook		}, /* 138 */

	/* arithmetic functions */
{	"TRUNCATE",			S, xfix			}, /* 139 */
{	"FLOAT",			S, xfloat		}, /* 140 */
{	"+",				S, xadd			}, /* 141 */
{	"-",				S, xsub			}, /* 142 */
{	"*",				S, xmul			}, /* 143 */
{	"/",				S, xdiv			}, /* 144 */
{	"1+",				S, xadd1		}, /* 145 */
{	"1-",				S, xsub1		}, /* 146 */
{	"REM",				S, xrem			}, /* 147 */
{	"MIN",				S, xmin			}, /* 148 */
{	"MAX",				S, xmax			}, /* 149 */
{	"ABS",				S, xabs			}, /* 150 */
{	"SIN",				S, xsin			}, /* 151 */
{	"COS",				S, xcos			}, /* 152 */
{	"TAN",				S, xtan			}, /* 153 */
{	"EXPT",				S, xexpt		}, /* 154 */
{	"EXP",				S, xexp			}, /* 155 */
{	"SQRT",		  		S, xsqrt		}, /* 156 */
{	"RANDOM",			S, xrand		}, /* 157 */
			
	/* bitwise logical functions */
{	"LOGAND",			S, xlogand  		}, /* 158 */
{	"LOGIOR",			S, xlogior  		}, /* 159 */
{	"LOGXOR",			S, xlogxor  		}, /* 160 */
{	"LOGNOT",			S, xlognot  		}, /* 161 */

	/* numeric comparison functions */
{	"<",				S, xlss			}, /* 162 */
{	"<=",				S, xleq			}, /* 163 */
{	"=",				S, xequ			}, /* 164 */
{	"/=",				S, xneq			}, /* 165 */
{	">=",				S, xgeq			}, /* 166 */
{	">",				S, xgtr			}, /* 167 */
			
	/* string functions */
{	"STRCAT",			S, xstrcat  		}, /* 168 */
{	"SUBSEQ",			S, xsubseq  		}, /* 169 */
{	"STRING",			S, xstring  		}, /* 170 */
{	"CHAR",				S, xchar		}, /* 171 */

	/* I/O functions */
{	"READ",				S, xread		}, /* 172 */
{	"PRINT",			S, xprint		}, /* 173 */
{	"PRIN1",			S, xprin1		}, /* 174 */
{	"PRINC",			S, xprinc		}, /* 175 */
{	"TERPRI",			S, xterpri  		}, /* 176 */
{	"FLATSIZE",			S, xflatsize		}, /* 177 */
{	"FLATC",			S, xflatc		}, /* 178 */
			
	/* file I/O functions */
{	"OPEN",				S, xopen		}, /* 179 */
{	"FORMAT",			S, xformat  		}, /* 180 */
{	"CLOSE",			S, xclose		}, /* 181 */
{	"READ-CHAR",			S, xrdchar  		}, /* 182 */
{	"PEEK-CHAR",			S, xpkchar  		}, /* 183 */
{	"WRITE-CHAR",			S, xwrchar  		}, /* 184 */
{	"READ-LINE",			S, xreadline		}, /* 185 */

	/* system functions */
{	"LOAD",				S, xload		}, /* 186 */
{	"DRIBBLE",			S, xtranscript		}, /* 187 */

/* functions specific to xldmem.c */
{	"GC",				S, xgc			}, /* 188 */
{	"EXPAND",			S, xexpand  		}, /* 189 */
{	"ALLOC",			S, xalloc		}, /* 190 */
{	"ROOM",				S, xmem			}, /* 191 */
#ifdef SAVERESTORE
{	"SAVE",				S, xsave		}, /* 192 */
{	"RESTORE",			S, xrestore		}, /* 193 */
#else
{	NULL,				S, xnotimp		}, /* 192 */
{	NULL,				S, xnotimp		}, /* 193 */
#endif
/* end of functions specific to xldmem.c */

{	"TYPE-OF",			S, xtype		}, /* 194 */
{	"EXIT",				S, xexit		}, /* 195 */
{	"PEEK",				S, xpeek		}, /* 196 */
{	"POKE",				S, xpoke		}, /* 197 */
{	"ADDRESS-OF",			S, xaddrs		}, /* 198 */

	/* new functions and special forms */
{	"VECTOR",			S, xvector  		}, /* 199 */
{	"BLOCK",			F, xblock		}, /* 200 */
{	"RETURN-FROM",			F, xrtnfrom		}, /* 201 */
{	"TAGBODY",			F, xtagbody		}, /* 202 */
{	"PSETQ",			F, xpsetq		}, /* 203 */
{	"FLET",				F, xflet		}, /* 204 */
{	"LABELS",			F, xlabels  		}, /* 205 */
{	"MACROLET",			F, xmacrolet		}, /* 206 */
{	"UNWIND-PROTECT",		F, xunwindprotect	}, /* 207 */
{	"PPRINT",			S, xpp			}, /* 208 */
{	"STRING<",			S, xstrlss  		}, /* 209 */
{	"STRING<=",			S, xstrleq  		}, /* 210 */
{	"STRING=",			S, xstreql  		}, /* 211 */
{	"STRING/=",			S, xstrneq  		}, /* 212 */
{	"STRING>=",			S, xstrgeq  		}, /* 213 */
{	"STRING>",			S, xstrgtr  		}, /* 214 */
{	"STRING-LESSP",			S, xstrilss		}, /* 215 */
{	"STRING-NOT-GREATERP",		S, xstrileq		}, /* 216 */
{	"STRING-EQUAL",			S, xstrieql		}, /* 217 */
{	"STRING-NOT-EQUAL",		S, xstrineq		}, /* 218 */
{	"STRING-NOT-LESSP",		S, xstrigeq		}, /* 219 */
{	"STRING-GREATERP",		S, xstrigtr		}, /* 220 */
{	"INTEGERP",			S, xintegerp		}, /* 221 */
{	"FLOATP",			S, xfloatp  		}, /* 222 */
{	"STRINGP",			S, xstringp		}, /* 223 */
{	"ARRAYP",			S, xarrayp  		}, /* 224 */
{	"STREAMP",			S, xstreamp		}, /* 225 */
{	"OBJECTP",			S, xobjectp		}, /* 226 */
{	"STRING-UPCASE",		S, xupcase  		}, /* 227 */
{	"STRING-DOWNCASE",		S, xdowncase		}, /* 228 */
{	"NSTRING-UPCASE",		S, xnupcase		}, /* 229 */
{	"NSTRING-DOWNCASE",		S, xndowncase		}, /* 230 */
{	"STRING-TRIM",			S, xtrim		}, /* 231 */
{	"STRING-LEFT-TRIM",		S, xlefttrim		}, /* 232 */
{	"STRING-RIGHT-TRIM",		S, xrighttrim		}, /* 233 */
{	"WHEN",				F, xwhen		}, /* 234 */
{	"UNLESS",			F, xunless  		}, /* 235 */
{	"LOOP",				F, xloop		}, /* 236 */
{	"SYMBOL-FUNCTION",		S, xsymfunction		}, /* 237 */
{	"FBOUNDP",			S, xfboundp		}, /* 238 */
{	"SEND",				S, xsend		}, /* 239 */
{	"SEND-SUPER",			S, xsendsuper		}, /* 240 */
{	"PROGV",			F, xprogv		}, /* 241 */
{	"CHARACTERP",			S, xcharp		}, /* 242 */
{	"CHAR-INT",			S, xcharint		}, /* 243 */
{	"INT-CHAR",			S, xintchar		}, /* 244 */
{	"READ-BYTE",			S, xrdbyte  		}, /* 245 */
{	"WRITE-BYTE",			S, xwrbyte  		}, /* 246 */
{	"MAKE-STRING-INPUT-STREAM", 	S, xmkstrinput		}, /* 247 */
{	"MAKE-STRING-OUTPUT-STREAM",	S, xmkstroutput		}, /* 248 */
{	"GET-OUTPUT-STREAM-STRING",	S, xgetstroutput	}, /* 249 */
{	"GET-OUTPUT-STREAM-LIST",	S, xgetlstoutput	}, /* 250 */
{	"GCD",				S, xgcd			}, /* 251 */
{	"GET-LAMBDA-EXPRESSION", 	S, xgetlambda		}, /* 252 */
{	"MACROEXPAND",			S, xmacroexpand		}, /* 253 */
{	"MACROEXPAND-1",		S, x1macroexpand	}, /* 254 */
{	"CHAR<",			S, xchrlss  		}, /* 255 */
{	"CHAR<=",			S, xchrleq  		}, /* 256 */
{	"CHAR=",			S, xchreql  		}, /* 257 */
{	"CHAR/=",			S, xchrneq  		}, /* 258 */
{	"CHAR>=",			S, xchrgeq  		}, /* 259 */
{	"CHAR>",			S, xchrgtr  		}, /* 260 */
{	"CHAR-LESSP",			S, xchrilss		}, /* 261 */
{	"CHAR-NOT-GREATERP",		S, xchrileq		}, /* 262 */
{	"CHAR-EQUAL",			S, xchrieql		}, /* 263 */
{	"CHAR-NOT-EQUAL",		S, xchrineq		}, /* 264 */
{	"CHAR-NOT-LESSP",		S, xchrigeq		}, /* 265 */
{	"CHAR-GREATERP",		S, xchrigtr		}, /* 266 */
{	"UPPER-CASE-P",			S, xuppercasep		}, /* 267 */
{	"LOWER-CASE-P",			S, xlowercasep		}, /* 268 */
{	"BOTH-CASE-P",			S, xbothcasep		}, /* 269 */
{	"DIGIT-CHAR-P",			S, xdigitp		}, /* 270 */
{	"ALPHANUMERICP",		S, xalphanumericp	}, /* 271 */
{	"CHAR-UPCASE",			S, xchupcase		}, /* 272 */
{	"CHAR-DOWNCASE",		S, xchdowncase		}, /* 273 */
{	"DIGIT-CHAR",			S, xdigitchar		}, /* 274 */
{	"CHAR-CODE",			S, xcharcode		}, /* 275 */
{	"CODE-CHAR",			S, xcodechar		}, /* 276 */
{	"ENDP",				S, xendp		}, /* 277 */
{	"REMOVE-IF",			S, xremif		}, /* 278 */
{	"REMOVE-IF-NOT",		S, xremifnot		}, /* 279 */
{	"DELETE-IF",			S, xdelif		}, /* 280 */
{	"DELETE-IF-NOT",		S, xdelifnot		}, /* 281 */
{	"TRACE",			F, xtrace		}, /* 282 */
{	"UNTRACE",			F, xuntrace		}, /* 283 */
{	"SORT",				S, xsort		}, /* 284 */

	/* extra table entries */
{	NULL,				S, xnotimp		}, /* 285 */
{	NULL,				S, xnotimp		}, /* 286 */
{	NULL,				S, xnotimp		}, /* 287 */
{	NULL,				S, xnotimp		}, /* 288 */
{	NULL,				S, xnotimp		}, /* 289 */
{	NULL,				S, xnotimp		}, /* 290 */
{	NULL,				S, xnotimp		}, /* 291 */
{	NULL,				S, xnotimp		}, /* 292 */
{	NULL,				S, xnotimp		}, /* 293 */
{	NULL,				S, xnotimp		}, /* 294 */
{	NULL,				S, xnotimp		}, /* 295 */
{	NULL,				S, xnotimp		}, /* 296 */
{	NULL,				S, xnotimp		}, /* 297 */
{	NULL,				S, xnotimp		}, /* 298 */
{	NULL,				S, xnotimp		}, /* 299 */

	/* include system dependant function pointers */
#include "osptrs.h"


{0,0,0} /* end of table marker */

};			

/* xnotimp - function table entries that are currently not implemented */
static LVAL xnotimp(void)
{
    xlfail("function not implemented");
}

/* xlglobals - xlisp global variables */
/*	Copyright (c) 1985, by David Michael Betz
	All Rights Reserved
	Permission is granted for unrestricted non-commercial use	*/


/* symbols */
LVAL s_true=NIL,obarray=NIL;
LVAL s_unbound=NIL,s_dot=NIL;
LVAL s_quote=NIL,s_function=NIL;
LVAL s_bquote=NIL,s_comma=NIL,s_comat=NIL;
LVAL s_evalhook=NIL,s_applyhook=NIL,s_tracelist;
LVAL s_lambda=NIL,s_macro=NIL;
LVAL s_stdin=NIL,s_stdout=NIL,s_stderr=NIL,s_debugio=NIL,s_traceout=NIL;
LVAL s_rtable=NIL;
LVAL s_tracenable=NIL,s_tlimit=NIL,s_breakenable=NIL;
LVAL s_setf=NIL,s_car=NIL,s_cdr=NIL,s_nth=NIL,s_aref=NIL,s_get=NIL;
LVAL s_svalue=NIL,s_sfunction=NIL,s_splist=NIL;
LVAL s_eql=NIL,s_gcflag=NIL,s_gchook=NIL;
LVAL s_ifmt=NIL,s_ffmt=NIL;
LVAL s_1plus=NIL,s_2plus=NIL,s_3plus=NIL;
LVAL s_1star=NIL,s_2star=NIL,s_3star=NIL;
LVAL s_minus=NIL,s_printcase=NIL;

/* keywords */
LVAL k_test=NIL,k_tnot=NIL;
LVAL k_wspace=NIL,k_const=NIL,k_nmacro=NIL,k_tmacro=NIL;
LVAL k_sescape=NIL,k_mescape=NIL;
LVAL k_direction=NIL,k_input=NIL,k_output=NIL;
LVAL k_start=NIL,k_end=NIL,k_1start=NIL,k_1end=NIL;
LVAL k_2start=NIL,k_2end=NIL,k_count=NIL,k_key=NIL;
LVAL k_verbose=NIL,k_print=NIL;
LVAL k_upcase=NIL,k_downcase=NIL;

/* lambda list keywords */
LVAL lk_optional=NIL,lk_rest=NIL,lk_key=NIL,lk_aux=NIL;
LVAL lk_allow_other_keys=NIL;

/* type names */
LVAL a_subr=NIL,a_fsubr=NIL;
LVAL a_cons=NIL,a_symbol=NIL,a_fixnum=NIL,a_flonum=NIL;
LVAL a_string=NIL,a_object=NIL,a_stream=NIL,a_vector=NIL;
LVAL a_closure=NIL,a_char=NIL,a_ustream=NIL;

/* evaluation variables */
LVAL **xlstack = NULL,**xlstkbase = NULL,**xlstktop = NULL;
LVAL xlenv=NIL,xlfenv=NIL,xldenv=NIL;

/* argument stack */
LVAL *xlargstkbase = NULL;	/* argument stack base */
LVAL *xlargstktop = NULL;	/* argument stack top */
LVAL *xlfp = NULL;		/* argument frame pointer */
LVAL *xlsp = NULL;		/* argument stack pointer */
LVAL *xlargv = NULL;		/* current argument vector */
int xlargc = 0;			/* current argument count */

/* exception handling variables */
CONTEXT *xlcontext = NULL;	/* current exception handler */
CONTEXT *xltarget = NULL;	/* target context (for xljump) */
LVAL xlvalue=NIL;		/* exception value (for xljump) */
int xlmask=0;			/* exception type (for xljump) */

/* debugging variables */
int xldebug = 0;		/* debug level */
int xlsample = 0;		/* control character sample rate */
int xltrcindent = 0;		/* trace indent level */

/* gensym variables */
char gsprefix[STRMAX+1] = { 'G',0 };	/* gensym prefix string */
int gsnumber = 1;		/* gensym number */

/* i/o variables */
int xlfsize = 0;		/* flat size of current print call */
FILE *tfp = NULL;		/* transcript file pointer */

/* general purpose string buffer */
char buf[STRMAX+1] = { 0 };

