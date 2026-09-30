/* xlisp - a small subset of lisp */
/*	Copyright (c) 1985, by David Michael Betz
	All Rights Reserved
	Permission is granted for unrestricted non-commercial use	*/

#ifndef XLISP_H
#define XLISP_H

/* system specific definitions */

/* silence MSVC "unsafe CRT function" deprecation warnings (sprintf, strcpy...) */
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <setjmp.h>
#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>

/* NNODES	number of nodes to allocate in each request (1000) */
/* EDEPTH	evaluation stack depth (2000) */
/* ADEPTH	argument stack depth (1000) */
/* FORWARD	type of a forward declaration () */
/* LOCAL	type of a local function (static) */
/* AFMT		printf format for addresses ("%p") */
/* FIXTYPE	data type for fixed point numbers (long long) */
/* ICNV		fixed point input conversion routine (strtoll) */
/* IFMT		printf format for fixed point numbers ("%lld") */
/* FLOTYPE	data type for floating point numbers (double) */
/* OFFTYPE	number the size of an image file offset (long) */

/*
 * 64-bit Windows (Microsoft Visual C++, x64) and other 64-bit hosts.
 *
 * Windows x64 is an LLP64 platform: int and long are 32 bits while
 * pointers are 64 bits.  FIXTYPE is therefore "long long" so that a
 * fixnum can hold a pointer (ADDRESS-OF, PEEK, POKE) and so that
 * integer arithmetic is 64 bits wide on every platform.
 */
#define NNODES		2000
#ifndef EDEPTH
#define EDEPTH		10000
#endif
#ifndef ADEPTH
#define ADEPTH		10000
#endif
#define AFMT		"%p"
#define FIXTYPE		long long
#define ICNV(n)		strtoll((n),NULL,10)
#define IFMT		"%lld"
#define FLOTYPE		double
#define OFFTYPE		long
#define SAVERESTORE

/* default important definitions */
#ifndef FORWARD
#define FORWARD
#endif
#ifndef LOCAL
#define LOCAL		static
#endif
#ifndef CVPTR
#define CVPTR(x)	(x)
#endif
#ifndef UCHAR
#define UCHAR		unsigned char
#endif

/* useful definitions */
#define TRUE	1
#define FALSE	0
#ifndef NIL
#define NIL	(LVAL )0
#endif

/* include the dynamic memory definitions */
#include "xldmem.h"

/* program limits */
#define STRMAX		100		/* maximum length of a string constant */
#define HSIZE		199		/* symbol hash table size */
#define SAMPLE		100		/* control character sample rate */

/* function table offsets for the initialization functions */
#define FT_RMHASH	0
#define FT_RMQUOTE	1
#define FT_RMDQUOTE	2
#define FT_RMBQUOTE	3
#define FT_RMCOMMA	4
#define FT_RMLPAR	5
#define FT_RMRPAR	6
#define FT_RMSEMI	7
#define FT_CLNEW	10
#define FT_CLISNEW	11
#define FT_CLANSWER	12
#define FT_OBISNEW	13
#define FT_OBCLASS	14
#define FT_OBSHOW	15
	
/* macro to push a value onto the argument stack */
#define pusharg(x)	{if (xlsp >= xlargstktop) xlargstkoverflow();\
			 *xlsp++ = (x);}

/* macros to protect pointers */
#define xlstkcheck(n)	{if (xlstack - (n) < xlstkbase) xlstkoverflow();}
#define xlsave(n)	{*--xlstack = &n; n = NIL;}
#define xlprotect(n)	{*--xlstack = &n;}

/* check the stack and protect a single pointer */
#define xlsave1(n)	{if (xlstack <= xlstkbase) xlstkoverflow();\
                         *--xlstack = &n; n = NIL;}
#define xlprot1(n)	{if (xlstack <= xlstkbase) xlstkoverflow();\
                         *--xlstack = &n;}

/* macros to pop pointers off the stack */
#define xlpop()		{++xlstack;}
#define xlpopn(n)	{xlstack+=(n);}

/* macros to manipulate the lexical environment */
#define xlframe(e)	cons(NIL,e)
#define xlbind(s,v)	xlpbind(s,v,xlenv)
#define xlfbind(s,v)	xlpbind(s,v,xlfenv);
#define xlpbind(s,v,e)	{rplaca(e,cons(cons(s,v),car(e)));}

/* macros to manipulate the dynamic environment */
#define xldbind(s,v)	{xldenv = cons(cons(s,getvalue(s)),xldenv);\
			 setvalue(s,v);}
#define xlunbind(e)	{for (; xldenv != (e); xldenv = cdr(xldenv))\
			   setvalue(car(car(xldenv)),cdr(car(xldenv)));}

/* type predicates */			       
#define atom(x)		((x) == NIL || ntype(x) != CONS)
#define null(x)		((x) == NIL)
#define listp(x)	((x) == NIL || ntype(x) == CONS)
#define consp(x)	((x) && ntype(x) == CONS)
#define subrp(x)	((x) && ntype(x) == SUBR)
#define fsubrp(x)	((x) && ntype(x) == FSUBR)
#define stringp(x)	((x) && ntype(x) == STRING)
#define symbolp(x)	((x) && ntype(x) == SYMBOL)
#define streamp(x)	((x) && ntype(x) == STREAM)
#define objectp(x)	((x) && ntype(x) == OBJECT)
#define fixp(x)		((x) && ntype(x) == FIXNUM)
#define floatp(x)	((x) && ntype(x) == FLONUM)
#define vectorp(x)	((x) && ntype(x) == VECTOR)
#define closurep(x)	((x) && ntype(x) == CLOSURE)
#define charp(x)	((x) && ntype(x) == CHAR)
#define ustreamp(x)	((x) && ntype(x) == USTREAM)
#define boundp(x)	(getvalue(x) != s_unbound)
#define fboundp(x)	(getfunction(x) != s_unbound)

/* shorthand functions */
#define consa(x)	cons(x,NIL)
#define consd(x)	cons(NIL,x)

/* argument list parsing macros */
#define xlgetarg()	(testarg(nextarg()))
#define xllastarg()	{if (xlargc != 0) xltoomany();}
#define testarg(e)	(moreargs() ? (e) : xltoofew())
#define typearg(tp)	(tp(*xlargv) ? nextarg() : xlbadtype(*xlargv))
#define nextarg()	(--xlargc, *xlargv++)
#define moreargs()	(xlargc > 0)

/* macros to get arguments of a particular type */
#define xlgacons()	(testarg(typearg(consp)))
#define xlgalist()	(testarg(typearg(listp)))
#define xlgasymbol()	(testarg(typearg(symbolp)))
#define xlgastring()	(testarg(typearg(stringp)))
#define xlgaobject()	(testarg(typearg(objectp)))
#define xlgafixnum()	(testarg(typearg(fixp)))
#define xlgaflonum()	(testarg(typearg(floatp)))
#define xlgachar()	(testarg(typearg(charp)))
#define xlgavector()	(testarg(typearg(vectorp)))
#define xlgastream()	(testarg(typearg(streamp)))
#define xlgaustream()	(testarg(typearg(ustreamp)))
#define xlgaclosure()	(testarg(typearg(closurep)))

/* function definition structure */
typedef struct {
    char *fd_name;	/* function name */
    int fd_type;	/* function type */
    SUBRFUN fd_subr;	/* function entry point */
} FUNDEF;

/* execution context flags */
#define CF_GO		0x0001
#define CF_RETURN	0x0002
#define CF_THROW	0x0004
#define CF_ERROR	0x0008
#define CF_CLEANUP	0x0010
#define CF_CONTINUE	0x0020
#define CF_TOPLEVEL	0x0040
#define CF_BRKLEVEL	0x0080
#define CF_UNWIND	0x0100

/* execution context */
typedef struct context {
    int c_flags;			/* context type flags */
    LVAL c_expr;			/* expression (type dependant) */
    jmp_buf c_jmpbuf;			/* longjmp context */
    struct context *c_xlcontext;	/* old value of xlcontext */
    LVAL **c_xlstack;			/* old value of xlstack */
    LVAL *c_xlargv;			/* old value of xlargv */
    int c_xlargc;			/* old value of xlargc */
    LVAL *c_xlfp;			/* old value of xlfp */
    LVAL *c_xlsp;			/* old value of xlsp */
    LVAL c_xlenv;			/* old value of xlenv */
    LVAL c_xlfenv;			/* old value of xlfenv */
    LVAL c_xldenv;			/* old value of xldenv */
} CONTEXT;

/* external variables */
extern LVAL **xlstktop;       	/* top of the evaluation stack */
extern LVAL **xlstkbase;	/* base of the evaluation stack */
extern LVAL **xlstack;		/* evaluation stack pointer */
extern LVAL *xlargstkbase;	/* base of the argument stack */
extern LVAL *xlargstktop;	/* top of the argument stack */
extern LVAL *xlfp;		/* argument frame pointer */
extern LVAL *xlsp;		/* argument stack pointer */
extern LVAL *xlargv;		/* current argument vector */
extern int xlargc;		/* current argument count */

/* mark functions that never return (they longjmp or exit) */
#if defined(_MSC_VER)
#define XLNORETURN	__declspec(noreturn)
#elif defined(__GNUC__) || defined(__clang__)
#define XLNORETURN	__attribute__((noreturn))
#else
#define XLNORETURN
#endif

/* prototypes for every global function */
#include "xlproto.h"

#endif /* XLISP_H */
