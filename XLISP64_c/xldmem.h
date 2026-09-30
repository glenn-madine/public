/* xldmem.h - dynamic memory definitions */
/*	Copyright (c) 1987, by David Michael Betz
	All Rights Reserved
	Permission is granted for unrestricted non-commercial use	*/

/*
 * 64-bit port changes (marked below):
 *  - getenv/setenv closure macros renamed getcenv/setcenv (they collided
 *    with the C runtime getenv() in <stdlib.h>)
 *  - subr function pointer given a prototype (SUBRFUN)
 *  - old-style extern declarations removed (see xlproto.h)
 */

#ifndef XLDMEM_H
#define XLDMEM_H

/* small fixnum range */
#define SFIXMIN		(-128)
#define SFIXMAX		255
#define SFIXSIZE	384

/* character range */
#define CHARMIN		0
#define CHARMAX		255
#define CHARSIZE	256

/* new node access macros */
#define ntype(x)	((x)->n_type)

/* cons access macros */
#define car(x)		((x)->n_car)
#define cdr(x)		((x)->n_cdr)
#define rplaca(x,y)	((x)->n_car = (y))
#define rplacd(x,y)	((x)->n_cdr = (y))

/* symbol access macros */
#define getvalue(x)	 ((x)->n_vdata[0])
#define setvalue(x,v)	 ((x)->n_vdata[0] = (v))
#define getfunction(x)	 ((x)->n_vdata[1])
#define setfunction(x,v) ((x)->n_vdata[1] = (v))
#define getplist(x)	 ((x)->n_vdata[2])
#define setplist(x,v)	 ((x)->n_vdata[2] = (v))
#define getpname(x)	 ((x)->n_vdata[3])
#define setpname(x,v)	 ((x)->n_vdata[3] = (v))
#define SYMSIZE		4

/* closure access macros */
#define getname(x)     	((x)->n_vdata[0])
#define setname(x,v)   	((x)->n_vdata[0] = (v))
#define gettype(x)    	((x)->n_vdata[1])
#define settype(x,v)  	((x)->n_vdata[1] = (v))
#define getargs(x)     	((x)->n_vdata[2])
#define setargs(x,v)   	((x)->n_vdata[2] = (v))
#define getoargs(x)    	((x)->n_vdata[3])
#define setoargs(x,v)  	((x)->n_vdata[3] = (v))
#define getrest(x)     	((x)->n_vdata[4])
#define setrest(x,v)   	((x)->n_vdata[4] = (v))
#define getkargs(x)    	((x)->n_vdata[5])
#define setkargs(x,v)  	((x)->n_vdata[5] = (v))
#define getaargs(x)    	((x)->n_vdata[6])
#define setaargs(x,v)  	((x)->n_vdata[6] = (v))
#define getbody(x)     	((x)->n_vdata[7])
#define setbody(x,v)   	((x)->n_vdata[7] = (v))
#define getcenv(x)	((x)->n_vdata[8])	/* 64-bit port: was getenv */
#define setcenv(x,v)	((x)->n_vdata[8] = (v))	/* 64-bit port: was setenv */
#define getfenv(x)	((x)->n_vdata[9])
#define setfenv(x,v)	((x)->n_vdata[9] = (v))
#define getlambda(x)	((x)->n_vdata[10])
#define setlambda(x,v)	((x)->n_vdata[10] = (v))
#define CLOSIZE		11

/* vector access macros */
#define getsize(x)	((x)->n_vsize)
#define getelement(x,i)	((x)->n_vdata[i])
#define setelement(x,i,v) ((x)->n_vdata[i] = (v))

/* object access macros */
#define getclass(x)	((x)->n_vdata[0])
#define getivar(x,i)	((x)->n_vdata[(i)+1])
#define setivar(x,i,v)	((x)->n_vdata[(i)+1] = (v))

/* subr/fsubr access macros */
#define getsubr(x)	((x)->n_subr)
#define getoffset(x)	((x)->n_offset)

/* fixnum/flonum/char access macros */
#define getfixnum(x)    ((x)->n_fixnum)
#define getflonum(x)	((x)->n_flonum)
#define getchcode(x)	((x)->n_chcode)

/* string access macros */
#define getstring(x)	((x)->n_string)
#define getslength(x)	((x)->n_strlen)

/* file stream access macros */
#define getfile(x)	((x)->n_fp)
#define setfile(x,v)	((x)->n_fp = (v))
#define getsavech(x)	((x)->n_savech)
#define setsavech(x,v)	((x)->n_savech = (v))

/* unnamed stream access macros */
#define gethead(x)	((x)->n_car)
#define sethead(x,v)	((x)->n_car = (v))
#define gettail(x)	((x)->n_cdr)
#define settail(x,v)	((x)->n_cdr = (v))

/* node types */
#define FREE	0
#define SUBR	1
#define FSUBR	2
#define CONS	3
#define SYMBOL	4
#define FIXNUM	5
#define FLONUM	6
#define STRING	7
#define OBJECT	8
#define STREAM	9
#define VECTOR	10
#define CLOSURE	11
#define CHAR	12
#define USTREAM	13

/* subr/fsubr node */
#define n_subr		n_info.n_xsubr.xs_subr
#define n_offset	n_info.n_xsubr.xs_offset

/* cons node */
#define n_car		n_info.n_xcons.xc_car
#define n_cdr		n_info.n_xcons.xc_cdr

/* fixnum node */
#define n_fixnum	n_info.n_xfixnum.xf_fixnum

/* flonum node */
#define n_flonum	n_info.n_xflonum.xf_flonum
/* character node */
#define n_chcode	n_info.n_xchar.xc_chcode

/* string node */
#define n_string	n_info.n_xstring.xs_string
#define n_strlen	n_info.n_xstring.xs_length

/* stream node */
#define n_fp		n_info.n_xstream.xs_fp
#define n_savech	n_info.n_xstream.xs_savech

/* vector/object node */
#define n_vsize		n_info.n_xvector.xv_size
#define n_vdata		n_info.n_xvector.xv_data

/* subr/fsubr function pointer type (64-bit port: prototyped) */
typedef struct node *(*SUBRFUN)(void);

/* node structure */
typedef struct node {
    char n_type;		/* type of node */
    char n_flags;		/* flag bits */
    union ninfo { 		/* value */
	struct xsubr {		/* subr/fsubr node */
	    SUBRFUN xs_subr;		/* function pointer */
	    int xs_offset;		/* offset into funtab */
	} n_xsubr;
	struct xcons {		/* cons node */
	    struct node *xc_car;	/* the car pointer */
	    struct node *xc_cdr;	/* the cdr pointer */
	} n_xcons;
	struct xfixnum {	/* fixnum node */
	    FIXTYPE xf_fixnum;		/* fixnum value */
	} n_xfixnum;
	struct xflonum {	/* flonum node */
	    FLOTYPE xf_flonum;		/* flonum value */
	} n_xflonum;
	struct xchar {		/* character node */
	    int xc_chcode;		/* character code */
	} n_xchar;
	struct xstring {	/* string node */
	    int xs_length;		/* string length */
	    unsigned char *xs_string;	/* string pointer */
	} n_xstring;
	struct xstream { 	/* stream node */
	    FILE *xs_fp;		/* the file pointer */
	    int xs_savech;		/* lookahead character */
	} n_xstream;
	struct xvector {	/* vector/object/symbol/structure node */
	    int xv_size;		/* vector size */
	    struct node **xv_data;	/* vector data */
	} n_xvector;
    } n_info;
} *LVAL;

/* memory segment structure definition */
typedef struct segment {
    int sg_size;
    struct segment *sg_next;
    struct node sg_nodes[1];
} SEGMENT;

/* memory allocation functions are declared in xlproto.h */

#endif /* XLDMEM_H */
