/* xlimage - xlisp memory image save/restore functions */
/*	Copyright (c) 1985, by David Michael Betz
	All Rights Reserved
	Permission is granted for unrestricted non-commercial use	*/

#include "xlisp.h"

/* prototypes for functions local to this file */
static void freeimage(void);
static void setoffset(void);
static void writenode(LVAL node);
static void writeptr(OFFTYPE off);
static void readnode(int type, LVAL node);
static OFFTYPE readptr(void);
static LVAL cviptr(OFFTYPE o);
static OFFTYPE cvoptr(LVAL p);
static void initwks(void);
static XLNORETURN void findandjump(int mask, char *error);
static LVAL cxr(char *adstr);
static LVAL subst(LVAL to, LVAL from, LVAL expr, LVAL fcn, int tresult);
static LVAL sublis(LVAL alist, LVAL expr, LVAL fcn, int tresult);
static LVAL assoc(LVAL expr, LVAL alist, LVAL fcn, int tresult);
static LVAL remif(int tresult);
static LVAL nth(int carflag);
static LVAL map(int carflag, int valflag);
static LVAL delif(int tresult);
static LVAL sortlist(LVAL list, LVAL fcn);
static void splitlist(LVAL pivot, LVAL list, LVAL *psmaller, LVAL *plarger, LVAL fcn);
static LVAL gluelists(LVAL smaller, LVAL pivot, LVAL larger);
static LVAL binary(int fcn);
static LVAL unary(int fcn);
static LVAL predicate(int fcn);
static LVAL compare(int fcn);
static XLNORETURN void badiop(void);
static XLNORETURN void badfop(void);
static LVAL entermsg(LVAL cls, LVAL msg);
static LVAL sendmsg(LVAL obj, LVAL cls, LVAL sym);
static LVAL evmethod(LVAL obj, LVAL msgcls, LVAL method);
static int getivcnt(LVAL cls, int ivar);
static int listlength(LVAL list);
static void pp(LVAL expr);
static void pplist(LVAL expr);
static void ppexpr(LVAL expr);
static void ppputc(int ch);
static void ppterpri(void);
static int flatsize(LVAL expr);
static void putsymbol(LVAL fptr, char *str, int escflag);
static void putstring(LVAL fptr, LVAL str);
static void putqstring(LVAL fptr, LVAL str);
static void putatm(LVAL fptr, char *tag, LVAL val);
static void putsubr(LVAL fptr, char *tag, LVAL val);
static void putclosure(LVAL fptr, LVAL val);
static void putfixnum(LVAL fptr, FIXTYPE n);
static void putflonum(LVAL fptr, FLOTYPE n);
static void putchcode(LVAL fptr, int ch, int escflag);
static void putoct(LVAL fptr, int n);
static void pcomment(LVAL fptr);
static LVAL pnumber(LVAL fptr, int radix);
static LVAL plist(LVAL fptr);
static LVAL pvector(LVAL fptr);
static LVAL pquote(LVAL fptr, LVAL sym);
static LVAL psymbol(LVAL fptr);
static LVAL punintern(LVAL fptr);
static int pname(LVAL fptr, int *pescflag);
static int storech(char *buf, int i, int ch);
static int nextch(LVAL fptr);
static int checkeof(LVAL fptr);
static void badeof(LVAL fptr);
static void upcase(unsigned char *str);
static LVAL strcompare(int fcn, int icase);
static LVAL changecase(int fcn, int destructive);
static LVAL trim(int fcn);
static void getbounds(LVAL str, LVAL skey, LVAL ekey, int *pstart, int *pend);
static int inbag(int ch, LVAL bag);
static LVAL chrcompare(int fcn, int icase);

#ifdef SAVERESTORE

/* external variables */
extern LVAL obarray,xlenv,xlfenv,xldenv,s_gchook,s_gcflag;
extern long nnodes,nfree,total;
extern int anodes,nsegs,gccalls;
extern struct segment *segs,*lastseg,*fixseg,*charseg;
extern CONTEXT *xlcontext;
extern LVAL fnodes;

/* local variables */
static OFFTYPE off,foff;
static FILE *fp;

/* external procedures */

/* forward declarations */

/* xlisave - save the memory image */
int xlisave(char *fname)
{
    char fullname[STRMAX+1];
    unsigned char *cp;
    SEGMENT *seg;
    int n,i,max;
    LVAL p;

    /* default the extension */
    if (needsextension(fname)) {
	strcpy(fullname,fname);
	strcat(fullname,".wks");
	fname = fullname;
    }

    /* open the output file */
    if ((fp = osbopen(fname,"w")) == NULL)
	return (FALSE);

    /* first call the garbage collector to clean up memory */
    gc();

    /* write out the pointer to the *obarray* symbol */
    writeptr(cvoptr(obarray));

    /* setup the initial file offsets */
    off = foff = (OFFTYPE)2;

    /* write out all nodes that are still in use */
    for (seg = segs; seg != NULL; seg = seg->sg_next) {
	p = &seg->sg_nodes[0];
	for (n = seg->sg_size; --n >= 0; ++p, off += 2)
	    switch (ntype(p)) {
	    case FREE:
		break;
	    case CONS:
	    case USTREAM:
		setoffset();
		osbputc(p->n_type,fp);
		writeptr(cvoptr(car(p)));
		writeptr(cvoptr(cdr(p)));
		foff += 2;
		break;
	    default:
		setoffset();
		writenode(p);
		break;
	    }
    }

    /* write the terminator */
    osbputc(FREE,fp);
    writeptr((OFFTYPE)0);

    /* write out data portion of SYMBOL/VECTOR/OBJECT/STRING/CLOSURE nodes */
    for (seg = segs; seg != NULL; seg = seg->sg_next) {
	p = &seg->sg_nodes[0];
	for (n = seg->sg_size; --n >= 0; ++p)
	    switch (ntype(p)) {
	    case SYMBOL:
	    case OBJECT:
	    case VECTOR:
	    case CLOSURE:
		max = getsize(p);
		for (i = 0; i < max; ++i)
		    writeptr(cvoptr(getelement(p,i)));
		break;
	    case STRING:
		max = getslength(p);
		for (cp = getstring(p); --max >= 0; )
		    osbputc(*cp++,fp);
		break;
	    }
    }

    /* close the output file */
    osclose(fp);

    /* return successfully */
    return (TRUE);
}

/* xlirestore - restore a saved memory image */
int xlirestore(char *fname)
{
    extern FUNDEF funtab[];
    char fullname[STRMAX+1];
    unsigned char *cp;
    int n,i,max,type;
    SEGMENT *seg;
    LVAL p;

    /* default the extension */
    if (needsextension(fname)) {
	strcpy(fullname,fname);
	strcat(fullname,".wks");
	fname = fullname;
    }

    /* open the file */
    if ((fp = osbopen(fname,"r")) == NULL)
	return (FALSE);

    /* free the old memory image */
    freeimage();

    /* initialize */
    off = (OFFTYPE)2;
    total = nnodes = nfree = 0L;
    fnodes = NIL;
    segs = lastseg = NULL;
    nsegs = gccalls = 0;
    xlenv = xlfenv = xldenv = s_gchook = s_gcflag = NIL;
    xlstack = xlstkbase + EDEPTH;
    xlcontext = NULL;

    /* create the fixnum segment */
    if ((fixseg = newsegment(SFIXSIZE)) == NULL)
	xlfatal("insufficient memory - fixnum segment");

    /* create the character segment */
    if ((charseg = newsegment(CHARSIZE)) == NULL)
	xlfatal("insufficient memory - character segment");

    /* read the pointer to the *obarray* symbol */
    obarray = cviptr(readptr());

    /* read each node */
    while ((type = osbgetc(fp)) >= 0)
	switch (type) {
	case FREE:
	    if ((off = readptr()) == (OFFTYPE)0)
		goto done;
	    break;
	case CONS:
	case USTREAM:
	    p = cviptr(off);
	    p->n_type = type;
	    p->n_flags = 0;
	    rplaca(p,cviptr(readptr()));
	    rplacd(p,cviptr(readptr()));
	    off += 2;
	    break;
	default:
	    readnode(type,cviptr(off));
	    off += 2;
	    break;
	}
done:

    /* read the data portion of SYMBOL/VECTOR/OBJECT/STRING/CLOSURE nodes */
    for (seg = segs; seg != NULL; seg = seg->sg_next) {
	p = &seg->sg_nodes[0];
	for (n = seg->sg_size; --n >= 0; ++p)
	    switch (ntype(p)) {
	    case SYMBOL:
	    case OBJECT:
	    case VECTOR:
	    case CLOSURE:
		max = getsize(p);
		if ((p->n_vdata = (LVAL *)malloc(max * sizeof(LVAL))) == NULL)
		    xlfatal("insufficient memory - vector");
		total += (long)(max * sizeof(LVAL));
		for (i = 0; i < max; ++i)
		    setelement(p,i,cviptr(readptr()));
		break;
	    case STRING:
		max = getslength(p);
		if ((p->n_string = (unsigned char *)malloc(max)) == NULL)
		    xlfatal("insufficient memory - string");
		total += (long)max;
		for (cp = getstring(p); --max >= 0; )
		    *cp++ = osbgetc(fp);
		break;
	    case STREAM:
		setfile(p,NULL);
		break;
	    case SUBR:
	    case FSUBR:
		p->n_subr = funtab[getoffset(p)].fd_subr;
		break;
	    }
    }

    /* close the input file */
    osclose(fp);

    /* collect to initialize the free space */
    gc();

    /* lookup all of the symbols the interpreter uses */
    xlsymbols();

    /* return successfully */
    return (TRUE);
}

/* freeimage - free the current memory image */
static void freeimage(void)
{
    SEGMENT *seg,*next;
    FILE *fp;
    LVAL p;
    int n;

    /* free the data portion of SYMBOL/VECTOR/OBJECT/STRING nodes */
    for (seg = segs; seg != NULL; seg = next) {
	p = &seg->sg_nodes[0];
	for (n = seg->sg_size; --n >= 0; ++p)
	    switch (ntype(p)) {
	    case SYMBOL:
	    case OBJECT:
	    case VECTOR:
	    case CLOSURE:
		if (p->n_vsize)
		    free(p->n_vdata);
		break;
	    case STRING:
		if (getslength(p))
		    free(getstring(p));
		break;
	    case STREAM:
		if ((fp = getfile(p)) && (fp != stdin && fp != stdout))
		    osclose(getfile(p));
		break;
	    }
	next = seg->sg_next;
	free(seg);
    }
}

/* setoffset - output a positioning command if nodes have been skipped */
static void setoffset(void)
{
    if (off != foff) {
	osbputc(FREE,fp);
	writeptr(off);
	foff = off;
    }
}

/* writenode - write a node to a file */
static void writenode(LVAL node)
{
    char *p = (char *)&node->n_info;
    int n = sizeof(union ninfo);
    osbputc(node->n_type,fp);
    while (--n >= 0)
	osbputc(*p++,fp);
    foff += 2;
}

/* writeptr - write a pointer to a file */
static void writeptr(OFFTYPE off)
{
    char *p = (char *)&off;
    int n = sizeof(OFFTYPE);
    while (--n >= 0)
	osbputc(*p++,fp);
}

/* readnode - read a node */
static void readnode(int type, LVAL node)
{
    char *p = (char *)&node->n_info;
    int n = sizeof(union ninfo);
    node->n_type = type;
    node->n_flags = 0;
    while (--n >= 0)
	*p++ = osbgetc(fp);
}

/* readptr - read a pointer */
static OFFTYPE readptr(void)
{
    OFFTYPE off;
    char *p = (char *)&off;
    int n = sizeof(OFFTYPE);
    while (--n >= 0)
	*p++ = osbgetc(fp);
    return (off);
}

/* cviptr - convert a pointer on input */
static LVAL cviptr(OFFTYPE o)
{
    OFFTYPE off = (OFFTYPE)2;
    SEGMENT *seg;

    /* check for nil */
    if (o == (OFFTYPE)0)
	return (NIL);

    /* compute a pointer for this offset */
    for (seg = segs; seg != NULL; seg = seg->sg_next) {
	if (o >= off && o < off + (OFFTYPE)(seg->sg_size << 1))
	    return (seg->sg_nodes + ((int)(o - off) >> 1));
	off += (OFFTYPE)(seg->sg_size << 1);
    }

    /* create new segments if necessary */
    for (;;) {

	/* create the next segment */
	if ((seg = newsegment(anodes)) == NULL)
	    xlfatal("insufficient memory - segment");

	/* check to see if the offset is in this segment */
	if (o >= off && o < off + (OFFTYPE)(seg->sg_size << 1))
	    return (seg->sg_nodes + ((int)(o - off) >> 1));
	off += (OFFTYPE)(seg->sg_size << 1);
    }
}

/* cvoptr - convert a pointer on output */
static OFFTYPE cvoptr(LVAL p)
{
    OFFTYPE off = (OFFTYPE)2;
    SEGMENT *seg;

    /* check for nil and small fixnums */
    if (p == NIL)
	return ((OFFTYPE)0);

    /* compute an offset for this pointer */
    for (seg = segs; seg != NULL; seg = seg->sg_next) {
	if (CVPTR(p) >= CVPTR(&seg->sg_nodes[0]) &&
	    CVPTR(p) <  CVPTR(&seg->sg_nodes[0] + seg->sg_size))
	    return (off + (OFFTYPE)((p - seg->sg_nodes) << 1));
	off += (OFFTYPE)(seg->sg_size << 1);
    }

    /* pointer not within any segment */
    xlerror("bad pointer found during image save",p);
}

#endif



/* xlinit.c - xlisp initialization module */
/*	Copyright (c) 1985, by David Michael Betz
	All Rights Reserved
	Permission is granted for unrestricted non-commercial use	*/


/* external variables */
extern LVAL s_true,s_dot,s_unbound;
extern LVAL s_quote,s_function,s_bquote,s_comma,s_comat;
extern LVAL s_lambda,s_macro;
extern LVAL s_stdin,s_stdout,s_stderr,s_debugio,s_traceout;
extern LVAL s_evalhook,s_applyhook,s_tracelist;
extern LVAL s_tracenable,s_tlimit,s_breakenable;
extern LVAL s_setf,s_car,s_cdr,s_nth,s_aref,s_get,s_eql;
extern LVAL s_svalue,s_sfunction,s_splist;
extern LVAL s_rtable,k_wspace,k_const,k_nmacro,k_tmacro;
extern LVAL k_sescape,k_mescape;
extern LVAL s_ifmt,s_ffmt,s_printcase;
extern LVAL s_1plus,s_2plus,s_3plus,s_1star,s_2star,s_3star,s_minus;
extern LVAL k_test,k_tnot;
extern LVAL k_direction,k_input,k_output;
extern LVAL k_start,k_end,k_1start,k_1end,k_2start,k_2end;
extern LVAL k_verbose,k_print,k_count,k_key,k_upcase,k_downcase;
extern LVAL lk_optional,lk_rest,lk_key,lk_aux,lk_allow_other_keys;
extern LVAL a_subr,a_fsubr,a_cons,a_symbol;
extern LVAL a_fixnum,a_flonum,a_string,a_stream,a_object;
extern LVAL a_vector,a_closure,a_char,a_ustream;
extern LVAL s_gcflag,s_gchook;
extern FUNDEF funtab[];

/* xlinit - xlisp initialization routine */
void xlinit(void)
{
    /* initialize xlisp (must be in this order) */
    xlminit();	/* initialize xldmem.c */
    xldinit();	/* initialize xldbug.c */

    /* finish initializing */
#ifdef SAVERESTORE
    if (!xlirestore("xlisp.wks"))
#endif
	initwks();
}

/* initwks - build an initial workspace */
static void initwks(void)
{
    FUNDEF *p;
    int i;
    
    xlsinit();	/* initialize xlsym.c */
    xlsymbols();/* enter all symbols used by the interpreter */
    xlrinit();	/* initialize xlread.c */
    xloinit();	/* initialize xlobj.c */

    /* setup defaults */
    setvalue(s_evalhook,NIL);		/* no evalhook function */
    setvalue(s_applyhook,NIL);		/* no applyhook function */
    setvalue(s_tracelist,NIL);		/* no functions being traced */
    setvalue(s_tracenable,NIL);		/* traceback disabled */
    setvalue(s_tlimit,NIL); 		/* trace limit infinite */
    setvalue(s_breakenable,NIL);	/* don't enter break loop on errors */
    setvalue(s_gcflag,NIL);		/* don't show gc information */
    setvalue(s_gchook,NIL);		/* no gc hook active */
    setvalue(s_ifmt,cvstring(IFMT));	/* integer print format */
    setvalue(s_ffmt,cvstring("%g"));	/* float print format */
    setvalue(s_printcase,k_upcase);	/* upper case output of symbols */

    /* install the built-in functions and special forms */
    for (i = 0, p = funtab; p->fd_subr != NULL; ++i, ++p)
	if (p->fd_name)
	    xlsubr(p->fd_name,p->fd_type,p->fd_subr,i);

    /* add some synonyms */
    setfunction(xlenter("NOT"),getfunction(xlenter("NULL")));
    setfunction(xlenter("FIRST"),getfunction(xlenter("CAR")));
    setfunction(xlenter("SECOND"),getfunction(xlenter("CADR")));
    setfunction(xlenter("THIRD"),getfunction(xlenter("CADDR")));
    setfunction(xlenter("FOURTH"),getfunction(xlenter("CADDDR")));
    setfunction(xlenter("REST"),getfunction(xlenter("CDR")));
}

/* xlsymbols - enter all of the symbols used by the interpreter */
void xlsymbols(void)
{
    LVAL sym;

    /* enter the unbound variable indicator (must be first) */
    s_unbound = xlenter("*UNBOUND*");
    setvalue(s_unbound,s_unbound);

    /* enter the 't' symbol */
    s_true = xlenter("T");
    setvalue(s_true,s_true);

    /* enter some important symbols */
    s_dot	= xlenter(".");
    s_quote	= xlenter("QUOTE");
    s_function	= xlenter("FUNCTION");
    s_bquote	= xlenter("BACKQUOTE");
    s_comma	= xlenter("COMMA");
    s_comat	= xlenter("COMMA-AT");
    s_lambda	= xlenter("LAMBDA");
    s_macro	= xlenter("MACRO");
    s_eql	= xlenter("EQL");
    s_ifmt	= xlenter("*INTEGER-FORMAT*");
    s_ffmt	= xlenter("*FLOAT-FORMAT*");

    /* symbols set by the read-eval-print loop */
    s_1plus	= xlenter("+");
    s_2plus	= xlenter("++");
    s_3plus	= xlenter("+++");
    s_1star	= xlenter("*");
    s_2star	= xlenter("**");
    s_3star	= xlenter("***");
    s_minus	= xlenter("-");

    /* enter setf place specifiers */
    s_setf	= xlenter("*SETF*");
    s_car	= xlenter("CAR");
    s_cdr	= xlenter("CDR");
    s_nth	= xlenter("NTH");
    s_aref	= xlenter("AREF");
    s_get	= xlenter("GET");
    s_svalue	= xlenter("SYMBOL-VALUE");
    s_sfunction	= xlenter("SYMBOL-FUNCTION");
    s_splist	= xlenter("SYMBOL-PLIST");

    /* enter the readtable variable and keywords */
    s_rtable	= xlenter("*READTABLE*");
    k_wspace	= xlenter(":WHITE-SPACE");
    k_const	= xlenter(":CONSTITUENT");
    k_nmacro	= xlenter(":NMACRO");
    k_tmacro	= xlenter(":TMACRO");
    k_sescape	= xlenter(":SESCAPE");
    k_mescape	= xlenter(":MESCAPE");

    /* enter parameter list keywords */
    k_test	= xlenter(":TEST");
    k_tnot	= xlenter(":TEST-NOT");

    /* "open" keywords */
    k_direction = xlenter(":DIRECTION");
    k_input     = xlenter(":INPUT");
    k_output    = xlenter(":OUTPUT");

    /* enter *print-case* symbol and keywords */
    s_printcase = xlenter("*PRINT-CASE*");
    k_upcase	= xlenter(":UPCASE");
    k_downcase  = xlenter(":DOWNCASE");

    /* other keywords */
    k_start	= xlenter(":START");
    k_end	= xlenter(":END");
    k_1start	= xlenter(":START1");
    k_1end	= xlenter(":END1");
    k_2start	= xlenter(":START2");
    k_2end	= xlenter(":END2");
    k_verbose	= xlenter(":VERBOSE");
    k_print	= xlenter(":PRINT");
    k_count	= xlenter(":COUNT");
    k_key	= xlenter(":KEY");

    /* enter lambda list keywords */
    lk_optional	= xlenter("&OPTIONAL");
    lk_rest	= xlenter("&REST");
    lk_key	= xlenter("&KEY");
    lk_aux	= xlenter("&AUX");
    lk_allow_other_keys = xlenter("&ALLOW-OTHER-KEYS");

    /* enter *standard-input*, *standard-output* and *error-output* */
    s_stdin = xlenter("*STANDARD-INPUT*");
    setvalue(s_stdin,cvfile(stdin));
    s_stdout = xlenter("*STANDARD-OUTPUT*");
    setvalue(s_stdout,cvfile(stdout));
    s_stderr = xlenter("*ERROR-OUTPUT*");
    setvalue(s_stderr,cvfile(stderr));

    /* enter *debug-io* and *trace-output* */
    s_debugio = xlenter("*DEBUG-IO*");
    setvalue(s_debugio,getvalue(s_stderr));
    s_traceout = xlenter("*TRACE-OUTPUT*");
    setvalue(s_traceout,getvalue(s_stderr));

    /* enter the eval and apply hook variables */
    s_evalhook = xlenter("*EVALHOOK*");
    s_applyhook = xlenter("*APPLYHOOK*");

    /* enter the symbol pointing to the list of functions being traced */
    s_tracelist = xlenter("*TRACELIST*");

    /* enter the error traceback and the error break enable flags */
    s_tracenable = xlenter("*TRACENABLE*");
    s_tlimit = xlenter("*TRACELIMIT*");
    s_breakenable = xlenter("*BREAKENABLE*");

    /* enter a symbol to control printing of garbage collection messages */
    s_gcflag = xlenter("*GC-FLAG*");
    s_gchook = xlenter("*GC-HOOK*");

    /* enter a copyright notice into the oblist */
    sym = xlenter("**Copyright-1988-by-David-Betz**");
    setvalue(sym,s_true);

    /* enter type names */
    a_subr	= xlenter("SUBR");
    a_fsubr	= xlenter("FSUBR");
    a_cons	= xlenter("CONS");
    a_symbol	= xlenter("SYMBOL");
    a_fixnum	= xlenter("FIXNUM");
    a_flonum	= xlenter("FLONUM");
    a_string	= xlenter("STRING");
    a_object	= xlenter("OBJECT");
    a_stream	= xlenter("FILE-STREAM");
    a_vector	= xlenter("ARRAY");
    a_closure	= xlenter("CLOSURE");
    a_char      = xlenter("CHARACTER");
    a_ustream	= xlenter("UNNAMED-STREAM");

    /* add the object-oriented programming symbols and os specific stuff */
    obsymbols();	/* object-oriented programming symbols */
    ossymbols();	/* os specific symbols */
}

/* xlio - xlisp i/o routines */
/*	Copyright (c) 1985, by David Michael Betz
	All Rights Reserved
	Permission is granted for unrestricted non-commercial use	*/

/* external variables */
extern LVAL s_stdin,s_stdout,s_stderr,s_debugio,s_traceout,s_unbound;
extern int xlfsize;

/* xlgetc - get a character from a file or stream */
int xlgetc(LVAL fptr)
{
    LVAL lptr,cptr;
    FILE *fp;
    int ch;

    /* check for input from nil */
    if (fptr == NIL)
	ch = EOF;

    /* otherwise, check for input from a stream */
    else if (ustreamp(fptr)) {
	if ((lptr = gethead(fptr)) == NIL)
	    ch = EOF;
	else {
	    if (!consp(lptr) || (cptr = car(lptr)) == NIL || !charp(cptr))
		xlfail("bad stream");
	    sethead(fptr,lptr = cdr(lptr));
	    if (lptr == NIL)
		settail(fptr,NIL);
	    ch = getchcode(cptr);
	}
    }

    /* otherwise, check for a buffered character */
    else if (ch = getsavech(fptr))
	setsavech(fptr,'\0');

    /* otherwise, check for terminal input or file input */
    else {
	fp = getfile(fptr);
	if (fp == stdin || fp == stderr)
	    ch = ostgetc();
	else
	    ch = osagetc(fp);
    }

    /* return the character */
    return (ch);
}

/* xlungetc - unget a character */
void xlungetc(LVAL fptr, int ch)
{
    LVAL lptr;
    
    /* check for ungetc from nil */
    if (fptr == NIL)
	;
	
    /* otherwise, check for ungetc to a stream */
    if (ustreamp(fptr)) {
	if (ch != EOF) {
	    lptr = cons(cvchar(ch),gethead(fptr));
	    if (gethead(fptr) == NIL)
		settail(fptr,lptr);
	    sethead(fptr,lptr);
	}
    }
    
    /* otherwise, it must be a file */
    else
	setsavech(fptr,ch);
}

/* xlpeek - peek at a character from a file or stream */
int xlpeek(LVAL fptr)
{
    LVAL lptr,cptr;
    int ch;

    /* check for input from nil */
    if (fptr == NIL)
	ch = EOF;

    /* otherwise, check for input from a stream */
    else if (ustreamp(fptr)) {
	if ((lptr = gethead(fptr)) == NIL)
	    ch = EOF;
	else {
	    if (!consp(lptr) || (cptr = car(lptr)) == NIL || !charp(cptr))
		xlfail("bad stream");
	    ch = getchcode(cptr);
	}
    }

    /* otherwise, get the next file character and save it */
    else {
	ch = xlgetc(fptr);
	setsavech(fptr,ch);
    }

    /* return the character */
    return (ch);
}

/* xlputc - put a character to a file or stream */
void xlputc(LVAL fptr, int ch)
{
    LVAL lptr;
    FILE *fp;

    /* count the character */
    ++xlfsize;

    /* check for output to nil */
    if (fptr == NIL)
	;

    /* otherwise, check for output to an unnamed stream */
    else if (ustreamp(fptr)) {
	lptr = consa(cvchar(ch));
	if (gettail(fptr))
	    rplacd(gettail(fptr),lptr);
	else
	    sethead(fptr,lptr);
	settail(fptr,lptr);
    }

    /* otherwise, check for terminal output or file output */
    else {
	fp = getfile(fptr);
	if (fp == stdout || fp == stderr)
	    ostputc(ch);
	else
	    osaputc(ch,fp);
    }
}

/* xlflush - flush the input buffer */
void xlflush(void)
{
    osflush();
}

/* stdprint - print to *standard-output* */
void stdprint(LVAL expr)
{
    xlprint(getvalue(s_stdout),expr,TRUE);
    xlterpri(getvalue(s_stdout));
}

/* stdputstr - print a string to *standard-output* */
void stdputstr(char *str)
{
    xlputstr(getvalue(s_stdout),str);
}

/* errprint - print to *error-output* */
void errprint(LVAL expr)
{
    xlprint(getvalue(s_stderr),expr,TRUE);
    xlterpri(getvalue(s_stderr));
}

/* errputstr - print a string to *error-output* */
void errputstr(char *str)
{
    xlputstr(getvalue(s_stderr),str);
}

/* dbgprint - print to *debug-io* */
void dbgprint(LVAL expr)
{
    xlprint(getvalue(s_debugio),expr,TRUE);
    xlterpri(getvalue(s_debugio));
}

/* dbgputstr - print a string to *debug-io* */
void dbgputstr(char *str)
{
    xlputstr(getvalue(s_debugio),str);
}

/* trcprin1 - print to *trace-output* */
void trcprin1(LVAL expr)
{
    xlprint(getvalue(s_traceout),expr,TRUE);
}

/* trcputstr - print a string to *trace-output* */
void trcputstr(char *str)
{
    xlputstr(getvalue(s_traceout),str);
}

/* xljump - execution context routines */
/*	Copyright (c) 1985, by David Michael Betz
	All Rights Reserved
	Permission is granted for unrestricted non-commercial use	*/

/* external variables */
extern CONTEXT *xlcontext,*xltarget;
extern LVAL xlvalue,xlenv,xlfenv,xldenv;
extern int xlmask;

/* xlbegin - beginning of an execution context */
void xlbegin(CONTEXT *cptr, int flags, LVAL expr)
{
    cptr->c_flags = flags;
    cptr->c_expr = expr;
    cptr->c_xlstack = xlstack;
    cptr->c_xlenv = xlenv;
    cptr->c_xlfenv = xlfenv;
    cptr->c_xldenv = xldenv;
    cptr->c_xlcontext = xlcontext;
    cptr->c_xlargv = xlargv;
    cptr->c_xlargc = xlargc;
    cptr->c_xlfp = xlfp;
    cptr->c_xlsp = xlsp;
    xlcontext = cptr;
}

/* xlend - end of an execution context */
void xlend(CONTEXT *cptr)
{
    xlcontext = cptr->c_xlcontext;
}

/* xlgo - go to a label */
void xlgo(LVAL label)
{
    CONTEXT *cptr;
    LVAL *argv;
    int argc;

    /* find a tagbody context */
    for (cptr = xlcontext; cptr; cptr = cptr->c_xlcontext)
	if (cptr->c_flags & CF_GO) {
	    argc = cptr->c_xlargc;
	    argv = cptr->c_xlargv;
	    while (--argc >= 0)
		if (*argv++ == label) {
		    cptr->c_xlargc = argc;
		    cptr->c_xlargv = argv;
		    xljump(cptr,CF_GO,NIL);
		}
	}
    xlfail("no target for GO");
}

/* xlreturn - return from a block */
void xlreturn(LVAL name, LVAL val)
{
    CONTEXT *cptr;

    /* find a block context */
    for (cptr = xlcontext; cptr; cptr = cptr->c_xlcontext)
	if (cptr->c_flags & CF_RETURN && cptr->c_expr == name)
	    xljump(cptr,CF_RETURN,val);
    xlfail("no target for RETURN");
}

/* xlthrow - throw to a catch */
void xlthrow(LVAL tag, LVAL val)
{
    CONTEXT *cptr;

    /* find a catch context */
    for (cptr = xlcontext; cptr; cptr = cptr->c_xlcontext)
	if ((cptr->c_flags & CF_THROW) && cptr->c_expr == tag)
	    xljump(cptr,CF_THROW,val);
    xlfail("no target for THROW");
}

/* xlsignal - signal an error */
void xlsignal(char *emsg, LVAL arg)
{
    CONTEXT *cptr;

    /* find an error catcher */
    for (cptr = xlcontext; cptr; cptr = cptr->c_xlcontext)
	if (cptr->c_flags & CF_ERROR) {
	    if (cptr->c_expr && emsg)
		xlerrprint("error",NULL,emsg,arg);
	    xljump(cptr,CF_ERROR,NIL);
	}
}

/* xltoplevel - go back to the top level */
void xltoplevel(void)
{
    stdputstr("[ back to top level ]\n");
    findandjump(CF_TOPLEVEL,"no top level");
}

/* xlbrklevel - go back to the previous break level */
void xlbrklevel(void)
{
    findandjump(CF_BRKLEVEL,"no previous break level");
}

/* xlcleanup - clean-up after an error */
void xlcleanup(void)
{
    stdputstr("[ back to previous break level ]\n");
    findandjump(CF_CLEANUP,"not in a break loop");
}

/* xlcontinue - continue from an error */
void xlcontinue(void)
{
    findandjump(CF_CONTINUE,"not in a break loop");
}

/* xljump - jump to a saved execution context */
void xljump(CONTEXT *target, int mask, LVAL val)
{
    /* unwind the execution stack */
    for (; xlcontext != target; xlcontext = xlcontext->c_xlcontext)

	/* check for an UNWIND-PROTECT */
	if ((xlcontext->c_flags & CF_UNWIND)) {
	    xltarget = target;
	    xlmask = mask;
	    break;
	}
	   
    /* restore the state */
    xlstack = xlcontext->c_xlstack;
    xlenv = xlcontext->c_xlenv;
    xlfenv = xlcontext->c_xlfenv;
    xlunbind(xlcontext->c_xldenv);
    xlargv = xlcontext->c_xlargv;
    xlargc = xlcontext->c_xlargc;
    xlfp = xlcontext->c_xlfp;
    xlsp = xlcontext->c_xlsp;
    xlvalue = val;

    /* call the handler */
    longjmp(xlcontext->c_jmpbuf,mask);
}

/* findandjump - find a target context frame and jump to it */
static void findandjump(int mask, char *error)
{
    CONTEXT *cptr;

    /* find a block context */
    for (cptr = xlcontext; cptr; cptr = cptr->c_xlcontext)
	if (cptr->c_flags & mask)
	    xljump(cptr,mask,NIL);
    xlabort(error);
}

/* xllist.c - xlisp built-in list functions */
/*	Copyright (c) 1985, by David Michael Betz
	All Rights Reserved
	Permission is granted for unrestricted non-commercial use	*/

/* forward declarations */

/* xcar - take the car of a cons cell */
LVAL xcar(void)
{
    LVAL list;
    list = xlgalist();
    xllastarg();
    return (list ? car(list) : NIL);
}

/* xcdr - take the cdr of a cons cell */
LVAL xcdr(void)
{
    LVAL list;
    list = xlgalist();
    xllastarg();
    return (list ? cdr(list) : NIL);
}

/* cxxr functions */
LVAL xcaar(void) { return (cxr("aa")); }
LVAL xcadr(void) { return (cxr("da")); }
LVAL xcdar(void) { return (cxr("ad")); }
LVAL xcddr(void) { return (cxr("dd")); }

/* cxxxr functions */
LVAL xcaaar(void) { return (cxr("aaa")); }
LVAL xcaadr(void) { return (cxr("daa")); }
LVAL xcadar(void) { return (cxr("ada")); }
LVAL xcaddr(void) { return (cxr("dda")); }
LVAL xcdaar(void) { return (cxr("aad")); }
LVAL xcdadr(void) { return (cxr("dad")); }
LVAL xcddar(void) { return (cxr("add")); }
LVAL xcdddr(void) { return (cxr("ddd")); }

/* cxxxxr functions */
LVAL xcaaaar(void) { return (cxr("aaaa")); }
LVAL xcaaadr(void) { return (cxr("daaa")); }
LVAL xcaadar(void) { return (cxr("adaa")); }
LVAL xcaaddr(void) { return (cxr("ddaa")); }
LVAL xcadaar(void) { return (cxr("aada")); }
LVAL xcadadr(void) { return (cxr("dada")); }
LVAL xcaddar(void) { return (cxr("adda")); }
LVAL xcadddr(void) { return (cxr("ddda")); }
LVAL xcdaaar(void) { return (cxr("aaad")); }
LVAL xcdaadr(void) { return (cxr("daad")); }
LVAL xcdadar(void) { return (cxr("adad")); }
LVAL xcdaddr(void) { return (cxr("ddad")); }
LVAL xcddaar(void) { return (cxr("aadd")); }
LVAL xcddadr(void) { return (cxr("dadd")); }
LVAL xcdddar(void) { return (cxr("addd")); }
LVAL xcddddr(void) { return (cxr("dddd")); }

/* cxr - common car/cdr routine */
static LVAL cxr(char *adstr)
{
    LVAL list;

    /* get the list */
    list = xlgalist();
    xllastarg();

    /* perform the car/cdr operations */
    while (*adstr && consp(list))
	list = (*adstr++ == 'a' ? car(list) : cdr(list));

    /* make sure the operation succeeded */
    if (*adstr && list)
	xlfail("bad argument");

    /* return the result */
    return (list);
}

/* xcons - construct a new list cell */
LVAL xcons(void)
{
    LVAL arg1,arg2;

    /* get the two arguments */
    arg1 = xlgetarg();
    arg2 = xlgetarg();
    xllastarg();

    /* construct a new list element */
    return (cons(arg1,arg2));
}

/* xlist - built a list of the arguments */
LVAL xlist(void)
{
    LVAL last=NIL,next,val;

    /* protect some pointers */
    xlsave1(val);

    /* add each argument to the list */
    for (val = NIL; moreargs(); ) {

	/* append this argument to the end of the list */
	next = consa(nextarg());
	if (val) rplacd(last,next);
	else val = next;
	last = next;
    }

    /* restore the stack */
    xlpop();

    /* return the list */
    return (val);
}

/* xappend - built-in function append */
LVAL xappend(void)
{
    LVAL list,last=NIL,next,val;

    /* protect some pointers */
    xlsave1(val);

    /* initialize */
    val = NIL;
    
    /* append each argument */
    if (moreargs()) {
	while (xlargc > 1) {

	    /* append each element of this list to the result list */
	    for (list = nextarg(); consp(list); list = cdr(list)) {
		next = consa(car(list));
		if (val) rplacd(last,next);
		else val = next;
		last = next;
	    }
	}

	/* handle the last argument */
	if (val) rplacd(last,nextarg());
	else val = nextarg();
    }

    /* restore the stack */
    xlpop();

    /* return the list */
    return (val);
}

/* xreverse - built-in function reverse */
LVAL xreverse(void)
{
    LVAL list,val;

    /* protect some pointers */
    xlsave1(val);

    /* get the list to reverse */
    list = xlgalist();
    xllastarg();

    /* append each element to the head of the result list */
    for (val = NIL; consp(list); list = cdr(list))
	val = cons(car(list),val);

    /* restore the stack */
    xlpop();

    /* return the list */
    return (val);
}

/* xlast - return the last cons of a list */
LVAL xlast(void)
{
    LVAL list;

    /* get the list */
    list = xlgalist();
    xllastarg();

    /* find the last cons */
    while (consp(list) && cdr(list))
	list = cdr(list);

    /* return the last element */
    return (list);
}

/* xmember - built-in function 'member' */
LVAL xmember(void)
{
    LVAL x,list,fcn,val;
    int tresult;

    /* protect some pointers */
    xlsave1(fcn);

    /* get the expression to look for and the list */
    x = xlgetarg();
    list = xlgalist();
    xltest(&fcn,&tresult);

    /* look for the expression */
    for (val = NIL; consp(list); list = cdr(list))
	if (dotest2(x,car(list),fcn) == tresult) {
	    val = list;
	    break;
	}

    /* restore the stack */
    xlpop();

    /* return the result */
    return (val);
}

/* xassoc - built-in function 'assoc' */
LVAL xassoc(void)
{
    LVAL x,alist,fcn,pair,val;
    int tresult;

    /* protect some pointers */
    xlsave1(fcn);

    /* get the expression to look for and the association list */
    x = xlgetarg();
    alist = xlgalist();
    xltest(&fcn,&tresult);

    /* look for the expression */
    for (val = NIL; consp(alist); alist = cdr(alist))
	if ((pair = car(alist)) && consp(pair))
	    if (dotest2(x,car(pair),fcn) == tresult) {
		val = pair;
		break;
	    }

    /* restore the stack */
    xlpop();

    /* return result */
    return (val);
}

/* xsubst - substitute one expression for another */
LVAL xsubst(void)
{
    LVAL to,from,expr,fcn,val;
    int tresult;

    /* protect some pointers */
    xlsave1(fcn);

    /* get the to value, the from value and the expression */
    to = xlgetarg();
    from = xlgetarg();
    expr = xlgetarg();
    xltest(&fcn,&tresult);

    /* do the substitution */
    val = subst(to,from,expr,fcn,tresult);

    /* restore the stack */
    xlpop();

    /* return the result */
    return (val);
}

/* subst - substitute one expression for another */
static LVAL subst(LVAL to, LVAL from, LVAL expr, LVAL fcn, int tresult)
{
    LVAL carval,cdrval;

    if (dotest2(expr,from,fcn) == tresult)
	return (to);
    else if (consp(expr)) {
	xlsave1(carval);
	carval = subst(to,from,car(expr),fcn,tresult);
	cdrval = subst(to,from,cdr(expr),fcn,tresult);
	xlpop();
	return (cons(carval,cdrval));
    }
    else
	return (expr);
}

/* xsublis - substitute using an association list */
LVAL xsublis(void)
{
    LVAL alist,expr,fcn,val;
    int tresult;

    /* protect some pointers */
    xlsave1(fcn);

    /* get the assocation list and the expression */
    alist = xlgalist();
    expr = xlgetarg();
    xltest(&fcn,&tresult);

    /* do the substitution */
    val = sublis(alist,expr,fcn,tresult);

    /* restore the stack */
    xlpop();

    /* return the result */
    return (val);
}

/* sublis - substitute using an association list */
static LVAL sublis(LVAL alist, LVAL expr, LVAL fcn, int tresult)
{
    LVAL carval,cdrval,pair;

    if (pair = assoc(expr,alist,fcn,tresult))
	return (cdr(pair));
    else if (consp(expr)) {
	xlsave1(carval);
	carval = sublis(alist,car(expr),fcn,tresult);
	cdrval = sublis(alist,cdr(expr),fcn,tresult);
	xlpop();
	return (cons(carval,cdrval));
    }
    else
	return (expr);
}

/* assoc - find a pair in an association list */
static LVAL assoc(LVAL expr, LVAL alist, LVAL fcn, int tresult)
{
    LVAL pair;

    for (; consp(alist); alist = cdr(alist))
	if ((pair = car(alist)) && consp(pair))
	    if (dotest2(expr,car(pair),fcn) == tresult)
		return (pair);
    return (NIL);
}

/* xremove - built-in function 'remove' */
LVAL xremove(void)
{
    LVAL x,list,fcn,val,last=NIL,next;
    int tresult;

    /* protect some pointers */
    xlstkcheck(2);
    xlsave(fcn);
    xlsave(val);

    /* get the expression to remove and the list */
    x = xlgetarg();
    list = xlgalist();
    xltest(&fcn,&tresult);

    /* remove matches */
    for (; consp(list); list = cdr(list))

	/* check to see if this element should be deleted */
	if (dotest2(x,car(list),fcn) != tresult) {
	    next = consa(car(list));
	    if (val) rplacd(last,next);
	    else val = next;
	    last = next;
	}

    /* restore the stack */
    xlpopn(2);

    /* return the updated list */
    return (val);
}

/* xremif - built-in function 'remove-if' */
LVAL xremif(void)
{
    return (remif(TRUE));
}

/* xremifnot - built-in function 'remove-if-not' */
LVAL xremifnot(void)
{
    return (remif(FALSE));
}

/* remif - common code for 'remove-if' and 'remove-if-not' */
static LVAL remif(int tresult)
{
    LVAL list,fcn,val,last=NIL,next;

    /* protect some pointers */
    xlstkcheck(2);
    xlsave(fcn);
    xlsave(val);

    /* get the expression to remove and the list */
    fcn = xlgetarg();
    list = xlgalist();
    xllastarg();

    /* remove matches */
    for (; consp(list); list = cdr(list))

	/* check to see if this element should be deleted */
	if (dotest1(car(list),fcn) != tresult) {
	    next = consa(car(list));
	    if (val) rplacd(last,next);
	    else val = next;
	    last = next;
	}

    /* restore the stack */
    xlpopn(2);

    /* return the updated list */
    return (val);
}

/* dotest1 - call a test function with one argument */
int dotest1(LVAL arg, LVAL fun)
{
    LVAL *newfp;

    /* create the new call frame */
    newfp = xlsp;
    pusharg(cvfixnum((FIXTYPE)(newfp - xlfp)));
    pusharg(fun);
    pusharg(cvfixnum((FIXTYPE)1));
    pusharg(arg);
    xlfp = newfp;

    /* return the result of applying the test function */
    return (xlapply(1) != NIL);

}

/* dotest2 - call a test function with two arguments */
int dotest2(LVAL arg1, LVAL arg2, LVAL fun)
{
    LVAL *newfp;

    /* create the new call frame */
    newfp = xlsp;
    pusharg(cvfixnum((FIXTYPE)(newfp - xlfp)));
    pusharg(fun);
    pusharg(cvfixnum((FIXTYPE)2));
    pusharg(arg1);
    pusharg(arg2);
    xlfp = newfp;

    /* return the result of applying the test function */
    return (xlapply(2) != NIL);

}

/* xnth - return the nth element of a list */
LVAL xnth(void)
{
    return (nth(TRUE));
}

/* xnthcdr - return the nth cdr of a list */
LVAL xnthcdr(void)
{
    return (nth(FALSE));
}

/* nth - internal nth function */
static LVAL nth(int carflag)
{
    LVAL list,num;
    FIXTYPE n;

    /* get n and the list */
    num = xlgafixnum();
    list = xlgacons();
    xllastarg();

    /* make sure the number isn't negative */
    if ((n = getfixnum(num)) < 0)
	xlfail("bad argument");

    /* find the nth element */
    while (consp(list) && --n >= 0)
	list = cdr(list);

    /* return the list beginning at the nth element */
    return (carflag && consp(list) ? car(list) : list);
}

/* xlength - return the length of a list or string */
LVAL xlength(void)
{
    FIXTYPE n;
    LVAL arg;

    /* get the list or string */
    arg = xlgetarg();
    xllastarg();

    /* find the length of a list */
    if (listp(arg))
	for (n = 0; consp(arg); n++)
	    arg = cdr(arg);

    /* find the length of a string */
    else if (stringp(arg))
	n = (FIXTYPE)getslength(arg)-1;

    /* find the length of a vector */
    else if (vectorp(arg))
	n = (FIXTYPE)getsize(arg);

    /* otherwise, bad argument type */
    else
	xlerror("bad argument type",arg);

    /* return the length */
    return (cvfixnum(n));
}

/* xmapc - built-in function 'mapc' */
LVAL xmapc(void)
{
    return (map(TRUE,FALSE));
}

/* xmapcar - built-in function 'mapcar' */
LVAL xmapcar(void)
{
    return (map(TRUE,TRUE));
}

/* xmapl - built-in function 'mapl' */
LVAL xmapl(void)
{
    return (map(FALSE,FALSE));
}

/* xmaplist - built-in function 'maplist' */
LVAL xmaplist(void)
{
    return (map(FALSE,TRUE));
}

/* map - internal mapping function */
static LVAL map(int carflag, int valflag)
{
    LVAL *newfp,fun,lists,val,last,p,x,y;
    int argc;

    /* protect some pointers */
    xlstkcheck(3);
    xlsave(fun);
    xlsave(lists);
    xlsave(val);

    /* get the function to apply and the first list */
    fun = xlgetarg();
    lists = xlgalist();

    /* initialize the result list */
    val = (valflag ? NIL : lists);

    /* build a list of argument lists */
    for (lists = last = consa(lists); moreargs(); last = cdr(last))
	rplacd(last,cons(xlgalist(),NIL));

    /* loop through each of the argument lists */
    for (;;) {

	/* build an argument list from the sublists */
	newfp = xlsp;
	pusharg(cvfixnum((FIXTYPE)(newfp - xlfp)));
	pusharg(fun);
	pusharg(NIL);
	argc = 0;
	for (x = lists; x && (y = car(x)) && consp(y); x = cdr(x)) {
	    pusharg(carflag ? car(y) : y);
	    rplaca(x,cdr(y));
	    ++argc;
	}

	/* quit if any of the lists were empty */
	if (x) {
	    xlsp = newfp;
	    break;
	}

	/* apply the function to the arguments */
	newfp[2] = cvfixnum((FIXTYPE)argc);
	xlfp = newfp;
	if (valflag) {
	    p = consa(xlapply(argc));
	    if (val) rplacd(last,p);
	    else val = p;
	    last = p;
	}
	else
	    xlapply(argc);
    }

    /* restore the stack */
    xlpopn(3);

    /* return the last test expression value */
    return (val);
}

/* xrplca - replace the car of a list node */
LVAL xrplca(void)
{
    LVAL list,newcar;

    /* get the list and the new car */
    list = xlgacons();
    newcar = xlgetarg();
    xllastarg();

    /* replace the car */
    rplaca(list,newcar);

    /* return the list node that was modified */
    return (list);
}

/* xrplcd - replace the cdr of a list node */
LVAL xrplcd(void)
{
    LVAL list,newcdr;

    /* get the list and the new cdr */
    list = xlgacons();
    newcdr = xlgetarg();
    xllastarg();

    /* replace the cdr */
    rplacd(list,newcdr);

    /* return the list node that was modified */
    return (list);
}

/* xnconc - destructively append lists */
LVAL xnconc(void)
{
    LVAL next,last,val;

    /* initialize */
    val = NIL;
    
    /* concatenate each argument */
    if (moreargs()) {
	while (xlargc > 1) {

	    /* ignore everything except lists */
	    if ((next = nextarg()) && consp(next)) {

		/* concatenate this list to the result list */
		if (val) rplacd(last,next);
		else val = next;

		/* find the end of the list */
		while (consp(cdr(next)))
		    next = cdr(next);
		last = next;
	    }
	}

	/* handle the last argument */
	if (val) rplacd(last,nextarg());
	else val = nextarg();
    }

    /* return the list */
    return (val);
}

/* xdelete - built-in function 'delete' */
LVAL xdelete(void)
{
    LVAL x,list,fcn,last,val;
    int tresult;

    /* protect some pointers */
    xlsave1(fcn);

    /* get the expression to delete and the list */
    x = xlgetarg();
    list = xlgalist();
    xltest(&fcn,&tresult);

    /* delete leading matches */
    while (consp(list)) {
	if (dotest2(x,car(list),fcn) != tresult)
	    break;
	list = cdr(list);
    }
    val = last = list;

    /* delete embedded matches */
    if (consp(list)) {

	/* skip the first non-matching element */
	list = cdr(list);

	/* look for embedded matches */
	while (consp(list)) {

	    /* check to see if this element should be deleted */
	    if (dotest2(x,car(list),fcn) == tresult)
		rplacd(last,cdr(list));
	    else
		last = list;

	    /* move to the next element */
	    list = cdr(list);
 	}
    }

    /* restore the stack */
    xlpop();

    /* return the updated list */
    return (val);
}

/* xdelif - built-in function 'delete-if' */
LVAL xdelif(void)
{
    return (delif(TRUE));
}

/* xdelifnot - built-in function 'delete-if-not' */
LVAL xdelifnot(void)
{
    return (delif(FALSE));
}

/* delif - common routine for 'delete-if' and 'delete-if-not' */
static LVAL delif(int tresult)
{
    LVAL list,fcn,last,val;

    /* protect some pointers */
    xlsave1(fcn);

    /* get the expression to delete and the list */
    fcn = xlgetarg();
    list = xlgalist();
    xllastarg();

    /* delete leading matches */
    while (consp(list)) {
	if (dotest1(car(list),fcn) != tresult)
	    break;
	list = cdr(list);
    }
    val = last = list;

    /* delete embedded matches */
    if (consp(list)) {

	/* skip the first non-matching element */
	list = cdr(list);

	/* look for embedded matches */
	while (consp(list)) {

	    /* check to see if this element should be deleted */
	    if (dotest1(car(list),fcn) == tresult)
		rplacd(last,cdr(list));
	    else
		last = list;

	    /* move to the next element */
	    list = cdr(list);
 	}
    }

    /* restore the stack */
    xlpop();

    /* return the updated list */
    return (val);
}

/* xsort - built-in function 'sort' */
LVAL xsort(void)
{
    LVAL list,fcn;

    /* protect some pointers */
    xlstkcheck(2);
    xlsave(list);
    xlsave(fcn);

    /* get the list to sort and the comparison function */
    list = xlgalist();
    fcn = xlgetarg();
    xllastarg();

    /* sort the list */
    list = sortlist(list,fcn);

    /* restore the stack and return the sorted list */
    xlpopn(2);
    return (list);
}

/*
    This sorting algorithm is based on a Modula-2 sort written by
    Richie Bielak and published in the February 1988 issue of
    "Computer Language" magazine in a letter to the editor.
*/

/* sortlist - sort a list using quicksort */
static LVAL sortlist(LVAL list, LVAL fcn)
{
    LVAL smaller,pivot,larger;
    
    /* protect some pointers */
    xlstkcheck(3);
    xlsave(smaller);
    xlsave(pivot);
    xlsave(larger);
    
    /* lists with zero or one element are already sorted */
    if (consp(list) && consp(cdr(list))) {
	pivot = list; list = cdr(list);
	splitlist(pivot,list,&smaller,&larger,fcn);
	smaller = sortlist(smaller,fcn);
	larger = sortlist(larger,fcn);
	list = gluelists(smaller,pivot,larger);
    }

    /* cleanup the stack and return the sorted list */
    xlpopn(3);
    return (list);
}

/* splitlist - split the list around the pivot */
static void splitlist(LVAL pivot, LVAL list, LVAL *psmaller, LVAL *plarger, LVAL fcn)
{
    LVAL next;
    
    /* initialize the result lists */
    *psmaller = *plarger = NIL;
    
    /* split the list */
    for (; consp(list); list = next) {
	next = cdr(list);
	if (dotest2(car(list),car(pivot),fcn)) {
	    rplacd(list,*psmaller);
	    *psmaller = list;
	}
	else {
	    rplacd(list,*plarger);
	    *plarger = list;
	}
    }
}

/* gluelists - glue the smaller and larger lists with the pivot */
static LVAL gluelists(LVAL smaller, LVAL pivot, LVAL larger)
{
    LVAL last;
    
    /* larger always goes after the pivot */
    rplacd(pivot,larger);

    /* if the smaller list is empty, we're done */
    if (null(smaller))
	return (pivot);

    /* append the smaller to the front of the resulting list */
    for (last = smaller; consp(cdr(last)); last = cdr(last))
	;
    rplacd(last,pivot);
    return (smaller);
}

/* xlmath - xlisp built-in arithmetic functions */
/*	Copyright (c) 1985, by David Michael Betz
	All Rights Reserved
	Permission is granted for unrestricted non-commercial use	*/

#include <math.h>

/* external variables */
extern LVAL s_true;

/* forward declarations */

/* binary functions */
LVAL xadd(void) { return (binary('+')); } /* + */
LVAL xsub(void) { return (binary('-')); } /* - */
LVAL xmul(void) { return (binary('*')); } /* * */
LVAL xdiv(void) { return (binary('/')); } /* / */
LVAL xrem(void) { return (binary('%')); } /* rem */
LVAL xmin(void) { return (binary('m')); } /* min */
LVAL xmax(void) { return (binary('M')); } /* max */
LVAL xexpt(void) { return (binary('E')); } /* expt */
LVAL xlogand(void) { return (binary('&')); } /* logand */
LVAL xlogior(void) { return (binary('|')); } /* logior */
LVAL xlogxor(void) { return (binary('^')); } /* logxor */

/* xgcd - greatest common divisor */
LVAL xgcd(void)
{
    FIXTYPE m,n,r;
    LVAL arg;

    if (!moreargs())			/* check for identity case */
	return (cvfixnum((FIXTYPE)0));
    arg = xlgafixnum();
    n = getfixnum(arg);
    if (n < (FIXTYPE)0) n = -n;		/* absolute value */
    while (moreargs()) {
	arg = xlgafixnum();
	m = getfixnum(arg);
	if (m < (FIXTYPE)0) m = -m;	/* absolute value */
	for (;;) {			/* euclid's algorithm */
	    r = m % n;
	    if (r == (FIXTYPE)0)
		break;
	    m = n;
	    n = r;
	}
    }
    return (cvfixnum(n));
}

/* binary - handle binary operations */
static LVAL binary(int fcn)
{
    FIXTYPE ival=0,iarg;
    FLOTYPE fval=0.0,farg;
    LVAL arg;
    int mode;

    /* get the first argument */
    arg = xlgetarg();

    /* set the type of the first argument */
    if (fixp(arg)) {
	ival = getfixnum(arg);
	mode = 'I';
    }
    else if (floatp(arg)) {
	fval = getflonum(arg);
	mode = 'F';
    }
    else
	xlerror("bad argument type",arg);

    /* treat a single argument as a special case */
    if (!moreargs()) {
	switch (fcn) {
	case '-':
	    switch (mode) {
	    case 'I':
		ival = -ival;
		break;
	    case 'F':
		fval = -fval;
		break;
	    }
	    break;
	case '/':
	    switch (mode) {
	    case 'I':
		checkizero(ival);
		ival = 1 / ival;
		break;
	    case 'F':
		checkfzero(fval);
		fval = 1.0 / fval;
		break;
	    }
	}
    }

    /* handle each remaining argument */
    while (moreargs()) {

	/* get the next argument */
	arg = xlgetarg();

	/* check its type */
	if (fixp(arg)) {
	    switch (mode) {
	    case 'I':
	        iarg = getfixnum(arg);
	        break;
	    case 'F':
	        farg = (FLOTYPE)getfixnum(arg);
		break;
	    }
	}
	else if (floatp(arg)) {
	    switch (mode) {
	    case 'I':
	        fval = (FLOTYPE)ival;
		farg = getflonum(arg);
		mode = 'F';
		break;
	    case 'F':
	        farg = getflonum(arg);
		break;
	    }
	}
	else
	    xlerror("bad argument type",arg);

	/* accumulate the result value */
	switch (mode) {
	case 'I':
	    switch (fcn) {
	    case '+':	ival += iarg; break;
	    case '-':	ival -= iarg; break;
	    case '*':	ival *= iarg; break;
	    case '/':	checkizero(iarg); ival /= iarg; break;
	    case '%':	checkizero(iarg); ival %= iarg; break;
	    case 'M':	if (iarg > ival) ival = iarg; break;
	    case 'm':	if (iarg < ival) ival = iarg; break;
	    case '&':	ival &= iarg; break;
	    case '|':	ival |= iarg; break;
	    case '^':	ival ^= iarg; break;
	    default:	badiop();
	    }
	    break;
	case 'F':
	    switch (fcn) {
	    case '+':	fval += farg; break;
	    case '-':	fval -= farg; break;
	    case '*':	fval *= farg; break;
	    case '/':	checkfzero(farg); fval /= farg; break;
	    case 'M':	if (farg > fval) fval = farg; break;
	    case 'm':	if (farg < fval) fval = farg; break;
	    case 'E':	fval = pow(fval,farg); break;
	    default:	badfop();
	    }
    	    break;
	}
    }

    /* return the result */
    switch (mode) {
    case 'I':	return (cvfixnum(ival));
    case 'F':	return (cvflonum(fval));
    }
    return (NIL);	/* not reached */
}

/* checkizero - check for integer division by zero */
void checkizero(FIXTYPE iarg)
{
    if (iarg == 0)
	xlfail("division by zero");
}

/* checkfzero - check for floating point division by zero */
void checkfzero(FLOTYPE farg)
{
    if (farg == 0.0)
	xlfail("division by zero");
}

/* checkfneg - check for square root of a negative number */
void checkfneg(FLOTYPE farg)
{
    if (farg < 0.0)
	xlfail("square root of a negative number");
}

/* unary functions */
LVAL xlognot(void) { return (unary('~')); } /* lognot */
LVAL xabs(void) { return (unary('A')); } /* abs */
LVAL xadd1(void) { return (unary('+')); } /* 1+ */
LVAL xsub1(void) { return (unary('-')); } /* 1- */
LVAL xsin(void) { return (unary('S')); } /* sin */
LVAL xcos(void) { return (unary('C')); } /* cos */
LVAL xtan(void) { return (unary('T')); } /* tan */
LVAL xexp(void) { return (unary('E')); } /* exp */
LVAL xsqrt(void) { return (unary('R')); } /* sqrt */
LVAL xfix(void) { return (unary('I')); } /* truncate */
LVAL xfloat(void) { return (unary('F')); } /* float */
LVAL xrand(void) { return (unary('?')); } /* random */

/* unary - handle unary operations */
static LVAL unary(int fcn)
{
    FLOTYPE fval;
    FIXTYPE ival;
    LVAL arg;

    /* get the argument */
    arg = xlgetarg();
    xllastarg();

    /* check its type */
    if (fixp(arg)) {
	ival = getfixnum(arg);
	switch (fcn) {
	case '~':	ival = ~ival; break;
	case 'A':	ival = (ival < 0 ? -ival : ival); break;
	case '+':	ival++; break;
	case '-':	ival--; break;
	case 'I':	break;
	case 'F':	return (cvflonum((FLOTYPE)ival));
	case '?':	ival = (FIXTYPE)osrand((int)ival); break;
	default:	badiop();
	}
	return (cvfixnum(ival));
    }
    else if (floatp(arg)) {
	fval = getflonum(arg);
	switch (fcn) {
	case 'A':	fval = (fval < 0.0 ? -fval : fval); break;
	case '+':	fval += 1.0; break;
	case '-':	fval -= 1.0; break;
	case 'S':	fval = sin(fval); break;
	case 'C':	fval = cos(fval); break;
	case 'T':	fval = tan(fval); break;
	case 'E':	fval = exp(fval); break;
	case 'R':	checkfneg(fval); fval = sqrt(fval); break;
	case 'I':	return (cvfixnum((FIXTYPE)fval));
	case 'F':	break;
	default:	badfop();
	}
	return (cvflonum(fval));
    }
    else
	xlerror("bad argument type",arg);
}

/* unary predicates */
LVAL xminusp(void) { return (predicate('-')); } /* minusp */
LVAL xzerop(void) { return (predicate('Z')); } /* zerop */
LVAL xplusp(void) { return (predicate('+')); } /* plusp */
LVAL xevenp(void) { return (predicate('E')); } /* evenp */
LVAL xoddp(void) { return (predicate('O')); } /* oddp */

/* predicate - handle a predicate function */
static LVAL predicate(int fcn)
{
    FLOTYPE fval;
    FIXTYPE ival;
    LVAL arg;

    /* get the argument */
    arg = xlgetarg();
    xllastarg();

    /* check the argument type */
    if (fixp(arg)) {
	ival = getfixnum(arg);
	switch (fcn) {
	case '-':	ival = (ival < 0); break;
	case 'Z':	ival = (ival == 0); break;
	case '+':	ival = (ival > 0); break;
	case 'E':	ival = ((ival & 1) == 0); break;
	case 'O':	ival = ((ival & 1) != 0); break;
	default:	badiop();
	}
    }
    else if (floatp(arg)) {
	fval = getflonum(arg);
	switch (fcn) {
	case '-':	ival = (fval < 0); break;
	case 'Z':	ival = (fval == 0); break;
	case '+':	ival = (fval > 0); break;
	default:	badfop();
	}
    }
    else
	xlerror("bad argument type",arg);

    /* return the result value */
    return (ival ? s_true : NIL);
}

/* comparison functions */
LVAL xlss(void) { return (compare('<')); } /* < */
LVAL xleq(void) { return (compare('L')); } /* <= */
LVAL xequ(void) { return (compare('=')); } /* = */
LVAL xneq(void) { return (compare('#')); } /* /= */
LVAL xgeq(void) { return (compare('G')); } /* >= */
LVAL xgtr(void) { return (compare('>')); } /* > */

/* compare - common compare function */
static LVAL compare(int fcn)
{
    FIXTYPE icmp,ival=0,iarg=0;
    FLOTYPE fcmp,fval=0.0,farg=0.0;
    LVAL arg;
    int mode;

    /* get the first argument */
    arg = xlgetarg();

    /* set the type of the first argument */
    if (fixp(arg)) {
	ival = getfixnum(arg);
	mode = 'I';
    }
    else if (floatp(arg)) {
	fval = getflonum(arg);
	mode = 'F';
    }
    else
	xlerror("bad argument type",arg);

    /* handle each remaining argument */
    for (icmp = TRUE; icmp && moreargs(); ival = iarg, fval = farg) {

	/* get the next argument */
	arg = xlgetarg();

	/* check its type */
	if (fixp(arg)) {
	    switch (mode) {
	    case 'I':
	        iarg = getfixnum(arg);
	        break;
	    case 'F':
	        farg = (FLOTYPE)getfixnum(arg);
		break;
	    }
	}
	else if (floatp(arg)) {
	    switch (mode) {
	    case 'I':
	        fval = (FLOTYPE)ival;
		farg = getflonum(arg);
		mode = 'F';
		break;
	    case 'F':
	        farg = getflonum(arg);
		break;
	    }
	}
	else
	    xlerror("bad argument type",arg);

	/* compute result of the compare */
	switch (mode) {
	case 'I':
	    icmp = (ival < iarg ? -1 : (ival > iarg ? 1 : 0)); /* no overflow */
	    switch (fcn) {
	    case '<':	icmp = (icmp < 0); break;
	    case 'L':	icmp = (icmp <= 0); break;
	    case '=':	icmp = (icmp == 0); break;
	    case '#':	icmp = (icmp != 0); break;
	    case 'G':	icmp = (icmp >= 0); break;
	    case '>':	icmp = (icmp > 0); break;
	    }
	    break;
	case 'F':
	    fcmp = fval - farg;
	    switch (fcn) {
	    case '<':	icmp = (fcmp < 0.0); break;
	    case 'L':	icmp = (fcmp <= 0.0); break;
	    case '=':	icmp = (fcmp == 0.0); break;
	    case '#':	icmp = (fcmp != 0.0); break;
	    case 'G':	icmp = (fcmp >= 0.0); break;
	    case '>':	icmp = (fcmp > 0.0); break;
	    }
	    break;
	}
    }

    /* return the result */
    return (icmp ? s_true : NIL);
}

/* badiop - bad integer operation */
static void badiop(void)
{
    xlfail("bad integer operation");
}

/* badfop - bad floating point operation */
static void badfop(void)
{
    xlfail("bad floating point operation");
}

/* xlobj - xlisp object functions */
/*	Copyright (c) 1985, by David Michael Betz
	All Rights Reserved
	Permission is granted for unrestricted non-commercial use	*/


/* external variables */
extern LVAL xlenv,xlfenv,xlvalue;
extern LVAL s_stdout,s_lambda;

/* local variables */
static LVAL s_self,k_new,k_isnew;
static LVAL class,object;

/* instance variable numbers for the class 'Class' */
#define MESSAGES	0	/* list of messages */
#define IVARS		1	/* list of instance variable names */
#define CVARS		2	/* list of class variable names */
#define CVALS		3	/* list of class variable values */
#define SUPERCLASS	4	/* pointer to the superclass */
#define IVARCNT		5	/* number of class instance variables */
#define IVARTOTAL	6	/* total number of instance variables */

/* number of instance variables for the class 'Class' */
#define CLASSSIZE	7

/* forward declarations */

/* xsend - send a message to an object */
LVAL xsend(void)
{
    LVAL obj;
    obj = xlgaobject();
    return (sendmsg(obj,getclass(obj),xlgasymbol()));
}

/* xsendsuper - send a message to the superclass of an object */
LVAL xsendsuper(void)
{
    LVAL env,p;
    for (env = xlenv; env; env = cdr(env))
	if ((p = car(env)) && objectp(car(p)))
	    return (sendmsg(car(p),
			    getivar(cdr(p),SUPERCLASS),
			    xlgasymbol()));
    xlfail("not in a method");
}

/* xlclass - define a class */
LVAL xlclass(char *name, int vcnt)
{
    LVAL sym,cls;

    /* create the class */
    sym = xlenter(name);
    cls = newobject(class,CLASSSIZE);
    setvalue(sym,cls);

    /* set the instance variable counts */
    setivar(cls,IVARCNT,cvfixnum((FIXTYPE)vcnt));
    setivar(cls,IVARTOTAL,cvfixnum((FIXTYPE)vcnt));

    /* set the superclass to 'Object' */
    setivar(cls,SUPERCLASS,object);

    /* return the new class */
    return (cls);
}

/* xladdivar - enter an instance variable */
void xladdivar(LVAL cls, char *var)
{
    setivar(cls,IVARS,cons(xlenter(var),getivar(cls,IVARS)));
}

/* xladdmsg - add a message to a class */
void xladdmsg(LVAL cls, char *msg, int offset)
{
    extern FUNDEF funtab[];
    LVAL mptr;

    /* enter the message selector */
    mptr = entermsg(cls,xlenter(msg));

    /* store the method for this message */
    rplacd(mptr,cvsubr(funtab[offset].fd_subr,funtab[offset].fd_type,offset));
}

/* xlobgetvalue - get the value of an instance variable */
int xlobgetvalue(LVAL pair, LVAL sym, LVAL *pval)
{
    LVAL cls,names;
    int ivtotal,n;

    /* find the instance or class variable */
    for (cls = cdr(pair); objectp(cls); cls = getivar(cls,SUPERCLASS)) {

	/* check the instance variables */
	names = getivar(cls,IVARS);
	ivtotal = getivcnt(cls,IVARTOTAL);
	for (n = ivtotal - getivcnt(cls,IVARCNT); n < ivtotal; ++n) {
	    if (car(names) == sym) {
		*pval = getivar(car(pair),n);
		return (TRUE);
	    }
	    names = cdr(names);
	}

	/* check the class variables */
	names = getivar(cls,CVARS);
	for (n = 0; consp(names); ++n) {
	    if (car(names) == sym) {
		*pval = getelement(getivar(cls,CVALS),n);
		return (TRUE);
	    }
	    names = cdr(names);
	}
    }

    /* variable not found */
    return (FALSE);
}

/* xlobsetvalue - set the value of an instance variable */
int xlobsetvalue(LVAL pair, LVAL sym, LVAL val)
{
    LVAL cls,names;
    int ivtotal,n;

    /* find the instance or class variable */
    for (cls = cdr(pair); objectp(cls); cls = getivar(cls,SUPERCLASS)) {

	/* check the instance variables */
	names = getivar(cls,IVARS);
	ivtotal = getivcnt(cls,IVARTOTAL);
	for (n = ivtotal - getivcnt(cls,IVARCNT); n < ivtotal; ++n) {
	    if (car(names) == sym) {
		setivar(car(pair),n,val);
		return (TRUE);
	    }
	    names = cdr(names);
	}

	/* check the class variables */
	names = getivar(cls,CVARS);
	for (n = 0; consp(names); ++n) {
	    if (car(names) == sym) {
		setelement(getivar(cls,CVALS),n,val);
		return (TRUE);
	    }
	    names = cdr(names);
	}
    }

    /* variable not found */
    return (FALSE);
}

/* obisnew - default 'isnew' method */
LVAL obisnew(void)
{
    LVAL self;
    self = xlgaobject();
    xllastarg();
    return (self);
}

/* obclass - get the class of an object */
LVAL obclass(void)
{
    LVAL self;
    self = xlgaobject();
    xllastarg();
    return (getclass(self));
}

/* obshow - show the instance variables of an object */
LVAL obshow(void)
{
    LVAL self,fptr,cls,names;
    int ivtotal,n;

    /* get self and the file pointer */
    self = xlgaobject();
    fptr = (moreargs() ? xlgetfile() : getvalue(s_stdout));
    xllastarg();

    /* get the object's class */
    cls = getclass(self);

    /* print the object and class */
    xlputstr(fptr,"Object is ");
    xlprint(fptr,self,TRUE);
    xlputstr(fptr,", Class is ");
    xlprint(fptr,cls,TRUE);
    xlterpri(fptr);

    /* print the object's instance variables */
    for (; cls; cls = getivar(cls,SUPERCLASS)) {
	names = getivar(cls,IVARS);
	ivtotal = getivcnt(cls,IVARTOTAL);
	for (n = ivtotal - getivcnt(cls,IVARCNT); n < ivtotal; ++n) {
	    xlputstr(fptr,"  ");
	    xlprint(fptr,car(names),TRUE);
	    xlputstr(fptr," = ");
	    xlprint(fptr,getivar(self,n),TRUE);
	    xlterpri(fptr);
	    names = cdr(names);
	}
    }

    /* return the object */
    return (self);
}

/* clnew - create a new object instance */
LVAL clnew(void)
{
    LVAL self;
    self = xlgaobject();
    return (newobject(self,getivcnt(self,IVARTOTAL)));
}

/* clisnew - initialize a new class */
LVAL clisnew(void)
{
    LVAL self,ivars,cvars,super;
    int n;

    /* get self, the ivars, cvars and superclass */
    self = xlgaobject();
    ivars = xlgalist();
    cvars = (moreargs() ? xlgalist() : NIL);
    super = (moreargs() ? xlgaobject() : object);
    xllastarg();

    /* store the instance and class variable lists and the superclass */
    setivar(self,IVARS,ivars);
    setivar(self,CVARS,cvars);
    setivar(self,CVALS,(cvars ? newvector(listlength(cvars)) : NIL));
    setivar(self,SUPERCLASS,super);

    /* compute the instance variable count */
    n = listlength(ivars);
    setivar(self,IVARCNT,cvfixnum((FIXTYPE)n));
    n += getivcnt(super,IVARTOTAL);
    setivar(self,IVARTOTAL,cvfixnum((FIXTYPE)n));

    /* return the new class object */
    return (self);
}

/* clanswer - define a method for answering a message */
LVAL clanswer(void)
{
    LVAL self,msg,fargs,code,mptr;

    /* message symbol, formal argument list and code */
    self = xlgaobject();
    msg = xlgasymbol();
    fargs = xlgalist();
    code = xlgalist();
    xllastarg();

    /* make a new message list entry */
    mptr = entermsg(self,msg);

    /* setup the message node */
    xlprot1(fargs);
    fargs = cons(s_self,fargs); /* add 'self' as the first argument */
    rplacd(mptr,xlclose(msg,s_lambda,fargs,code,NIL,NIL));
    xlpop();

    /* return the object */
    return (self);
}

/* entermsg - add a message to a class */
static LVAL entermsg(LVAL cls, LVAL msg)
{
    LVAL lptr,mptr;

    /* lookup the message */
    for (lptr = getivar(cls,MESSAGES); lptr; lptr = cdr(lptr))
	if (car(mptr = car(lptr)) == msg)
	    return (mptr);

    /* allocate a new message entry if one wasn't found */
    xlsave1(mptr);
    mptr = consa(msg);
    setivar(cls,MESSAGES,cons(mptr,getivar(cls,MESSAGES)));
    xlpop();

    /* return the symbol node */
    return (mptr);
}

/* sendmsg - send a message to an object */
static LVAL sendmsg(LVAL obj, LVAL cls, LVAL sym)
{
    LVAL msg,msgcls,method,val,p;

    /* look for the message in the class or superclasses */
    for (msgcls = cls; msgcls; ) {

	/* lookup the message in this class */
	for (p = getivar(msgcls,MESSAGES); p; p = cdr(p))
	    if ((msg = car(p)) && car(msg) == sym)
		goto send_message;

	/* look in class's superclass */
	msgcls = getivar(msgcls,SUPERCLASS);
    }

    /* message not found */
    xlerror("no method for this message",sym);

send_message:

    /* insert the value for 'self' (overwrites message selector) */
    *--xlargv = obj;
    ++xlargc;
    
    /* invoke the method */
    if ((method = cdr(msg)) == NULL)
	xlerror("bad method",method);
    switch (ntype(method)) {
    case SUBR:
	val = (*getsubr(method))();
	break;
    case CLOSURE:
	if (gettype(method) != s_lambda)
	    xlerror("bad method",method);
	val = evmethod(obj,msgcls,method);
	break;
    default:
	xlerror("bad method",method);
    }

    /* after creating an object, send it the ":isnew" message */
    if (car(msg) == k_new && val) {
	xlprot1(val);
	sendmsg(val,getclass(val),k_isnew);
	xlpop();
    }
    
    /* return the result value */
    return (val);
}

/* evmethod - evaluate a method */
static LVAL evmethod(LVAL obj, LVAL msgcls, LVAL method)
{
    LVAL oldenv,oldfenv,cptr,name,val;
    CONTEXT cntxt;

    /* protect some pointers */
    xlstkcheck(3);
    xlsave(oldenv);
    xlsave(oldfenv);
    xlsave(cptr);

    /* create an 'object' stack entry and a new environment frame */
    oldenv = xlenv;
    oldfenv = xlfenv;
    xlenv = cons(cons(obj,msgcls),getcenv(method));
    xlenv = xlframe(xlenv);
    xlfenv = getfenv(method);

    /* bind the formal parameters */
    xlabind(method,xlargc,xlargv);

    /* setup the implicit block */
    if (name = getname(method))
	xlbegin(&cntxt,CF_RETURN,name);

    /* execute the block */
    if (name && setjmp(cntxt.c_jmpbuf))
	val = xlvalue;
    else
	for (cptr = getbody(method); consp(cptr); cptr = cdr(cptr))
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

/* getivcnt - get the number of instance variables for a class */
static int getivcnt(LVAL cls, int ivar)
{
    LVAL cnt;
    if ((cnt = getivar(cls,ivar)) == NIL || !fixp(cnt))
	xlfail("bad value for instance variable count");
    return ((int)getfixnum(cnt));
}

/* listlength - find the length of a list */
static int listlength(LVAL list)
{
    int len;
    for (len = 0; consp(list); len++)
	list = cdr(list);
    return (len);
}

/* obsymbols - initialize symbols */
void obsymbols(void)
{
    /* enter the object related symbols */
    s_self  = xlenter("SELF");
    k_new   = xlenter(":NEW");
    k_isnew = xlenter(":ISNEW");

    /* get the Object and Class symbol values */
    object = getvalue(xlenter("OBJECT"));
    class  = getvalue(xlenter("CLASS"));
}

/* xloinit - object function initialization routine */
void xloinit(void)
{
    /* create the 'Class' object */
    class = xlclass("CLASS",CLASSSIZE);
    setelement(class,0,class);

    /* create the 'Object' object */
    object = xlclass("OBJECT",0);

    /* finish initializing 'class' */
    setivar(class,SUPERCLASS,object);
    xladdivar(class,"IVARTOTAL");	/* ivar number 6 */
    xladdivar(class,"IVARCNT");		/* ivar number 5 */
    xladdivar(class,"SUPERCLASS");	/* ivar number 4 */
    xladdivar(class,"CVALS");		/* ivar number 3 */
    xladdivar(class,"CVARS");		/* ivar number 2 */
    xladdivar(class,"IVARS");		/* ivar number 1 */
    xladdivar(class,"MESSAGES");	/* ivar number 0 */
    xladdmsg(class,":NEW",FT_CLNEW);
    xladdmsg(class,":ISNEW",FT_CLISNEW);
    xladdmsg(class,":ANSWER",FT_CLANSWER);

    /* finish initializing 'object' */
    setivar(object,SUPERCLASS,NIL);
    xladdmsg(object,":ISNEW",FT_OBISNEW);
    xladdmsg(object,":CLASS",FT_OBCLASS);
    xladdmsg(object,":SHOW",FT_OBSHOW);
}

/* xlpp.c - xlisp pretty printer */
/*	Copyright (c) 1985, by David Betz
	All Rights Reserved			*/

/* external variables */
extern LVAL s_stdout;
extern int xlfsize;

/* local variables */
static int pplevel,ppmargin,ppmaxlen;
static LVAL ppfile;

/* xpp - pretty-print an expression */
LVAL xpp(void)
{
    LVAL expr;

    /* get expression to print and file pointer */
    expr = xlgetarg();
    ppfile = (moreargs() ? xlgetfile() : getvalue(s_stdout));
    xllastarg();

    /* pretty print the expression */
    pplevel = ppmargin = 0; ppmaxlen = 40;
    pp(expr); ppterpri();

    /* return nil */
    return (NIL);
}

/* pp - pretty print an expression */
static void pp(LVAL expr)
{
    if (consp(expr))
	pplist(expr);
    else
	ppexpr(expr);
}

/* pplist - pretty print a list */
static void pplist(LVAL expr)
{
    int n;

    /* if the expression will fit on one line, print it on one */
    if ((n = flatsize(expr)) < ppmaxlen) {
	xlprint(ppfile,expr,TRUE);
	pplevel += n;
    }

    /* otherwise print it on several lines */
    else {
	n = ppmargin;
	ppputc('(');
	if (atom(car(expr))) {
	    ppexpr(car(expr));
	    ppputc(' ');
	    ppmargin = pplevel;
	    expr = cdr(expr);
	}
	else
	    ppmargin = pplevel;
	for (; consp(expr); expr = cdr(expr)) {
	    pp(car(expr));
	    if (consp(cdr(expr)))
		ppterpri();
	}
	if (expr != NIL) {
	    ppputc(' '); ppputc('.'); ppputc(' ');
	    ppexpr(expr);
	}
	ppputc(')');
	ppmargin = n;
    }
}

/* ppexpr - print an expression and update the indent level */
static void ppexpr(LVAL expr)
{
    xlprint(ppfile,expr,TRUE);
    pplevel += flatsize(expr);
}

/* ppputc - output a character and update the indent level */
static void ppputc(int ch)
{
    xlputc(ppfile,ch);
    pplevel++;
}

/* ppterpri - terminate the print line and indent */
static void ppterpri(void)
{
    xlterpri(ppfile);
    for (pplevel = 0; pplevel < ppmargin; pplevel++)
	xlputc(ppfile,' ');
}

/* flatsize - compute the flat size of an expression */
static int flatsize(LVAL expr)
{
    xlfsize = 0;
    xlprint(NIL,expr,TRUE);
    return (xlfsize);
}

/* xlprint - xlisp print routine */
/*	Copyright (c) 1985, by David Michael Betz
	All Rights Reserved
	Permission is granted for unrestricted non-commercial use	*/


/* external variables */
extern LVAL s_printcase,k_downcase,k_const,k_nmacro;
extern LVAL s_ifmt,s_ffmt;
extern FUNDEF funtab[];
extern char buf[];

/* xlprint - print an xlisp value */
void xlprint(LVAL fptr, LVAL vptr, int flag)
{
    LVAL nptr,next;
    int n,i;

    /* print nil */
    if (vptr == NIL) {
	putsymbol(fptr,"NIL",flag);
	return;
    }

    /* check value type */
    switch (ntype(vptr)) {
    case SUBR:
	    putsubr(fptr,"Subr",vptr);
	    break;
    case FSUBR:
	    putsubr(fptr,"FSubr",vptr);
	    break;
    case CONS:
	    xlputc(fptr,'(');
	    for (nptr = vptr; nptr != NIL; nptr = next) {
	        xlprint(fptr,car(nptr),flag);
		if (next = cdr(nptr))
		    if (consp(next))
			xlputc(fptr,' ');
		    else {
			xlputstr(fptr," . ");
			xlprint(fptr,next,flag);
			break;
		    }
	    }
	    xlputc(fptr,')');
	    break;
    case SYMBOL:
	    putsymbol(fptr,getstring(getpname(vptr)),flag);
	    break;
    case FIXNUM:
	    putfixnum(fptr,getfixnum(vptr));
	    break;
    case FLONUM:
	    putflonum(fptr,getflonum(vptr));
	    break;
    case CHAR:
	    putchcode(fptr,getchcode(vptr),flag);
	    break;
    case STRING:
	    if (flag)
		putqstring(fptr,vptr);
	    else
		putstring(fptr,vptr);
	    break;
    case STREAM:
	    putatm(fptr,"File-Stream",vptr);
	    break;
    case USTREAM:
	    putatm(fptr,"Unnamed-Stream",vptr);
	    break;
    case OBJECT:
	    putatm(fptr,"Object",vptr);
	    break;
    case VECTOR:
	    xlputc(fptr,'#'); xlputc(fptr,'(');
	    for (i = 0, n = getsize(vptr); n-- > 0; ) {
		xlprint(fptr,getelement(vptr,i++),flag);
		if (n) xlputc(fptr,' ');
	    }
	    xlputc(fptr,')');
	    break;
    case CLOSURE:
	    putclosure(fptr,vptr);
	    break;
    case FREE:
	    putatm(fptr,"Free",vptr);
	    break;
    default:
	    putatm(fptr,"Foo",vptr);
	    break;
    }
}

/* xlterpri - terminate the current print line */
void xlterpri(LVAL fptr)
{
    xlputc(fptr,'\n');
}

/* xlputstr - output a string */
void xlputstr(LVAL fptr, char *str)
{
    while (*str)
	xlputc(fptr,*str++);
}

/* putsymbol - output a symbol */
static void putsymbol(LVAL fptr, char *str, int escflag)
{
    int downcase;
    LVAL type;
    char *p;

    /* check for printing without escapes */
    if (!escflag) {
	xlputstr(fptr,str);
	return;
    }

    /* check to see if symbol needs escape characters */
    if (tentry(*str) == k_const) {
	for (p = str; *p; ++p)
	    if (islower(*p)
	    ||  ((type = tentry(*p)) != k_const
	      && (!consp(type) || car(type) != k_nmacro))) {
		xlputc(fptr,'|');
		while (*str) {
		    if (*str == '\\' || *str == '|')
			xlputc(fptr,'\\');
		    xlputc(fptr,*str++);
		}
		xlputc(fptr,'|');
		return;
	    }
    }

    /* get the case translation flag */
    downcase = (getvalue(s_printcase) == k_downcase);

    /* check for the first character being '#' */
    if (*str == '#' || *str == '.' || isnumber(str,NULL))
	xlputc(fptr,'\\');

    /* output each character */
    while (*str) {
	/* don't escape colon until we add support for packages */
	if (*str == '\\' || *str == '|' /* || *str == ':' */)
	    xlputc(fptr,'\\');
	xlputc(fptr,(downcase && isupper(*str) ? tolower(*str++) : *str++));
    }
}

/* putstring - output a string */
static void putstring(LVAL fptr, LVAL str)
{
    unsigned char *p;
    int ch;

    /* output each character */
    for (p = getstring(str); (ch = *p) != '\0'; ++p)
	xlputc(fptr,ch);
}

/* putqstring - output a quoted string */
static void putqstring(LVAL fptr, LVAL str)
{
    unsigned char *p;
    int ch;

    /* get the string pointer */
    p = getstring(str);

    /* output the initial quote */
    xlputc(fptr,'"');

    /* output each character in the string */
    for (p = getstring(str); (ch = *p) != '\0'; ++p)

	/* check for a control character */
	if (ch < 040 || ch == '\\' || ch > 0176) {
	    xlputc(fptr,'\\');
	    switch (ch) {
	    case '\011':
		    xlputc(fptr,'t');
		    break;
	    case '\012':
		    xlputc(fptr,'n');
		    break;
	    case '\014':
		    xlputc(fptr,'f');
		    break;
	    case '\015':
		    xlputc(fptr,'r');
		    break;
	    case '\\':
		    xlputc(fptr,'\\');
		    break;
	    default:
		    putoct(fptr,ch);
		    break;
	    }
	}

	/* output a normal character */
	else
	    xlputc(fptr,ch);

    /* output the terminating quote */
    xlputc(fptr,'"');
}

/* putatm - output an atom */
static void putatm(LVAL fptr, char *tag, LVAL val)
{
    sprintf(buf,"#<%s: #",tag); xlputstr(fptr,buf);
    sprintf(buf,AFMT,(void *)val); xlputstr(fptr,buf);
    xlputc(fptr,'>');
}

/* putsubr - output a subr/fsubr */
static void putsubr(LVAL fptr, char *tag, LVAL val)
{
    sprintf(buf,"#<%s-%s: #",tag,funtab[getoffset(val)].fd_name);
    xlputstr(fptr,buf);
    sprintf(buf,AFMT,(void *)val); xlputstr(fptr,buf);
    xlputc(fptr,'>');
}

/* putclosure - output a closure */
static void putclosure(LVAL fptr, LVAL val)
{
    LVAL name;
    if (name = getname(val))
	sprintf(buf,"#<Closure-%s: #",getstring(getpname(name)));
    else
	strcpy(buf,"#<Closure: #");
    xlputstr(fptr,buf);
    sprintf(buf,AFMT,(void *)val); xlputstr(fptr,buf);
    xlputc(fptr,'>');
/*
    xlputstr(fptr,"\nName:   "); xlprint(fptr,getname(val),TRUE);
    xlputstr(fptr,"\nType:   "); xlprint(fptr,gettype(val),TRUE);
    xlputstr(fptr,"\nLambda: "); xlprint(fptr,getlambda(val),TRUE);
    xlputstr(fptr,"\nArgs:   "); xlprint(fptr,getargs(val),TRUE);
    xlputstr(fptr,"\nOargs:  "); xlprint(fptr,getoargs(val),TRUE);
    xlputstr(fptr,"\nRest:   "); xlprint(fptr,getrest(val),TRUE);
    xlputstr(fptr,"\nKargs:  "); xlprint(fptr,getkargs(val),TRUE);
    xlputstr(fptr,"\nAargs:  "); xlprint(fptr,getaargs(val),TRUE);
    xlputstr(fptr,"\nBody:   "); xlprint(fptr,getbody(val),TRUE);
    xlputstr(fptr,"\nEnv:    "); xlprint(fptr,getcenv(val),TRUE);
    xlputstr(fptr,"\nFenv:   "); xlprint(fptr,getfenv(val),TRUE);
*/
}

/* putfixnum - output a fixnum */
static void putfixnum(LVAL fptr, FIXTYPE n)
{
    unsigned char *fmt;
    LVAL val;
    fmt = ((val = getvalue(s_ifmt)) && stringp(val) ? getstring(val)
						    : (unsigned char *)IFMT);
    sprintf(buf,fmt,n);
    xlputstr(fptr,buf);
}

/* putflonum - output a flonum */
static void putflonum(LVAL fptr, FLOTYPE n)
{
    unsigned char *fmt;
    LVAL val;
    fmt = ((val = getvalue(s_ffmt)) && stringp(val) ? getstring(val)
						    : (unsigned char *)"%g");
    sprintf(buf,fmt,n);
    xlputstr(fptr,buf);
}

/* putchcode - output a character */
static void putchcode(LVAL fptr, int ch, int escflag)
{
    if (escflag) {
	switch (ch) {
	case '\n':
	    xlputstr(fptr,"#\\Newline");
	    break;
	case ' ':
	    xlputstr(fptr,"#\\Space");
	    break;
	default:
	    sprintf(buf,"#\\%c",ch);
	    xlputstr(fptr,buf);
	    break;
	}
    }
    else
	xlputc(fptr,ch);
}

/* putoct - output an octal byte value */
static void putoct(LVAL fptr, int n)
{
    sprintf(buf,"%03o",n);
    xlputstr(fptr,buf);
}

/* xlread - xlisp expression input routine */
/*	Copyright (c) 1985, by David Michael Betz
	All Rights Reserved
	Permission is granted for unrestricted non-commercial use	*/


/* symbol parser modes */
#define DONE	0
#define NORMAL	1
#define ESCAPE	2

/* external variables */
extern LVAL s_stdout,s_true,s_dot;
extern LVAL s_quote,s_function,s_bquote,s_comma,s_comat;
extern LVAL s_rtable,k_wspace,k_const,k_nmacro,k_tmacro;
extern LVAL k_sescape,k_mescape;
extern char buf[];

/* external routines */

#define WSPACE "\t \f\r\n"
#define CONST1 "!$%&*+-./0123456789:<=>?@[]^_{}~"
#define CONST2 "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"

/* forward declarations */

/* xlload - load a file of xlisp expressions */
int xlload(char *fname, int vflag, int pflag)
{
    char fullname[STRMAX+1];
    LVAL fptr,expr;
    CONTEXT cntxt;
    FILE *fp;
    int sts;

    /* protect some pointers */
    xlstkcheck(2);
    xlsave(fptr);
    xlsave(expr);

    /* default the extension */
    if (needsextension(fname)) {
	strcpy(fullname,fname);
	strcat(fullname,".lsp");
	fname = fullname;
    }

    /* allocate a file node */
    fptr = cvfile(NULL);

    /* open the file */
    if ((fp = osaopen(fname,"r")) == NULL) {
	xlpopn(2);
	return (FALSE);
    }
    setfile(fptr,fp);

    /* print the information line */
    if (vflag)
	{ sprintf(buf,"; loading \"%s\"\n",fname); stdputstr(buf); }

    /* read, evaluate and possibly print each expression in the file */
    xlbegin(&cntxt,CF_ERROR,s_true);
    if (setjmp(cntxt.c_jmpbuf))
	sts = FALSE;
    else {
	while (xlread(fptr,&expr,FALSE)) {
	    expr = xleval(expr);
	    if (pflag)
		stdprint(expr);
	}
	sts = TRUE;
    }
    xlend(&cntxt);

    /* close the file */
    osclose(getfile(fptr));
    setfile(fptr,NULL);

    /* restore the stack */
    xlpopn(2);

    /* return status */
    return (sts);
}

/* xlread - read an xlisp expression */
int xlread(LVAL fptr, LVAL *pval, int rflag)
{
    int sts;

    /* read an expression */
    while ((sts = readone(fptr,pval)) == FALSE)
	;

    /* return status */
    return (sts == EOF ? FALSE : TRUE);
}

/* readone - attempt to read a single expression */
int readone(LVAL fptr, LVAL *pval)
{
    LVAL val,type;
    int ch;

    /* get a character and check for EOF */
    if ((ch = xlgetc(fptr)) == EOF)
	return (EOF);

    /* handle white space */
    if ((type = tentry(ch)) == k_wspace)
	return (FALSE);

    /* handle symbol constituents */
    else if (type == k_const) {
	xlungetc(fptr,ch);
	*pval = psymbol(fptr);
	return (TRUE);	    
    }

    /* handle single and multiple escapes */
    else if (type == k_sescape || type == k_mescape) {
	xlungetc(fptr,ch);
	*pval = psymbol(fptr);
	return (TRUE);
    }
    
    /* handle read macros */
    else if (consp(type)) {
	if ((val = callmacro(fptr,ch)) && consp(val)) {
	    *pval = car(val);
	    return (TRUE);
	}
	else
	    return (FALSE);
    }

    /* handle illegal characters */
    else
	xlerror("illegal character",cvfixnum((FIXTYPE)ch));
}

/* rmhash - read macro for '#' */
LVAL rmhash(void)
{
    LVAL fptr,mch,val;
    int escflag,ch;

    /* protect some pointers */
    xlsave1(val);

    /* get the file and macro character */
    fptr = xlgetfile();
    mch = xlgachar();
    xllastarg();

    /* make the return value */
    val = consa(NIL);

    /* check the next character */
    switch (ch = xlgetc(fptr)) {
    case '\'':
		rplaca(val,pquote(fptr,s_function));
		break;
    case '(':
		rplaca(val,pvector(fptr));
		break;
    case 'b':
    case 'B':
		rplaca(val,pnumber(fptr,2));
		break;
    case 'o':
    case 'O':
		rplaca(val,pnumber(fptr,8));
		break;
    case 'x':
    case 'X':
    		rplaca(val,pnumber(fptr,16));
		break;
    case '\\':
		xlungetc(fptr,ch);
		pname(fptr,&escflag);
		ch = buf[0];
		if (strlen(buf) > 1) {
		    upcase(buf);
		    if (strcmp(buf,"NEWLINE") == 0)
			ch = '\n';
		    else if (strcmp(buf,"SPACE") == 0)
			ch = ' ';
		    else
			xlerror("unknown character name",cvstring(buf));
		}
		rplaca(val,cvchar(ch));
		break;
    case ':':
	        rplaca(val,punintern(fptr));
		break;
    case '|':
    		pcomment(fptr);
		val = NIL;
		break;
    default:
		xlerror("illegal character after #",cvfixnum((FIXTYPE)ch));
    }

    /* restore the stack */
    xlpop();

    /* return the value */
    return (val);
}

/* rmquote - read macro for '\'' */
LVAL rmquote(void)
{
    LVAL fptr,mch;

    /* get the file and macro character */
    fptr = xlgetfile();
    mch = xlgachar();
    xllastarg();

    /* parse the quoted expression */
    return (consa(pquote(fptr,s_quote)));
}

/* rmdquote - read macro for '"' */
LVAL rmdquote(void)
{
    unsigned char buf[STRMAX+1],*p,*sptr;
    LVAL fptr,str,newstr,mch;
    int len,blen,ch,d2,d3;

    /* protect some pointers */
    xlsave1(str);

    /* get the file and macro character */
    fptr = xlgetfile();
    mch = xlgachar();
    xllastarg();

    /* loop looking for a closing quote */
    len = blen = 0; p = buf;
    while ((ch = checkeof(fptr)) != '"') {

	/* handle escaped characters */
	switch (ch) {
	case '\\':
		switch (ch = checkeof(fptr)) {
		case 't':
			ch = '\011';
			break;
		case 'n':
			ch = '\012';
			break;
		case 'f':
			ch = '\014';
			break;
		case 'r':
			ch = '\015';
			break;
		default:
			if (ch >= '0' && ch <= '7') {
			    d2 = checkeof(fptr);
			    d3 = checkeof(fptr);
			    if (d2 < '0' || d2 > '7'
			     || d3 < '0' || d3 > '7')
				xlfail("invalid octal digit");
			    ch -= '0'; d2 -= '0'; d3 -= '0';
			    ch = (ch << 6) | (d2 << 3) | d3;
			}
			break;
		}
	}

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

    /* return the new string */
    return (consa(str));
}

/* rmbquote - read macro for '`' */
LVAL rmbquote(void)
{
    LVAL fptr,mch;

    /* get the file and macro character */
    fptr = xlgetfile();
    mch = xlgachar();
    xllastarg();

    /* parse the quoted expression */
    return (consa(pquote(fptr,s_bquote)));
}

/* rmcomma - read macro for ',' */
LVAL rmcomma(void)
{
    LVAL fptr,mch,sym;
    int ch;

    /* get the file and macro character */
    fptr = xlgetfile();
    mch = xlgachar();
    xllastarg();

    /* check the next character */
    if ((ch = xlgetc(fptr)) == '@')
	sym = s_comat;
    else {
	xlungetc(fptr,ch);
	sym = s_comma;
    }

    /* make the return value */
    return (consa(pquote(fptr,sym)));
}

/* rmlpar - read macro for '(' */
LVAL rmlpar(void)
{
    LVAL fptr,mch;

    /* get the file and macro character */
    fptr = xlgetfile();
    mch = xlgachar();
    xllastarg();

    /* make the return value */
    return (consa(plist(fptr)));
}

/* rmrpar - read macro for ')' */
LVAL rmrpar(void)
{
    xlfail("misplaced right paren");
}

/* rmsemi - read macro for ';' */
LVAL rmsemi(void)
{
    LVAL fptr,mch;
    int ch;

    /* get the file and macro character */
    fptr = xlgetfile();
    mch = xlgachar();
    xllastarg();

    /* skip to end of line */
    while ((ch = xlgetc(fptr)) != EOF && ch != '\n')
	;

    /* return nil (nothing read) */
    return (NIL);
}

/* pcomment - parse a comment delimited by #| and |# */
static void pcomment(LVAL fptr)
{
    int lastch,ch,n;

    /* look for the matching delimiter (and handle nesting) */
    for (n = 1, lastch = -1; n > 0 && (ch = xlgetc(fptr)) != EOF; ) {
	if (lastch == '|' && ch == '#')
	    { --n; ch = -1; }
	else if (lastch == '#' && ch == '|')
	    { ++n; ch = -1; }
	lastch = ch;
    }
}

/* pnumber - parse a number */
static LVAL pnumber(LVAL fptr, int radix)
{
    int digit,ch;
    FIXTYPE num;
    
    for (num = 0; (ch = xlgetc(fptr)) != EOF; ) {
	if (islower(ch)) ch = toupper(ch);
	if (!('0' <= ch && ch <= '9') && !('A' <= ch && ch <= 'F'))
	    break;
	if ((digit = (ch <= '9' ? ch - '0' : ch - 'A' + 10)) >= radix)
	    break;
	num = num * (FIXTYPE)radix + (FIXTYPE)digit;
    }
    xlungetc(fptr,ch);
    return (cvfixnum(num));
}

/* plist - parse a list */
static LVAL plist(LVAL fptr)
{
    LVAL val,expr,lastnptr,nptr;

    /* protect some pointers */
    xlstkcheck(2);
    xlsave(val);
    xlsave(expr);

    /* keep appending nodes until a closing paren is found */
    for (lastnptr = NIL; nextch(fptr) != ')'; )

	/* get the next expression */
	switch (readone(fptr,&expr)) {
	case EOF:
	    badeof(fptr);
	case TRUE:

	    /* check for a dotted tail */
	    if (expr == s_dot) {

		/* make sure there's a node */
		if (lastnptr == NIL)
		    xlfail("invalid dotted pair");

		/* parse the expression after the dot */
		if (!xlread(fptr,&expr,TRUE))
		    badeof(fptr);
		rplacd(lastnptr,expr);

		/* make sure its followed by a close paren */
		if (nextch(fptr) != ')')
		    xlfail("invalid dotted pair");
	    }

	    /* otherwise, handle a normal list element */
	    else {
		nptr = consa(expr);
		if (lastnptr == NIL)
		    val = nptr;
		else
		    rplacd(lastnptr,nptr);
		lastnptr = nptr;
	    }
	    break;
	}

    /* skip the closing paren */
    xlgetc(fptr);

    /* restore the stack */
    xlpopn(2);

    /* return successfully */
    return (val);
}

/* pvector - parse a vector */
static LVAL pvector(LVAL fptr)
{
    LVAL list,expr,val,lastnptr,nptr;
    int len,ch,i;

    /* protect some pointers */
    xlstkcheck(2);
    xlsave(list);
    xlsave(expr);

    /* keep appending nodes until a closing paren is found */
    for (lastnptr = NIL, len = 0; (ch = nextch(fptr)) != ')'; ) {

	/* check for end of file */
	if (ch == EOF)
	    badeof(fptr);

	/* get the next expression */
	switch (readone(fptr,&expr)) {
	case EOF:
	    badeof(fptr);
	case TRUE:
	    nptr = consa(expr);
	    if (lastnptr == NIL)
		list = nptr;
	    else
		rplacd(lastnptr,nptr);
	    lastnptr = nptr;
	    len++;
	    break;
	}
    }

    /* skip the closing paren */
    xlgetc(fptr);

    /* make a vector of the appropriate length */
    val = newvector(len);

    /* copy the list into the vector */
    for (i = 0; i < len; ++i, list = cdr(list))
	setelement(val,i,car(list));

    /* restore the stack */
    xlpopn(2);

    /* return successfully */
    return (val);
}

/* pquote - parse a quoted expression */
static LVAL pquote(LVAL fptr, LVAL sym)
{
    LVAL val,p;

    /* protect some pointers */
    xlsave1(val);

    /* allocate two nodes */
    val = consa(sym);
    rplacd(val,consa(NIL));

    /* initialize the second to point to the quoted expression */
    if (!xlread(fptr,&p,TRUE))
	badeof(fptr);
    rplaca(cdr(val),p);

    /* restore the stack */
    xlpop();

    /* return the quoted expression */
    return (val);
}

/* psymbol - parse a symbol name */
static LVAL psymbol(LVAL fptr)
{
    int escflag;
    LVAL val;
    pname(fptr,&escflag);
    return (escflag || !isnumber(buf,&val) ? xlenter(buf) : val);
}

/* punintern - parse an uninterned symbol */
static LVAL punintern(LVAL fptr)
{
    int escflag;
    pname(fptr,&escflag);
    return (xlmakesym(buf));
}

/* pname - parse a symbol/package name */
static int pname(LVAL fptr, int *pescflag)
{
    int mode,ch,i;
    LVAL type;

    /* initialize */
    *pescflag = FALSE;
    mode = NORMAL;
    i = 0;

    /* accumulate the symbol name */
    while (mode != DONE) {

	/* handle normal mode */
	while (mode == NORMAL)
	    if ((ch = xlgetc(fptr)) == EOF)
		mode = DONE;
	    else if ((type = tentry(ch)) == k_sescape) {
		i = storech(buf,i,checkeof(fptr));
		*pescflag = TRUE;
	    }
	    else if (type == k_mescape) {
		*pescflag = TRUE;
		mode = ESCAPE;
	    }
	    else if (type == k_const
		 ||  (consp(type) && car(type) == k_nmacro))
		i = storech(buf,i,islower(ch) ? toupper(ch) : ch);
	    else
		mode = DONE;

	/* handle multiple escape mode */
	while (mode == ESCAPE)
	    if ((ch = xlgetc(fptr)) == EOF)
		badeof(fptr);
	    else if ((type = tentry(ch)) == k_sescape)
		i = storech(buf,i,checkeof(fptr));
	    else if (type == k_mescape)
		mode = NORMAL;
	    else
		i = storech(buf,i,ch);
    }
    buf[i] = 0;

    /* check for a zero length name */
    if (i == 0)
	xlfail("zero length name");

    /* unget the last character and return it */
    xlungetc(fptr,ch);
    return (ch);
}

/* storech - store a character in the print name buffer */
static int storech(char *buf, int i, int ch)
{
    if (i < STRMAX)
	buf[i++] = ch;
    return (i);
}

/* tentry - get a readtable entry */
LVAL tentry(int ch)
{
    LVAL rtable;
    rtable = getvalue(s_rtable);
    if (!vectorp(rtable) || ch < 0 || ch >= getsize(rtable))
	return (NIL);
    return (getelement(rtable,ch));
}

/* nextch - look at the next non-blank character */
static int nextch(LVAL fptr)
{
    int ch;

    /* return and save the next non-blank character */
    while ((ch = xlgetc(fptr)) != EOF && isspace(ch))
	;
    xlungetc(fptr,ch);
    return (ch);
}

/* checkeof - get a character and check for end of file */
static int checkeof(LVAL fptr)
{
    int ch;

    if ((ch = xlgetc(fptr)) == EOF)
	badeof(fptr);
    return (ch);
}

/* badeof - unexpected eof */
static void badeof(LVAL fptr)
{
    xlgetc(fptr);
    xlfail("unexpected EOF");
}

/* isnumber - check if this string is a number */
int isnumber(char *str, LVAL *pval)
{
    int dl,dr;
    char *p;

    /* initialize */
    p = str; dl = dr = 0;

    /* check for a sign */
    if (*p == '+' || *p == '-')
	p++;

    /* check for a string of digits */
    while (isdigit(*p))
	p++, dl++;

    /* check for a decimal point */
    if (*p == '.') {
	p++;
	while (isdigit(*p))
	    p++, dr++;
    }

    /* check for an exponent */
    if ((dl || dr) && *p == 'E') {
	p++;

	/* check for a sign */
	if (*p == '+' || *p == '-')
	    p++;

	/* check for a string of digits */
	while (isdigit(*p))
	    p++, dr++;
    }

    /* make sure there was at least one digit and this is the end */
    if ((dl == 0 && dr == 0) || *p)
	return (FALSE);

    /* convert the string to an integer and return successfully */
    if (pval) {
	if (*str == '+') ++str;
	if (str[strlen(str)-1] == '.') str[strlen(str)-1] = 0;
	*pval = (dr ? cvflonum(atof(str)) : cvfixnum(ICNV(str)));
    }
    return (TRUE);
}

/* defmacro - define a read macro */
void defmacro(int ch, LVAL type, int offset)
{
    extern FUNDEF funtab[];
    LVAL subr;
    subr = cvsubr(funtab[offset].fd_subr,funtab[offset].fd_type,offset);
    setelement(getvalue(s_rtable),ch,cons(type,subr));
}

/* callmacro - call a read macro */
LVAL callmacro(LVAL fptr, int ch)
{
    LVAL *newfp;

    /* create the new call frame */
    newfp = xlsp;
    pusharg(cvfixnum((FIXTYPE)(newfp - xlfp)));
    pusharg(cdr(getelement(getvalue(s_rtable),ch)));
    pusharg(cvfixnum((FIXTYPE)2));
    pusharg(fptr);
    pusharg(cvchar(ch));
    xlfp = newfp;
    return (xlapply(2));
}

/* upcase - translate a string to upper case */
static void upcase(unsigned char *str)
{
    for (; *str != '\0'; ++str)
	if (islower(*str))
	    *str = toupper(*str);
}

/* xlrinit - initialize the reader */
void xlrinit(void)
{
    LVAL rtable;
    char *p;
    int ch;

    /* create the read table */
    rtable = newvector(256);
    setvalue(s_rtable,rtable);

    /* initialize the readtable */
    for (p = WSPACE; ch = *p++; )
	setelement(rtable,ch,k_wspace);
    for (p = CONST1; ch = *p++; )
	setelement(rtable,ch,k_const);
    for (p = CONST2; ch = *p++; )
	setelement(rtable,ch,k_const);

    /* setup the escape characters */
    setelement(rtable,'\\',k_sescape);
    setelement(rtable,'|', k_mescape);

    /* install the read macros */
    defmacro('#', k_nmacro,FT_RMHASH);
    defmacro('\'',k_tmacro,FT_RMQUOTE);
    defmacro('"', k_tmacro,FT_RMDQUOTE);
    defmacro('`', k_tmacro,FT_RMBQUOTE);
    defmacro(',', k_tmacro,FT_RMCOMMA);
    defmacro('(', k_tmacro,FT_RMLPAR);
    defmacro(')', k_tmacro,FT_RMRPAR);
    defmacro(';', k_tmacro,FT_RMSEMI);
}

/* xlstr - xlisp string and character built-in functions */
/*	Copyright (c) 1985, by David Michael Betz
	All Rights Reserved
	Permission is granted for unrestricted non-commercial use	*/


/* local definitions */
#define fix(n)	cvfixnum((FIXTYPE)(n))
#define TLEFT	1
#define TRIGHT	2

/* external variables */
extern LVAL k_start,k_end,k_1start,k_1end,k_2start,k_2end;
extern LVAL s_true;
extern char buf[];

/* external procedures */

/* forward declarations */

/* string comparision functions */
LVAL xstrlss(void) { return (strcompare('<',FALSE)); } /* string< */
LVAL xstrleq(void) { return (strcompare('L',FALSE)); } /* string<= */
LVAL xstreql(void) { return (strcompare('=',FALSE)); } /* string= */
LVAL xstrneq(void) { return (strcompare('#',FALSE)); } /* string/= */
LVAL xstrgeq(void) { return (strcompare('G',FALSE)); } /* string>= */
LVAL xstrgtr(void) { return (strcompare('>',FALSE)); } /* string> */

/* string comparison functions (not case sensitive) */
LVAL xstrilss(void) { return (strcompare('<',TRUE)); } /* string-lessp */
LVAL xstrileq(void) { return (strcompare('L',TRUE)); } /* string-not-greaterp */
LVAL xstrieql(void) { return (strcompare('=',TRUE)); } /* string-equal */
LVAL xstrineq(void) { return (strcompare('#',TRUE)); } /* string-not-equal */
LVAL xstrigeq(void) { return (strcompare('G',TRUE)); } /* string-not-lessp */
LVAL xstrigtr(void) { return (strcompare('>',TRUE)); } /* string-greaterp */

/* strcompare - compare strings */
static LVAL strcompare(int fcn, int icase)
{
    int start1,end1,start2,end2,ch1,ch2;
    unsigned char *p1,*p2;
    LVAL str1,str2;

    /* get the strings */
    str1 = xlgastring();
    str2 = xlgastring();

    /* get the substring specifiers */
    getbounds(str1,k_1start,k_1end,&start1,&end1);
    getbounds(str2,k_2start,k_2end,&start2,&end2);

    /* setup the string pointers */
    p1 = &getstring(str1)[start1];
    p2 = &getstring(str2)[start2];

    /* compare the strings */
    for (; start1 < end1 && start2 < end2; ++start1,++start2) {
	ch1 = *p1++;
	ch2 = *p2++;
	if (icase) {
	    if (isupper(ch1)) ch1 = tolower(ch1);
	    if (isupper(ch2)) ch2 = tolower(ch2);
	}
	if (ch1 != ch2)
	    switch (fcn) {
	    case '<':	return (ch1 < ch2 ? fix(start1) : NIL);
	    case 'L':	return (ch1 <= ch2 ? fix(start1) : NIL);
	    case '=':	return (NIL);
	    case '#':	return (fix(start1));
	    case 'G':	return (ch1 >= ch2 ? fix(start1) : NIL);
	    case '>':	return (ch1 > ch2 ? fix(start1) : NIL);
	    }
    }

    /* check the termination condition */
    switch (fcn) {
    case '<':	return (start1 >= end1 && start2 < end2 ? fix(start1) : NIL);
    case 'L':	return (start1 >= end1 ? fix(start1) : NIL);
    case '=':	return (start1 >= end1 && start2 >= end2 ? s_true : NIL);
    case '#':	return (start1 >= end1 && start2 >= end2 ? NIL : fix(start1));
    case 'G':	return (start2 >= end2 ? fix(start1) : NIL);
    case '>':	return (start2 >= end2 && start1 < end1 ? fix(start1) : NIL);
    }
    return (NIL);	/* not reached */
}

/* case conversion functions */
LVAL xupcase(void) { return (changecase('U',FALSE)); }
LVAL xdowncase(void) { return (changecase('D',FALSE)); }

/* destructive case conversion functions */
LVAL xnupcase(void) { return (changecase('U',TRUE)); }
LVAL xndowncase(void) { return (changecase('D',TRUE)); }

/* changecase - change case */
static LVAL changecase(int fcn, int destructive)
{
    unsigned char *srcp,*dstp;
    int start,end,len,ch,i;
    LVAL src,dst;

    /* get the string */
    src = xlgastring();

    /* get the substring specifiers */
    getbounds(src,k_start,k_end,&start,&end);
    len = getslength(src) - 1;

    /* make a destination string */
    dst = (destructive ? src : newstring(len+1));

    /* setup the string pointers */
    srcp = getstring(src);
    dstp = getstring(dst);

    /* copy the source to the destination */
    for (i = 0; i < len; ++i) {
	ch = *srcp++;
	if (i >= start && i < end)
	    switch (fcn) {
	    case 'U':	if (islower(ch)) ch = toupper(ch); break;
	    case 'D':	if (isupper(ch)) ch = tolower(ch); break;
	    }
	*dstp++ = ch;
    }
    *dstp = '\0';

    /* return the new string */
    return (dst);
}

/* trim functions */
LVAL xtrim(void) { return (trim(TLEFT|TRIGHT)); }
LVAL xlefttrim(void) { return (trim(TLEFT)); }
LVAL xrighttrim(void) { return (trim(TRIGHT)); }

/* trim - trim character from a string */
static LVAL trim(int fcn)
{
    unsigned char *leftp,*rightp,*dstp;
    LVAL bag,src,dst;

    /* get the bag and the string */
    bag = xlgastring();
    src = xlgastring();
    xllastarg();

    /* setup the string pointers */
    leftp = getstring(src);
    rightp = leftp + getslength(src) - 2;

    /* trim leading characters */
    if (fcn & TLEFT)
	while (leftp <= rightp && inbag(*leftp,bag))
	    ++leftp;

    /* trim character from the right */
    if (fcn & TRIGHT)
	while (rightp >= leftp && inbag(*rightp,bag))
	    --rightp;

    /* make a destination string and setup the pointer */
    dst = newstring((int)(rightp-leftp+2));
    dstp = getstring(dst);

    /* copy the source to the destination */
    while (leftp <= rightp)
	*dstp++ = *leftp++;
    *dstp = '\0';

    /* return the new string */
    return (dst);
}

/* getbounds - get the start and end bounds of a string */
static void getbounds(LVAL str, LVAL skey, LVAL ekey, int *pstart, int *pend)
{
    LVAL arg;
    int len;

    /* get the length of the string */
    len = getslength(str) - 1;

    /* get the starting index */
    if (xlgkfixnum(skey,&arg)) {
	*pstart = (int)getfixnum(arg);
	if (*pstart < 0 || *pstart > len)
	    xlerror("string index out of bounds",arg);
    }
    else
	*pstart = 0;

    /* get the ending index */
    if (xlgkfixnum(ekey,&arg)) {
	*pend = (int)getfixnum(arg);
	if (*pend < 0 || *pend > len)
	    xlerror("string index out of bounds",arg);
    }
    else
	*pend = len;

    /* make sure the start is less than or equal to the end */
    if (*pstart > *pend)
	xlerror("starting index error",cvfixnum((FIXTYPE)*pstart));
}

/* inbag - test if a character is in a bag */
static int inbag(int ch, LVAL bag)
{
    unsigned char *p;
    for (p = getstring(bag); *p != '\0'; ++p)
	if (*p == ch)
	    return (TRUE);
    return (FALSE);
}

/* xstrcat - concatenate a bunch of strings */
LVAL xstrcat(void)
{
    LVAL *saveargv,tmp,val;
    unsigned char *str;
    int saveargc,len;

    /* save the argument list */
    saveargv = xlargv;
    saveargc = xlargc;

    /* find the length of the new string */
    for (len = 0; moreargs(); ) {
	tmp = xlgastring();
	len += (int)getslength(tmp) - 1;
    }

    /* create the result string */
    val = newstring(len+1);
    str = getstring(val);

    /* restore the argument list */
    xlargv = saveargv;
    xlargc = saveargc;
    
    /* combine the strings */
    for (*str = '\0'; moreargs(); ) {
	tmp = nextarg();
	strcat(str,getstring(tmp));
    }

    /* return the new string */
    return (val);
}

/* xsubseq - return a subsequence */
LVAL xsubseq(void)
{
    unsigned char *srcp,*dstp;
    int start,end,len;
    LVAL src,dst;

    /* get string and starting and ending positions */
    src = xlgastring();

    /* get the starting position */
    dst = xlgafixnum(); start = (int)getfixnum(dst);
    if (start < 0 || start > getslength(src) - 1)
	xlerror("string index out of bounds",dst);

    /* get the ending position */
    if (moreargs()) {
	dst = xlgafixnum(); end = (int)getfixnum(dst);
	if (end < 0 || end > getslength(src) - 1)
	    xlerror("string index out of bounds",dst);
    }
    else
	end = getslength(src) - 1;
    xllastarg();

    /* setup the source pointer */
    srcp = getstring(src) + start;
    len = end - start;

    /* make a destination string and setup the pointer */
    dst = newstring(len+1);
    dstp = getstring(dst);

    /* copy the source to the destination */
    while (--len >= 0)
	*dstp++ = *srcp++;
    *dstp = '\0';

    /* return the substring */
    return (dst);
}

/* xstring - return a string consisting of a single character */
LVAL xstring(void)
{
    LVAL arg;

    /* get the argument */
    arg = xlgetarg();
    xllastarg();

    /* make sure its not NIL */
    if (null(arg))
	xlbadtype(arg);

    /* check the argument type */
    switch (ntype(arg)) {
    case STRING:
	return (arg);
    case SYMBOL:
	return (getpname(arg));
    case CHAR:
	buf[0] = (int)getchcode(arg);
	buf[1] = '\0';
	return (cvstring(buf));
    default:
	xlbadtype(arg);
    }
}

/* xchar - extract a character from a string */
LVAL xchar(void)
{
    LVAL str,num;
    int n;

    /* get the string and the index */
    str = xlgastring();
    num = xlgafixnum();
    xllastarg();

    /* range check the index */
    if ((n = (int)getfixnum(num)) < 0 || n >= getslength(str) - 1)
	xlerror("index out of range",num);

    /* return the character */
    return (cvchar(getstring(str)[n]));
}

/* xcharint - convert an integer to a character */
LVAL xcharint(void)
{
    LVAL arg;
    arg = xlgachar();
    xllastarg();
    return (cvfixnum((FIXTYPE)getchcode(arg)));
}

/* xintchar - convert a character to an integer */
LVAL xintchar(void)
{
    LVAL arg;
    arg = xlgafixnum();
    xllastarg();
    return (cvchar((int)getfixnum(arg)));
}

/* xuppercasep - built-in function 'upper-case-p' */
LVAL xuppercasep(void)
{
    int ch;
    ch = getchcode(xlgachar());
    xllastarg();
    return (isupper(ch) ? s_true : NIL);
}

/* xlowercasep - built-in function 'lower-case-p' */
LVAL xlowercasep(void)
{
    int ch;
    ch = getchcode(xlgachar());
    xllastarg();
    return (islower(ch) ? s_true : NIL);
}

/* xbothcasep - built-in function 'both-case-p' */
LVAL xbothcasep(void)
{
    int ch;
    ch = getchcode(xlgachar());
    xllastarg();
    return (isupper(ch) || islower(ch) ? s_true : NIL);
}

/* xdigitp - built-in function 'digit-char-p' */
LVAL xdigitp(void)
{
    int ch;
    ch = getchcode(xlgachar());
    xllastarg();
    return (isdigit(ch) ? cvfixnum((FIXTYPE)(ch - '0')) : NIL);
}

/* xcharcode - built-in function 'char-code' */
LVAL xcharcode(void)
{
    int ch;
    ch = getchcode(xlgachar());
    xllastarg();
    return (cvfixnum((FIXTYPE)ch));
}

/* xcodechar - built-in function 'code-char' */
LVAL xcodechar(void)
{
    LVAL arg;
    int ch;
    arg = xlgafixnum(); ch = (int)getfixnum(arg);
    xllastarg();
    return (ch >= 0 && ch <= 127 ? cvchar(ch) : NIL);
}

/* xchupcase - built-in function 'char-upcase' */
LVAL xchupcase(void)
{
    LVAL arg;
    int ch;
    arg = xlgachar(); ch = getchcode(arg);
    xllastarg();
    return (islower(ch) ? cvchar(toupper(ch)) : arg);
}

/* xchdowncase - built-in function 'char-downcase' */
LVAL xchdowncase(void)
{
    LVAL arg;
    int ch;
    arg = xlgachar(); ch = getchcode(arg);
    xllastarg();
    return (isupper(ch) ? cvchar(tolower(ch)) : arg);
}

/* xdigitchar - built-in function 'digit-char' */
LVAL xdigitchar(void)
{
    LVAL arg;
    int n;
    arg = xlgafixnum(); n = (int)getfixnum(arg);
    xllastarg();
    return (n >= 0 && n <= 9 ? cvchar(n + '0') : NIL);
}

/* xalphanumericp - built-in function 'alphanumericp' */
LVAL xalphanumericp(void)
{
    int ch;
    ch = getchcode(xlgachar());
    xllastarg();
    return (isupper(ch) || islower(ch) || isdigit(ch) ? s_true : NIL);
}

/* character comparision functions */
LVAL xchrlss(void) { return (chrcompare('<',FALSE)); } /* char< */
LVAL xchrleq(void) { return (chrcompare('L',FALSE)); } /* char<= */
LVAL xchreql(void) { return (chrcompare('=',FALSE)); } /* char= */
LVAL xchrneq(void) { return (chrcompare('#',FALSE)); } /* char/= */
LVAL xchrgeq(void) { return (chrcompare('G',FALSE)); } /* char>= */
LVAL xchrgtr(void) { return (chrcompare('>',FALSE)); } /* char> */

/* character comparision functions (case insensitive) */
LVAL xchrilss(void) { return (chrcompare('<',TRUE)); } /* char-lessp */
LVAL xchrileq(void) { return (chrcompare('L',TRUE)); } /* char-not-greaterp */
LVAL xchrieql(void) { return (chrcompare('=',TRUE)); } /* char-equalp */
LVAL xchrineq(void) { return (chrcompare('#',TRUE)); } /* char-not-equalp */
LVAL xchrigeq(void) { return (chrcompare('G',TRUE)); } /* char-not-lessp */
LVAL xchrigtr(void) { return (chrcompare('>',TRUE)); } /* char-greaterp */

/* chrcompare - compare characters */
static LVAL chrcompare(int fcn, int icase)
{
    int ch1,ch2,icmp;
    LVAL arg;
    
    /* get the characters */
    arg = xlgachar(); ch1 = getchcode(arg);

    /* convert to lowercase if case insensitive */
    if (icase && isupper(ch1))
	ch1 = tolower(ch1);

    /* handle each remaining argument */
    for (icmp = TRUE; icmp && moreargs(); ch1 = ch2) {

	/* get the next argument */
	arg = xlgachar(); ch2 = getchcode(arg);

	/* convert to lowercase if case insensitive */
	if (icase && isupper(ch2))
	    ch2 = tolower(ch2);

	/* compare the characters */
	switch (fcn) {
	case '<':	icmp = (ch1 < ch2); break;
	case 'L':	icmp = (ch1 <= ch2); break;
	case '=':	icmp = (ch1 == ch2); break;
	case '#':	icmp = (ch1 != ch2); break;
	case 'G':	icmp = (ch1 >= ch2); break;
	case '>':	icmp = (ch1 > ch2); break;
	}
    }

    /* return the result */
    return (icmp ? s_true : NIL);
}

/* xlsubr - xlisp builtin function support routines */
/*	Copyright (c) 1985, by David Michael Betz
	All Rights Reserved
	Permission is granted for unrestricted non-commercial use	*/

/* external variables */
extern LVAL k_test,k_tnot,s_eql;

/* xlsubr - define a builtin function */
LVAL xlsubr(char *sname, int type, LVAL (*fcn)(void), int offset)
{
    LVAL sym;
    sym = xlenter(sname);
    setfunction(sym,cvsubr(fcn,type,offset));
    return (sym);
}

/* xlgetkeyarg - get a keyword argument */
int xlgetkeyarg(LVAL key, LVAL *pval)
{
    LVAL *argv=xlargv;
    int argc=xlargc;
    for (argv = xlargv, argc = xlargc; (argc -= 2) >= 0; argv += 2) {
	if (*argv == key) {
	    *pval = *++argv;
	    return (TRUE);
	}
    }
    return (FALSE);
}

/* xlgkfixnum - get a fixnum keyword argument */
int xlgkfixnum(LVAL key, LVAL *pval)
{
    if (xlgetkeyarg(key,pval)) {
	if (!fixp(*pval))
	    xlbadtype(*pval);
	return (TRUE);
    }
    return (FALSE);
}

/* xltest - get the :test or :test-not keyword argument */
void xltest(LVAL *pfcn, int *ptresult)
{
    if (xlgetkeyarg(k_test,pfcn))	/* :test */
	*ptresult = TRUE;
    else if (xlgetkeyarg(k_tnot,pfcn))	/* :test-not */
	*ptresult = FALSE;
    else {
	*pfcn = getfunction(s_eql);
	*ptresult = TRUE;
    }
}

/* xlgetfile - get a file or stream */
LVAL xlgetfile(void)
{
    LVAL arg;

    /* get a file or stream (cons) or nil */
    if (arg = xlgetarg()) {
	if (streamp(arg)) {
	    if (getfile(arg) == NULL)
		xlfail("file not open");
	}
	else if (!ustreamp(arg))
	    xlerror("bad argument type",arg);
    }
    return (arg);
}

/* xlgetfname - get a filename */
LVAL xlgetfname(void)
{
    LVAL name;

    /* get the next argument */
    name = xlgetarg();

    /* get the filename string */
    if (symbolp(name))
	name = getpname(name);
    else if (!stringp(name))
	xlerror("bad argument type",name);

    /* return the name */
    return (name);
}

/* needsextension - check if a filename needs an extension */
int needsextension(char *name)
{
    char *p;

    /* check for an extension */
    for (p = &name[strlen(name)]; --p >= &name[0]; )
	if (*p == '.')
	    return (FALSE);
	else if (!islower(*p) && !isupper(*p) && !isdigit(*p))
	    return (TRUE);

    /* no extension found */
    return (TRUE);
}

/* xlbadtype - report a "bad argument type" error */
LVAL xlbadtype(LVAL arg)
{
    xlerror("bad argument type",arg);
}

/* xltoofew - report a "too few arguments" error */
LVAL xltoofew(void)
{
    xlfail("too few arguments");
}

/* xltoomany - report a "too many arguments" error */
void xltoomany(void)
{
    xlfail("too many arguments");
}

/* eq - internal eq function */
int eq(LVAL arg1, LVAL arg2)
{
    return (arg1 == arg2);
}

/* eql - internal eql function */
int eql(LVAL arg1, LVAL arg2)
{
    /* compare the arguments */
    if (arg1 == arg2)
	return (TRUE);
    else if (arg1) {
	switch (ntype(arg1)) {
	case FIXNUM:
	    return (fixp(arg2) ? getfixnum(arg1)==getfixnum(arg2) : FALSE);
	case FLONUM:
	    return (floatp(arg2) ? getflonum(arg1)==getflonum(arg2) : FALSE);
	default:
	    return (FALSE);
	}
    }
    else
	return (FALSE);
}

/* equal - internal equal function */
int equal(LVAL arg1, LVAL arg2)
{
    /* compare the arguments */
    if (arg1 == arg2)
	return (TRUE);
    else if (arg1) {
	switch (ntype(arg1)) {
	case FIXNUM:
	    return (fixp(arg2) ? getfixnum(arg1)==getfixnum(arg2) : FALSE);
	case FLONUM:
	    return (floatp(arg2) ? getflonum(arg1)==getflonum(arg2) : FALSE);
	case STRING:
	    return (stringp(arg2) ? strcmp(getstring(arg1),
					   getstring(arg2)) == 0 : FALSE);
	case CONS:
	    return (consp(arg2) ? equal(car(arg1),car(arg2))
			       && equal(cdr(arg1),cdr(arg2)) : FALSE);
	default:
	    return (FALSE);
	}
    }
    else
	return (FALSE);
}

