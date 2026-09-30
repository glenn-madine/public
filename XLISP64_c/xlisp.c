/* xlsym - symbol handling routines */
/*	Copyright (c) 1985, by David Michael Betz
	All Rights Reserved
	Permission is granted for unrestricted non-commercial use	*/

#include "xlisp.h"

/* prototypes for functions local to this file */
static LVAL findprop(LVAL sym, LVAL prp);

/* external variables */
extern LVAL obarray,s_unbound;
extern LVAL xlenv,xlfenv,xldenv;

/* forward declarations */

/* xlenter - enter a symbol into the obarray */
LVAL xlenter(char *name)
{
    LVAL sym,array;
    int i;

    /* check for nil */
    if (strcmp(name,"NIL") == 0)
	return (NIL);

    /* check for symbol already in table */
    array = getvalue(obarray);
    i = hash(name,HSIZE);
    for (sym = getelement(array,i); sym; sym = cdr(sym))
	if (strcmp(name,getstring(getpname(car(sym)))) == 0)
	    return (car(sym));

    /* make a new symbol node and link it into the list */
    xlsave1(sym);
    sym = consd(getelement(array,i));
    rplaca(sym,xlmakesym(name));
    setelement(array,i,sym);
    xlpop();

    /* return the new symbol */
    return (car(sym));
}

/* xlmakesym - make a new symbol node */
LVAL xlmakesym(char *name)
{
    LVAL sym;
    sym = cvsymbol(name);
    if (*name == ':')
	setvalue(sym,sym);
    return (sym);
}

/* xlkeyword - enter the keyword ":name" (name may be any length) */
LVAL xlkeyword(char *name)
{
    static char *kbuf = NULL;
    static size_t kbsize = 0;
    size_t len = strlen(name) + 2;
    char *p;

    /* grow the scratch buffer as needed (it is kept for reuse) */
    if (len > kbsize) {
	if ((p = (char *)realloc(kbuf,len)) == NULL)
	    xlfail("insufficient memory - keyword name");
	kbuf = p;
	kbsize = len;
    }
    kbuf[0] = ':';
    memcpy(kbuf + 1,name,len - 1);
    return (xlenter(kbuf));
}

/* xlgetvalue - get the value of a symbol (with check) */
LVAL xlgetvalue(LVAL sym)
{
    LVAL val;

    /* look for the value of the symbol */
    while ((val = xlxgetvalue(sym)) == s_unbound)
	xlunbound(sym);

    /* return the value */
    return (val);
}

/* xlxgetvalue - get the value of a symbol */
LVAL xlxgetvalue(LVAL sym)
{
    register LVAL fp,ep;
    LVAL val;

    /* check the environment list */
    for (fp = xlenv; fp; fp = cdr(fp))

	/* check for an instance variable */
	if ((ep = car(fp)) && objectp(car(ep))) {
	    if (xlobgetvalue(ep,sym,&val))
		return (val);
	}

	/* check an environment stack frame */
	else {
	    for (; ep; ep = cdr(ep))
		if (sym == car(car(ep)))
		    return (cdr(car(ep)));
	}

    /* return the global value */
    return (getvalue(sym));
}

/* xlsetvalue - set the value of a symbol */
void xlsetvalue(LVAL sym, LVAL val)
{
    register LVAL fp,ep;

    /* look for the symbol in the environment list */
    for (fp = xlenv; fp; fp = cdr(fp))

	/* check for an instance variable */
	if ((ep = car(fp)) && objectp(car(ep))) {
	    if (xlobsetvalue(ep,sym,val))
		return;
	}

	/* check an environment stack frame */
	else {
	    for (; ep; ep = cdr(ep))
		if (sym == car(car(ep))) {
		    rplacd(car(ep),val);
		    return;
		}
	}

    /* store the global value */
    setvalue(sym,val);
}

/* xlgetfunction - get the functional value of a symbol (with check) */
LVAL xlgetfunction(LVAL sym)
{
    LVAL val;

    /* look for the functional value of the symbol */
    while ((val = xlxgetfunction(sym)) == s_unbound)
	xlfunbound(sym);

    /* return the value */
    return (val);
}

/* xlxgetfunction - get the functional value of a symbol */
LVAL xlxgetfunction(LVAL sym)
{
    register LVAL fp,ep;

    /* check the environment list */
    for (fp = xlfenv; fp; fp = cdr(fp))
	for (ep = car(fp); ep; ep = cdr(ep))
	    if (sym == car(car(ep)))
		return (cdr(car(ep)));

    /* return the global value */
    return (getfunction(sym));
}

/* xlsetfunction - set the functional value of a symbol */
void xlsetfunction(LVAL sym, LVAL val)
{
    register LVAL fp,ep;

    /* look for the symbol in the environment list */
    for (fp = xlfenv; fp; fp = cdr(fp))
	for (ep = car(fp); ep; ep = cdr(ep))
	    if (sym == car(car(ep))) {
		rplacd(car(ep),val);
		return;
	    }

    /* store the global value */
    setfunction(sym,val);
}

/* xlgetprop - get the value of a property */
LVAL xlgetprop(LVAL sym, LVAL prp)
{
    LVAL p;
    return ((p = findprop(sym,prp)) ? car(p) : NIL);
}

/* xlputprop - put a property value onto the property list */
void xlputprop(LVAL sym, LVAL val, LVAL prp)
{
    LVAL pair;
    if (pair = findprop(sym,prp))
	rplaca(pair,val);
    else
	setplist(sym,cons(prp,cons(val,getplist(sym))));
}

/* xlremprop - remove a property from a property list */
void xlremprop(LVAL sym, LVAL prp)
{
    LVAL last,p;
    last = NIL;
    for (p = getplist(sym); consp(p) && consp(cdr(p)); p = cdr(last)) {
	if (car(p) == prp)
	    if (last)
		rplacd(last,cdr(cdr(p)));
	    else
		setplist(sym,cdr(cdr(p)));
	last = cdr(p);
    }
}

/* findprop - find a property pair */
static LVAL findprop(LVAL sym, LVAL prp)
{
    LVAL p;
    for (p = getplist(sym); consp(p) && consp(cdr(p)); p = cdr(cdr(p)))
	if (car(p) == prp)
	    return (cdr(p));
    return (NIL);
}

/* hash - hash a symbol name string */
int hash(char *str, int len)
{
    unsigned int u;	/* unsigned: the shift may overflow (UB for int) */
    int i;
    for (u = 0; *str; )
	u = (u << 2) ^ (unsigned int)(int)*str++;
    /* same value the old signed version produced on two's complement */
    i = (u >= 0x80000000u ? -(int)(0xFFFFFFFFu - u) - 1 : (int)u);
    i %= len;
    return (i < 0 ? -i : i);
}

/* xlsinit - symbol initialization routine */
void xlsinit(void)
{
    LVAL array,p;

    /* initialize the obarray */
    obarray = xlmakesym("*OBARRAY*");
    array = newvector(HSIZE);
    setvalue(obarray,array);

    /* add the symbol *OBARRAY* to the obarray */
    p = consa(obarray);
    setelement(array,hash("*OBARRAY*",HSIZE),p);
}


/* xlsys.c - xlisp builtin system functions */
/*	Copyright (c) 1985, by David Michael Betz
	All Rights Reserved
	Permission is granted for unrestricted non-commercial use	*/

/* external variables */
extern jmp_buf top_level;
extern FILE *tfp;

/* external symbols */
extern LVAL a_subr,a_fsubr,a_cons,a_symbol;
extern LVAL a_fixnum,a_flonum,a_string,a_object,a_stream;
extern LVAL a_vector,a_closure,a_char,a_ustream;
extern LVAL k_verbose,k_print;
extern LVAL s_true;

/* external routines */

/* xload - read and evaluate expressions from a file */
LVAL xload(void)
{
    unsigned char *name;
    int vflag,pflag;
    LVAL arg;

    /* get the file name */
    name = getstring(xlgetfname());

    /* get the :verbose flag */
    if (xlgetkeyarg(k_verbose,&arg))
	vflag = (arg != NIL);
    else
	vflag = TRUE;

    /* get the :print flag */
    if (xlgetkeyarg(k_print,&arg))
	pflag = (arg != NIL);
    else
	pflag = FALSE;

    /* load the file */
    return (xlload(name,vflag,pflag) ? s_true : NIL);
}

/* xtranscript - open or close a transcript file */
LVAL xtranscript(void)
{
    unsigned char *name;

    /* get the transcript file name */
    name = (moreargs() ? getstring(xlgetfname()) : NULL);
    xllastarg();

    /* close the current transcript */
    if (tfp) osclose(tfp);

    /* open the new transcript */
    tfp = (name ? osaopen(name,"w") : NULL);

    /* return T if a transcript is open, NIL otherwise */
    return (tfp ? s_true : NIL);
}

/* xtype - return type of a thing */
LVAL xtype(void)
{
    LVAL arg;

    if (!(arg = xlgetarg()))
	return (NIL);

    switch (ntype(arg)) {
    case SUBR:		return (a_subr);
    case FSUBR:		return (a_fsubr);
    case CONS:		return (a_cons);
    case SYMBOL:	return (a_symbol);
    case FIXNUM:	return (a_fixnum);
    case FLONUM:	return (a_flonum);
    case STRING:	return (a_string);
    case OBJECT:	return (a_object);
    case STREAM:	return (a_stream);
    case VECTOR:	return (a_vector);
    case CLOSURE:	return (a_closure);
    case CHAR:		return (a_char);
    case USTREAM:	return (a_ustream);
    default:		xlfail("bad node type");
    }
}

/* xbaktrace - print the trace back stack */
LVAL xbaktrace(void)
{
    LVAL num;
    int n;

    if (moreargs()) {
	num = xlgafixnum();
	n = xlfixint(num);
    }
    else
	n = -1;
    xllastarg();
    xlbaktrace(n);
    return (NIL);
}

/* xexit - get out of xlisp */
LVAL xexit(void)
{
    xllastarg();
    wrapup();
}

/*
 * PEEK and POKE read and write arbitrary memory.  On a modern protected
 * 64-bit OS they are only a way to crash the interpreter (or worse), so
 * they are compiled out unless XL_ALLOW_PEEKPOKE is defined, e.g.
 *     cl ... /DXL_ALLOW_PEEKPOKE ...
 * ADDRESS-OF only reports an address and stays available.
 */
#ifndef XL_ALLOW_PEEKPOKE

/* xpeek - disabled */
LVAL xpeek(void)
{
    xlfail("PEEK is disabled in this build (define XL_ALLOW_PEEKPOKE to enable it)");
}

/* xpoke - disabled */
LVAL xpoke(void)
{
    xlfail("POKE is disabled in this build (define XL_ALLOW_PEEKPOKE to enable it)");
}

#else

/* xpeek - peek at a location in memory */
LVAL xpeek(void)
{
    LVAL num;
    int *adr;

    /* get the address */
    num = xlgafixnum(); adr = (int *)(intptr_t)getfixnum(num);
    xllastarg();

    /* return the value at that address */
    return (cvfixnum((FIXTYPE)*adr));
}

/* xpoke - poke a value into memory */
LVAL xpoke(void)
{
    LVAL val;
    int *adr;

    /* get the address and the new value */
    val = xlgafixnum(); adr = (int *)(intptr_t)getfixnum(val);
    val = xlgafixnum();
    xllastarg();

    /* store the new value */
    *adr = (int)getfixnum(val);

    /* return the new value */
    return (val);
}

#endif /* XL_ALLOW_PEEKPOKE */

/* xaddrs - get the address of an XLISP node */
LVAL xaddrs(void)
{
    LVAL val;

    /* get the node */
    val = xlgetarg();
    xllastarg();

    /* return the address of the node */
    return (cvfixnum((FIXTYPE)(intptr_t)val));
}

/* xlisp.c - a small implementation of lisp with object-oriented programming */
/*	Copyright (c) 1987, by David Michael Betz
	All Rights Reserved
	Permission is granted for unrestricted non-commercial use	*/

/* define the banner line string */
#define BANNER	"XLISP version 2.0.1 64-bit, Copyright (c) 1988, by David Betz"

/* global variables */
jmp_buf top_level;

/* external variables */
extern LVAL s_stdin,s_evalhook,s_applyhook;
extern LVAL s_1plus,s_2plus,s_3plus,s_1star,s_2star,s_3star,s_minus;
extern int xltrcindent;
extern int xldebug;
extern LVAL s_true;
extern char buf[];
extern FILE *tfp;

/* external routines */

/* main - the main routine */
int main(int argc, char *argv[])
{
    char * volatile transcript;
    CONTEXT cntxt;
    volatile int verbose;
    int i;
    LVAL expr;

    /* setup default argument values */
    transcript = NULL;
    verbose = FALSE;

    /* parse the argument list switches */
#ifndef LSC
    for (i = 1; i < argc; ++i)
	if (argv[i][0] == '-')
	    switch(argv[i][1]) {
	    case 't':
	    case 'T':
		transcript = &argv[i][2];
		break;
	    case 'v':
	    case 'V':
		verbose = TRUE;
		break;
	    }
#endif

    /* initialize and print the banner line */
    osinit(BANNER);

    /* setup initialization error handler */
    xlbegin(&cntxt,CF_TOPLEVEL|CF_CLEANUP|CF_BRKLEVEL,(LVAL)(intptr_t)1);
    if (setjmp(cntxt.c_jmpbuf))
	xlfatal("fatal initialization error");
    if (setjmp(top_level))
	xlfatal("RESTORE not allowed during initialization");

    /* initialize xlisp */
    xlinit();
    xlend(&cntxt);

    /* reset the error handler */
    xlbegin(&cntxt,CF_TOPLEVEL|CF_CLEANUP|CF_BRKLEVEL,s_true);

    /* open the transcript file */
    if (transcript && (tfp = osaopen(transcript,"w")) == NULL) {
	stdputstr("error: can't open transcript file: ");
	stdputstr(transcript);
	stdputstr("\n");
    }

    /* load "init.lsp" */
    if (setjmp(cntxt.c_jmpbuf) == 0)
	xlload("init.lsp",TRUE,FALSE);

    /* load any files mentioned on the command line */
    if (setjmp(cntxt.c_jmpbuf) == 0)
	for (i = 1; i < argc; i++)
	    if (argv[i][0] != '-' && !xlload(argv[i],TRUE,verbose))
		xlerror("can't load file",cvstring(argv[i]));

    /* target for restore */
    if (setjmp(top_level))
	xlbegin(&cntxt,CF_TOPLEVEL|CF_CLEANUP|CF_BRKLEVEL,s_true);

    /* protect some pointers */
    xlsave1(expr);

    /* main command processing loop */
    for (;;) {

	/* setup the error return */
	if (setjmp(cntxt.c_jmpbuf)) {
	    setvalue(s_evalhook,NIL);
	    setvalue(s_applyhook,NIL);
	    xltrcindent = 0;
	    xldebug = 0;
	    xlflush();
	}

	/* print a prompt */
	stdputstr("> ");

	/* read an expression */
	if (!xlread(getvalue(s_stdin),&expr,FALSE))
	    break;

	/* save the input expression */
	xlrdsave(expr);

	/* evaluate the expression */
	expr = xleval(expr);

	/* save the result */
	xlevsave(expr);

	/* print it */
	stdprint(expr);
    }
    xlend(&cntxt);

    /* clean up */
    wrapup();
    return (0);	/* not reached */
}

/* xlrdsave - save the last expression returned by the reader */
void xlrdsave(LVAL expr)
{
    setvalue(s_3plus,getvalue(s_2plus));
    setvalue(s_2plus,getvalue(s_1plus));
    setvalue(s_1plus,getvalue(s_minus));
    setvalue(s_minus,expr);
}

/* xlevsave - save the last expression returned by the evaluator */
void xlevsave(LVAL expr)
{
    setvalue(s_3star,getvalue(s_2star));
    setvalue(s_2star,getvalue(s_1star));
    setvalue(s_1star,expr);
}

/* xlfatal - print a fatal error message and exit */
void xlfatal(char *msg)
{
    oserror(msg);
    wrapup();
}

/* wrapup - clean up and exit to the operating system */
void wrapup(void)
{
    if (tfp)
	osclose(tfp);
    osfinish();
    exit(0);
}

