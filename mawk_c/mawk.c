/* ============================================================================
   mawk.c  --  A feature-rich AWK interpreter in portable C.

   Language coverage:
     - BEGIN / END, pattern { action } rules, range patterns (pat1,pat2)
     - Full field handling: $0, $1..$NF, NF, NR, FNR, FILENAME, assigning
       to fields / $0 / NF with automatic OFS rebuild
     - Variables: FS OFS ORS RS SUBSEP CONVFMT OFMT RSTART RLENGTH,
       ARGC / ARGV, ENVIRON, plus user scalars and associative (incl.
       multi-subscript "i SUBSEP j") arrays
     - Full expression grammar: assignment ops, ?: ternary, || && !,
       relational + match (~ !~), concatenation, + - * / % ^ (power),
       unary +/-, ++/-- (pre & post), (expr) in array / (a,b) in array,
       grouping
     - Statements: if/else, while, do/while, for(;;), for(k in arr),
       break, continue, next, nextfile, exit, delete arr / delete arr[k],
       print / printf with > file, >> file, | command redirection,
       getline (plain / var / <file / var<file / cmd|getline / cmd|getline var)
     - User-defined functions: function name(params) { ... }, recursion,
       arrays passed by reference, scalars by value, extra params usable
       as local scalars/arrays, return
     - Built-ins: length substr index split sub gsub match sprintf
       toupper tolower sin cos atan2 sqrt exp log int rand srand
       system close fflush
     - A real regex engine: literals . ^ $ * + ? [...] [^...] (group)
       and alternation a|b, used for /ere/ patterns, ~ !~, FS, RS, split(),
       sub()/gsub(), match()
     - Command line: -v var=value, -F fs, -f progfile (repeatable),
       var=value file-list arguments, ARGV/ARGC mutation honored

   Not implemented (explicitly out of scope for this "mini but full-featured"
   build): true POSIX locale collation in bracket expressions, backreferences,
   coprocesses ("|&"), printf %*d dynamic width/precision, multi-byte/locale
   character classes ([:alpha:] etc. are NOT supported).

   Build (MSVC):     cl /TP /EHsc /O2 mawk.c /Fe:mawk.exe
                (or)  cl /std:c11 /O2 mawk.c /Fe:mawk.exe
   Build (gcc/clang): gcc -std=c11 -D_POSIX_C_SOURCE=200809L -O2 -o mawk mawk.c -lm

   Usage:
     mawk [-F fs] [-v var=val ...] 'program text' [file|var=val ...]
     mawk [-F fs] [-v var=val ...] -f prog.awk [-f prog2.awk ...] [file|var=val ...]
   ============================================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>

#ifdef _MSC_VER
  #define strdup _strdup
  #define popen  _popen
  #define pclose _pclose
  extern char **_environ;
  #define ENVIRON_ARR _environ
#else
  extern char **environ;
  #define ENVIRON_ARR environ
#endif

/* ---------------------------------------------------------------------- */
/* Utility                                                                 */
/* ---------------------------------------------------------------------- */

static void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) { fprintf(stderr, "mawk: out of memory\n"); exit(1); }
    return p;
}
static void *xrealloc(void *o, size_t n) {
    void *p = realloc(o, n ? n : 1);
    if (!p) { fprintf(stderr, "mawk: out of memory\n"); exit(1); }
    return p;
}
static char *xstrdup(const char *s) {
    char *p = (char *)xmalloc(strlen(s) + 1);
    strcpy(p, s);
    return p;
}
static void die(const char *msg) {
    fprintf(stderr, "mawk: %s\n", msg);
    exit(2);
}

/* growable string buffer helper */
typedef struct { char *data; size_t len, cap; } SBuf;
static void sb_init(SBuf *b) { b->cap = 64; b->len = 0; b->data = (char *)xmalloc(b->cap); b->data[0] = '\0'; }
static void sb_addn(SBuf *b, const char *s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        while (b->len + n + 1 > b->cap) b->cap *= 2;
        b->data = (char *)xrealloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}
static void sb_addc(SBuf *b, char c) { sb_addn(b, &c, 1); }
static void sb_adds(SBuf *b, const char *s) { sb_addn(b, s, strlen(s)); }
static void sb_free(SBuf *b) { free(b->data); b->data = NULL; }

/* ---------------------------------------------------------------------- */
/* Regex engine: literals, '.', '^', '$', '[...]', '[^...]', * + ?,       */
/* grouping '(...)' and alternation 'a|b', with backtracking.             */
/* ---------------------------------------------------------------------- */

typedef struct RAtom RAtom;
typedef struct RConcat { RAtom *atoms; int n; } RConcat;
typedef struct RAlt { RConcat *concats; int n; } RAlt;

struct RAtom {
    int type;                 /* 0=char 1=any 2=class 3=group */
    unsigned char lit;
    unsigned char bits[32];   /* class bitmap */
    int neg;
    RAlt *group;
    int quant;                /* 0 none, 1 star, 2 plus, 3 ques */
};

typedef struct {
    RAlt *top;
    int anchor_start;
    int anchor_end;
    char *srctext;             /* cached compiled source, for cache lookup */
} Regex;

/* small cache so repeatedly-used /literal/ patterns aren't recompiled every call */
typedef struct RCacheEnt { char *pat; Regex *re; struct RCacheEnt *next; } RCacheEnt;
static RCacheEnt *g_recache = NULL;

static void class_set(unsigned char *bits, unsigned char c) { bits[c >> 3] |= (unsigned char)(1u << (c & 7)); }
static int class_test(const unsigned char *bits, unsigned char c) { return (bits[c >> 3] >> (c & 7)) & 1; }

static RAlt *ralt_new(void) { RAlt *a = (RAlt *)xmalloc(sizeof(RAlt)); a->concats = NULL; a->n = 0; return a; }
static void ralt_add(RAlt *a, RConcat c) {
    a->concats = (RConcat *)xrealloc(a->concats, sizeof(RConcat) * (a->n + 1));
    a->concats[a->n++] = c;
}
static void rconcat_add(RConcat *c, RAtom a) {
    c->atoms = (RAtom *)xrealloc(c->atoms, sizeof(RAtom) * (c->n + 1));
    c->atoms[c->n++] = a;
}

/* recursive-descent regex parser: alt := concat ('|' concat)*  */
static RAlt *rparse_alt(const char **pp);

static RAtom rparse_atom(const char **pp) {
    const char *p = *pp;
    RAtom a; memset(&a, 0, sizeof(a));
    if (*p == '(') {
        p++;
        a.type = 3;
        a.group = rparse_alt(&p);
        if (*p == ')') p++;
    } else if (*p == '.') {
        a.type = 1; p++;
    } else if (*p == '[') {
        a.type = 2; p++;
        if (*p == '^') { a.neg = 1; p++; }
        int first = 1;
        while (*p && (*p != ']' || first)) {
            first = 0;
            unsigned char lo;
            if (*p == '\\' && p[1]) { lo = (unsigned char)p[1]; p += 2; }
            else lo = (unsigned char)*p++;
            if (*p == '-' && p[1] && p[1] != ']') {
                unsigned char hi = (unsigned char)p[1];
                p += 2;
                for (unsigned c = lo; c <= hi; c++) class_set(a.bits, (unsigned char)c);
            } else {
                class_set(a.bits, lo);
            }
        }
        if (*p == ']') p++;
    } else if (*p == '\\' && p[1]) {
        a.type = 0;
        char e = p[1];
        switch (e) {
            case 'n': a.lit = '\n'; break;
            case 't': a.lit = '\t'; break;
            case 'r': a.lit = '\r'; break;
            default: a.lit = (unsigned char)e; break;
        }
        p += 2;
    } else {
        a.type = 0; a.lit = (unsigned char)*p; p++;
    }
    if (*p == '*') { a.quant = 1; p++; }
    else if (*p == '+') { a.quant = 2; p++; }
    else if (*p == '?') { a.quant = 3; p++; }
    *pp = p;
    return a;
}

static RConcat rparse_concat(const char **pp) {
    RConcat c; c.atoms = NULL; c.n = 0;
    const char *p = *pp;
    while (*p && *p != '|' && *p != ')') {
        RAtom a = rparse_atom(&p);
        rconcat_add(&c, a);
    }
    *pp = p;
    return c;
}

static RAlt *rparse_alt(const char **pp) {
    RAlt *alt = ralt_new();
    const char *p = *pp;
    for (;;) {
        RConcat c = rparse_concat(&p);
        ralt_add(alt, c);
        if (*p == '|') { p++; continue; }
        break;
    }
    *pp = p;
    return alt;
}

static Regex *regex_compile(const char *pat) {
    for (RCacheEnt *e = g_recache; e; e = e->next)
        if (strcmp(e->pat, pat) == 0) return e->re;
    Regex *re = (Regex *)xmalloc(sizeof(Regex));
    const char *p;
    re->anchor_start = 0; re->anchor_end = 0;
    size_t plen = strlen(pat);
    char *tmp = xstrdup(pat);
    if (tmp[0] == '^') { re->anchor_start = 1; memmove(tmp, tmp + 1, strlen(tmp)); }
    if (plen > 0 && pat[plen - 1] == '$') {
        size_t tl = strlen(tmp);
        if (tl > 0 && tmp[tl - 1] == '$' && (tl < 2 || tmp[tl - 2] != '\\')) {
            tmp[tl - 1] = '\0';
            re->anchor_end = 1;
        }
    }
    p = tmp;
    re->top = rparse_alt(&p);
    re->srctext = tmp;
    RCacheEnt *ent = (RCacheEnt *)xmalloc(sizeof(RCacheEnt));
    ent->pat = xstrdup(pat); ent->re = re; ent->next = g_recache; g_recache = ent;
    return re;
}

/* continuation-frame based backtracking matcher */
typedef struct Frame {
    int kind;              /* 0 = sequence, 1 = repeat-atom */
    RAtom *atoms; int natoms; int idx;
    RAtom *ratom; int repcount;
    struct Frame *parent;
} Frame;

static int g_anchor_end_active;

static int matchatoms(Frame *f, const char *text, const char *start, int *outlen);

static int atom_char_matches(const RAtom *a, unsigned char c) {
    if (a->type == 1) return 1;
    if (a->type == 0) return a->lit == c;
    int r = class_test(a->bits, c);
    return a->neg ? !r : r;
}

static int try_repeat(RAtom *a, int repcount, Frame *cont, const char *text, const char *start, int *outlen) {
    int maxrep = (a->quant == 3) ? 1 : -1;
    int minrep_total = (a->quant == 2) ? 1 : 0;
    if (maxrep == -1 || repcount < maxrep) {
        if (a->type == 3) {
            for (int i = 0; i < a->group->n; i++) {
                Frame repf; repf.kind = 1; repf.ratom = a; repf.repcount = repcount + 1; repf.parent = cont;
                Frame bf; bf.kind = 0; bf.atoms = a->group->concats[i].atoms; bf.natoms = a->group->concats[i].n; bf.idx = 0; bf.parent = &repf;
                if (matchatoms(&bf, text, start, outlen)) return 1;
            }
        } else if (*text && atom_char_matches(a, (unsigned char)*text)) {
            Frame repf; repf.kind = 1; repf.ratom = a; repf.repcount = repcount + 1; repf.parent = cont;
            if (matchatoms(&repf, text + 1, start, outlen)) return 1;
        }
    }
    if (repcount >= minrep_total) {
        return matchatoms(cont, text, start, outlen);
    }
    return 0;
}

static int match_one_atom(RAtom *a, Frame *cont, const char *text, const char *start, int *outlen) {
    if (a->quant == 0) {
        if (a->type == 3) {
            for (int i = 0; i < a->group->n; i++) {
                Frame bf; bf.kind = 0; bf.atoms = a->group->concats[i].atoms; bf.natoms = a->group->concats[i].n; bf.idx = 0; bf.parent = cont;
                if (matchatoms(&bf, text, start, outlen)) return 1;
            }
            return 0;
        }
        if (*text && atom_char_matches(a, (unsigned char)*text))
            return matchatoms(cont, text + 1, start, outlen);
        return 0;
    }
    return try_repeat(a, 0, cont, text, start, outlen);
}

static int matchatoms(Frame *f, const char *text, const char *start, int *outlen) {
    if (!f) {
        if (g_anchor_end_active && *text != '\0') return 0;
        *outlen = (int)(text - start);
        return 1;
    }
    if (f->kind == 1) return try_repeat(f->ratom, f->repcount, f->parent, text, start, outlen);
    if (f->idx == f->natoms) return matchatoms(f->parent, text, start, outlen);
    RAtom *a = &f->atoms[f->idx];
    Frame nextf; nextf.kind = 0; nextf.atoms = f->atoms; nextf.natoms = f->natoms; nextf.idx = f->idx + 1; nextf.parent = f->parent;
    return match_one_atom(a, &nextf, text, start, outlen);
}

static int regex_match_at(Regex *re, const char *pos, int *outlen) {
    g_anchor_end_active = re->anchor_end;
    for (int i = 0; i < re->top->n; i++) {
        Frame bf; bf.kind = 0; bf.atoms = re->top->concats[i].atoms; bf.natoms = re->top->concats[i].n; bf.idx = 0; bf.parent = NULL;
        if (matchatoms(&bf, pos, pos, outlen)) return 1;
    }
    return 0;
}

/* returns 1 if found; fills mstart (offset in text) and mlen on success */
static int regex_search(const char *pat, const char *text, int *mstart, int *mlen) {
    Regex *re = regex_compile(pat);
    const char *t = text;
    do {
        int len;
        if (regex_match_at(re, t, &len)) {
            if (mstart) *mstart = (int)(t - text);
            if (mlen) *mlen = len;
            return 1;
        }
        if (re->anchor_start) break;
    } while (*t++ != '\0');
    return 0;
}
static int regex_match_bool(const char *pat, const char *text) { return regex_search(pat, text, NULL, NULL); }

/* ---------------------------------------------------------------------- */
/* Values                                                                  */
/* ---------------------------------------------------------------------- */

typedef struct {
    double num;
    char  *str;       /* NULL => pure number */
    int    is_strnum;  /* string that MAY be compared numerically if it looks numeric */
} Value;

static int looks_numeric(const char *s) {
    if (!s) return 0;
    while (isspace((unsigned char)*s)) s++;
    if (!*s) return 0;
    char *end;
    strtod(s, &end);
    if (end == s) return 0;
    while (isspace((unsigned char)*end)) end++;
    return *end == '\0';
}

static Value mknum(double d) { Value v; v.num = d; v.str = NULL; v.is_strnum = 0; return v; }
static Value mkstr(const char *s) { Value v; v.num = 0; v.str = xstrdup(s); v.is_strnum = 0; return v; }
static Value mkstrnum(const char *s) { Value v = mkstr(s); v.is_strnum = 1; return v; }

static const char *getvar_str(const char *name, char *buf, size_t bufsz); /* fwd */

static double to_num(const Value *v) {
    if (!v->str) return v->num;
    return strtod(v->str, NULL);
}
static const char *fmt_num(double d, const char *fmt, char *buf, size_t bufsz) {
    if (d == (double)(long long)d && fabs(d) < 1e15)
        snprintf(buf, bufsz, "%lld", (long long)d);
    else
        snprintf(buf, bufsz, fmt && *fmt ? fmt : "%.6g", d);
    return buf;
}
static const char *to_str_buf_fmt(const Value *v, char *buf, size_t bufsz, int use_ofmt) {
    if (v->str) return v->str;
    char fmtbuf[64];
    const char *fmt = getvar_str(use_ofmt ? "OFMT" : "CONVFMT", fmtbuf, sizeof(fmtbuf));
    return fmt_num(v->num, fmt, buf, bufsz);
}
static const char *to_str_buf(const Value *v, char *buf, size_t bufsz) { return to_str_buf_fmt(v, buf, bufsz, 0); }

static int is_numericish(const Value *v) {
    if (!v->str) return 1;
    return v->is_strnum && looks_numeric(v->str);
}
static int truthy(const Value *v) {
    if (!v->str) return v->num != 0.0;
    if (v->is_strnum && looks_numeric(v->str)) return to_num(v) != 0.0;
    return v->str[0] != '\0';
}
static void free_val(Value *v) { if (v->str) { free(v->str); v->str = NULL; } }
static Value copy_val(const Value *v) {
    Value r = *v;
    if (v->str) r.str = xstrdup(v->str);
    return r;
}

/* ---------------------------------------------------------------------- */
/* Variables / arrays / function-local scoping                            */
/* ---------------------------------------------------------------------- */

typedef struct ArrEntry {
    char *key;
    Value val;
    struct ArrEntry *next;
} ArrEntry;

typedef struct Var {
    char *name;
    Value val;
    int is_arr;
    ArrEntry *arr;
    struct Var *next;
} Var;

static Var *g_vars = NULL;

typedef struct Frame2 {         /* function call frame */
    char **names; Var **vars; int *is_alias; int n;
    struct Frame2 *prev;
} Frame2;
static Frame2 *g_curframe = NULL;

static Var *new_var(const char *name) {
    Var *v = (Var *)xmalloc(sizeof(Var));
    v->name = xstrdup(name);
    v->val = mkstr("");
    v->is_arr = 0;
    v->arr = NULL;
    v->next = NULL;
    return v;
}

static Var *frame_lookup(const char *name) {
    if (!g_curframe) return NULL;
    for (int i = 0; i < g_curframe->n; i++)
        if (strcmp(g_curframe->names[i], name) == 0) return g_curframe->vars[i];
    return NULL;
}

static Var *var_find(const char *name) {
    Var *lv = frame_lookup(name);
    if (lv) return lv;
    for (Var *v = g_vars; v; v = v->next)
        if (strcmp(v->name, name) == 0) return v;
    return NULL;
}
static Var *var_get(const char *name) {
    Var *lv = frame_lookup(name);
    if (lv) return lv;
    Var *v = var_find(name);
    if (v) return v;
    v = new_var(name);
    v->next = g_vars;
    g_vars = v;
    return v;
}
static ArrEntry *arr_find(Var *v, const char *key) {
    for (ArrEntry *e = v->arr; e; e = e->next)
        if (strcmp(e->key, key) == 0) return e;
    return NULL;
}
static ArrEntry *arr_get(Var *v, const char *key) {
    v->is_arr = 1;
    ArrEntry *e = arr_find(v, key);
    if (e) return e;
    e = (ArrEntry *)xmalloc(sizeof(ArrEntry));
    e->key = xstrdup(key);
    e->val = mkstr("");
    e->next = v->arr;
    v->arr = e;
    return e;
}
static void arr_clear(Var *v) {
    for (ArrEntry *e = v->arr; e; ) { ArrEntry *nx = e->next; free(e->key); free_val(&e->val); free(e); e = nx; }
    v->arr = NULL;
}

static void setvar_num(const char *name, double d) { Var *v = var_get(name); free_val(&v->val); v->val = mknum(d); }
static void setvar_str(const char *name, const char *s) { Var *v = var_get(name); free_val(&v->val); v->val = mkstr(s); }
static const char *getvar_str(const char *name, char *buf, size_t bufsz) {
    Var *v = var_find(name);
    if (!v) return "";
    return to_str_buf(&v->val, buf, bufsz);
}
static double getvar_num(const char *name) {
    Var *v = var_find(name);
    if (!v) return 0;
    return to_num(&v->val);
}

/* build SUBSEP-joined key from an expression list; caller frees nothing extra */
struct Node; /* fwd */
static Value eval(struct Node *n); /* fwd */
static void build_key(struct Node **idxs, int n, char *key, size_t keysz) {
    char subbuf[64];
    const char *subsep = getvar_str("SUBSEP", subbuf, sizeof(subbuf));
    if (!*subsep) subsep = "\x1c";
    key[0] = '\0';
    for (int i = 0; i < n; i++) {
        Value kv = eval(idxs[i]);
        char kb[128];
        if (i > 0) strncat(key, subsep, keysz - strlen(key) - 1);
        strncat(key, to_str_buf(&kv, kb, sizeof(kb)), keysz - strlen(key) - 1);
        free_val(&kv);
    }
}

/* ---------------------------------------------------------------------- */
/* Fields / current record                                                */
/* ---------------------------------------------------------------------- */

#define MAXFIELDS 8192
static char *g_fields[MAXFIELDS + 1];
static int g_nf = 0;
static char *g_record = NULL;

static void free_fields(void) {
    for (int i = 0; i <= MAXFIELDS; i++) if (g_fields[i]) { free(g_fields[i]); g_fields[i] = NULL; }
}

static void split_record(void) {
    free_fields();
    g_nf = 0;
    char fsbuf[64];
    const char *fs = getvar_str("FS", fsbuf, sizeof(fsbuf));
    if (!*fs) fs = " ";
    char *rec = xstrdup(g_record ? g_record : "");

    if (strcmp(fs, " ") == 0) {
        char *p = rec;
        while (*p) {
            while (*p && isspace((unsigned char)*p)) p++;
            if (!*p) break;
            char *start = p;
            while (*p && !isspace((unsigned char)*p)) p++;
            int len = (int)(p - start);
            if (g_nf < MAXFIELDS) { char *f = (char *)xmalloc(len + 1); memcpy(f, start, len); f[len] = '\0'; g_fields[++g_nf] = f; }
        }
    } else if (strlen(fs) == 1 && fs[0] != '\\') {
        char sep = fs[0]; char *p = rec;
        for (;;) {
            char *start = p;
            while (*p && *p != sep) p++;
            int len = (int)(p - start);
            if (g_nf < MAXFIELDS) { char *f = (char *)xmalloc(len + 1); memcpy(f, start, len); f[len] = '\0'; g_fields[++g_nf] = f; }
            if (!*p) break;
            p++;
        }
    } else {
        char *p = rec;
        while (1) {
            int ms, ml;
            if (regex_search(fs, p, &ms, &ml) && ml > 0) {
                int len = ms;
                if (g_nf < MAXFIELDS) { char *f = (char *)xmalloc(len + 1); memcpy(f, p, len); f[len] = '\0'; g_fields[++g_nf] = f; }
                p += ms + ml;
            } else {
                int len = (int)strlen(p);
                if (g_nf < MAXFIELDS) { char *f = (char *)xmalloc(len + 1); memcpy(f, p, len); f[len] = '\0'; g_fields[++g_nf] = f; }
                break;
            }
        }
    }
    free(rec);
    setvar_num("NF", g_nf);
}

static void rebuild_record(void) {
    char ofsbuf[64];
    const char *ofs = getvar_str("OFS", ofsbuf, sizeof(ofsbuf));
    SBuf b; sb_init(&b);
    for (int i = 1; i <= g_nf; i++) {
        if (i > 1) sb_adds(&b, ofs);
        sb_adds(&b, g_fields[i] ? g_fields[i] : "");
    }
    if (g_record) free(g_record);
    g_record = b.data; /* transfer ownership */
}

static void set_record(const char *s) {
    if (g_record) free(g_record);
    g_record = xstrdup(s);
    split_record();
}
static const char *get_field(int i) {
    if (i == 0) return g_record ? g_record : "";
    if (i < 0 || i > g_nf || !g_fields[i]) return "";
    return g_fields[i];
}
static void set_field(int i, const char *s) {
    if (i == 0) { set_record(s); return; }
    if (i < 0) return;
    if (i > MAXFIELDS) die("field index too large");
    if (i > g_nf) {
        for (int k = g_nf + 1; k < i; k++) if (!g_fields[k]) g_fields[k] = xstrdup("");
        g_nf = i;
        setvar_num("NF", g_nf);
    }
    if (g_fields[i]) free(g_fields[i]);
    g_fields[i] = xstrdup(s);
    rebuild_record();
}

/* ---------------------------------------------------------------------- */
/* I/O stream cache (for getline <file / cmd|getline and print redirection)*/
/* ---------------------------------------------------------------------- */

typedef struct IOEnt {
    char *key; FILE *fp; int is_pipe; int is_output;
    char *leftover; /* buffered bytes for regex-RS reads */
    struct IOEnt *next;
} IOEnt;
static IOEnt *g_io = NULL;

static IOEnt *io_find(const char *key, int is_output) {
    for (IOEnt *e = g_io; e; e = e->next) if (e->is_output == is_output && strcmp(e->key, key) == 0) return e;
    return NULL;
}
static IOEnt *io_open_input(const char *key, int is_cmd) {
    IOEnt *e = io_find(key, 0);
    if (e) return e;
    FILE *fp = is_cmd ? popen(key, "r") : fopen(key, "rb");
    if (!fp) return NULL;
    e = (IOEnt *)xmalloc(sizeof(IOEnt));
    e->key = xstrdup(key); e->fp = fp; e->is_pipe = is_cmd; e->is_output = 0; e->leftover = xstrdup("");
    e->next = g_io; g_io = e;
    return e;
}
static IOEnt *io_open_output(const char *key, int mode /*0=truncate,1=append,2=pipe*/) {
    IOEnt *e = io_find(key, 1);
    if (e) return e;
    FILE *fp = (mode == 2) ? popen(key, "w") : fopen(key, mode == 1 ? "ab" : "wb");
    if (!fp) return NULL;
    e = (IOEnt *)xmalloc(sizeof(IOEnt));
    e->key = xstrdup(key); e->fp = fp; e->is_pipe = (mode == 2); e->is_output = 1; e->leftover = NULL;
    e->next = g_io; g_io = e;
    return e;
}
static int io_close(const char *key) {
    int rc = -1;
    IOEnt **pp = &g_io;
    while (*pp) {
        if (strcmp((*pp)->key, key) == 0) {
            IOEnt *dead = *pp;
            rc = dead->is_pipe ? pclose(dead->fp) : fclose(dead->fp);
            *pp = dead->next;
            free(dead->key); if (dead->leftover) free(dead->leftover);
            free(dead);
        } else pp = &(*pp)->next;
    }
    return rc;
}
static void io_close_all(void) {
    while (g_io) {
        IOEnt *dead = g_io; g_io = g_io->next;
        if (dead->is_pipe) pclose(dead->fp); else fclose(dead->fp);
        free(dead->key); if (dead->leftover) free(dead->leftover);
        free(dead);
    }
}

/* ---------------------------------------------------------------------- */
/* Record reading according to RS (line / char / paragraph / regex)       */
/* ---------------------------------------------------------------------- */

static char *read_line_raw(FILE *fp, int *got_any) {
    SBuf b; sb_init(&b);
    int c; int any = 0;
    while ((c = fgetc(fp)) != EOF) {
        any = 1;
        if (c == '\n') break;
        sb_addc(&b, (char)c);
    }
    *got_any = any;
    if (!any) { sb_free(&b); return NULL; }
    if (b.len && b.data[b.len - 1] == '\r') b.data[--b.len] = '\0';
    return b.data;
}

static char *read_record_generic(FILE *fp, IOEnt *ioent /* may be NULL for main input */) {
    char rsbuf[16];
    const char *rs = getvar_str("RS", rsbuf, sizeof(rsbuf));
    if (!rs[0]) {
        char *line; int any;
        for (;;) {
            line = read_line_raw(fp, &any);
            if (!any) return NULL;
            if (line[0] != '\0') break;
            free(line);
        }
        SBuf b; sb_init(&b); sb_adds(&b, line); free(line);
        for (;;) {
            line = read_line_raw(fp, &any);
            if (!any) break;
            if (line[0] == '\0') { free(line); break; }
            sb_addc(&b, '\n'); sb_adds(&b, line); free(line);
        }
        return b.data;
    }
    if (rs[0] && rs[1] == '\0') {
        if (rs[0] == '\n') { int any; return read_line_raw(fp, &any); }
        SBuf b; sb_init(&b); int c; int any = 0;
        while ((c = fgetc(fp)) != EOF) { any = 1; if ((char)c == rs[0]) break; sb_addc(&b, (char)c); }
        if (!any) { sb_free(&b); return NULL; }
        return b.data;
    }
    /* multi-char RS: treated as regex, using a growable leftover buffer per stream */
    static char *g_anon_leftover = NULL;
    char **leftoverp = ioent ? &ioent->leftover : &g_anon_leftover;
    if (!*leftoverp) *leftoverp = xstrdup("");
    for (;;) {
        int ms, ml;
        if (**leftoverp && regex_search(rs, *leftoverp, &ms, &ml) && ml > 0) {
            char *rec = (char *)xmalloc(ms + 1);
            memcpy(rec, *leftoverp, ms); rec[ms] = '\0';
            char *rest = xstrdup(*leftoverp + ms + ml);
            free(*leftoverp); *leftoverp = rest;
            return rec;
        }
        char chunk[4096];
        size_t got = fread(chunk, 1, sizeof(chunk), fp);
        if (got == 0) {
            if (**leftoverp) { char *rec = xstrdup(*leftoverp); free(*leftoverp); *leftoverp = xstrdup(""); return rec; }
            return NULL;
        }
        size_t oldlen = strlen(*leftoverp);
        *leftoverp = (char *)xrealloc(*leftoverp, oldlen + got + 1);
        memcpy(*leftoverp + oldlen, chunk, got);
        (*leftoverp)[oldlen + got] = '\0';
    }
}

/* ---------------------------------------------------------------------- */
/* Lexer                                                                   */
/* ---------------------------------------------------------------------- */

typedef enum {
    T_EOF, T_NUM, T_STR, T_ERE, T_IDENT, T_FUNCNAME,
    T_BEGIN, T_END, T_IF, T_ELSE, T_WHILE, T_DO, T_FOR, T_PRINT, T_PRINTF,
    T_NEXT, T_NEXTFILE, T_EXIT, T_BREAK, T_CONTINUE, T_DELETE, T_IN,
    T_FUNCTION, T_GETLINE, T_RETURN,
    T_LBRACE, T_RBRACE, T_LPAREN, T_RPAREN, T_LBRACKET, T_RBRACKET,
    T_SEMI, T_NEWLINE, T_COMMA, T_DOLLAR,
    T_ASSIGN, T_ADDASSIGN, T_SUBASSIGN, T_MULASSIGN, T_DIVASSIGN, T_MODASSIGN, T_POWASSIGN,
    T_OR, T_AND, T_NOT, T_LT, T_LE, T_GT, T_GE, T_EQ, T_NE, T_MATCH, T_NOMATCH,
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PERCENT, T_CARET, T_INCR, T_DECR,
    T_QUESTION, T_COLON, T_PIPE, T_APPEND
} TokType;

typedef struct { TokType type; double num; char *text; } Token;

static const char *g_src;
static int g_pos;
static Token g_cur;
static int g_prev_significant;

static void lex_skip_ws_comments(void) {
    for (;;) {
        while (g_src[g_pos] == ' ' || g_src[g_pos] == '\t' || g_src[g_pos] == '\r' || (g_src[g_pos] == '\\' && g_src[g_pos + 1] == '\n')) {
            if (g_src[g_pos] == '\\') g_pos += 2; else g_pos++;
        }
        if (g_src[g_pos] == '#') { while (g_src[g_pos] && g_src[g_pos] != '\n') g_pos++; continue; }
        break;
    }
}

static Token lex_next(void) {
    lex_skip_ws_comments();
    Token t; t.text = NULL; t.num = 0;
    char c = g_src[g_pos];
    if (c == '\0') { t.type = T_EOF; return t; }
    if (c == '\n') { g_pos++; t.type = T_NEWLINE; return t; }
    if (c == '"') {
        g_pos++;
        SBuf b; sb_init(&b);
        while (g_src[g_pos] && g_src[g_pos] != '"') {
            char ch = g_src[g_pos++];
            if (ch == '\\' && g_src[g_pos]) {
                char e = g_src[g_pos++];
                switch (e) {
                    case 'n': ch = '\n'; break;
                    case 't': ch = '\t'; break;
                    case 'r': ch = '\r'; break;
                    case '\\': ch = '\\'; break;
                    case '"': ch = '"'; break;
                    case '/': ch = '/'; break;
                    default: ch = e; break;
                }
            }
            sb_addc(&b, ch);
        }
        if (g_src[g_pos] == '"') g_pos++;
        t.type = T_STR; t.text = b.data;
        return t;
    }
    if (isdigit((unsigned char)c) || (c == '.' && isdigit((unsigned char)g_src[g_pos + 1]))) {
        char *end;
        t.num = strtod(g_src + g_pos, &end);
        g_pos = (int)(end - g_src);
        t.type = T_NUM;
        return t;
    }
    if (isalpha((unsigned char)c) || c == '_') {
        int start = g_pos;
        while (isalnum((unsigned char)g_src[g_pos]) || g_src[g_pos] == '_') g_pos++;
        int len = g_pos - start;
        char *word = (char *)xmalloc(len + 1);
        memcpy(word, g_src + start, len); word[len] = '\0';
        struct { const char *w; TokType tt; } kw[] = {
            {"BEGIN", T_BEGIN}, {"END", T_END}, {"if", T_IF}, {"else", T_ELSE},
            {"while", T_WHILE}, {"do", T_DO}, {"for", T_FOR}, {"print", T_PRINT}, {"printf", T_PRINTF},
            {"next", T_NEXT}, {"nextfile", T_NEXTFILE}, {"exit", T_EXIT}, {"break", T_BREAK}, {"continue", T_CONTINUE},
            {"delete", T_DELETE}, {"in", T_IN}, {"function", T_FUNCTION}, {"func", T_FUNCTION},
            {"getline", T_GETLINE}, {"return", T_RETURN}, {NULL, T_EOF}
        };
        for (int i = 0; kw[i].w; i++) if (strcmp(word, kw[i].w) == 0) { t.type = kw[i].tt; free(word); return t; }
        if (g_src[g_pos] == '(') { t.type = T_FUNCNAME; t.text = word; return t; }
        t.type = T_IDENT; t.text = word;
        return t;
    }
    if (c == '/') {
        if (!g_prev_significant) {
            g_pos++;
            SBuf b; sb_init(&b);
            while (g_src[g_pos] && g_src[g_pos] != '/') {
                char ch = g_src[g_pos++];
                if (ch == '\\' && g_src[g_pos]) { sb_addc(&b, ch); ch = g_src[g_pos++]; }
                sb_addc(&b, ch);
            }
            if (g_src[g_pos] == '/') g_pos++;
            t.type = T_ERE; t.text = b.data;
            return t;
        }
    }
    #define OP2(a,b,tt) if (c==a && g_src[g_pos+1]==b) { g_pos+=2; t.type=tt; return t; }
    OP2('+','+',T_INCR) OP2('-','-',T_DECR)
    OP2('+','=',T_ADDASSIGN) OP2('-','=',T_SUBASSIGN)
    OP2('*','=',T_MULASSIGN) OP2('/','=',T_DIVASSIGN) OP2('%','=',T_MODASSIGN) OP2('^','=',T_POWASSIGN)
    OP2('=','=',T_EQ) OP2('!','=',T_NE)
    OP2('<','=',T_LE) OP2('>','=',T_GE)
    OP2('&','&',T_AND) OP2('|','|',T_OR)
    OP2('!','~',T_NOMATCH)
    OP2('>','>',T_APPEND)
    #undef OP2
    g_pos++;
    switch (c) {
        case '{': t.type = T_LBRACE; return t;
        case '}': t.type = T_RBRACE; return t;
        case '(': t.type = T_LPAREN; return t;
        case ')': t.type = T_RPAREN; return t;
        case '[': t.type = T_LBRACKET; return t;
        case ']': t.type = T_RBRACKET; return t;
        case ';': t.type = T_SEMI; return t;
        case ',': t.type = T_COMMA; return t;
        case '$': t.type = T_DOLLAR; return t;
        case '=': t.type = T_ASSIGN; return t;
        case '<': t.type = T_LT; return t;
        case '>': t.type = T_GT; return t;
        case '!': t.type = T_NOT; return t;
        case '~': t.type = T_MATCH; return t;
        case '+': t.type = T_PLUS; return t;
        case '-': t.type = T_MINUS; return t;
        case '*': t.type = T_STAR; return t;
        case '/': t.type = T_SLASH; return t;
        case '%': t.type = T_PERCENT; return t;
        case '^': t.type = T_CARET; return t;
        case '?': t.type = T_QUESTION; return t;
        case ':': t.type = T_COLON; return t;
        case '|': t.type = T_PIPE; return t;
    }
    fprintf(stderr, "mawk: unexpected character '%c'\n", c);
    exit(2);
}

static int token_is_value_end(TokType t) {
    switch (t) {
        case T_NUM: case T_STR: case T_IDENT: case T_RPAREN: case T_RBRACKET:
        case T_DOLLAR: case T_INCR: case T_DECR:
            return 1;
        default: return 0;
    }
}
static void advance(void) {
    g_cur = lex_next();
    g_prev_significant = token_is_value_end(g_cur.type);
}

/* ---------------------------------------------------------------------- */
/* AST                                                                     */
/* ---------------------------------------------------------------------- */

typedef enum {
    N_NUM, N_STR, N_ERE, N_VAR, N_ARRREF, N_FIELD, N_ASSIGN, N_BINOP, N_CONCAT,
    N_AND, N_OR, N_NOT, N_UNARY, N_PREINCDEC, N_POSTINCDEC, N_CALL, N_MATCH,
    N_GROUP, N_TERNARY, N_IN, N_GETLINE, N_POW
} NType;

typedef struct Node {
    NType type;
    int op;
    double num;
    char *str;
    struct Node *a, *b, *c;
    struct Node **list; int listn;
    int gmode; /* getline mode: 0 plain, 1 <file, 2 cmd| */
} Node;

static Node *newnode(NType t) { Node *n = (Node *)xmalloc(sizeof(Node)); memset(n, 0, sizeof(*n)); n->type = t; return n; }

typedef enum { S_EXPR, S_PRINT, S_PRINTF, S_IF, S_WHILE, S_DOWHILE, S_FOR, S_FORIN,
               S_BLOCK, S_NEXT, S_NEXTFILE, S_EXIT, S_BREAK, S_CONTINUE, S_DELETE, S_RETURN } SType;

typedef struct Stmt {
    SType type;
    Node *e1, *e2, *e3;
    struct Stmt *s1, *s2;
    struct Stmt **list; int listn;
    Node **args; int argn;
    char *name;
    int redir_mode;      /* 0 none, 1 '>', 2 '>>', 3 '|' */
    Node *redir_target;
} Stmt;

static Stmt *newstmt(SType t) { Stmt *s = (Stmt *)xmalloc(sizeof(Stmt)); memset(s, 0, sizeof(*s)); s->type = t; return s; }

typedef enum { P_BEGIN, P_END, P_ALWAYS, P_EXPR, P_ERE, P_RANGE } PType;
typedef struct Rule {
    PType ptype;
    Node *pexpr, *pexpr2;
    char *pere;
    Stmt *action;
    int range_active; /* runtime state for range patterns */
    struct Rule *next;
} Rule;
static Rule *g_rules = NULL, *g_rules_tail = NULL;
static void add_rule(Rule *r) { r->next = NULL; if (!g_rules) g_rules = r; else g_rules_tail->next = r; g_rules_tail = r; }

typedef struct FuncDef {
    char *name; char **params; int nparams; Stmt *body;
    struct FuncDef *next;
} FuncDef;
static FuncDef *g_funcs = NULL;
static FuncDef *find_func(const char *name) { for (FuncDef *f = g_funcs; f; f = f->next) if (strcmp(f->name, name) == 0) return f; return NULL; }

/* ---------------------------------------------------------------------- */
/* Parser                                                                  */
/* ---------------------------------------------------------------------- */

static void skip_newlines(void) { while (g_cur.type == T_NEWLINE) advance(); }
static void skip_terms(void) { while (g_cur.type == T_NEWLINE || g_cur.type == T_SEMI) advance(); }
static void expect(TokType t, const char *what) {
    if (g_cur.type != t) { fprintf(stderr, "mawk: parse error, expected %s\n", what); exit(2); }
    advance();
}

static Node *parse_expr(void);
static Node *parse_ternary(void);
static Node *parse_unary_lvalue(void);
static Stmt *parse_stmt(void);
static Stmt *parse_block(void);
static int g_suppress_gt = 0; /* used while parsing print/printf argument lists */

static Node **parse_expr_list(int *count, TokType endtok) {
    Node **arr = NULL; int n = 0, cap = 0;
    skip_newlines();
    if (g_cur.type != endtok) {
        for (;;) {
            skip_newlines();
            Node *e = parse_ternary();
            if (n == cap) { cap = cap ? cap * 2 : 4; arr = (Node **)xrealloc(arr, cap * sizeof(Node *)); }
            arr[n++] = e;
            skip_newlines();
            if (g_cur.type == T_COMMA) { advance(); continue; }
            break;
        }
    }
    *count = n;
    return arr;
}

/* parse a bare lvalue target for getline: $expr | IDENT[idx,...] | IDENT */
static Node *parse_lvalue_only(void) {
    if (g_cur.type == T_DOLLAR) { advance(); Node *idx = parse_unary_lvalue(); Node *n = newnode(N_FIELD); n->a = idx; return n; }
    if (g_cur.type == T_IDENT) {
        char *name = g_cur.text; advance();
        if (g_cur.type == T_LBRACKET) {
            advance();
            int cnt; Node **idxs = parse_expr_list(&cnt, T_RBRACKET);
            expect(T_RBRACKET, "]");
            Node *n = newnode(N_ARRREF); n->str = name; n->list = idxs; n->listn = cnt;
            return n;
        }
        Node *n = newnode(N_VAR); n->str = name; return n;
    }
    return NULL;
}

static int starts_getline_target(void) { return g_cur.type == T_DOLLAR || g_cur.type == T_IDENT; }

static Node *parse_primary(void) {
    Node *n;
    if (g_cur.type == T_GETLINE) {
        advance();
        Node *target = NULL;
        if (starts_getline_target()) target = parse_lvalue_only();
        n = newnode(N_GETLINE);
        n->a = target;
        if (g_cur.type == T_LT) {
            advance();
            int save = g_suppress_gt; g_suppress_gt = 0;
            Node *file = parse_unary_lvalue();
            g_suppress_gt = save;
            n->b = file; n->gmode = 1;
        } else {
            n->gmode = 0;
        }
        return n;
    }
    if (g_cur.type == T_NUM) { n = newnode(N_NUM); n->num = g_cur.num; advance(); return n; }
    if (g_cur.type == T_STR) { n = newnode(N_STR); n->str = g_cur.text; advance(); return n; }
    if (g_cur.type == T_ERE) { n = newnode(N_ERE); n->str = g_cur.text; advance(); return n; }
    if (g_cur.type == T_DOLLAR) { advance(); Node *idx = parse_unary_lvalue(); n = newnode(N_FIELD); n->a = idx; return n; }
    if (g_cur.type == T_LPAREN) {
        advance();
        int save = g_suppress_gt; g_suppress_gt = 0;
        Node *e = parse_expr();
        if (g_cur.type == T_COMMA) {
            Node **lst = NULL; int cnt = 0, cap = 0;
            lst = (Node **)xrealloc(lst, sizeof(Node *) * (cap = 4)); lst[cnt++] = e;
            while (g_cur.type == T_COMMA) {
                advance(); skip_newlines();
                Node *e2 = parse_expr();
                if (cnt == cap) { cap *= 2; lst = (Node **)xrealloc(lst, sizeof(Node *) * cap); }
                lst[cnt++] = e2;
            }
            expect(T_RPAREN, ")");
            g_suppress_gt = save;
            expect(T_IN, "in");
            if (g_cur.type != T_IDENT) { fprintf(stderr, "mawk: expected array name after 'in'\n"); exit(2); }
            char *arrname = g_cur.text; advance();
            n = newnode(N_IN); n->list = lst; n->listn = cnt; n->str = arrname;
            return n;
        }
        expect(T_RPAREN, ")");
        g_suppress_gt = save;
        n = newnode(N_GROUP); n->a = e;
        return n;
    }
    if (g_cur.type == T_NOT) { advance(); n = newnode(N_NOT); n->a = parse_primary(); return n; }
    if (g_cur.type == T_MINUS) { advance(); n = newnode(N_UNARY); n->op = T_MINUS; n->a = parse_primary(); return n; }
    if (g_cur.type == T_PLUS) { advance(); n = newnode(N_UNARY); n->op = T_PLUS; n->a = parse_primary(); return n; }
    if (g_cur.type == T_INCR || g_cur.type == T_DECR) {
        int op = g_cur.type; advance();
        Node *target = parse_primary();
        n = newnode(N_PREINCDEC); n->op = op; n->a = target; return n;
    }
    if (g_cur.type == T_FUNCNAME) {
        char *name = g_cur.text; advance();
        expect(T_LPAREN, "(");
        int save = g_suppress_gt; g_suppress_gt = 0;
        int cnt; Node **args = parse_expr_list(&cnt, T_RPAREN);
        g_suppress_gt = save;
        expect(T_RPAREN, ")");
        n = newnode(N_CALL); n->str = name; n->list = args; n->listn = cnt;
        return n;
    }
    if (g_cur.type == T_IDENT) {
        char *name = g_cur.text; advance();
        if (g_cur.type == T_LBRACKET) {
            advance();
            int save = g_suppress_gt; g_suppress_gt = 0;
            int cnt; Node **idxs = parse_expr_list(&cnt, T_RBRACKET);
            g_suppress_gt = save;
            expect(T_RBRACKET, "]");
            n = newnode(N_ARRREF); n->str = name; n->list = idxs; n->listn = cnt;
        } else {
            n = newnode(N_VAR); n->str = name;
        }
        if (g_cur.type == T_INCR || g_cur.type == T_DECR) {
            int op = g_cur.type; advance();
            Node *p = newnode(N_POSTINCDEC); p->op = op; p->a = n;
            return p;
        }
        return n;
    }
    fprintf(stderr, "mawk: parse error near token %d\n", g_cur.type);
    exit(2);
}

static Node *parse_unary_lvalue(void) { return parse_primary(); }

static Node *parse_pow(void) {
    Node *l = parse_primary();
    if (g_cur.type == T_CARET) {
        advance();
        Node *r = parse_pow();
        Node *n = newnode(N_POW); n->a = l; n->b = r; return n;
    }
    return l;
}
static Node *parse_mul(void) {
    Node *l = parse_pow();
    for (;;) {
        if (g_cur.type == T_STAR || g_cur.type == T_SLASH || g_cur.type == T_PERCENT) {
            int op = g_cur.type; advance();
            Node *r = parse_pow();
            Node *n = newnode(N_BINOP); n->op = op; n->a = l; n->b = r; l = n;
        } else break;
    }
    return l;
}
static Node *parse_add(void) {
    Node *l = parse_mul();
    for (;;) {
        if (g_cur.type == T_PLUS || g_cur.type == T_MINUS) {
            int op = g_cur.type; advance();
            Node *r = parse_mul();
            Node *n = newnode(N_BINOP); n->op = op; n->a = l; n->b = r; l = n;
        } else break;
    }
    return l;
}
static int starts_value(TokType t) {
    switch (t) {
        case T_NUM: case T_STR: case T_ERE: case T_IDENT: case T_FUNCNAME:
        case T_DOLLAR: case T_LPAREN: case T_NOT: case T_MINUS: case T_PLUS:
        case T_INCR: case T_DECR:
            return 1;
        default: return 0;
    }
}
static Node *parse_concat(void) {
    Node *l = parse_add();
    while (starts_value(g_cur.type)) {
        Node *r = parse_add();
        Node *n = newnode(N_CONCAT); n->a = l; n->b = r; l = n;
    }
    while (g_cur.type == T_PIPE) {
        int save_pos = g_pos; Token save_cur = g_cur; int save_sig = g_prev_significant;
        advance();
        if (g_cur.type == T_GETLINE) {
            advance();
            Node *target = NULL;
            if (starts_getline_target()) target = parse_lvalue_only();
            Node *n = newnode(N_GETLINE); n->a = target; n->b = l; n->gmode = 2;
            l = n;
        } else {
            g_pos = save_pos; g_cur = save_cur; g_prev_significant = save_sig;
            break;
        }
    }
    return l;
}
static Node *parse_rel(void) {
    Node *l = parse_concat();
    TokType t = g_cur.type;
    if (t == T_LT || t == T_LE || t == T_GE || t == T_EQ || t == T_NE || (t == T_GT && !g_suppress_gt)) {
        advance();
        Node *r = parse_concat();
        Node *n = newnode(N_BINOP); n->op = t; n->a = l; n->b = r; l = n;
    } else if (t == T_MATCH || t == T_NOMATCH) {
        advance();
        Node *r = parse_concat();
        Node *n = newnode(N_MATCH); n->op = t; n->a = l; n->b = r; l = n;
    } else if (t == T_IN) {
        advance();
        if (g_cur.type != T_IDENT) { fprintf(stderr, "mawk: expected array name after 'in'\n"); exit(2); }
        char *arrname = g_cur.text; advance();
        Node **lst = (Node **)xmalloc(sizeof(Node *));
        lst[0] = l;
        Node *n = newnode(N_IN); n->list = lst; n->listn = 1; n->str = arrname;
        l = n;
    }
    return l;
}
static Node *parse_and(void) {
    Node *l = parse_rel();
    while (g_cur.type == T_AND) { advance(); skip_newlines(); Node *r = parse_rel(); Node *n = newnode(N_AND); n->a = l; n->b = r; l = n; }
    return l;
}
static Node *parse_or(void) {
    Node *l = parse_and();
    while (g_cur.type == T_OR) { advance(); skip_newlines(); Node *r = parse_and(); Node *n = newnode(N_OR); n->a = l; n->b = r; l = n; }
    return l;
}
static Node *parse_ternary(void) {
    Node *c = parse_or();
    if (g_cur.type == T_QUESTION) {
        advance(); skip_newlines();
        Node *a = parse_ternary();
        skip_newlines(); expect(T_COLON, ":"); skip_newlines();
        Node *b = parse_ternary();
        Node *n = newnode(N_TERNARY); n->a = c; n->b = a; n->c = b;
        return n;
    }
    return c;
}
static Node *parse_expr(void) {
    Node *l = parse_ternary();
    TokType t = g_cur.type;
    if (t == T_ASSIGN || t == T_ADDASSIGN || t == T_SUBASSIGN || t == T_MULASSIGN || t == T_DIVASSIGN || t == T_MODASSIGN || t == T_POWASSIGN) {
        advance();
        Node *r = parse_expr();
        Node *n = newnode(N_ASSIGN); n->op = t; n->a = l; n->b = r;
        return n;
    }
    return l;
}

static Stmt *parse_simple_or_null(void) {
    if (g_cur.type == T_SEMI || g_cur.type == T_RPAREN) return NULL;
    return parse_stmt();
}

static Stmt *parse_block(void) {
    expect(T_LBRACE, "{");
    Stmt *blk = newstmt(S_BLOCK);
    Stmt **arr = NULL; int n = 0, cap = 0;
    skip_terms();
    while (g_cur.type != T_RBRACE && g_cur.type != T_EOF) {
        Stmt *s = parse_stmt();
        if (s) { if (n == cap) { cap = cap ? cap * 2 : 8; arr = (Stmt **)xrealloc(arr, cap * sizeof(Stmt *)); } arr[n++] = s; }
        skip_terms();
    }
    expect(T_RBRACE, "}");
    blk->list = arr; blk->listn = n;
    return blk;
}

static Node **parse_print_args(int *count) {
    Node **arr = NULL; int n = 0, cap = 0;
    int save = g_suppress_gt; g_suppress_gt = 1;
    if (g_cur.type != T_SEMI && g_cur.type != T_NEWLINE && g_cur.type != T_RBRACE && g_cur.type != T_EOF &&
        g_cur.type != T_GT && g_cur.type != T_APPEND && g_cur.type != T_PIPE) {
        for (;;) {
            Node *e = parse_ternary();
            if (n == cap) { cap = cap ? cap * 2 : 4; arr = (Node **)xrealloc(arr, cap * sizeof(Node *)); }
            arr[n++] = e;
            if (g_cur.type == T_COMMA) { advance(); skip_newlines(); continue; }
            break;
        }
    }
    g_suppress_gt = save;
    *count = n;
    return arr;
}

static Stmt *parse_stmt(void) {
    skip_newlines();
    switch (g_cur.type) {
        case T_LBRACE: return parse_block();
        case T_IF: {
            advance(); expect(T_LPAREN, "(");
            Node *cond = parse_expr();
            expect(T_RPAREN, ")");
            skip_newlines();
            Stmt *then_s = parse_stmt();
            Stmt *else_s = NULL;
            int save_pos = g_pos; Token save_cur = g_cur; int save_sig = g_prev_significant;
            skip_terms();
            if (g_cur.type == T_ELSE) { advance(); skip_newlines(); else_s = parse_stmt(); }
            else { g_pos = save_pos; g_cur = save_cur; g_prev_significant = save_sig; }
            Stmt *s = newstmt(S_IF); s->e1 = cond; s->s1 = then_s; s->s2 = else_s;
            return s;
        }
        case T_WHILE: {
            advance(); expect(T_LPAREN, "(");
            Node *cond = parse_expr();
            expect(T_RPAREN, ")");
            skip_newlines();
            Stmt *body = parse_stmt();
            Stmt *s = newstmt(S_WHILE); s->e1 = cond; s->s1 = body;
            return s;
        }
        case T_DO: {
            advance(); skip_newlines();
            Stmt *body = parse_stmt();
            skip_terms();
            expect(T_WHILE, "while");
            expect(T_LPAREN, "(");
            Node *cond = parse_expr();
            expect(T_RPAREN, ")");
            Stmt *s = newstmt(S_DOWHILE); s->e1 = cond; s->s1 = body;
            return s;
        }
        case T_FOR: {
            advance(); expect(T_LPAREN, "(");
            if (g_cur.type == T_IDENT) {
                int save_pos = g_pos; Token save_cur = g_cur; int save_sig = g_prev_significant;
                char *var = xstrdup(g_cur.text);
                advance();
                if (g_cur.type == T_IN) {
                    advance();
                    if (g_cur.type != T_IDENT) { fprintf(stderr, "mawk: expected array name after 'in'\n"); exit(2); }
                    char *arrname = xstrdup(g_cur.text);
                    advance();
                    expect(T_RPAREN, ")");
                    skip_newlines();
                    Stmt *body = parse_stmt();
                    Stmt *s = newstmt(S_FORIN);
                    s->name = var; s->s1 = body;
                    s->args = (Node **)xmalloc(sizeof(Node *));
                    Node *an = newnode(N_VAR); an->str = arrname;
                    s->args[0] = an; s->argn = 1;
                    return s;
                }
                g_pos = save_pos; g_cur = save_cur; g_prev_significant = save_sig;
                free(var);
            }
            Stmt *init = parse_simple_or_null();
            expect(T_SEMI, ";");
            Node *cond = (g_cur.type == T_SEMI) ? NULL : parse_expr();
            expect(T_SEMI, ";");
            Stmt *incr = parse_simple_or_null();
            expect(T_RPAREN, ")");
            skip_newlines();
            Stmt *body = parse_stmt();
            Stmt *s = newstmt(S_FOR); s->s1 = init; s->e1 = cond; s->s2 = incr;
            s->list = (Stmt **)xmalloc(sizeof(Stmt *)); s->list[0] = body; s->listn = 1;
            return s;
        }
        case T_PRINT: {
            advance();
            int cnt; Node **args = parse_print_args(&cnt);
            Stmt *s = newstmt(S_PRINT); s->args = args; s->argn = cnt;
            if (g_cur.type == T_GT) { advance(); s->redir_mode = 1; s->redir_target = parse_concat(); }
            else if (g_cur.type == T_APPEND) { advance(); s->redir_mode = 2; s->redir_target = parse_concat(); }
            else if (g_cur.type == T_PIPE) { advance(); s->redir_mode = 3; s->redir_target = parse_concat(); }
            return s;
        }
        case T_PRINTF: {
            advance();
            int cnt; Node **args = parse_print_args(&cnt);
            Stmt *s = newstmt(S_PRINTF); s->args = args; s->argn = cnt;
            if (g_cur.type == T_GT) { advance(); s->redir_mode = 1; s->redir_target = parse_concat(); }
            else if (g_cur.type == T_APPEND) { advance(); s->redir_mode = 2; s->redir_target = parse_concat(); }
            else if (g_cur.type == T_PIPE) { advance(); s->redir_mode = 3; s->redir_target = parse_concat(); }
            return s;
        }
        case T_NEXT: advance(); return newstmt(S_NEXT);
        case T_NEXTFILE: advance(); return newstmt(S_NEXTFILE);
        case T_BREAK: advance(); return newstmt(S_BREAK);
        case T_CONTINUE: advance(); return newstmt(S_CONTINUE);
        case T_RETURN: {
            advance();
            Stmt *s = newstmt(S_RETURN);
            if (g_cur.type != T_SEMI && g_cur.type != T_NEWLINE && g_cur.type != T_RBRACE && g_cur.type != T_EOF)
                s->e1 = parse_expr();
            return s;
        }
        case T_EXIT: {
            advance();
            Stmt *s = newstmt(S_EXIT);
            if (g_cur.type != T_SEMI && g_cur.type != T_NEWLINE && g_cur.type != T_RBRACE && g_cur.type != T_EOF)
                s->e1 = parse_expr();
            return s;
        }
        case T_DELETE: {
            advance();
            if (g_cur.type != T_IDENT) { fprintf(stderr, "mawk: expected array name after delete\n"); exit(2); }
            char *name = xstrdup(g_cur.text);
            advance();
            Stmt *s = newstmt(S_DELETE);
            s->name = name;
            if (g_cur.type == T_LBRACKET) {
                advance();
                int cnt; Node **idxs = parse_expr_list(&cnt, T_RBRACKET);
                expect(T_RBRACKET, "]");
                s->args = idxs; s->argn = cnt;
            }
            return s;
        }
        case T_SEMI: advance(); return NULL;
        default: {
            Node *e = parse_expr();
            Stmt *s = newstmt(S_EXPR); s->e1 = e;
            return s;
        }
    }
}

static void parse_function_def(void) {
    advance();
    char *fname;
    if (g_cur.type == T_FUNCNAME || g_cur.type == T_IDENT) { fname = xstrdup(g_cur.text); advance(); }
    else { fprintf(stderr, "mawk: expected function name\n"); exit(2); }
    expect(T_LPAREN, "(");
    char **params = NULL; int n = 0, cap = 0;
    while (g_cur.type == T_IDENT) {
        char *p = xstrdup(g_cur.text); advance();
        if (n == cap) { cap = cap ? cap * 2 : 4; params = (char **)xrealloc(params, cap * sizeof(char *)); }
        params[n++] = p;
        if (g_cur.type == T_COMMA) { advance(); skip_newlines(); continue; }
        break;
    }
    expect(T_RPAREN, ")");
    skip_newlines();
    Stmt *body = parse_block();
    FuncDef *fd = (FuncDef *)xmalloc(sizeof(FuncDef));
    fd->name = fname; fd->params = params; fd->nparams = n; fd->body = body; fd->next = g_funcs;
    g_funcs = fd;
}

static void parse_program(void) {
    skip_terms();
    while (g_cur.type != T_EOF) {
        if (g_cur.type == T_FUNCTION) { parse_function_def(); skip_terms(); continue; }
        Rule *r = (Rule *)xmalloc(sizeof(Rule));
        memset(r, 0, sizeof(*r));
        if (g_cur.type == T_BEGIN) { advance(); r->ptype = P_BEGIN; skip_newlines(); r->action = parse_block(); }
        else if (g_cur.type == T_END) { advance(); r->ptype = P_END; skip_newlines(); r->action = parse_block(); }
        else if (g_cur.type == T_LBRACE) { r->ptype = P_ALWAYS; r->action = parse_block(); }
        else if (g_cur.type == T_ERE) {
            char *pere1 = xstrdup(g_cur.text); advance();
            if (g_cur.type == T_COMMA) {
                advance(); skip_newlines();
                r->ptype = P_RANGE;
                Node *ere1 = newnode(N_ERE); ere1->str = pere1;
                r->pexpr = ere1;
                if (g_cur.type == T_ERE) { Node *ere2 = newnode(N_ERE); ere2->str = xstrdup(g_cur.text); advance(); r->pexpr2 = ere2; }
                else r->pexpr2 = parse_expr();
            } else {
                r->ptype = P_ERE;
                r->pere = pere1;
            }
            skip_newlines();
            r->action = (g_cur.type == T_LBRACE) ? parse_block() : NULL;
        } else {
            Node *e = parse_expr();
            skip_newlines();
            if (g_cur.type == T_COMMA) {
                advance(); skip_newlines();
                Node *e2 = parse_expr();
                r->ptype = P_RANGE; r->pexpr = e; r->pexpr2 = e2;
            } else {
                r->ptype = P_EXPR; r->pexpr = e;
            }
            skip_newlines();
            r->action = (g_cur.type == T_LBRACE) ? parse_block() : NULL;
        }
        add_rule(r);
        skip_terms();
    }
}

/* ---------------------------------------------------------------------- */
/* Evaluator                                                               */
/* ---------------------------------------------------------------------- */

typedef enum { FLOW_NONE, FLOW_BREAK, FLOW_CONTINUE, FLOW_NEXT, FLOW_NEXTFILE, FLOW_EXIT, FLOW_RETURN } Flow;
static Flow g_flow = FLOW_NONE;
static int g_exit_code = 0;
static Value g_retval;

static void exec_stmt(Stmt *s);
static void exec_block(Stmt *blk);
static char *do_sprintf(const char *fmt, Node **args, int argn, int startidx);
static FILE *resolve_output(Stmt *s);
static char *getline_read_main(void);

static void assign_to(Node *target, Value v) {
    if (target->type == N_VAR) {
        if (strcmp(target->str, "NF") == 0) {
            int newnf = (int)to_num(&v);
            if (newnf < 0) newnf = 0;
            if (newnf < g_nf) g_nf = newnf;
            else if (newnf > g_nf) { for (int k = g_nf + 1; k <= newnf; k++) if (!g_fields[k]) g_fields[k] = xstrdup(""); g_nf = newnf; }
            setvar_num("NF", g_nf);
            rebuild_record();
            free_val(&v);
            return;
        }
        Var *var = var_get(target->str);
        free_val(&var->val);
        var->val = v;
        return;
    }
    if (target->type == N_FIELD) {
        Value iv = eval(target->a);
        int idx = (int)to_num(&iv);
        char buf[128];
        set_field(idx, to_str_buf(&v, buf, sizeof(buf)));
        free_val(&iv); free_val(&v);
        return;
    }
    if (target->type == N_ARRREF) {
        Var *var = var_get(target->str);
        char key[1024];
        build_key(target->list, target->listn, key, sizeof(key));
        ArrEntry *e = arr_get(var, key);
        free_val(&e->val);
        e->val = v;
        return;
    }
    if (target->type == N_GROUP) { assign_to(target->a, v); return; }
    die("invalid assignment target");
}

static Value eval_lvalue_current(Node *target) {
    if (target->type == N_GROUP) return eval_lvalue_current(target->a);
    return eval(target);
}

static Value call_user_func(FuncDef *fd, Node **args, int argn) {
    Var **pvars = (Var **)xmalloc(sizeof(Var *) * (fd->nparams ? fd->nparams : 1));
    int  *alias = (int *)xmalloc(sizeof(int) * (fd->nparams ? fd->nparams : 1));
    for (int i = 0; i < fd->nparams; i++) {
        if (i < argn) {
            if (args[i]->type == N_VAR) {
                Var *src = var_get(args[i]->str);
                pvars[i] = src; alias[i] = 1;
            } else {
                Value v = eval(args[i]);
                Var *nv = new_var(fd->params[i]);
                nv->val = v;
                pvars[i] = nv; alias[i] = 0;
            }
        } else {
            pvars[i] = new_var(fd->params[i]);
            alias[i] = 0;
        }
    }
    Frame2 *fr = (Frame2 *)xmalloc(sizeof(Frame2));
    fr->names = fd->params; fr->vars = pvars; fr->is_alias = alias; fr->n = fd->nparams;
    fr->prev = g_curframe;
    g_curframe = fr;

    g_retval = mknum(0);
    exec_stmt(fd->body);
    Value ret = g_retval;
    if (g_flow == FLOW_RETURN) g_flow = FLOW_NONE;

    g_curframe = fr->prev;
    for (int i = 0; i < fd->nparams; i++) {
        if (!alias[i]) {
            Var *v = pvars[i];
            free_val(&v->val); arr_clear(v); free(v->name); free(v);
        }
    }
    free(pvars); free(alias); free(fr);
    return ret;
}

static char *do_getline_from(FILE *fp, IOEnt *ioent) { return read_record_generic(fp, ioent); }

static Value eval_getline(Node *n) {
    if (n->gmode == 0) {
        char *rec = getline_read_main();
        if (!rec) return mknum(0);
        setvar_num("NR", getvar_num("NR") + 1);
        setvar_num("FNR", getvar_num("FNR") + 1);
        if (n->a) assign_to(n->a, mkstrnum(rec));
        else set_record(rec);
        free(rec);
        return mknum(1);
    }
    if (n->gmode == 1) {
        Value fv = eval(n->b);
        char fb[512]; const char *fname = to_str_buf(&fv, fb, sizeof(fb));
        IOEnt *e = io_open_input(fname, 0);
        free_val(&fv);
        if (!e) return mknum(-1);
        char *rec = do_getline_from(e->fp, e);
        if (!rec) return mknum(0);
        if (n->a) assign_to(n->a, mkstrnum(rec));
        else set_record(rec);
        free(rec);
        return mknum(1);
    }
    Value cv = eval(n->b);
    char cb[1024]; const char *cmd = to_str_buf(&cv, cb, sizeof(cb));
    IOEnt *e = io_open_input(cmd, 1);
    free_val(&cv);
    if (!e) return mknum(-1);
    char *rec = do_getline_from(e->fp, e);
    if (!rec) return mknum(0);
    setvar_num("NR", getvar_num("NR") + 1);
    if (n->a) assign_to(n->a, mkstrnum(rec));
    else set_record(rec);
    free(rec);
    return mknum(1);
}

static Value call_builtin(const char *name, Node **args, int argn) {
    if (strcmp(name, "length") == 0) {
        if (argn == 0) return mknum((double)strlen(g_record ? g_record : ""));
        if (args[0]->type == N_VAR) {
            Var *v = var_find(args[0]->str);
            if (v && v->is_arr) { int cnt = 0; for (ArrEntry *e = v->arr; e; e = e->next) cnt++; return mknum(cnt); }
        }
        Value a = eval(args[0]);
        char buf[256];
        double r = (double)strlen(to_str_buf(&a, buf, sizeof(buf)));
        free_val(&a);
        return mknum(r);
    }
    if (strcmp(name, "substr") == 0) {
        Value s = eval(args[0]);
        char sb[1024]; const char *str = to_str_buf(&s, sb, sizeof(sb));
        int len = (int)strlen(str);
        int m;
        { Value mv = argn > 1 ? eval(args[1]) : mknum(1); m = (int)to_num(&mv); free_val(&mv); }
        int n;
        if (argn > 2) { Value nv = eval(args[2]); n = (int)to_num(&nv); free_val(&nv); }
        else n = len - m + 1 + 1000000;
        if (m < 1) { n += (m - 1); m = 1; }
        if (m > len) { free_val(&s); return mkstr(""); }
        if (n < 0) n = 0;
        if (m - 1 + n > len) n = len - (m - 1);
        char *out = (char *)xmalloc(n + 1);
        memcpy(out, str + (m - 1), n); out[n] = '\0';
        Value r = mkstr(out);
        free(out); free_val(&s);
        return r;
    }
    if (strcmp(name, "index") == 0) {
        Value s = eval(args[0]); Value t = eval(args[1]);
        char sb[512], tb[512];
        const char *ss = to_str_buf(&s, sb, sizeof(sb));
        const char *tt = to_str_buf(&t, tb, sizeof(tb));
        char *p = strstr(ss, tt);
        double r = p ? (double)(p - ss + 1) : 0;
        free_val(&s); free_val(&t);
        return mknum(r);
    }
    if (strcmp(name, "match") == 0) {
        Value s = eval(args[0]);
        char sb[2048]; const char *str = to_str_buf(&s, sb, sizeof(sb));
        const char *pat = (args[1]->type == N_ERE) ? args[1]->str : NULL;
        char patbuf[512];
        Value pv = mknum(0); int have_pv = 0;
        if (!pat) { pv = eval(args[1]); have_pv = 1; pat = to_str_buf(&pv, patbuf, sizeof(patbuf)); }
        int ms, ml;
        double r;
        if (regex_search(pat, str, &ms, &ml)) { r = ms + 1; setvar_num("RSTART", ms + 1); setvar_num("RLENGTH", ml); }
        else { r = 0; setvar_num("RSTART", 0); setvar_num("RLENGTH", -1); }
        free_val(&s); if (have_pv) free_val(&pv);
        return mknum(r);
    }
    if (strcmp(name, "split") == 0) {
        Value s = eval(args[0]);
        char sb[4096]; const char *str = to_str_buf(&s, sb, sizeof(sb));
        if (args[1]->type != N_VAR) die("split: second argument must be an array name");
        Var *arrv = var_get(args[1]->str);
        arr_clear(arrv); arrv->is_arr = 1;
        char fsbuf[64]; const char *fs;
        if (argn > 2) {
            if (args[2]->type == N_ERE) fs = xstrdup(args[2]->str);
            else { Value f = eval(args[2]); fs = xstrdup(to_str_buf(&f, fsbuf, sizeof(fsbuf))); free_val(&f); }
        } else fs = xstrdup(getvar_str("FS", fsbuf, sizeof(fsbuf)));
        int count = 0;
        char *tmp = xstrdup(str);
        if (!*fs) fs = " ";
        if (strcmp(fs, " ") == 0) {
            char *p = tmp;
            while (*p) {
                while (*p && isspace((unsigned char)*p)) p++;
                if (!*p) break;
                char *start = p;
                while (*p && !isspace((unsigned char)*p)) p++;
                int len = (int)(p - start);
                char *piece = (char *)xmalloc(len + 1); memcpy(piece, start, len); piece[len] = '\0';
                char idxs[32]; snprintf(idxs, sizeof(idxs), "%d", ++count);
                ArrEntry *e = arr_get(arrv, idxs); free_val(&e->val); e->val = mkstrnum(piece);
                free(piece);
            }
        } else if (strlen(fs) == 1) {
            char sep = fs[0]; char *p = tmp;
            for (;;) {
                char *start = p;
                while (*p && *p != sep) p++;
                int len = (int)(p - start);
                char *piece = (char *)xmalloc(len + 1); memcpy(piece, start, len); piece[len] = '\0';
                char idxs[32]; snprintf(idxs, sizeof(idxs), "%d", ++count);
                ArrEntry *e = arr_get(arrv, idxs); free_val(&e->val); e->val = mkstrnum(piece);
                free(piece);
                if (!*p) break;
                p++;
            }
        } else {
            char *p = tmp;
            while (1) {
                int ms, ml;
                if (regex_search(fs, p, &ms, &ml) && ml > 0) {
                    int len = ms;
                    char *piece = (char *)xmalloc(len + 1); memcpy(piece, p, len); piece[len] = '\0';
                    char idxs[32]; snprintf(idxs, sizeof(idxs), "%d", ++count);
                    ArrEntry *e = arr_get(arrv, idxs); free_val(&e->val); e->val = mkstrnum(piece);
                    free(piece);
                    p += ms + ml;
                } else {
                    char idxs[32]; snprintf(idxs, sizeof(idxs), "%d", ++count);
                    ArrEntry *e = arr_get(arrv, idxs); free_val(&e->val); e->val = mkstrnum(p);
                    break;
                }
            }
        }
        free(tmp);
        free_val(&s);
        return mknum(count);
    }
    if (strcmp(name, "sub") == 0 || strcmp(name, "gsub") == 0) {
        int global = (name[0] == 'g');
        const char *pat; char reb[512]; Value re; int have_re = 0;
        if (args[0]->type == N_ERE) pat = args[0]->str;
        else { re = eval(args[0]); have_re = 1; pat = to_str_buf(&re, reb, sizeof(reb)); }
        Value rep = eval(args[1]);
        char repb[512]; const char *repl = to_str_buf(&rep, repb, sizeof(repb));
        Node *target = argn > 2 ? args[2] : NULL;
        char curbuf[4096];
        const char *cur;
        Value tv; int have_tv = 0;
        if (target) { tv = eval_lvalue_current(target); have_tv = 1; cur = to_str_buf(&tv, curbuf, sizeof(curbuf)); }
        else cur = g_record ? g_record : "";
        SBuf out; sb_init(&out);
        const char *p = cur;
        int nsubs = 0;
        while (*p) {
            int ms, ml;
            if (regex_search(pat, p, &ms, &ml)) {
                sb_addn(&out, p, ms);
                size_t rl = strlen(repl);
                for (size_t i = 0; i < rl; i++) {
                    if (repl[i] == '&') sb_addn(&out, p + ms, ml);
                    else if (repl[i] == '\\' && i + 1 < rl && repl[i + 1] == '&') { sb_addc(&out, '&'); i++; }
                    else sb_addc(&out, repl[i]);
                }
                nsubs++;
                if (ml == 0) { if (p[ms]) sb_addc(&out, p[ms]); p += ms + 1; }
                else p += ms + ml;
                if (!global) { sb_adds(&out, p); p += strlen(p); break; }
            } else { sb_adds(&out, p); break; }
        }
        if (nsubs > 0) {
            if (target) assign_to(target, mkstr(out.data));
            else set_record(out.data);
        }
        sb_free(&out);
        if (have_re) free_val(&re);
        free_val(&rep);
        if (have_tv) free_val(&tv);
        return mknum(nsubs);
    }
    if (strcmp(name, "toupper") == 0 || strcmp(name, "tolower") == 0) {
        Value s = eval(args[0]);
        char sb[2048]; const char *str = to_str_buf(&s, sb, sizeof(sb));
        char *out = xstrdup(str);
        for (char *p = out; *p; p++) *p = (name[2] == 'u') ? (char)toupper((unsigned char)*p) : (char)tolower((unsigned char)*p);
        Value r = mkstr(out);
        free(out); free_val(&s);
        return r;
    }
    if (strcmp(name, "sprintf") == 0) {
        Value f = eval(args[0]);
        char fb[1024]; const char *fmt = to_str_buf(&f, fb, sizeof(fb));
        char *out = do_sprintf(fmt, args, argn, 1);
        Value r = mkstr(out);
        free(out); free_val(&f);
        return r;
    }
    if (strcmp(name, "int") == 0) { Value a = eval(args[0]); double d = to_num(&a); free_val(&a); return mknum((double)(long long)d); }
    if (strcmp(name, "sin") == 0) { Value a = eval(args[0]); double d = sin(to_num(&a)); free_val(&a); return mknum(d); }
    if (strcmp(name, "cos") == 0) { Value a = eval(args[0]); double d = cos(to_num(&a)); free_val(&a); return mknum(d); }
    if (strcmp(name, "atan2") == 0) { Value a = eval(args[0]); Value b = eval(args[1]); double d = atan2(to_num(&a), to_num(&b)); free_val(&a); free_val(&b); return mknum(d); }
    if (strcmp(name, "sqrt") == 0) { Value a = eval(args[0]); double d = sqrt(to_num(&a)); free_val(&a); return mknum(d); }
    if (strcmp(name, "exp") == 0) { Value a = eval(args[0]); double d = exp(to_num(&a)); free_val(&a); return mknum(d); }
    if (strcmp(name, "log") == 0) { Value a = eval(args[0]); double d = log(to_num(&a)); free_val(&a); return mknum(d); }
    if (strcmp(name, "rand") == 0) { return mknum((double)rand() / ((double)RAND_MAX + 1)); }
    if (strcmp(name, "srand") == 0) {
        unsigned sd;
        if (argn > 0) { Value sv = eval(args[0]); sd = (unsigned)to_num(&sv); free_val(&sv); }
        else sd = (unsigned)time(NULL);
        srand(sd);
        return mknum(0);
    }
    if (strcmp(name, "system") == 0) {
        Value a = eval(args[0]);
        char ab[2048]; const char *cmd = to_str_buf(&a, ab, sizeof(ab));
        fflush(stdout);
        int rc = system(cmd);
        free_val(&a);
        return mknum(rc);
    }
    if (strcmp(name, "close") == 0) {
        Value a = eval(args[0]);
        char ab[512]; const char *key = to_str_buf(&a, ab, sizeof(ab));
        int rc = io_close(key);
        free_val(&a);
        return mknum(rc);
    }
    if (strcmp(name, "fflush") == 0) {
        if (argn == 0) { fflush(NULL); return mknum(0); }
        Value a = eval(args[0]);
        char ab[512]; const char *key = to_str_buf(&a, ab, sizeof(ab));
        IOEnt *e = io_find(key, 1);
        int rc = e ? fflush(e->fp) : -1;
        free_val(&a);
        return mknum(rc);
    }
    fprintf(stderr, "mawk: unknown function '%s'\n", name);
    exit(2);
}

static int in_test(Node *n) {
    Var *v = var_find(n->str);
    if (!v) return 0;
    char key[1024];
    build_key(n->list, n->listn, key, sizeof(key));
    return arr_find(v, key) != NULL;
}

static Value eval(Node *n) {
    switch (n->type) {
        case N_NUM: return mknum(n->num);
        case N_STR: return mkstr(n->str);
        case N_ERE: return mknum(regex_match_bool(n->str, g_record ? g_record : ""));
        case N_GROUP: return eval(n->a);
        case N_GETLINE: return eval_getline(n);
        case N_IN: return mknum(in_test(n));
        case N_VAR: {
            if (strcmp(n->str, "NF") == 0) return mknum(g_nf);
            Var *v = var_find(n->str);
            if (!v) return mkstr("");
            Value copy = v->val;
            if (copy.str) return copy.is_strnum ? mkstrnum(copy.str) : mkstr(copy.str);
            return mknum(copy.num);
        }
        case N_ARRREF: {
            Var *v = var_get(n->str);
            char key[1024];
            build_key(n->list, n->listn, key, sizeof(key));
            ArrEntry *e = arr_get(v, key);
            if (e->val.str) return mkstrnum(e->val.str);
            return mknum(e->val.num);
        }
        case N_FIELD: {
            Value iv = eval(n->a);
            int idx = (int)to_num(&iv);
            free_val(&iv);
            return mkstrnum(get_field(idx));
        }
        case N_TERNARY: {
            Value c = eval(n->a);
            int t = truthy(&c); free_val(&c);
            return t ? eval(n->b) : eval(n->c);
        }
        case N_ASSIGN: {
            if (n->op == T_ASSIGN) {
                Value v = eval(n->b);
                Value stored = copy_val(&v);
                assign_to(n->a, v);
                return stored;
            } else {
                Value cur = eval_lvalue_current(n->a);
                double c = to_num(&cur);
                Value rv = eval(n->b);
                double r = to_num(&rv);
                double res;
                switch (n->op) {
                    case T_ADDASSIGN: res = c + r; break;
                    case T_SUBASSIGN: res = c - r; break;
                    case T_MULASSIGN: res = c * r; break;
                    case T_DIVASSIGN: if (r == 0) die("division by zero"); res = c / r; break;
                    case T_MODASSIGN: if (r == 0) die("division by zero"); res = fmod(c, r); break;
                    case T_POWASSIGN: res = pow(c, r); break;
                    default: res = 0;
                }
                free_val(&cur); free_val(&rv);
                assign_to(n->a, mknum(res));
                return mknum(res);
            }
        }
        case N_POW: {
            Value a = eval(n->a), b = eval(n->b);
            double r = pow(to_num(&a), to_num(&b));
            free_val(&a); free_val(&b);
            return mknum(r);
        }
        case N_BINOP: {
            Value a = eval(n->a), b = eval(n->b);
            Value result;
            if (n->op == T_PLUS || n->op == T_MINUS || n->op == T_STAR || n->op == T_SLASH || n->op == T_PERCENT) {
                double x = to_num(&a), y = to_num(&b), r;
                switch (n->op) {
                    case T_PLUS: r = x + y; break;
                    case T_MINUS: r = x - y; break;
                    case T_STAR: r = x * y; break;
                    case T_SLASH: if (y == 0) die("division by zero"); r = x / y; break;
                    default: if (y == 0) die("division by zero"); r = fmod(x, y); break;
                }
                result = mknum(r);
            } else {
                int numeric = is_numericish(&a) && is_numericish(&b);
                int cmp;
                if (numeric) { double x = to_num(&a), y = to_num(&b); cmp = (x < y) ? -1 : (x > y) ? 1 : 0; }
                else { char ba[512], bb[512]; cmp = strcmp(to_str_buf(&a, ba, sizeof(ba)), to_str_buf(&b, bb, sizeof(bb))); }
                int r;
                switch (n->op) {
                    case T_LT: r = cmp < 0; break;
                    case T_LE: r = cmp <= 0; break;
                    case T_GT: r = cmp > 0; break;
                    case T_GE: r = cmp >= 0; break;
                    case T_EQ: r = cmp == 0; break;
                    case T_NE: r = cmp != 0; break;
                    default: r = 0;
                }
                result = mknum(r);
            }
            free_val(&a); free_val(&b);
            return result;
        }
        case N_MATCH: {
            Value a = eval(n->a);
            char ab[2048]; const char *s = to_str_buf(&a, ab, sizeof(ab));
            const char *pat = (n->b->type == N_ERE) ? n->b->str : NULL;
            char patbuf[1024]; Value bv = mknum(0); int have_bv = 0;
            if (!pat) { bv = eval(n->b); have_bv = 1; pat = to_str_buf(&bv, patbuf, sizeof(patbuf)); }
            int m = regex_match_bool(pat, s);
            if (n->op == T_NOMATCH) m = !m;
            free_val(&a); if (have_bv) free_val(&bv);
            return mknum(m);
        }
        case N_CONCAT: {
            Value a = eval(n->a), b = eval(n->b);
            char ba[1024], bb[1024];
            const char *sa = to_str_buf(&a, ba, sizeof(ba));
            const char *sb = to_str_buf(&b, bb, sizeof(bb));
            SBuf outb; sb_init(&outb); sb_adds(&outb, sa); sb_adds(&outb, sb);
            Value r; r.str = outb.data; r.num = 0; r.is_strnum = 0;
            free_val(&a); free_val(&b);
            return r;
        }
        case N_AND: {
            Value a = eval(n->a); int av = truthy(&a); free_val(&a);
            if (!av) return mknum(0);
            Value b = eval(n->b); int bv = truthy(&b); free_val(&b);
            return mknum(av && bv);
        }
        case N_OR: {
            Value a = eval(n->a); int av = truthy(&a); free_val(&a);
            if (av) return mknum(1);
            Value b = eval(n->b); int bv = truthy(&b); free_val(&b);
            return mknum(av || bv);
        }
        case N_NOT: { Value a = eval(n->a); int v = !truthy(&a); free_val(&a); return mknum(v); }
        case N_UNARY: { Value a = eval(n->a); double d = to_num(&a); free_val(&a); return mknum(n->op == T_MINUS ? -d : d); }
        case N_PREINCDEC: {
            Value cur = eval_lvalue_current(n->a);
            double d = to_num(&cur); free_val(&cur);
            d += (n->op == T_INCR) ? 1 : -1;
            assign_to(n->a, mknum(d));
            return mknum(d);
        }
        case N_POSTINCDEC: {
            Value cur = eval_lvalue_current(n->a);
            double d = to_num(&cur); free_val(&cur);
            double nd = d + ((n->op == T_INCR) ? 1 : -1);
            assign_to(n->a, mknum(nd));
            return mknum(d);
        }
        case N_CALL: {
            FuncDef *fd = find_func(n->str);
            if (fd) return call_user_func(fd, n->list, n->listn);
            return call_builtin(n->str, n->list, n->listn);
        }
        default: die("internal error: bad node in eval");
    }
    return mknum(0);
}

static char *do_sprintf(const char *fmt, Node **args, int argn, int startidx) {
    SBuf out; sb_init(&out);
    int ai = startidx;
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { sb_addc(&out, *p); continue; }
        const char *start = p;
        p++;
        if (*p == '%') { sb_addc(&out, '%'); continue; }
        char spec[64]; int si = 0;
        spec[si++] = '%';
        while (*p && strchr("-+ 0#", *p)) spec[si++] = *p++;
        while (*p && isdigit((unsigned char)*p)) spec[si++] = *p++;
        if (*p == '.') { spec[si++] = *p++; while (*p && isdigit((unsigned char)*p)) spec[si++] = *p++; }
        char conv = *p;
        spec[si] = '\0';
        char piece[1024];
        Value v; int has_v = 0;
        if (conv == 'd' || conv == 'i' || conv == 'o' || conv == 'x' || conv == 'X' || conv == 'u') {
            v = (ai < argn) ? eval(args[ai++]) : mknum(0); has_v = 1;
            char cvt = (conv == 'u') ? 'u' : (conv == 'o') ? 'o' : (conv == 'x') ? 'x' : (conv == 'X') ? 'X' : 'd';
            char spec2[70]; snprintf(spec2, sizeof(spec2), "%s%s%c", spec, "ll", cvt);
            snprintf(piece, sizeof(piece), spec2, (long long)to_num(&v));
        } else if (conv == 'f' || conv == 'F' || conv == 'e' || conv == 'E' || conv == 'g' || conv == 'G') {
            v = (ai < argn) ? eval(args[ai++]) : mknum(0); has_v = 1;
            char spec2[70]; snprintf(spec2, sizeof(spec2), "%s%c", spec, conv);
            snprintf(piece, sizeof(piece), spec2, to_num(&v));
        } else if (conv == 's') {
            v = (ai < argn) ? eval(args[ai++]) : mkstr(""); has_v = 1;
            char vb[900]; char spec2[70]; snprintf(spec2, sizeof(spec2), "%s%c", spec, 's');
            snprintf(piece, sizeof(piece), spec2, to_str_buf(&v, vb, sizeof(vb)));
        } else if (conv == 'c') {
            v = (ai < argn) ? eval(args[ai++]) : mkstr(""); has_v = 1;
            char ch;
            if (v.str) ch = v.str[0]; else ch = (char)(int)v.num;
            char spec2[70]; snprintf(spec2, sizeof(spec2), "%s%c", spec, 'c');
            snprintf(piece, sizeof(piece), spec2, ch);
        } else {
            size_t rawlen = (size_t)(p - start) + 1;
            if (rawlen >= sizeof(piece)) rawlen = sizeof(piece) - 1;
            memcpy(piece, start, rawlen); piece[rawlen] = '\0';
        }
        sb_adds(&out, piece);
        if (has_v) free_val(&v);
    }
    return out.data;
}

static FILE *resolve_output(Stmt *s) {
    if (s->redir_mode == 0) return stdout;
    Value tv = eval(s->redir_target);
    char tb[1024]; const char *target = to_str_buf(&tv, tb, sizeof(tb));
    IOEnt *e = io_open_output(target, s->redir_mode == 1 ? 0 : s->redir_mode == 2 ? 1 : 2);
    free_val(&tv);
    if (!e) { fprintf(stderr, "mawk: cannot open '%s' for output\n", target); exit(2); }
    return e->fp;
}

static void do_print(Stmt *s) {
    FILE *out = resolve_output(s);
    char ofsbuf[64], orsbuf[64];
    const char *ofs = getvar_str("OFS", ofsbuf, sizeof(ofsbuf));
    const char *ors = getvar_str("ORS", orsbuf, sizeof(orsbuf));
    if (s->argn == 0) fputs(g_record ? g_record : "", out);
    else {
        for (int i = 0; i < s->argn; i++) {
            if (i > 0) fputs(ofs, out);
            Value v = eval(s->args[i]);
            char b[2048];
            fputs(to_str_buf_fmt(&v, b, sizeof(b), 1), out);
            free_val(&v);
        }
    }
    fputs(ors, out);
}
static void do_printf(Stmt *s) {
    if (s->argn == 0) return;
    FILE *out = resolve_output(s);
    Value f = eval(s->args[0]);
    char fb[1024]; const char *fmt = to_str_buf(&f, fb, sizeof(fb));
    char *o = do_sprintf(fmt, s->args, s->argn, 1);
    fputs(o, out);
    free(o); free_val(&f);
}

static void exec_block(Stmt *blk) { for (int i = 0; i < blk->listn && g_flow == FLOW_NONE; i++) exec_stmt(blk->list[i]); }

static void exec_stmt(Stmt *s) {
    if (!s || g_flow != FLOW_NONE) return;
    switch (s->type) {
        case S_BLOCK: exec_block(s); return;
        case S_EXPR: { Value v = eval(s->e1); free_val(&v); return; }
        case S_PRINT: do_print(s); return;
        case S_PRINTF: do_printf(s); return;
        case S_IF: {
            Value c = eval(s->e1); int t = truthy(&c); free_val(&c);
            if (t) exec_stmt(s->s1); else if (s->s2) exec_stmt(s->s2);
            return;
        }
        case S_WHILE: {
            for (;;) {
                Value c = eval(s->e1); int t = truthy(&c); free_val(&c);
                if (!t) break;
                exec_stmt(s->s1);
                if (g_flow == FLOW_BREAK) { g_flow = FLOW_NONE; break; }
                if (g_flow == FLOW_CONTINUE) g_flow = FLOW_NONE;
                if (g_flow != FLOW_NONE) return;
            }
            return;
        }
        case S_DOWHILE: {
            for (;;) {
                exec_stmt(s->s1);
                if (g_flow == FLOW_BREAK) { g_flow = FLOW_NONE; break; }
                if (g_flow == FLOW_CONTINUE) g_flow = FLOW_NONE;
                if (g_flow != FLOW_NONE) return;
                Value c = eval(s->e1); int t = truthy(&c); free_val(&c);
                if (!t) break;
            }
            return;
        }
        case S_FOR: {
            exec_stmt(s->s1);
            for (;;) {
                if (s->e1) { Value c = eval(s->e1); int t = truthy(&c); free_val(&c); if (!t) break; }
                exec_stmt(s->list[0]);
                if (g_flow == FLOW_BREAK) { g_flow = FLOW_NONE; break; }
                if (g_flow == FLOW_CONTINUE) g_flow = FLOW_NONE;
                if (g_flow != FLOW_NONE) return;
                exec_stmt(s->s2);
            }
            return;
        }
        case S_FORIN: {
            Var *arrv = var_get(s->args[0]->str);
            int cnt = 0; for (ArrEntry *e = arrv->arr; e; e = e->next) cnt++;
            char **keys = (char **)xmalloc(sizeof(char *) * (cnt ? cnt : 1));
            int i = 0; for (ArrEntry *e = arrv->arr; e; e = e->next) keys[i++] = xstrdup(e->key);
            for (i = 0; i < cnt; i++) {
                setvar_str(s->name, keys[i]);
                exec_stmt(s->s1);
                if (g_flow == FLOW_BREAK) { g_flow = FLOW_NONE; break; }
                if (g_flow == FLOW_CONTINUE) g_flow = FLOW_NONE;
                if (g_flow != FLOW_NONE) break;
            }
            for (int k = 0; k < cnt; k++) free(keys[k]);
            free(keys);
            return;
        }
        case S_NEXT: g_flow = FLOW_NEXT; return;
        case S_NEXTFILE: g_flow = FLOW_NEXTFILE; return;
        case S_BREAK: g_flow = FLOW_BREAK; return;
        case S_CONTINUE: g_flow = FLOW_CONTINUE; return;
        case S_RETURN:
            if (s->e1) g_retval = eval(s->e1); else g_retval = mknum(0);
            g_flow = FLOW_RETURN;
            return;
        case S_EXIT:
            if (s->e1) { Value v = eval(s->e1); g_exit_code = (int)to_num(&v); free_val(&v); }
            g_flow = FLOW_EXIT;
            return;
        case S_DELETE: {
            Var *v = var_get(s->name);
            if (s->argn == 0) arr_clear(v);
            else {
                char key[1024];
                build_key(s->args, s->argn, key, sizeof(key));
                ArrEntry **pp = &v->arr;
                while (*pp) {
                    if (strcmp((*pp)->key, key) == 0) { ArrEntry *dead = *pp; *pp = dead->next; free(dead->key); free_val(&dead->val); free(dead); break; }
                    pp = &(*pp)->next;
                }
            }
            return;
        }
    }
}

/* ---------------------------------------------------------------------- */
/* Driver                                                                  */
/* ---------------------------------------------------------------------- */

static int rule_matches(Rule *r) {
    if (r->ptype == P_ALWAYS) return 1;
    if (r->ptype == P_ERE) return regex_match_bool(r->pere, g_record ? g_record : "");
    if (r->ptype == P_EXPR) { Value v = eval(r->pexpr); int t = truthy(&v); free_val(&v); return t; }
    if (r->ptype == P_RANGE) {
        if (!r->range_active) {
            int t1;
            if (r->pexpr->type == N_ERE) t1 = regex_match_bool(r->pexpr->str, g_record ? g_record : "");
            else { Value v1 = eval(r->pexpr); t1 = truthy(&v1); free_val(&v1); }
            if (!t1) return 0;
            r->range_active = 1;
        }
        int t2;
        if (r->pexpr2->type == N_ERE) t2 = regex_match_bool(r->pexpr2->str, g_record ? g_record : "");
        else { Value v2 = eval(r->pexpr2); t2 = truthy(&v2); free_val(&v2); }
        if (t2) r->range_active = 0;
        return 1;
    }
    return 0;
}

static void run_rules_for_record(void) {
    for (Rule *r = g_rules; r && g_flow == FLOW_NONE; r = r->next) {
        if (r->ptype == P_BEGIN || r->ptype == P_END) continue;
        if (rule_matches(r)) {
            if (r->action) exec_stmt(r->action);
            else { Stmt tmp; memset(&tmp, 0, sizeof(tmp)); tmp.type = S_PRINT; do_print(&tmp); }
        }
        if (g_flow == FLOW_NEXT) { g_flow = FLOW_NONE; return; }
        if (g_flow == FLOW_NEXTFILE) return;
    }
}

static FILE *g_main_fp = NULL;
static int g_main_used_stdin_fallback = 0;
static int g_argv_pos = 1;
static int g_main_all_done = 0;
static int g_any_real_file_seen = 0;

static int open_next_main_file(void) {
    g_main_fp = NULL;
    Var *argv_v = var_get("ARGV");
    int argc = (int)getvar_num("ARGC");
    for (; g_argv_pos < argc; g_argv_pos++) {
        char idxbuf[32]; snprintf(idxbuf, sizeof(idxbuf), "%d", g_argv_pos);
        ArrEntry *e = arr_find(argv_v, idxbuf);
        const char *arg = e ? (e->val.str ? e->val.str : "") : "";
        if (!*arg) continue;
        char *eq = strchr(arg, '=');
        if (eq && eq != arg) {
            int ok = 1;
            for (const char *q = arg; q < eq; q++) if (!(isalnum((unsigned char)*q) || *q == '_')) { ok = 0; break; }
            if (ok && !isdigit((unsigned char)arg[0])) {
                char *nm = (char *)xmalloc(eq - arg + 1);
                memcpy(nm, arg, eq - arg); nm[eq - arg] = '\0';
                setvar_str(nm, eq + 1);
                free(nm);
                continue;
            }
        }
        FILE *fp = (strcmp(arg, "-") == 0) ? stdin : fopen(arg, "rb");
        if (!fp) { fprintf(stderr, "mawk: cannot open %s\n", arg); continue; }
        setvar_str("FILENAME", arg);
        setvar_num("FNR", 0);
        g_main_fp = fp;
        g_argv_pos++;
        return 1;
    }
    return 0;
}

static char *getline_read_main(void) {
    for (;;) {
        if (!g_main_fp) {
            if (g_main_all_done) return NULL;
            if (!open_next_main_file()) {
                if (!g_any_real_file_seen && !g_main_used_stdin_fallback) {
                    g_main_used_stdin_fallback = 1;
                    g_main_fp = stdin;
                    setvar_str("FILENAME", "");
                    setvar_num("FNR", 0);
                } else { g_main_all_done = 1; return NULL; }
            } else g_any_real_file_seen = 1;
        }
        char *rec = read_record_generic(g_main_fp, NULL);
        if (rec) return rec;
        if (g_main_fp != stdin) fclose(g_main_fp);
        g_main_fp = NULL;
        if (g_main_used_stdin_fallback) { g_main_all_done = 1; return NULL; }
    }
}

int main(int argc, char **argv) {
    setvar_str("FS", " ");
    setvar_str("OFS", " ");
    setvar_str("ORS", "\n");
    setvar_str("RS", "\n");
    setvar_str("SUBSEP", "\x1c");
    setvar_str("CONVFMT", "%.6g");
    setvar_str("OFMT", "%.6g");
    setvar_num("NR", 0);
    setvar_num("FNR", 0);
    setvar_num("RSTART", 0);
    setvar_num("RLENGTH", -1);
    setvar_str("FILENAME", "");
    srand(1);

    Var *environ_v = var_get("ENVIRON");
    for (char **e = ENVIRON_ARR; e && *e; e++) {
        char *eq = strchr(*e, '=');
        if (!eq) continue;
        char key[512]; size_t klen = (size_t)(eq - *e); if (klen >= sizeof(key)) klen = sizeof(key) - 1;
        memcpy(key, *e, klen); key[klen] = '\0';
        ArrEntry *ent = arr_get(environ_v, key);
        free_val(&ent->val); ent->val = mkstrnum(eq + 1);
    }

    SBuf progbuf; sb_init(&progbuf);
    int have_prog = 0;
    char **preassign_names = NULL, **preassign_vals = NULL; int npreassign = 0;
    const char *fsopt = NULL;
    int i = 1;
    for (; i < argc; i++) {
        if (strcmp(argv[i], "--") == 0) { i++; break; }
        if (strncmp(argv[i], "-f", 2) == 0 && strlen(argv[i]) > 0) {
            const char *fname;
            if (strlen(argv[i]) > 2) fname = argv[i] + 2;
            else if (i + 1 < argc) { i++; fname = argv[i]; }
            else break;
            FILE *pf = fopen(fname, "rb");
            if (!pf) die("cannot open program file");
            char chunk[4096]; size_t got;
            while ((got = fread(chunk, 1, sizeof(chunk), pf)) > 0) sb_addn(&progbuf, chunk, got);
            sb_addc(&progbuf, '\n');
            fclose(pf);
            have_prog = 1;
            continue;
        }
        if (strncmp(argv[i], "-v", 2) == 0 && strlen(argv[i]) > 0) {
            const char *assign;
            if (strlen(argv[i]) > 2) assign = argv[i] + 2;
            else if (i + 1 < argc) { i++; assign = argv[i]; }
            else break;
            const char *eq = strchr(assign, '=');
            if (eq) {
                preassign_names = (char **)xrealloc(preassign_names, sizeof(char *) * (npreassign + 1));
                preassign_vals  = (char **)xrealloc(preassign_vals,  sizeof(char *) * (npreassign + 1));
                char *nm = (char *)xmalloc(eq - assign + 1);
                memcpy(nm, assign, eq - assign); nm[eq - assign] = '\0';
                preassign_names[npreassign] = nm;
                preassign_vals[npreassign] = xstrdup(eq + 1);
                npreassign++;
            }
            continue;
        }
        if (strncmp(argv[i], "-F", 2) == 0 && strlen(argv[i]) > 0) {
            if (strlen(argv[i]) > 2) fsopt = argv[i] + 2;
            else if (i + 1 < argc) { i++; fsopt = argv[i]; }
            else break;
            continue;
        }
        if (!have_prog && argv[i][0] != '-') { sb_adds(&progbuf, argv[i]); have_prog = 1; continue; }
        break;
    }
    if (!have_prog) { fprintf(stderr, "usage: mawk [-F fs] [-v var=val] 'program' [file|var=val ...]\n       mawk [-F fs] [-v var=val] -f prog.awk [-f prog2.awk ...] [file|var=val ...]\n"); return 2; }
    if (fsopt) {
        SBuf fb; sb_init(&fb);
        for (const char *p = fsopt; *p; p++) {
            if (*p == '\\' && p[1]) { p++; switch (*p) { case 't': sb_addc(&fb, '\t'); break; case 'n': sb_addc(&fb, '\n'); break; default: sb_addc(&fb, *p); } }
            else sb_addc(&fb, *p);
        }
        setvar_str("FS", fb.data);
        sb_free(&fb);
    }
    for (int k = 0; k < npreassign; k++) { setvar_str(preassign_names[k], preassign_vals[k]); free(preassign_names[k]); free(preassign_vals[k]); }
    free(preassign_names); free(preassign_vals);

    g_src = progbuf.data; g_pos = 0; g_prev_significant = 0;
    advance();
    parse_program();

    Var *argv_v = var_get("ARGV");
    char idxbuf[32];
    snprintf(idxbuf, sizeof(idxbuf), "%d", 0);
    { ArrEntry *e = arr_get(argv_v, idxbuf); free_val(&e->val); e->val = mkstr("awk"); }
    int argc_count = 1;
    for (; i < argc; i++) {
        snprintf(idxbuf, sizeof(idxbuf), "%d", argc_count);
        ArrEntry *e = arr_get(argv_v, idxbuf);
        free_val(&e->val); e->val = mkstrnum(argv[i]);
        argc_count++;
    }
    setvar_num("ARGC", argc_count);

    for (Rule *r = g_rules; r; r = r->next) {
        if (r->ptype == P_BEGIN) { exec_stmt(r->action); if (g_flow == FLOW_EXIT) goto do_end; }
    }
    g_flow = FLOW_NONE;

    {
        int needs_input = 0;
        for (Rule *r = g_rules; r; r = r->next) if (r->ptype != P_BEGIN) { needs_input = 1; break; }
        if (needs_input) {
            for (;;) {
                char *rec = getline_read_main();
                if (!rec) break;
                setvar_num("NR", getvar_num("NR") + 1);
                setvar_num("FNR", getvar_num("FNR") + 1);
                set_record(rec);
                free(rec);
                run_rules_for_record();
                if (g_flow == FLOW_EXIT) break;
                if (g_flow == FLOW_NEXTFILE) {
                    g_flow = FLOW_NONE;
                    if (g_main_fp && g_main_fp != stdin) fclose(g_main_fp);
                    g_main_fp = NULL;
                    if (g_main_used_stdin_fallback) g_main_all_done = 1;
                }
            }
        }
    }
    g_flow = FLOW_NONE;

do_end:
    g_flow = FLOW_NONE;
    for (Rule *r = g_rules; r; r = r->next) {
        if (r->ptype == P_END) { exec_stmt(r->action); if (g_flow == FLOW_EXIT) break; }
    }

    fflush(stdout);
    io_close_all();
    return g_exit_code;
}
