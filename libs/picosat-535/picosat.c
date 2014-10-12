/****************************************************************************
Copyright (c) 2006-2007, Armin Biere, Johannes Kepler University.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to
deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
IN THE SOFTWARE.
****************************************************************************/

static const char * id = \
"$Id: picosat.c,v 1.535 2007-01-30 15:56:28 biere Exp $";

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <ctype.h>

#include "config.h"
#include "picosat.h"

#define MINRESTART 100		/* minimum restart limit */
#define MAXRESTART 1000000	/* maximum restart limit */

#define FRESTART 110		/* restart increase factor in percent */
#define FREDUCE 105		/* reduce increase factor in percent  */

#define MINSPREAD 100		/* not more than 1%=1/100 random dec. */
#define MAXSPREAD 10000		/* not less than .01%=1/10000 random dec. */

#define IFREDUCE 100000		/* initial forced reduce limit */

/* Hardcoded memory limit for reducing learned clauses.
 */
#define LBYTES  ((size_t)(1300 * (1 << 20)))	/* 1300 MB */

// #define USE_CLAUSE_STACK	/* use stack based occurrence lists */
#ifndef TRACE
#define NO_BINARY_CLAUSES	/* store binary clauses more compactly */
#endif

/* For debugging purposes you may want to define 'LOGGING', which actually
 * can be enforced by using the '--log' option for the configure script.
 */
#ifdef LOGGING
#define LOG(code) do { code; } while (0)
#else
#define LOG(code) do { } while (0)
#endif
#define NOLOG(code) do { } while (0)		/* log exception */
#define ONLYLOG(code) do { code; } while (0)	/* force logging */

#define FALSE ((Val)-1)
#define UNDEF ((Val)0)
#define TRUE ((Val)1)

#define NEWN(p,n) do { (p) = new (sizeof (*(p)) * (n)); } while (0)
#define CLRN(p,n) do { memset ((p), 0, sizeof (*(p)) * (n)); } while (0)
#define DELETEN(p,n) \
  do { delete (p, sizeof (*(p)) * (n)); (p) = 0; } while (0)

#define RESIZEN(p,old_num,new_num) \
  do { \
    size_t old_size = sizeof (*(p)) * (old_num); \
    size_t new_size = sizeof (*(p)) * (new_num); \
    (p) = resize ((p), old_size, new_size) ; \
  } while (0)

#define ENLARGE(start,head,end) \
  do { \
    unsigned old_num = (unsigned)((end) - (start)); \
    size_t new_num = old_num ? (2 * old_num) : 1; \
    unsigned count = (head) - (start); \
    assert ((start) <= (start)); \
    RESIZEN((start),old_num,new_num); \
    (head) = (start) + count; \
    (end) = (start) + new_num; \
  } while (0)

#define NOTLIT(l) (lits + (1 ^ ((l) - lits)))

#define LIT2IDX(l) ((unsigned)((l) - lits) / 2)
#define LIT2IMPLS(l) (impls + (unsigned)((l) - lits))
#define LIT2INT(l) (LIT2SGN(l) * LIT2IDX(l))
#define LIT2SGN(l) (((unsigned)((l) - lits) & 1) ? -1 : 1)
#define LIT2VAR(l) (vars + LIT2IDX(l))
#define LIT2WCHS(l) (wchs + (unsigned)((l) - lits))
#define LIT2JWH(l) (jwh + ((l) - lits))

#ifdef NO_BINARY_CLAUSES
typedef unsigned long Wrd;
#define ISLITREASON(cls) (1&(Wrd)cls)
#define LIT2REASON(lit) \
  (assert (lit->val==TRUE), ((Cls*)(1 + (2*(lit - lits)))))
#define REASON2LIT(cls) ((Lit*)(lits + (1^(Wrd)cls)/2))
#endif

#define ENDOFCLS(c) ((void*)((c)->lits + (c)->size))

#define CLS2TRD(c) (((Trd*)(c)) - 1)
#define CLS2IDX(c) ((((Trd*)(c)) - 1)->idx)

#define CLS2ACT(c) \
  ((Act*)((assert((c)->learned)),assert((c)->size>2),ENDOFCLS(c)))

#define VAR2LIT(v) (lits + 2 * ((v) - vars))
#define VAR2RNK(v) (rnks + ((v) - vars))

#define RNK2LIT(v) (lits + 2 * ((v) - rnks))

#define PTR2BLK(void_ptr) \
  ((void_ptr) ? (Blk*)(((char*)(void_ptr)) - sizeof(Blk)) : 0)

#define AVERAGE(a,b) ((b) ? (((double)a) / (double)(b)) : 0.0)
#define PERCENT(a,b) (100.0 * AVERAGE(a,b))

#define ABORT(msg) \
  do { \
    fputs ("*** picosat: " msg "\n", stderr); \
    abort (); \
  } while (0)

#define ABORTIF(cond,msg) \
  do { \
    if (!(cond)) break; \
    ABORT (msg); \
  } while (0)

#define ZEROFLT() (0x00000000u)
#define INFFLT() (0xffffffffu)

#define FLTCARRY (1u << 25)
#define FLTMSB (1u << 24)
#define FLTMAXMANTISSA (FLTMSB - 1)

#define FLTMANTISSA(d) ((d) & FLTMAXMANTISSA)
#define FLTEXPONENT(d) ((int)((d) >> 24) - 128)

#define FLTMINEXPONENT (-128)
#define FLTMAXEXPONENT (127)

#define cmpswapflt(a,b) \
  do { \
    Flt tmp; \
    if (((a) < (b))) \
      { \
	tmp = (a); \
	(a) = (b); \
	(b) = tmp; \
      } \
  } while (0)

#define unpackflt(u,m,e) \
  do { \
    (m) = FLTMANTISSA(u); \
    (e) = FLTEXPONENT(u); \
    (m) |= FLTMSB; \
  } while (0)

typedef unsigned Flt;		/* 32 bit deterministic soft float */
typedef Flt Act;		/* clause activity */
typedef struct Als Als;		/* assumed literal with trail pos */
typedef struct Blk Blk;		/* allocated memory block */
typedef struct Cls Cls;		/* clause */
typedef struct Lit Lit;		/* literal */
typedef struct Rnk Rnk;		/* variable to score mapping */
typedef signed char Val;	/* TRUE, UNDEF, FALSE */
typedef struct Var Var;		/* variable */

#ifdef TRACE
typedef struct Trd Trd;		/* trace data for clauses */
typedef struct Zhn Zhn;		/* compressed chain (=zain) data */
typedef unsigned char Znt;	/* compressed antecedent data */
#endif

#ifdef USE_CLAUSE_STACK
typedef struct Ctk Ctk;

struct Ctk
{
  Cls ** start;
  Cls ** top;
  Cls ** end;
};
#endif

#ifdef NO_BINARY_CLAUSES
typedef struct Ltk Ltk;

struct Ltk
{
  Lit ** start;
  Lit ** top;
  Lit ** end;
};
#endif

struct Lit
{
  Val val;
};

#define LD_MAX_LEVEL 27
#define MAX_LEVEL (1 << LD_MAX_LEVEL)

struct Var
{
  Cls *reason;
  unsigned level:LD_MAX_LEVEL;
  unsigned mark:1;
  unsigned core:1;
  unsigned assumption:1;
  unsigned validposlastphase:1;
  unsigned poslastphase:1;
};

struct Rnk
{
  unsigned pos;			/* 0 iff not on heap */
  Act score;
};

#define LD_MAX_CLAUSE_SIZE 24
#define MAX_CLAUSE_SIZE (1 << LD_MAX_CLAUSE_SIZE)

struct Cls
{
#ifndef USE_CLAUSE_STACK
  Cls *next[2];
#endif
  unsigned size:LD_MAX_CLAUSE_SIZE;
  unsigned learned:1;
  unsigned collect:1;
  unsigned collected:1;
  unsigned connected:1;
  unsigned used:1;
  unsigned core:1;
  unsigned locked:1;
  unsigned fixed:1;
  Lit *lits[2];
};

#ifdef TRACE
struct Zhn
{
  unsigned ref:31;
  unsigned core:1;
  Znt znt[0];
};

struct Trd
{
  unsigned idx;
  Cls cls[0];
};
#endif

struct Blk
{
#ifndef NDEBUG
  union
  {
    size_t size;		/* this is what we really use */
    void *as_two_ptrs[2];	/* 2 * sizeof (void*) alignment of data */
  };
#endif
  char data[0];
};

struct Als
{
  Lit *lit;			/* the assumption */
  int pos;			/* neg. if not on trail else trail pos */
};

static FILE *out;
static int verbose;
static unsigned level;
static unsigned max_var;
static unsigned size_vars;
static Lit *lits;
static Flt *jwh;
#ifdef USE_CLAUSE_STACK
static Ctk *wchs;
#else
static Cls **wchs;
#endif
#ifdef NO_BINARY_CLAUSES
static Ltk *impls;
static Cls impl;
#elif defined(USE_CLAUSE_STACK)
static Ctk *impls;
#else
static Cls **impls;
#endif
static Var *vars;
static Rnk *rnks;
static Lit **trail, **thead, **eot, **ttail, ** ttail2;
static Als *als, *alshead, *alstail, *eoals;
static Rnk **heap, **hhead, **eoh;
static Cls **clauses, **chead, **eoc;
#ifdef TRACE
static int trace;
static Zhn **zhains, **zhead, **eoz;
#endif
static Cls *mtcls;
static Lit *failed_assumption;
static int assignments_and_failed_assumption_valid;
static Cls *conflict;
static int ocore = -1;
static Lit **added, **ahead, **eoa;
static Var **marked, **mhead, **eom;
static Cls **resolved, **rhead, **eor;
static unsigned char *buffer, *bhead, *eob;
static Act vinc, lvinc, fvinc;
static Act cinc, lcinc, fcinc;
static unsigned srng;
static size_t current_bytes;
static size_t max_bytes;
static size_t lbytes;
static size_t srecycled;
static size_t rrecycled;
static double seconds;
static double entered;
static char *rline[2];
static int szrline, rcount;
static double levelsum;
static unsigned iterations;
static int reports;
static int lastrheader = -2;
static unsigned calls;
static unsigned assumptions;
static unsigned decisions;
static unsigned sdecisions;
static unsigned rdecisions;
static unsigned restarts;
static unsigned simps;
static unsigned fsimplify;
static unsigned reductions;
static unsigned lastreductionsatrestart;
static unsigned freductions;
static unsigned lreduce;
static unsigned lastreduceconflicts;
static unsigned dfreduce;
static unsigned llocked;	/* locked large learned clauses */
static unsigned lfixed;		/* fixed large learned clauses */
static unsigned lrestart;
static unsigned drestart;
static unsigned ddrestart;
static unsigned long long lsimplify;
static unsigned long long propagations;
static unsigned fixed;		/* top level assignments */
static unsigned conflicts;
static unsigned oclauses;	/* current number large original clauses */
static unsigned lclauses;	/* current number large learned clauses */
static unsigned oused;		/* used large original clauses */
static unsigned lused;		/* used large learned clauses */
static unsigned olits;		/* current literals in large original clauses */
static unsigned llits;		/* current literals in large learned clauses */
static unsigned oadded;		/* added original clauses */
static unsigned ladded;		/* added learned clauses */
static unsigned addedclauses;	/* oadded + ladded */
#ifdef STATS
static unsigned llitsadded;	/* added learned literals */
static unsigned long long visits;
static unsigned long long othertrue;
static unsigned long long traversals;
static unsigned long long assignments;
static unsigned long long antecedents;
static unsigned minimizedllits;
static unsigned nonminimizedllits;
static unsigned znts;
#endif

static Flt
packflt (unsigned m, int e)
{
  Flt res;
  assert (m < FLTMSB);
  assert (FLTMINEXPONENT <= e);
  assert (e <= FLTMAXEXPONENT);
  res = m | ((e + 128) << 24);
  return res;
}

static Flt
base2flt (unsigned m, int e)
{
  if (!m)
    return ZEROFLT ();

  if (m < FLTMSB)
    {
      do
	{
	  if (e <= FLTMINEXPONENT)
	    return ZEROFLT ();

	  e--;
	  m <<= 1;

	}
      while (m < FLTMSB);
    }
  else
    {
      while (m >= FLTCARRY)
	{
	  if (e >= FLTMAXEXPONENT)
	    return INFFLT ();

	  e++;
	  m >>= 1;
	}
    }

  m &= ~FLTMSB;
  return packflt (m, e);
}

static Flt
addflt (Flt a, Flt b)
{
  unsigned ma, mb, delta;
  int ea, eb;

  cmpswapflt (a, b);
  if (!b)
    return a;

  unpackflt (a, ma, ea);
  unpackflt (b, mb, eb);

  assert (ea >= eb);
  delta = ea - eb;
  mb >>= delta;
  if (!mb)
    return a;

  ma += mb;
  if (ma & FLTCARRY)
    {
      if (ea == FLTMAXEXPONENT)
	return INFFLT ();

      ea++;
      ma >>= 1;
    }

  assert (ma < FLTCARRY);
  ma &= FLTMAXMANTISSA;

  return packflt (ma, ea);
}

static Flt
mulflt (Flt a, Flt b)
{
  unsigned ma, mb;
  unsigned long long accu;
  int ea, eb;

  cmpswapflt (a, b);
  if (!b)
    return ZEROFLT ();

  unpackflt (a, ma, ea);
  unpackflt (b, mb, eb);

  ea += eb;
  ea += 24;
  if (ea > FLTMAXEXPONENT)
    return INFFLT ();

  if (ea < FLTMINEXPONENT)
    return ZEROFLT ();

  accu = ma;
  accu *= mb;
  accu >>= 24;

  if (accu >= FLTCARRY)
    {
      if (ea == FLTMAXEXPONENT)
	return INFFLT ();

      ea++;
      accu >>= 1;

      if (accu >= FLTCARRY)
	return INFFLT ();
    }

  assert (accu < FLTCARRY);
  assert (accu & FLTMSB);

  ma = accu;
  ma &= ~FLTMSB;

  return packflt (ma, ea);
}

static int
ISDIGIT (char c)
{
  return '0' <= c && c <= '9';
}

static Flt
ascii2flt (const char *str)
{
  Flt ten = base2flt (10, 0);
  Flt onetenth = base2flt (26843546, -28);
  Flt res = ZEROFLT (), tmp, base;
  const char *p = str;
  char ch;

  ch = *p++;

  if (ch != '.')
    {
      if (!ISDIGIT (ch))
	return INFFLT ();	/* better abort ? */

      res = base2flt (ch - '0', 0);

      while ((ch = *p++))
	{
	  if (ch == '.')
	    break;

	  if (!ISDIGIT (ch))
	    return INFFLT ();	/* better abort? */

	  res = mulflt (res, ten);
	  tmp = base2flt (ch - '0', 0);
	  res = addflt (res, tmp);
	}
    }

  if (ch == '.')
    {
      ch = *p++;
      if (!ISDIGIT (ch))
	return INFFLT ();	/* better abort ? */

      base = onetenth;
      tmp = mulflt (base2flt (ch - '0', 0), base);
      res = addflt (res, tmp);

      while ((ch = *p++))
	{
	  if (!ISDIGIT (ch))
	    return INFFLT ();	/* better abort? */

	  base = mulflt (base, onetenth);
	  tmp = mulflt (base2flt (ch - '0', 0), base);
	  res = addflt (res, tmp);
	}
    }

  return res;
}

static int
log2flt (Flt a)
{
  return FLTEXPONENT (a) + 24;
}

static int
cmpflt (Flt a, Flt b)
{
  if (a < b)
    return -1;

  if (a > b)
    return 1;

  return 0;
}

static void *
new (size_t size)
{
  Blk *b;
  b = malloc (sizeof (*b) + size);
  ABORTIF (!b, "out of memory in 'malloc'");
#ifndef NDEBUG
  b->size = size;
#endif
  current_bytes += size;
  if (current_bytes > max_bytes)
    max_bytes = current_bytes;
  return b->data;
}

static void
delete (void *void_ptr, size_t size)
{
  Blk *b = PTR2BLK (void_ptr);
  assert (size <= current_bytes);
  current_bytes -= size;
  assert ((!size && !b) || (size && b->size == size));
  free (b);
}

static void *
resize (void *void_ptr, size_t old_size, size_t new_size)
{
  Blk *b = PTR2BLK (void_ptr);
  assert (new_size);		/* otherwise checking does not work */
  assert (old_size <= current_bytes);
  current_bytes -= old_size;
  assert ((!old_size && !b) || (old_size && b->size == old_size));
  b = realloc (b, new_size + sizeof (*b));
  ABORTIF (!b, "out of memory in 'realloc'");
#ifndef NDEBUG
  b->size = new_size;
#endif
  current_bytes += new_size;
  if (current_bytes > max_bytes)
    max_bytes = current_bytes;
  return b->data;
}

static unsigned
int2unsigned (int l)
{
  return (l < 0) ? 1 + 2 * -l : 2 * l;
}

static Lit *
int2lit (int l)
{
  return lits + int2unsigned (l);
}

static Lit **
end_of_lits (Cls * cls)
{
  return cls->lits + cls->size;
}

static int
lit2idx (Lit * lit)
{
  return (lit - lits) / 2;
}

static int
lit2sign (Lit * lit)
{
  return ((lit - lits) & 1) ? -1 : 1;
}

int
lit2int (Lit * l)
{
  return lit2idx (l) * lit2sign (l);
}

#if !defined(NDEBUG) || defined(LOGGING)

static void
dumplits (Lit ** lits, Lit ** eol)
{
  int first;
  Lit ** p;

  if (lits == eol)
    {
      /* empty clause */
    }
  else if (lits + 1 == eol)
    {
      fprintf (out, "%d ", lit2int (lits[0]));
    }
  else
    { 
      assert (lits + 2 <= eol);
      first = (abs (lit2int (lits[0])) > abs (lit2int (lits[1])));
      fprintf (out, "%d ", lit2int (lits[first]));
      fprintf (out, "%d ", lit2int (lits[!first]));
      for (p = lits + 2; p < eol; p++)
	fprintf (out, "%d ", lit2int (*p));
    }

  fputc ('0', out);
}

static void
dumpcls (Cls * cls)
{
  Lit **eol;

  if (cls)
    {
      eol = end_of_lits (cls);
      dumplits (cls->lits, eol);
#ifdef TRACE
      if (trace)
	fprintf (out, " clause(%u)", CLS2IDX (cls));
#endif
    }
  else
    fputs ("DECISION", out);
}

static void
dumpclsnl (Cls * cls)
{
  dumpcls (cls);
  fputc ('\n', out);
}

void
dumpcnf (void)
{
  Cls **p, *cls;
  for (p = clauses; p < chead; p++)
    {
      cls = *p;

      if (!cls)
	continue;

      if (cls->collected)
	continue;

      dumpclsnl (*p);
    }
}

#endif

static void
init (void)
{
  assert (!max_var);		/* check for proper reset */
  assert (!size_vars);		/* check for proper reset */
  size_vars = (1 << 8);

  NEWN (lits, 2 * size_vars);
  NEWN (jwh, 2 * size_vars);
  NEWN (wchs, 2 * size_vars);
  NEWN (impls, 2 * size_vars);
  NEWN (vars, size_vars);
  NEWN (rnks, size_vars);

  ENLARGE (heap, hhead, eoh);	/* because '0' pos denotes not on heap */
  hhead = heap + 1;

  vinc = base2flt (1, 0);
  cinc = base2flt (1, 0);

  lvinc = base2flt (1, 90);
  lcinc = base2flt (1, 90);

  fvinc = ascii2flt ("1.1");
  fcinc = ascii2flt ("1.001");
  out = stdout;
  verbose = 0;

  ENLARGE (clauses, chead, eoc);
  *chead++ = 0;
#ifdef TRACE
  ENLARGE (zhains, zhead, eoz);
  *zhead++ = 0;
#endif

  lbytes = LBYTES;		/* TODO remove hard coded space limit */
#ifdef NO_BINARY_CLAUSES
  memset (&impl, 0, sizeof (impl));
  impl.size = 2;
#endif
}

static size_t
bytes_clause (unsigned size, unsigned learned)
{
  size_t res;

  res = sizeof (Cls);
  res += size * sizeof (Lit *);
  res -= 2 * sizeof (Lit *);

  if (learned && size > 2)
    res += sizeof (Act);	/* add activity */

#ifdef TRACE
  if (trace)
    res += sizeof (Trd);	/* add trace data */
#endif

  return res;
}

static Cls *
new_clause (unsigned size, unsigned learned)
{
  size_t bytes;
  void * tmp;
#ifdef TRACE
  Trd *trd;
#endif
  Cls *res;

  bytes = bytes_clause (size, learned);
  tmp = new (bytes);

#ifdef TRACE
  if (trace)
    {
      trd = tmp;
      trd->idx = chead - clauses;
      res = trd->cls;
    }
  else
#endif
    res = tmp;

  res->size = size;
  res->learned = learned;

  res->collected = 0;
  res->collect = 0;
  res->connected = 0;
  res->core = 0;
  res->used = 0;
  res->locked = 0;
  res->fixed = 0;
  if (learned && size > 2)
    *CLS2ACT (res) = cinc;

  return res;
}

static void
delete_clause (Cls * cls)
{
  size_t bytes;
#ifdef TRACE
  Trd *trd;
#endif

  bytes = bytes_clause (cls->size, cls->learned);

#ifdef TRACE
  if (trace)
    {
      trd = CLS2TRD (cls);
      delete (trd, bytes);
    }
  else
#endif
    delete (cls, bytes);
}

static void
delete_clauses (void)
{
  Cls **p;
  for (p = clauses; p < chead; p++)
    if (*p)
      delete_clause (*p);

  DELETEN (clauses, eoc - clauses);
  chead = eoc = 0;
}

#ifdef TRACE

static void
delete_zhain (Zhn * zhain)
{
  const Znt *p, *znt;

  assert (zhain);

  znt = zhain->znt;
  for (p = znt; *p; p++)
    ;

  delete (zhain, sizeof (Zhn) + (p - znt) + 1);
}

static void
delete_zhains (void)
{
  Zhn **p, *z;
  for (p = zhains; p < zhead; p++)
    if ((z = *p))
      delete_zhain (z);

  DELETEN (zhains, eoz - zhains);
  eoz = zhead = 0;
}

#endif

#ifdef USE_CLAUSE_STACK
static void
wpush (Ctk * stk, Cls * cls)
{
  if (stk->top == stk->end)
    ENLARGE (stk->start, stk->top, stk->end);

  *stk->top++ = cls;
}

static void
wrelease (Ctk * stk)
{
  DELETEN (stk->start, stk->end - stk->start);
  memset (stk, 0, sizeof (*stk));
}
#endif

#ifdef NO_BINARY_CLAUSES
static void
lrelease (Ltk * stk)
{
  DELETEN (stk->start, stk->end - stk->start);
  memset (stk, 0, sizeof (*stk));
}
#endif

static void
reset (void)
{
  delete_clauses ();
#ifdef TRACE
  delete_zhains ();
#endif
#ifdef NO_BINARY_CLAUSES
  {
    unsigned i;
    for (i = 2; i <= 2 * max_var + 1; i++)
      lrelease (impls + i);
  }
#endif
#ifdef USE_CLAUSE_STACK
  {
    unsigned i;
    for (i = 2; i <= 2 * max_var + 1; i++)
      {
	wrelease (wchs + i);
#ifndef NO_BINARY_CLAUSES
	wrelease (impls + i);
#endif
      }
  }
#endif
  DELETEN (wchs, 2 * size_vars);
  DELETEN (impls, 2 * size_vars);
  DELETEN (lits, 2 * size_vars);
  DELETEN (jwh, 2 * size_vars);
  DELETEN (vars, size_vars);
  DELETEN (rnks, size_vars);

  DELETEN (trail, eot - trail);
  trail = ttail = ttail2 = thead = eot = 0;

  DELETEN (heap, eoh - heap);
  heap = hhead = eoh = 0;

  DELETEN (als, eoals - als);
  als = eoals = alshead = alstail = 0;

  size_vars = 0;
  max_var = 0;

  mtcls = 0;
  failed_assumption = 0;
  ocore = -1;
  conflict = 0;

  DELETEN (added, eoa - added);
  eoa = ahead = 0;

  DELETEN (marked, eom - marked);
  eom = mhead = 0;

  DELETEN (resolved, eor - resolved);
  eor = rhead = 0;

  DELETEN (buffer, eob - buffer);
  eob = bhead = 0;

  delete (rline[0], szrline);
  delete (rline[1], szrline);
  rline[0] = rline[1] = 0;
  szrline = rcount = 0;
  assert (getenv ("LEAK") || !current_bytes);	/* found leak if failing */
  max_bytes = 0;
  lbytes = 0;
  current_bytes = 0;
  srecycled = 0;
  rrecycled = 0;

  lrestart = 0;
  lreduce = 0;
  lastreduceconflicts = 0;
  llocked = 0;
  lfixed = 0;
  lsimplify = 0;

  seconds = 0;
  entered = 0;

  levelsum = 0.0;
  calls = 0;
  assumptions = 0;
  decisions = 0;
  rdecisions = 0;
  sdecisions = 0;
  restarts = 0;
  simps = 0;
  iterations = 0;
  reports = 0;
  lastrheader = -2;
  fixed = 0;
  propagations = 0;
  conflicts = 0;
  oclauses = 0;
  oadded = 0;
  oused = 0;
  lused = 0;
  olits = 0;
  lclauses = 0;
  ladded = 0;
  addedclauses = 0;
  llits = 0;
  out = 0;
#ifdef TRACE
  trace = 0;
#endif
  level = 0;

  reductions = 0;
  lastreductionsatrestart = 0;

#ifdef STATS
  visits = 0;
  othertrue = 0;
  traversals = 0;
  assignments = 0;
  antecedents = 0;
  znts = 0;
  minimizedllits = 0;
  nonminimizedllits = 0;
  llitsadded = 0;
#endif
}

static void
tpush (Lit * lit)
{
  if (thead == eot)
    {
      unsigned ttail2count = ttail2 - trail;
      unsigned ttailcount = ttail - trail;
      ENLARGE (trail, thead, eot);
      ttail = trail + ttailcount;
      ttail2 = trail + ttail2count;
    }

  *thead++ = lit;
}

static void
assign_assumption (Lit * lit)
{
  Var *v;

  assert (!level);
  LOG (fprintf (out,
		"c assign %d at level 0 <= ASSUMPTION\n", lit2int (lit)));

  assert (lit->val == UNDEF);

#ifdef STATS
  assignments++;
#endif

  v = LIT2VAR (lit);
  v->level = 0;
  v->reason = 0;
  assert (!v->assumption);
  v->assumption = 1;

  lit->val = TRUE;
  NOTLIT (lit)->val = FALSE;

  tpush (lit);
}

static int
cmp_added (const void *p, const void *q)
{
  Lit *k = *(Lit **) p;
  Lit *l = *(Lit **) q;
  Var *u, *v;
  int res;

  if (k->val == UNDEF)		/* unassigned first */
    {
      if (l->val == UNDEF)
	return l - k;		/* larger index first */

      return -1;
    }

  if (l->val == UNDEF)
    return 1;

  assert (k->val != UNDEF && l->val != UNDEF);

  u = LIT2VAR (k);
  v = LIT2VAR (l);

  res = cmpflt (v->level, u->level);
  if (res)
    return res;			/* lower activity first */

  res = v->level - u->level;
  if (res)
    return res;			/* larger level first */

  return u - v;			/* smaller index first */
}

static void
sorttwolits (Lit ** v)
{
  Lit * a = v[0], * b = v[1];

  assert (a != b);

  if (a < b)
    return;

  v[0] = b;
  v[1] = a;
}

static void
sortlits (Lit ** v, unsigned size)
{
  if (size == 2)
    sorttwolits (v);	/* same order with and with out 'NO_BINARY_CLAUSES' */
  else
    qsort (v, size, sizeof (v[0]), cmp_added);
}

#ifdef NO_BINARY_CLAUSES
static Cls *
set_impl (Lit * a, Lit * b)
{
  assert (impl.size == 2);
  impl.lits[0] = a;
  impl.lits[1] = b;
  sorttwolits (impl.lits);
  return &impl;
}
#endif

static Cls *add_simplified_clause (int learned);

static void
assign (Lit * lit, Cls * reason)
{
  Lit **p, **eol, *other;
  Var *u, *v;

  assert (lit->val == UNDEF);
#ifdef STATS
  assignments++;
#endif
  v = LIT2VAR (lit);
  v->level = level;
  v->reason = reason;
#ifdef NO_BINARY_CLAUSES
  if (ISLITREASON (reason))
    reason = set_impl (lit, NOTLIT (REASON2LIT (reason)));
#endif
  LOG (fprintf (out, "c assign %d at level %d by ", lit2int (lit), level);
       dumpclsnl (reason));

  assert (!v->assumption);
  v->poslastphase = (LIT2SGN (lit) > 0);
  v->validposlastphase = 1;
  lit->val = TRUE;
  NOTLIT (lit)->val = FALSE;

  /* Whenever we have a top level derived unit we really should derive a
   * unit clause otherwise the resolutions in 'add_simplified_clause' become
   * incorrect.
   */
  if (reason && !level && reason->size > 1)
    {
      assert (rhead == resolved);
      assert (ahead == added);

      *ahead++ = lit;

      while (reason->size > eor - resolved)
	ENLARGE (resolved, rhead, eor);

      assert (rhead < eor);
      *rhead++ = reason;

      eol = end_of_lits (reason);
      for (p = reason->lits; p < eol; p++)
	{
	  other = *p;
	  u = LIT2VAR (other);
	  if (u == v)
	    continue;

	  if (u->assumption)
	    {
	      assert (ahead < eoa);
	      *ahead++ = other;
	    }
	  else
	    {
	      assert (u->reason);
	      assert (rhead < eor);
	      *rhead++ = u->reason;
	    }
	}

      /* Some of the literals could be assumptions.  If at least one
       * variable is not an assumption, we should resolve.
       */
      if (rhead - resolved > 1)
	{
	  reason = add_simplified_clause (1);
#ifdef NO_BINARY_CLAUSES
	  if (reason->size == 2)
	    {
	      assert (reason == &impl);
	      other = reason->lits[0];
	      if (lit == other)
		other = reason->lits[1];
	      assert (other->val == FALSE);
	      reason = LIT2REASON (NOTLIT (other));
	    }
#endif
	  v->reason = reason;
	}
      else
	{
	  ahead = added;
	  rhead = resolved;
	}
    }

#ifdef NO_BINARY_CLAUSES
  if (ISLITREASON (reason) || reason == &impl)
    {
      /* DO NOTHING */
    }
  else
#endif
  if (reason)
    {
      assert (!reason->locked);
      reason->locked = 1;

      if (reason->learned && reason->size > 2)
	llocked++;

    }

  if (reason && !level)
    {
      eol = end_of_lits (reason);
      for (p = reason->lits; p < eol; p++)
	{
	  other = *p;
	  u = LIT2VAR (other);
	  if (!u->assumption)
	    continue;

	  v->assumption = 1;
	  break;
	}
    }

  tpush (lit);

  if (!level && !v->assumption)
    {
      fixed++;

      /* These maintain the invariant that top level assigned literals
       * assigned to TRUE are not watched.
       */
#ifdef USE_CLAUSE_STACK
      wrelease (LIT2WCHS(lit));
#else
      *LIT2WCHS (lit) = 0;
#endif
#ifdef NO_BINARY_CLAUSES
      lrelease (LIT2IMPLS (lit));
#elif defined(USE_CLAUSE_STACK)
      wrelease (LIT2IMPLS(lit));
#else
      *LIT2IMPLS (lit) = 0;
#endif
    }
}

static void
add_lit (Lit * lit)
{
  assert (lit);

  if (ahead == eoa)
    ENLARGE (added, ahead, eoa);

  *ahead++ = lit;
}

#ifdef NO_BINARY_CLAUSES

static void
lpush (Lit * lit, Cls * cls)
{
  int pos = (cls->lits[0] == lit);
  Ltk * s = LIT2IMPLS (lit);

  assert (cls->size == 2);

  if (s->top == s->end)
    ENLARGE (s->start, s->top, s->end);

  *s->top++ = cls->lits[pos];
}

#endif

static void
connect_watch (Lit * lit, Cls * cls)
{
#ifdef USE_CLAUSE_STACK
  Ctk * s;
#else
  Cls ** s;
#endif
  assert (cls->size >= 1);
  if (cls->size == 2)
    {
#ifdef NO_BINARY_CLAUSES
      lpush (lit, cls);
      return;
#else
      s = LIT2IMPLS (lit);
#endif
    }
  else
    s = LIT2WCHS (lit);

#ifdef USE_CLAUSE_STACK
  wpush (s, cls);
#else
  if (cls->lits[0] != lit)
    {
      assert (cls->size >= 2);
      assert (cls->lits[1] == lit);
      cls->next[1] = *s;
    }
  else
    cls->next[0] = *s;

  *s = cls;
#endif
}

#ifdef TRACE
static void
zpush (Zhn * zhain)
{
  if (zhead == eoz)
    ENLARGE (zhains, zhead, eoz);

  *zhead++ = zhain;
}

static int
cmp_resolved (const void *p, const void *q)
{
  Cls *c = *(Cls **) p;
  Cls *d = *(Cls **) q;

  assert (trace);

  return CLS2IDX (d) - CLS2IDX (c);
}

static void
bpushc (unsigned char ch)
{
  if (bhead == eob)
    ENLARGE (buffer, bhead, eob);

  *bhead++ = ch;
}

static void
bpushu (unsigned u)
{
  while (u & ~0x7f)
    {
      bpushc (u | 0x80);
      u >>= 7;
    }

  bpushc (u);
}

static void
bpushd (unsigned prev, unsigned this)
{
  unsigned delta;
  assert (prev > this);
  delta = prev - this;
  bpushu (delta);
}

static void
add_zhain (void)
{
  unsigned idx, prev, this, count;
  Cls **p, *c;
  Zhn *res;

  assert (trace);
  assert (bhead == buffer);
  assert (rhead > resolved);

  idx = zhead - zhains;
  assert (idx == (unsigned)(chead - clauses));

  qsort (resolved, rhead - resolved, sizeof (resolved[0]), cmp_resolved);

  prev = idx;
  for (p = resolved; p < rhead; p++)
    {
      c = *p;
      this = CLS2TRD (c)->idx;
      bpushd (prev, this);
      prev = this;
    }
  bpushc (0);

  count = bhead - buffer;

  res = new (sizeof (Zhn) + count);
  res->core = 0;
  res->ref = 0;
  memcpy (res->znt, buffer, count);

  bhead = buffer;
#ifdef STATS
  znts += count - 1;
#endif
  zpush (res);

  rhead = resolved;
}

#endif

static void
add_resolved (int learned)
{
  Cls **p, *c;

  for (p = resolved; p < rhead; p++)
    {
      c = *p;
      if (c->used)
	continue;

      c->used = 1;

      if (c->size <= 2)
	continue;

      if (c->learned)
	lused++;
      else
	oused++;
    }

#ifdef TRACE
  if (learned && trace)
    add_zhain ();
  else
#else
  (void) learned;
#endif
    rhead = resolved;
}

static Cls *
add_simplified_clause (int learned)
{
  unsigned num_true, num_undef, num_false, num_false_assumption, idx, size;
  Lit **p, **q, *lit, *assumption, ** eol;
  Cls *res, * reason;
  Flt * f, inc, sum;
  Val val;
  Var *v;

REENTER:

  size = ahead - added;
  ABORTIF (size >= MAX_CLAUSE_SIZE, "maximal clause size exhausted");

  add_resolved (learned);

  if (learned)
    {
      ladded++;
#ifdef STATS
      llitsadded += size;
#endif
      if (size > 2)
	{
	  lclauses++;
	  llits += size;
	}
    }
  else
    {
      oadded++;
      if (size > 2)
	{
	  oclauses++;
	  olits += size;
	}

#ifdef TRACE
      if (trace)
	zpush (0);
#endif
    }

  addedclauses++;
  assert (addedclauses == ladded + oadded);

#ifdef NO_BINARY_CLAUSES
  if (size == 2)
    res = set_impl (added[0], added[1]);
  else
#endif
    {
      sortlits (added, size); 

      if (chead == eoc)
	ENLARGE (clauses, chead, eoc);

      res = new_clause (size, learned);
      idx = chead - clauses;
#if !defined(NDEBUG) && defined(TRACE)
      if (trace)
	assert (CLS2IDX (res) == idx);
#endif
      *chead++ = res;

#if !defined(NDEBUG) && defined(TRACE)
      if (trace)
	assert (zhead - zhains == chead - clauses);
#endif
    }

  num_true = num_undef = num_false = num_false_assumption = 0;
  assumption = 0;

  q = res->lits;
  for (p = added; p < ahead; p++)
    {
      lit = *p;
      *q++ = lit;

      val = lit->val;

      num_true += (val == TRUE);
      num_undef += (val == UNDEF);
      num_false += (val == FALSE);

      v = LIT2VAR (lit);
      if (v->assumption)
	{
	  if (val == FALSE)
	    num_false_assumption++;

	  if (!assumption)
	    assumption = lit;
	}
    }
  assert (num_false + num_true + num_undef == size);

  if (!num_true && !learned)
    {
      for (p = added; p < ahead; p++)
	{
	  lit = *p;
	  f = LIT2JWH (lit);
	  inc = base2flt (1, -size);
	  sum = addflt (*f, inc);
	  *f = sum;
	}
    }

  ahead = added;		/* reset */

  if (size > 0)
    {
      connect_watch (res->lits[0], res);
      if (size > 1)
	connect_watch (res->lits[1], res);
    }

  if (assumption)
    {
      assert (!failed_assumption);
      if (num_true + num_undef == 0)	/* empty clause */
	{
	  assert (!level);
	  failed_assumption = assumption;
	}
    }
  else if (size == 0)
    {
      assert (!level);
      if (!mtcls)
	mtcls = res;
    }

#ifdef NO_BINARY_CLAUSES
  if (size != 2)
#endif
    res->connected = 1;

  LOG (fputs (learned ? "c learned " : "c original ", out);
       dumpclsnl (res));

  /* Shrink clause by resolving it against top level assignments.
   */
  if (!level && num_false > num_false_assumption)
    {
      assert (ahead == added);
      assert (rhead == resolved);

      if (rhead == eor)
	ENLARGE (resolved, rhead, eor);

      *rhead++ = res;

      eol = end_of_lits (res);
      for (p = res->lits; p < eol; p++)
	{
	  lit = *p;

	  if (lit->val == FALSE)
	    {
	      v = LIT2VAR (lit);
	      if (v->assumption)
		goto ADD;

	      assert (v->reason);

	      if (rhead == eor)
		ENLARGE (resolved, rhead, eor);

	      *rhead++ = v->reason;
	    }
	  else
	    {
	    ADD:
	      if (ahead == eoa)
		ENLARGE (added, ahead, eoa);

	      *ahead++ = lit;
	    }
	}

      assert (rhead - resolved >= 2);

      learned = 1;
      goto REENTER;		/* and return simplified clause */
    }

  if (!num_true && num_undef == 1)	/* unit clause */
    {
      lit = 0;
      for (p = res->lits; !lit && p < res->lits + size; p++)
        if ((*p)->val == UNDEF)
	  lit = *p;
      assert (lit);

      reason = res;
#ifdef NO_BINARY_CLAUSES
      if (size == 2)
        {
	  Lit * other = res->lits[0];
	  if (other == lit)
	    other = res->lits[1];

	  assert (other->val == FALSE);
	  reason = LIT2REASON (NOTLIT (other));
	}
#endif
      assign (lit, reason);
    }

  if (num_false == size && !conflict)
    conflict = res;

  return res;
}

static int
cmp_address (const void *p, const void *q)
{
  void *k = *(void **) p;
  void *l = *(void **) q;
  return l - k;			/* arbitrarily already reverse */
}

static int
trivial_clause (void)
{
  Lit **p, **q, *prev;
  Var *v;

  qsort (added, ahead - added, sizeof (added[0]), cmp_address);
  prev = 0;
  q = added;
  for (p = q; p < ahead; p++)
    {
      Lit *this = *p;

      v = LIT2VAR (this);

      if (prev == this)		/* skip repeated literals */
	continue;

      if (!v->level)
	{
	  if (this->val == TRUE)/* top level satisfied ? */
	    return 1;
	}

      if (prev == NOTLIT (this))/* found pair of dual literals */
	return 1;

      *q++ = prev = this;
    }

  ahead = q;			/* shrink */

  return 0;
}

static void
simplify_and_add_clause (int learned)
{
  if (trivial_clause ())
    ahead = added;
  else
    add_simplified_clause (learned);
}

static void
hup (Rnk * v)
{
  Act vscore = v->score;
  int upos, vpos;
  Rnk *u;

  vpos = v->pos;

  assert (0 < vpos);
  assert (vpos < hhead - heap);
  assert (heap[vpos] == v);

  while (vpos > 1)
    {
      upos = vpos / 2;

      u = heap[upos];
      if (u->score > vscore)
	break;

      if (u->score == vscore && u > v)
        break;

      heap[vpos] = u;
      u->pos = vpos;

      vpos = upos;
    }

  heap[vpos] = v;
  v->pos = vpos;
}

static Rnk *
hpop (void)
{
  Rnk *res, *last, *child, *other;
  Act last_score, child_score;
  int end, lpos, cpos, opos;

  assert (hhead > heap);

  res = heap[lpos = 1];
  res->pos = 0;

  end = --hhead - heap;
  if (end == 1)
    return res;

  last = heap[end];
  last_score = last->score;

  for (;;)
    {
      cpos = 2 * lpos;
      if (cpos >= end)
	break;

      opos = cpos + 1;
      child = heap[cpos];

      child_score = child->score;
      if (last_score < child_score || 
          (last_score == child_score && last < child))
	{
	  if (opos < end)
	    {
	      other = heap[opos];

	      if (child_score < other->score ||
	          (child_score == other->score && child < other))
		{
		  child = other;
		  cpos = opos;
		}
	    }
	}
      else if (opos < end)
	{
	  child = heap[opos];
	  if (last_score < child->score ||
	      (last_score == child->score && last < child))
	    cpos = opos;
	  else
	    break;
	}
      else
	break;

      heap[lpos] = child;
      child->pos = lpos;
      lpos = cpos;
    }

  last->pos = lpos;
  heap[lpos] = last;

  return res;
}

static void
hpush (Rnk * v)
{
  assert (!v->pos);

  if (hhead == eoh)
    ENLARGE (heap, hhead, eoh);

  v->pos = hhead++ - heap;
  heap[v->pos] = v;
  hup (v);
}

static void
fix_trail_lits (long delta)
{
  Lit **p;
  for (p = trail; p < thead; p++)
    *p += delta;
}

#ifdef NO_BINARY_CLAUSES
static void
fix_impl_lits (long delta)
{
  Ltk * s;
  Lit ** p;

  for (s = impls + 2; s < impls + 2 * max_var; s++)
    for (p = s->start; p < s->top; p++)
      *p += delta;
}

static void
fix_impl_reason (long delta)
{
  Lit **p, * lit;
  Cls * reason;
  Var * v;

  for (p = trail; p < thead; p++)
    {
      v = LIT2VAR (*p);
      reason = v->reason;
      if (ISLITREASON (reason))
	{
	  lit = REASON2LIT (reason);
	  lit += delta;
	  reason = LIT2REASON (lit);
	  v->reason = reason;
	}
    }
}
#endif

static void
fix_clause_lits (long delta)
{
  Cls **p, *clause;
  Lit **q, *lit, **eol;

  for (p = clauses; p < chead; p++)
    {
      clause = *p;
      if (!clause)
	continue;

      q = clause->lits;
      eol = end_of_lits (clause);
      while (q < eol)
	{
	  assert (q - clause->lits <= clause->size);
	  lit = *q;
	  lit += delta;
	  *q++ = lit;
	}
    }
}

static void
fix_added_lits (long delta)
{
  Lit **p;
  for (p = added; p < ahead; p++)
    *p += delta;
}

static void
fix_heap_rnks (long delta)
{
  Rnk **p;

  for (p = heap + 1; p < hhead; p++)
    *p += delta;
}

static void
enlarge (void)
{
  long rnks_delta, lits_delta, vars_delta;

  Lit *old_lits = lits;
  Rnk *old_rnks = rnks;
  Var *old_vars = vars;

  RESIZEN (lits, 2 * size_vars, 4 * size_vars);
  RESIZEN (jwh, 2 * size_vars, 4 * size_vars);
  RESIZEN (wchs, 2 * size_vars, 4 * size_vars);
  RESIZEN (impls, 2 * size_vars, 4 * size_vars);
  RESIZEN (vars, size_vars, 2 * size_vars);
  RESIZEN (rnks, size_vars, 2 * size_vars);

  lits_delta = lits - old_lits;
  rnks_delta = rnks - old_rnks;
  vars_delta = vars - old_vars;

  fix_trail_lits (lits_delta);
  fix_clause_lits (lits_delta);
  fix_added_lits (lits_delta);
  fix_heap_rnks (rnks_delta);
#ifdef NO_BINARY_CLAUSES
  fix_impl_lits (lits_delta);
  fix_impl_reason (lits_delta);
#endif

  assert (mhead == marked);

  size_vars *= 2;
}

static void
unassign (Lit * lit)
{
  Cls *reason;
  Var *v;
  Rnk *r;

  assert (lit->val);

  LOG (fprintf (out, "c unassign %d\n", lit2int (lit)));

  v = LIT2VAR (lit);
  reason = v->reason;

#ifdef NO_BINARY_CLAUSES
  if (ISLITREASON (reason))
    {
      /* DO NOTHING */
    }
  else
#endif
  if (reason)
    {
      assert (reason->locked);
      reason->locked = 0;

      if (reason->learned && reason->size > 2)
	{
	  assert (llocked > 0);
	  llocked--;
	}
    }

  v->assumption = 0;

  lit->val = UNDEF;
  NOTLIT (lit)->val = UNDEF;

  r = VAR2RNK (v);
  if (!r->pos)
    hpush (r);
}

static void
undo (int new_level)
{
  Lit *lit;
  Var *v;

  while (thead > trail)
    {
      lit = *--thead;
      v = LIT2VAR (lit);
      if (v->level == new_level)
	{
	  thead++;		/* fix pre decrement */
	  break;
	}

      unassign (lit);
    }

  level = new_level;
  ttail = thead;
  ttail2 = thead;
  conflict = mtcls;

  LOG (fprintf (out, "c back to level %u\n", level));
}

#ifndef NDEBUG

static int
clause_satisfied (Cls * cls)
{
  Lit **p, **eol, *lit;

  eol = end_of_lits (cls);
  for (p = cls->lits; p < eol; p++)
    {
      lit = *p;
      if (lit->val == TRUE)
	return 1;
    }

  return 0;
}

static void
original_clauses_satisfied (void)
{
  Cls **p, *cls;

  for (p = clauses; p < chead; p++)
    {
      cls = *p;

      if (!cls)
	continue;

      if (cls->learned)
	continue;

      assert (clause_satisfied (cls));
    }
}

static void
assumptions_satisfied (void)
{
  Lit *lit;
  Als *p;

  for (p = als; p < alshead; p++)
    {
      lit = p->lit;
      assert (lit->val == TRUE);
    }
}

#endif

static void
sflush (void)
{
  double now = picosat_time_stamp ();
  double delta = now - entered;
  delta = (delta < 0) ? 0 : delta;
  seconds += delta;
  entered = now;
}

static double
mb (void)
{
  return current_bytes / (double) (1 << 20);
}

static double
avglevel (void)
{
  return decisions ? levelsum / decisions : 0.0;
}

static void
rheader (void)
{
  assert (lastrheader <= reports);

  if (lastrheader == reports)
    return;

  lastrheader = reports;

  fputs ("c\n", out);
  fprintf (out, "c  %s\n", rline[0]);
  fprintf (out, "c  %s\n", rline[1]);
  fputs ("c\n", out);
}

static void
relem (const char *name, int fp, double val)
{
  int x, y, len, size;
  const char *fmt;
  unsigned tmp, e;
  char *p;

  assert (val >= 0);

  if (name)
    {
      if (reports < 0)
	{
	  x = rcount & 1;
	  y = (rcount / 2) * 12 + x * 6;

	  if (rcount == 1)
	    sprintf (rline[1], "%6s", "");

	  len = strlen (name);
	  while (szrline <= len + y + 1)
	    {
	      size = szrline ? 2 * szrline : 128;
	      rline[0] = resize (rline[0], szrline, size);
	      rline[1] = resize (rline[1], szrline, size);
	      szrline = size;
	    }

	  fmt = (len <= 6) ? "%6s%10s" : "%-10s%4s";
	  sprintf (rline[x] + y, fmt, name, "");
	}
      else
	{
	  if (fp && val < 1000 && (tmp = val * 10.0 + 0.5) < 10000)
	    {
	      fprintf (out, "%5.1f ", tmp / 10.0);
	    }
	  else if (!fp && val < 100000 && (tmp = val) < 100000)
	    {
	      fprintf (out, "%5u ", tmp);
	    }
	  else
	    {
	      tmp = val / 10.0 + 0.5;
	      e = 1;

	      while (tmp >= 1000)
		{
		  tmp /= 10;
		  e++;
		}

	      fprintf (out, "%3ue%u ", tmp, e);
	    }
	}

      rcount++;
    }
  else
    {
      if (reports < 0)
	{
	  /* strip trailing white space 
	   */
	  for (x = 0; x <= 1; x++)
	    {
	      p = rline[x] + strlen (rline[x]);
	      while (p-- > rline[x])
		{
		  if (*p != ' ')
		    break;

		  *p = 0;
		}
	    }

	  rheader ();
	}
      else
	fputc ('\n', out);

      rcount = 0;
    }
}

static unsigned
reduce_limit_on_lclauses (void)
{
  unsigned res = lreduce;
  res += llocked;
  res += lfixed;
  return res;
}

static void
report (char type)
{
  int rounds;

  if (!verbose)
    return;

  sflush ();

  if (!reports)
    reports = -1;

  for (rounds = (reports < 0) ? 2 : 1; rounds; rounds--)
    {
      if (reports >= 0)
	fprintf (out, "c %c ", type);

      relem ("seconds", 1, seconds);
      relem ("fixed", 0, fixed);
      relem ("level", 1, avglevel ());
      relem ("conflicts", 0, conflicts);
      relem ("decisions", 0, decisions);
      relem ("conf/dec", 1, PERCENT(conflicts,decisions));
      relem ("limit", 0, reduce_limit_on_lclauses ());
      relem ("learned", 0, lclauses);
      relem ("used", 1, PERCENT (lused, ladded));
      relem ("original", 0, oclauses);
      relem ("used", 1, PERCENT (oused, oadded));
      relem ("MB", 1, mb ());

      relem (0, 0, 0);

      reports++;
    }

  /* Adapt this to the number of rows in your terminal.
   */
  #define ROWS 25

  if (reports % (ROWS - 3) == (ROWS - 4))
    rheader ();

  fflush (out);
}

static int
bcp_queue_is_empty (void)
{
  return ttail == thead && ttail2 == thead;
}

static int
satisfied (void)
{
  assert (!mtcls);
  assert (!failed_assumption);
  assert (!conflict);
  assert (bcp_queue_is_empty ());
  return thead == trail + max_var;	/* all assigned */
}

static unsigned
rng (void)
{
  unsigned res = srng;
  srng *= 1664525u;
  srng += 1013904223u;
  NOLOG (fprintf (out, "c rng () = %u\n", res));
  return res;
}

static unsigned
rrng (unsigned low, unsigned high)
{
  unsigned long long tmp;
  unsigned res, elements;
  assert (low <= high);
  elements = high - low + 1;
  tmp = rng ();
  tmp *= elements;
  tmp >>= 32;
  tmp += low;
  res = tmp;
  NOLOG (fprintf (out, "c rrng (%u, %u) = %u\n", low, high, res));
  assert (low <= res);
  assert (res <= high);
  return res;
}

static Lit *
decide_phase (Lit * lit)
{
  Lit * not_lit = NOTLIT (lit);
  Var *v = LIT2VAR (lit);

  assert (LIT2SGN (lit) > 0);
  if (!v->validposlastphase)
    {
      if (*LIT2JWH(lit) <= *LIT2JWH (not_lit))
	lit = not_lit;
    }
  else if (!v->poslastphase)
    lit = not_lit;

  return lit;
}

static unsigned
gcd (unsigned a, unsigned b)
{
  unsigned tmp;

  assert (a);
  assert (b);

  if (a < b)
    {
      tmp = a;
      a = b;
      b = tmp;
    }

  while (b)
    {
      assert (a >= b);
      tmp = b;
      b = a % b;
      a = tmp;
    }
  
  return a;
}

static Lit *
rdecide (void)
{
  unsigned spread, idx, delta;
  Lit *res;

  /* Increase spread linearly with the length of the outer restart interval.
   * This implies that the number of random decisions goes down as longer
   * the interval gets.
   */
  spread = drestart / 20;

  assert (MINSPREAD < MAXSPREAD);

  if (spread < MINSPREAD)
    spread = MINSPREAD;

  if (spread > MAXSPREAD)
    spread = MAXSPREAD;

  assert (spread > 1);
  if (rrng (1, spread) != 2)	/*  1 out of 'spread' cases */
    return 0;

  assert (1 <= max_var);
  idx = rrng (1, max_var);
  res = int2lit (idx);

  if (res->val != UNDEF)
    {
      delta = rrng (1, max_var);
      while (gcd (delta, max_var) != 1)
	delta--;

      assert (1 <= delta);
      assert (delta <= max_var);

      do {
	idx += delta;
	if (idx > max_var)
	  idx -= max_var;
	res = int2lit (idx);
      } while (res->val != UNDEF);
    }

  rdecisions++;
  res = decide_phase (res);
  LOG (fprintf (out, "c rdecide %d\n", lit2int (res)));

  return res;
}

static Lit *
sdecide (void)
{
  Lit *res;
  Rnk *r;

  do
    {
      r = hpop ();
      NOLOG (fprintf (out, "c hpop %u %u %u\n", 
                    r - rnks, FLTMANTISSA(r->score), FLTEXPONENT(r->score)));
      res = RNK2LIT (r);
    }
  while (res->val != UNDEF);

  sdecisions++;
  res = decide_phase (res);

  LOG (fprintf (out, "c sdecide %d\n", lit2int (res)));

  return res;
}

static int
adecide (void)
{
  Lit *lit;
  Als *p;

  while (alstail < alshead)
    {
      p = alstail++;
      lit = p->lit;

      if (lit->val == FALSE)
	return 0;

      if (lit->val == TRUE)
	continue;

      p->pos = thead - trail;
      assign_assumption (lit);
      assumptions++;
    }

  return 1;
}

static void
assign_decision (Lit * lit)
{
  Var *v;

  assert (lit->val == UNDEF);
  assert (!satisfied ());
  assert (!conflict);

  level++;
  ABORTIF (level >= MAX_LEVEL, "maximum decision level reached");

  LOG (fprintf (out, "c new level %u\n", level));
  LOG (fprintf (out,
		"c assign %d at level %d <= DECISION\n",
		lit2int (lit), level));

#ifdef STATS
  assignments++;
#endif
  v = LIT2VAR (lit);
  v->level = level;
  v->reason = 0;
  assert (!v->assumption);

  lit->val = TRUE;
  NOTLIT (lit)->val = FALSE;

  tpush (lit);
}

static void
decide (void)
{
  Lit *lit;

  assert (!satisfied ());
  assert (!conflict);

  if (!(lit = rdecide ()))
    lit = sdecide ();

  assert (lit);
  assign_decision (lit);
  levelsum += level;
  decisions++;
}

static void
inc_score (Var * v, Act inc)
{
  Rnk *r = VAR2RNK (v);
  r->score = addflt (r->score, inc);
  if (r->pos)
    hup (r);

  NOLOG (fprintf (out, "c inc score %u %u %u\n", 
		r - rnks, FLTMANTISSA(r->score), FLTEXPONENT(r->score)));
}

static Cls *
var2reason (Var * var)
{
  Cls * res = var->reason;
#ifdef NO_BINARY_CLAUSES
  Lit * this, * other;
  if (ISLITREASON (res))
    {
      this = VAR2LIT (var);
      if (this->val == FALSE)
	this = NOTLIT (this);

      other = REASON2LIT (res);
      assert (other->val == TRUE);
      assert (this->val == TRUE);
      res = set_impl (NOTLIT (other), this);
    }
#endif
  return res;
}

static void
inc_activity (Cls * cls, Act inc)
{
  Act *p;

  if (!cls->learned)
    return;

  if (cls->size <= 2)
    return;

  p = CLS2ACT (cls);
  *p = addflt (*p, inc);
}

static void
analyze (void)
{
  Var *v, *u, *uip, **m, *start, **original, **old, **next;
  unsigned tcount = (unsigned)(thead - trail), open, min_level;
  Lit *this, *other, **p, **q, **eol;
  Cls **r, *c;

  assert (conflict);

  /* 1. Make sure that the three stacks 'marked', 'added' and 'resolved'
   * are large enough.  The number of currenlty assigned variables is an
   * upper limit on the number of marked variables, resolved clauses and
   * added literals.
   */
  while (tcount > (unsigned)(eoa - added))
    ENLARGE (added, ahead, eoa);

  /* need to hold 'conflict' as well */
  while (tcount >= (unsigned)(eor - resolved))
    ENLARGE (resolved, rhead, eor);

  while (tcount > (unsigned)(eom - marked))
    ENLARGE (marked, mhead, eom);

  assert (mhead == marked);

  /* 2. Search for First UIP variable and mark all resolved variables.  At
   * the same time determine the minimum decision level involved.   Increase
   * activities of resolved variables.
   */
  q = thead;
  this = 0;
  open = 0;
  min_level = level;

  c = conflict;
  uip = 0;

  for (;;)
    {
      eol = end_of_lits (c);
      for (p = c->lits; p < eol; p++)
	{
	  other = *p;
	  if (other == this)
	    continue;

	  assert (other->val == FALSE);
	  u = LIT2VAR (other);
	  if (u->mark)
	    continue;

	  u->mark = 1;
	  *mhead++ = u;

	  inc_score (u, vinc);

	  if (u->level == level)
	    open++;
	  else if (u->level < min_level)
	    min_level = u->level;
	}

      do
	{
	  if (q == trail)
	    goto DONE_FIRST_UIP;

	  this = *--q;
	  v = LIT2VAR (this);
	}
      while (!v->mark);

      assert (v->level == level);
      c = var2reason (v);
      open--;

      if (!open)
	{
	  uip = v;

	  if (level)
	    break;
	}

      if (!c)
	break;
    }

DONE_FIRST_UIP:
  assert (mhead <= eom);	/* no overflow */

#ifdef STATS
  /* The statistics counter 'nonminimizedllits' sums up the number of
   * literals that would be added if only the 'first UIP' scheme for learned
   * clauses would be used and no clause minimization.
   */
  nonminimizedllits += open + 1;	/* vars on this level (incl. UIP) */
  for (m = marked; m < mhead; m++)
    if ((*m)->level < level)		/* all other cut variables */
      nonminimizedllits++;
#endif

  /* 3. Try to mark more intermediate variables, with the goal to minimize
   * the conflict clause.  This is a BFS from already marked variables
   * backward through the implication graph.  It tries to reach other marked
   * variables.  If the search reaches an unmarked decision variable or a
   * variable assigned below the minimum level of variables in the first uip
   * learned clause, then the variable from which the BFS is started is not
   * redundant.  Otherwise the start variable is redundant and will
   * eventually be removed from the learned clause in step 4.
   */
  original = mhead;
  for (m = marked; m < original; m++)
    {
      start = *m;
      assert (start->mark);

      if (start == uip)
	continue;

      if (!start->reason)
	continue;

      old = mhead;
      c = var2reason (start);
      eol = end_of_lits (c);
      for (p = c->lits; p < eol; p++)
	{
	  v = LIT2VAR (*p);
	  if (v->mark)			/* avoid overflow */
	    continue;

	  v->mark = 1;
	  *mhead++ = v;
	}

      next = old;
      while (next < mhead)
	{
	  u = *next++;
	  assert (u->mark);

	  c = var2reason (u);
	  if (!c || u->level < min_level)	/* start is non redundant */
	    {
	      while (mhead > old)		/* reset all marked */
		(*--mhead)->mark = 0;

	      break;
	    }

	  eol = end_of_lits (c);
	  for (p = c->lits; p < eol; p++)
	    {
	      v = LIT2VAR (*p);
	      if (v->mark)	/* avoid overflow */
		continue;

	      v->mark = 1;
	      *mhead++ = v;
	    }
	}
    }
  assert (mhead <= eom);	/* no overflow */

  /* 4. Add only non redundant marked variables as literals of new clause.
   */
  assert (ahead == added);
  assert (rhead == resolved);

  *rhead++ = conflict;		/* conflict is first resolved clause */

  for (m = marked; m < mhead; m++)
    {
      v = *m;
      assert (v->mark);

      c = var2reason (v);
      if (c)
	{
	  eol = end_of_lits (c);
	  for (p = c->lits; p < eol; p++)
	    {
	      other = *p;
	      u = LIT2VAR (other);
	      if (!u->mark)	/* 'MARKTEST' */
		goto ADD_LITERAL;
	    }

	  *rhead++ = c;
	  continue;
	}

    ADD_LITERAL:
      this = VAR2LIT (v);
      if (this->val == TRUE)
	this++;			/* actually NOTLIT */

      *ahead++ = this;
#ifdef STATS
      minimizedllits++;
#endif
    }

  assert (ahead <= eoa);
  assert (rhead <= eor);

  /* 5. Reset marked variables.  This loop can not be fused with the
   * preovious loop, since marks are tested at 'MARKTEST'.
   */
  for (m = marked; m < mhead; m++)
    {
      v = *m;
      assert (v->mark);
      v->mark = 0;
    }

  mhead = marked;

  /* 6. Finally count antecedents and increase clause activity.
   */
#ifdef STATS
  antecedents += rhead - resolved;
#endif
  for (r = resolved; r < rhead; r++)
    inc_activity (*r, cinc);
}

/* Propagate assignment of 'this' to 'FALSE' by visiting all binary clauses in
 * which 'this' occurs.
 */
static void
prop2 (Lit * this)
{
#ifdef NO_BINARY_CLAUSES
  Ltk * lstk;
  Lit ** l;
#else
  Cls * cls, ** p;
#ifdef USE_CLAUSE_STACK
  Ctk * cstk;
#else
  Cls * next;
#endif
#endif
  Lit * other;

  assert (!conflict);
  assert (this->val == FALSE);

#ifdef NO_BINARY_CLAUSES
  lstk = LIT2IMPLS (this);
  l = lstk->top;
  while (l != lstk->start)
    {
#ifdef STATS
      /* The counter 'visits' is the number of clauses that are
       * visited during propagations of assignments.
       */
      visits++;

      /* The counter 'traversals' is the number of literals
       * traversed in each visited clause.  If we do not actually
       * have binary clauses, it is kind of arbitrary, whether 
       * we increment this number or not.
       */
      traversals++;
#endif
      other = *--l;

      if (other->val == TRUE)
	{
#ifdef STATS
	  othertrue++;
#endif
	  continue;
	}

      if (other->val == FALSE)
	{
	  assert (!conflict);
	  conflict = set_impl (this, other);
	  break;
	}

      assign (other, LIT2REASON (NOTLIT(this)));/* unit clause */
    }
#else
  /* Traverse all binary clauses with 'this'.  Watches for binary
   * clauses do not have to be modified here.
   */
#ifdef USE_CLAUSE_STACK
  cstk = LIT2IMPLS (this);
  p = cstk->top;
  while (p != cstk->start)
    {
      cls = *--p;
#else
  p = LIT2IMPLS (this);
  for (cls = *p; cls; cls = next)
    {
#endif
#ifdef STATS
      visits++;
      traversals++;
#endif
      assert (!cls->collected);
      assert (cls->size == 2);
      
      other = cls->lits[0];
      if (other == this)
	{
#ifndef USE_CLAUSE_STACK
	  next = cls->next[0];
#endif
	  other = cls->lits[1];
#ifdef STATS
	  traversals++;
#endif
	}
#ifndef USE_CLAUSE_STACK
      else
	next = cls->next[1];
#endif
      if (other->val == TRUE)
	{
#ifdef STATS
	  othertrue++;
#endif
	  continue;
	}

      if (other->val == FALSE)
	{
	  assert (!conflict);
	  conflict = cls;
	  break;
	}

      assign (other, cls);	/* unit clause */
    }
#endif /* !defined(NO_BINARY_CLAUSES) */
}

static void
propl (Lit * this)
{
#ifdef USE_CLAUSE_STACK
  Cls ** p, ** q;
  Ctk * cstk;
#else
  Cls *next, **wch_ptr, **new_lit_wch_ptr, *new_lit_next;
#endif
  Lit **l, *other, *new_lit, **eol;
  unsigned pos;
  Cls *cls;
#if 0
  assert (ttail == ttail2);
#endif
#ifdef USE_CLAUSE_STACK
  cstk = LIT2WCHS (this);
#else
  wch_ptr = LIT2WCHS (this);
#endif
  assert (this->val == FALSE);

  /* Traverse all non binary clauses with 'this'.  Watches are
   * updated as well.
   */
#ifdef USE_CLAUSE_STACK
  p = cstk->top;
  while (p != cstk->start)
    {
      cls = *--p;
#else
  for (cls = *wch_ptr; cls; cls = next)
    {
#endif
#ifdef STATS
      visits++;
      traversals++;
#endif
      assert (!cls->collected);
      assert (cls->size > 0);

      /* With assumptions we need to traverse unit clauses as well.
       */
      if (cls->size == 1)
	{
	  assert (!conflict);
	  conflict = cls;
	  break;
	}

      other = cls->lits[0];
      pos = (other != this);
      if (!pos)
	{
	  other = cls->lits[1];
#ifdef STATS
	  traversals++;
#endif
	}

      assert (other == cls->lits[!pos]);
      assert (this == cls->lits[pos]);

#ifndef USE_CLAUSE_STACK
      next = cls->next[pos];
#endif
      if (other->val == TRUE)
	{
#ifdef STATS
	  othertrue++;
#endif
	KEEP_WATCH_AND_CONTINUE:
#ifndef USE_CLAUSE_STACK
	  wch_ptr = cls->next + pos;
#endif
	  continue;
	}

      l = cls->lits + 1;
      eol = cls->lits + cls->size;

      /* Try to find new watched literal instead of 'this'.  We use
       * 'goto' style programming here in order to be able to break
       * out of the outer loop from within the following loop, e.g.
       * with the following 'break' statement.
       */
    FIND_NEW_WATCHED_LITERAL:

      if (++l == eol)
	{
	  if (other->val == FALSE)	/* found conflict */
	    {
	      assert (!conflict);
	      conflict = cls;
	      break;			/* leave 'for' loop */
	    }

	  assign (other, cls);		/* unit clause */
	  goto KEEP_WATCH_AND_CONTINUE;
	}

#ifdef STATS
      traversals++;
#endif
      new_lit = *l;
      if (new_lit->val == FALSE)
	goto FIND_NEW_WATCHED_LITERAL;

      assert (new_lit->val == TRUE || new_lit->val == UNDEF);

      /* Swap new watched literal with previously watched 'this'.
       */
      cls->lits[pos] = new_lit;
      *l = this;

#ifdef USE_CLAUSE_STACK
      /* Remove 'cls' clause from watched list for 'this'.
       */
      *p = 0;

      /* Watch 'cls' for newly watched literal 'new_lit'.
       */
      if (new_lit->val == TRUE)
	{
	  Var *new_lit_var = LIT2VAR (new_lit);
	  if (!new_lit_var->level && !new_lit_var->assumption)
	    continue;
	}

      wpush (LIT2WCHS (new_lit), cls);
#else
      /* Remove 'cls' clause from watched list for 'this'.
       */
      *wch_ptr = next;

      /* Watch 'cls' for newly watched literal 'new_lit'.
       */
      new_lit_wch_ptr = LIT2WCHS (new_lit);
      new_lit_next = *new_lit_wch_ptr;

      /* Maintain invariant that top level assigned literals
       * assigned to TRUE are not watched unless they are
       * assumptions.
       */
      if (!new_lit_next && new_lit->val == TRUE)
	{
	  Var *new_lit_var = LIT2VAR (new_lit);
	  if (!new_lit_var->level && !new_lit_var->assumption)
	    continue;
	}

      *new_lit_wch_ptr = cls;
      cls->next[pos] = new_lit_next;
#endif
    }
#ifdef USE_CLAUSE_STACK
  q = cstk->start;
  for (p = q; p < cstk->top; p++)
    if ((cls = *p))
      *q++ = cls;
  cstk->top = q;
#endif
}

static void
bcp (void)
{
  assert (!conflict);

  if (mtcls || failed_assumption)
    {
      conflict = 0;
      return;
    }

  while (!conflict)
    {
      if (ttail2 < thead)	/* prioritize implications */
	{
	  propagations++;
	  prop2 (NOTLIT (*ttail2++));
	}
      else 
      if (ttail < thead)	/* unit clauses or clauses with length > 2 */
	propl (NOTLIT (*ttail++));
      else
	break;		/* all assignments propagated, so break */
    }
}

/* This version of 'drive' is independent of the global variable 'level' and
 * thus even works if we resolve ala 'relsat' without driving an assignment.
 */
static unsigned
drive (void)
{
  Var *v, *first, *second;
  Lit **p;

  first = 0;
  for (p = added; p < ahead; p++)
    {
      v = LIT2VAR (*p);
      if (!first || v->level > first->level)
	first = v;
    }

  if (!first)
    return 0;

  second = 0;
  for (p = added; p < ahead; p++)
    {
      v = LIT2VAR (*p);

      if (v->level == first->level)
	continue;

      if (!second || v->level > second->level)
	second = v;
    }

  if (!second)
    return 0;

  return second->level;
}

static void
vrescore (void)
{
  Rnk *p, *eor = rnks + max_var;
  int l = log2flt (vinc);
  Flt factor;

  assert (l >= 0);
  factor = base2flt (1, -l);

  for (p = rnks + 1; p <= eor; p++)
    p->score = mulflt (p->score, factor);

  vinc = mulflt (vinc, factor);
}

static void
crescore (void)
{
  Cls **p, *cls;
  Act *a;
  Flt factor;
  int l = log2flt (cinc);
  assert (l > 0);
  factor = base2flt (1, -l);

  for (p = clauses; p < chead; p++)
    {
      cls = *p;

      if (!cls)
	continue;

      if (cls->collected)
	continue;

      if (!cls->learned)
	continue;

      if (cls->size <= 2)
	continue;

      a = CLS2ACT (cls);
      *a = mulflt (*a, factor);
    }

  cinc = mulflt (cinc, factor);
}

static void
inc_vinc_for_new_var (void)
{
  if (lvinc < vinc)
    vrescore ();

  vinc = addflt (vinc, base2flt (1, 0));
}

static void
new_max_var (void)
{
  Lit *lit;
  Rnk *r;
  Var *v;

  max_var++;			/* new index of variable */
  assert (max_var);		/* no unsigned overflow */

  if (max_var == size_vars)
    enlarge ();

  lit = lits + 2 * max_var;
  lit[0].val = lit[1].val = UNDEF;

  memset (wchs + 2 * max_var, 0, 2 * sizeof (wchs[0]) );
  memset (impls + 2 * max_var, 0, 2 * sizeof (impls[0]) );
  memset (jwh + 2 * max_var, 0, 2 * sizeof (jwh[0]));

  v = vars + max_var;		/* initialize variable components */
  CLRN (v, 1);

  r = rnks + max_var;		/* initialize rank */
  CLRN (r, 1);

  hpush (r);

  inc_vinc_for_new_var ();
  inc_score (v, vinc);
}

static void
backtrack (void)
{
  unsigned new_level;

  conflicts++;
  LOG (fputs ("c conflict ", out);
       dumpclsnl (conflict));

  analyze ();			/* get learned minimized 1st UIP clause */
  new_level = drive ();		/* new decision level */
  undo (new_level);		/* has to happen before adding the clause */

  add_simplified_clause (1);
}

static void
inc_vinc_cinc_at_conflict (void)
{
  if (lvinc < vinc)
    vrescore ();

  if (lcinc < cinc)
    crescore ();

  vinc = mulflt (vinc, fvinc);
  cinc = mulflt (cinc, fcinc);
}

static void
disconnect_clause (Cls * cls)
{
  assert (cls->connected);

  if (cls->size > 2)
    {
      if (cls->learned)
	{
	  assert (lclauses > 0);
	  lclauses--;

	  assert (llits >= cls->size);
	  llits -= cls->size;
	}
      else
	{
	  assert (oclauses > 0);
	  oclauses--;

	  assert (olits >= cls->size);
	  olits -= cls->size;
	}
    }

  cls->connected = 0;
}

static int
clause_is_toplevel_satisfied (Cls * cls)
{
  Lit *lit, **p, **eol = end_of_lits (cls);
  Var *v;

  for (p = cls->lits; p < eol; p++)
    {
      lit = *p;
      if (lit->val == TRUE)
	{
	  v = LIT2VAR (lit);
	  if (!v->level && !v->assumption)
	    return 1;
	}
    }

  return 0;
}

static int
collect_clause (Cls * cls)
{
  assert (!cls->collected);

  cls->collect = 0;
  cls->collected = 1;
  if (cls->fixed)
    {
      assert (lfixed);
      lfixed--;
    }
  disconnect_clause (cls);

#ifdef TRACE
  if (trace && !cls->learned && cls->used)
    return 0;
#endif
  delete_clause (cls);

  return 1;
}

static size_t
collect_clauses (void)
{
  Cls *cls, **p, **q;
  Lit * lit, * eol;
  size_t res;

  res = current_bytes;

  eol = lits + 2 * max_var + 1;
  for (lit = lits + 2; lit <= eol; lit++)
    {
      int i;
      for (i = 0; i <= 1; i++)
	{
#ifdef USE_CLAUSE_STACK
	  Ctk * s;
#else
	  Cls * next;
#endif
	  if (i)
	    {
#ifdef NO_BINARY_CLAUSES
	      Ltk * lstk = LIT2IMPLS (lit);
	      Lit ** r, ** s;
	      r = lstk->start;
	      for (s = r; s < lstk->top; s++)
		{
		  Lit * other = *s;
		  Var *v = LIT2VAR (other);
		  if (v->level || other->val != TRUE)
		    *r++ = other;
		}
	      lstk->top = r;
	      continue;
#else
#ifdef USE_CLAUSE_STACK
	      s = LIT2IMPLS (lit);
#else
	      p = LIT2IMPLS (lit);
#endif
#endif
	    }
	  else
#ifdef USE_CLAUSE_STACK
	    s = LIT2WCHS (lit);
#else
	    p = LIT2WCHS (lit);
#endif
#ifdef USE_CLAUSE_STACK
	  q = s->start;
	  for (p = q; p < s->top; p++)
	    if (!(cls = *p)->collect)
	      *q++ = cls;
	  s->top = q;
#else
	  for (cls = *p; cls; cls = next)
	    {
	      q = cls->next;
	      if (cls->lits[0] != lit)
		q++;

	      next = *q;
	      if (cls->collect)
		*p = next;
	      else
		p = q;
	    }
#endif
	}
    }

  for (p = clauses; p < chead; p++)
    {
      cls = *p;

      if (!cls)
	continue;

      if (!cls->collect)
	continue;

      if (collect_clause (cls))
	*p = 0;
    }

#ifdef TRACE
  if (!trace)
#endif
    {
      q = clauses;
      for (p = q; p < chead; p++)
	if ((cls = *p))
	  *q++ = cls;
      chead = q;
    }

  assert (current_bytes <= res);
  res -= current_bytes;

  return res;
}

static int
need_to_reduce (void)
{
  if (current_bytes >= lbytes)
    return 1;

  if (lastreduceconflicts + dfreduce <= conflicts)
    {
      dfreduce *= FREDUCE;
      dfreduce /= 100;
      freductions++;
      return 1;
    }

  return lclauses >= reduce_limit_on_lclauses ();
}

static void
inc_lreduce (void)
{
  lreduce *= FREDUCE;
  lreduce /= 100;
}

static void
restart (void)
{
  unsigned new_level;
  assert (conflicts >= lrestart);
  restarts++;
  assert (level > 1);
#if 0
  new_level = (drestart >= ddrestart ? 0 : rrng (0, level - 1));
#else
  new_level = 0;
#endif
#if 0
  if (drestart < ddrestart && clauses < chead)
    {
      Cls * c = chead[-1];
      Lit ** p, ** eol = end_of_lits (c), * maxlit = 0;
      Act maxact = 0;
      for (p = c->lits; p < eol; p++)
	{
	  Act tmp = VAR2RNK (LIT2VAR (*p))->score;
	  if (maxlit && maxact > tmp)
	    continue;

	  maxlit = *p;
	  maxact = tmp;
	}

      if (maxlit)
	{
	  unsigned maxlit_level = LIT2VAR (maxlit)->level;
	  if (maxlit_level < level)
	    new_level = maxlit_level;
	}
    }
#endif
  LOG (fprintf (out, 
                "c restart %u from %u to %u\n", 
		restarts, level, new_level));
  undo (new_level);

  if (clauses < chead)
    {
      Cls * c, ** p = chead;
      while (p > clauses)
	{
	  c = *--p;

	  if (!c || c->collected)
	    continue;

	  if (c->fixed)
	    break;

	  if (!c->learned || c->size <= 2)
	    continue;

	  lfixed++;
	  c->fixed = 1;
	  break;
	}
    }

  drestart *= FRESTART;
  drestart /= 100;
  if (drestart >= ddrestart)
    {
      ddrestart *= FRESTART;
      ddrestart /= 100;
      if (ddrestart > MAXRESTART)
	ddrestart = MAXRESTART;

      drestart = MINRESTART;

      if (lastreductionsatrestart < reductions)
	{
	  lastreductionsatrestart = reductions;
	  inc_lreduce ();
	}
    }
  assert (drestart <= MAXRESTART);
  lrestart = conflicts + drestart;
  assert (lrestart > conflicts);
  report ('r');
}

static void
simplify (void)
{
  unsigned collect;
  Cls **p, *cls;

  assert (!mtcls);
  assert (!failed_assumption);
  assert (!satisfied ());
  assert (lsimplify <= propagations);
  assert (fsimplify <= fixed);

  collect = 0;
  for (p = clauses; p < chead; p++)
    {
      cls = *p;
      if (!cls)
	continue;

      assert (!cls->collect);
      if (cls->collected)
	continue;

      if (cls->fixed)
	continue;

      if (cls->locked)
	continue;

      if (clause_is_toplevel_satisfied (cls))
	{
	  cls->collect = 1;
	  collect++;
	}
    }

  if (collect)
    srecycled += collect_clauses ();

  report ('s');

  lsimplify = propagations + 10 * (olits + llits);
  fsimplify = fixed;
  simps++;
}

static void
iteration (void)
{
  assert (!level);
  assert (bcp_queue_is_empty ());
  iterations++;
  report ('i');
  lrestart = conflicts + drestart;
}

static int
cmp_activity (const void *p, const void *q)
{
  Cls *c = *(Cls **) p;
  Cls *d = *(Cls **) q;
  Act a;
  Act b;

  assert (c->learned);
  assert (d->learned);

  a = *CLS2ACT (c);
  b = *CLS2ACT (d);

  if (a < b)
    return -1;

  if (b < a)
    return 1;

  /* Prefer shorter clauses.
   */
  if (c->size < d->size)
    return 1;

  if (c->size > d->size)
    return -1;

  return 0;
}

static void
reduce (void)
{
  unsigned rcount, collect, lcollect, count, used, i;
  int log2_target, target;
  Act min_cls_activity;
  Cls **p, *cls;

  report ('l');
  lastreduceconflicts = conflicts;

  assert (rhead == resolved);
  while (lclauses - llocked > (unsigned)(eor - resolved))
    ENLARGE (resolved, rhead, eor);

  lcollect = collect = 0;

  for (p = clauses; p < chead; p++)
    {
      cls = *p;
      if (!cls)
	continue;

      assert (!cls->collect);
      if (cls->collected)
	continue;

      if (cls->fixed)
        continue;

      if (cls->locked)
	continue;

      if (fsimplify < fixed && clause_is_toplevel_satisfied (cls))
	{
	  cls->collect = 1;
	  collect++;

	  if (cls->learned && cls->size > 2)
	    lcollect++;
	}
      else
	{
	  if (!cls->learned)
	    continue;

	  if (cls->size <= 2)
	    continue;

	  *rhead++ = cls;
	}
    }
  assert (rhead <= eor);

  fsimplify = fixed;

  rcount = rhead - resolved;
  qsort (resolved, rcount, sizeof (resolved[0]), cmp_activity);

  assert (lclauses >= lcollect);
  i = (lclauses - lcollect) / 2;

  target = lclauses - lcollect + 1;

  for (log2_target = 1; (1 << log2_target) < target; log2_target++)
    ;

  min_cls_activity = mulflt (cinc, base2flt (1, -log2_target));

  if (i + 1 >= rcount)
    {
      /* Do not update 'rhead' and collect all remaining learned clauses. */
    }
  else if (*CLS2ACT (resolved[i]) < min_cls_activity)
    {
      /* If the distribution of clause activities is skewed and the median
       * is actually below the maximum average activity, then we collect all
       * clauses below this activity.
       */
      while (++i < rcount && *CLS2ACT (resolved[i]) < min_cls_activity)
	;

      rhead = resolved + i;
    }
  else
    {
      /* Since 'qsort' is not stable, we need to find a position close to
       * the target position 'i' where the comparison function makes a
       * differences.
       */
      assert (i < rcount - 1);
      if (cmp_activity (resolved + i, resolved + i + 1) < 0)
	{
	  rhead = resolved + i + 1;
	}
      else
	{
	  while (i > 0 && !cmp_activity (resolved + i - 1, resolved + i))
	    i--;

	  rhead = resolved + i;
	}
    }

  count = used = 0;
  while (rhead > resolved)
    {
      cls = *--rhead;
      assert (!cls->locked);
      assert (!cls->collect);
      cls->collect = 1;
      collect++;
      count++;
      if (cls->used)
	used++;
    }

  if (collect)
    {
      reductions++;
      rrecycled += collect_clauses ();
      report ('-');

      LOG (fprintf (out,
               "c reduced %u used clauses out of %u collected clauses "
	       "(%.1f%%)\n", 
               used, count,
	       PERCENT (used, count)));
    }

  if (!collect)
    inc_lreduce ();

  assert (rhead == resolved);
}

static void
init_restart (void)
{
  ddrestart = MINRESTART;
  drestart = MINRESTART;
  lrestart = conflicts + drestart;
}

static void
init_reduce (void)
{
  lreduce = oclauses / 4;
  if (lreduce < 1000)
    lreduce = 1000;

  if (verbose)
    fprintf (out, "c\nc initial reduction limit %u clauses\nc\n", lreduce);
}

static void
init_forced_reduce (void)
{
  dfreduce = IFREDUCE;
}

static void
find_failed_assumption (void)
{
  Lit *lit;
  Als *p;

  assert (!failed_assumption);
  for (p = als; !failed_assumption && p < alshead; p++)
    {
      lit = p->lit;
      if (lit->val == FALSE)
	failed_assumption = lit;
    }

  LOG (if (failed_assumption)
       fprintf (out, "c failed assumption %d\n",
                lit2int (failed_assumption)));
}

static int
sat (int l)
{
  int count = 0, backtracked;

  if (conflict)
    backtrack ();

  if (mtcls || failed_assumption)
    return PICOSAT_UNSATISFIABLE;

  if (l < 0)
    l = INT_MAX;

  init_restart ();
  init_forced_reduce ();
  fsimplify = 0;
  backtracked = 0;

  for (;;)
    {
      if (!conflict)
	bcp ();

      if (conflict)
	{
	  backtrack ();
	  if (mtcls || failed_assumption)
	    return PICOSAT_UNSATISFIABLE;

	  inc_vinc_cinc_at_conflict ();
	  backtracked = 1;
	  continue;
	}

      if (satisfied ())
	{
	  if (als < alshead)
	    {
	      find_failed_assumption ();

	      if (failed_assumption)
		return PICOSAT_UNSATISFIABLE;
	    }
#ifndef NDEBUG
	  original_clauses_satisfied ();
	  assumptions_satisfied ();
#endif
	  return PICOSAT_SATISFIABLE;
	}

      if (alstail < alshead)
	{
	  if (!adecide ())
	    {
	      find_failed_assumption ();
	      assert (failed_assumption);
	      return PICOSAT_UNSATISFIABLE;
	    }

	  continue;
	}

      if (backtracked)
	{
	  backtracked = 0;
	  if (!level)
	    iteration ();
	}

      if (count >= l)		/* decision limit reached ? */
	return PICOSAT_UNKNOWN;

      if (fsimplify < fixed && lsimplify <= propagations)
	{
	  simplify ();
	  if (!bcp_queue_is_empty ())
	    continue;
	}

      if (!lreduce)
	init_reduce ();

      if (need_to_reduce ())
	reduce ();

      if (conflicts >= lrestart && level > 2)
	restart ();

      decide ();
      count++;
    }
}

#ifdef TRACE

static unsigned
core (void)
{
  unsigned idx, prev, this, i, lcore, vcore;
  unsigned *stack, *shead, *eos;
  Lit **q, **eol;
  Znt *p, byte;
  Zhn *zhain;
  Cls *cls;
  Var *v;

  assert (trace);
  assert (mtcls || failed_assumption);

  if (ocore >= 0)
    return ocore;

  lcore = ocore = vcore = 0;

  stack = shead = eos = 0;
  ENLARGE (stack, shead, eos);

  if (mtcls)
    {
      idx = CLS2IDX (mtcls);
      *shead++ = idx;
    }
  else
    {
      assert (failed_assumption);
      v = LIT2VAR (failed_assumption);
      if (v->reason)
	{
	  idx = CLS2IDX (v->reason);
	  *shead++ = idx;
	}
    }

  while (shead > stack)
    {
      idx = *--shead;
      zhain = zhains[idx];

      if (zhain)
	{
	  if (zhain->core)
	    continue;

	  zhain->core = 1;
	  lcore++;

	  i = 0;
	  this = 0;
	  prev = idx;
	  for (p = zhain->znt; (byte = *p); p++, i += 7)
	    {
	      this |= (byte & 0x7f) << i;
	      if (byte & 0x80)
		continue;

	      assert (prev > this);
	      this = prev - this;

	      if (shead == eos)
		ENLARGE (stack, shead, eos);
	      *shead++ = this;

	      prev = this;
	      this = 0;
	      i = -7;
	    }
	}
      else
	{
	  cls = clauses[idx];

	  assert (cls);
	  assert (!cls->learned);

	  if (cls->core)
	    continue;

	  cls->core = 1;
	  ocore++;

	  eol = end_of_lits (cls);
	  for (q = cls->lits; q < eol; q++)
	    {
	      v = LIT2VAR (*q);
	      if (v->core)
		continue;

	      v->core = 1;
	      vcore++;
	    }
	}
    }

  DELETEN (stack, eos - stack);

  if (verbose)
    fprintf (out,
	     "c %u core variables out of %u (%.1f%%)\n"
	     "c %u core original clauses out of %u (%.1f%%)\n"
	     "c %u core learned clauses out of %u (%.1f%%)\n",
	     vcore, max_var, PERCENT (vcore, max_var),
	     ocore, oadded, PERCENT (ocore, oadded),
	     lcore, ladded, PERCENT (lcore, ladded));

  return ocore;
}

static void
trace_zhain (unsigned idx, Zhn * zhain, FILE * file)
{
  unsigned prev, this, i;
  Znt *p, byte;

  assert (zhain);
  assert (zhain->core);

  fprintf (file, "%u * ", idx);

  i = 0;
  this = 0;
  prev = idx;

  for (p = zhain->znt; (byte = *p); p++, i += 7)
    {
      this |= (byte & 0x7f) << i;
      if (byte & 0x80)
	continue;

      assert (prev > this);
      this = prev - this;

      fprintf (file, "%u ", this);

      prev = this;
      this = 0;
      i = -7;
    }

  fputs ("0\n", file);
}

static void
trace_clause (unsigned idx, Cls * cls, FILE * file)
{
  Lit **p, **eol = end_of_lits (cls);

  assert (cls);
  assert (cls->core);
  assert (!cls->learned);
  assert (CLS2IDX (cls) == idx);

  fprintf (file, "%u ", idx);
  for (p = cls->lits; p < eol; p++)
    fprintf (file, "%d ", LIT2INT (*p));
  fputs ("0 0\n", file);
}

static void
write_trace (FILE * file)
{
  unsigned i, count = chead - clauses;
  Zhn *zhain;
  Cls *cls;

  core ();

  for (i = 0; i < count; i++)
    {
      cls = clauses[i];
      zhain = zhains[i];

      if (zhain)
	{
	  if (zhain->core)
	    trace_zhain (i, zhain, file);
	}
      else if (cls)
	{
	  if (cls->core)
	    trace_clause (i, cls, file);
	}
    }
}

static void
write_core (FILE * file)
{
  Lit **q, **eol;
  Cls **p, *cls;

  fprintf (file, "p cnf %u %u\n", max_var, core ());

  for (p = clauses; p < chead; p++)
    {
      cls = *p;

      if (!cls || cls->learned || !cls->core)
	continue;

      eol = end_of_lits (cls);
      for (q = cls->lits; q < eol; q++)
	fprintf (file, "%d ", LIT2INT (*q));

      fputs ("0\n", file);
    }
}

#endif

static Lit *
import_lit (int lit)
{
  ABORTIF (lit == INT_MIN, "INT_MIN literal");

  while (abs (lit) > max_var)
    new_max_var ();

  return int2lit (lit);
}

static void
remove_assumptions_from_trail (Lit ** start)
{
  Lit **p, **q, *lit;
  Var *v;

  assert (!level);
  assert (trail <= start);

  q = start;
  for (p = start; p < thead; p++)
    {
      lit = *p;
      v = LIT2VAR (lit);
      assert (!v->level);

      if (v->assumption)
	unassign (lit);
      else
	*q++ = lit;
    }

  LOG (fprintf (out, "c REMOVED %d assumptions\n", thead - q));

  thead = q;
  ttail = thead;
  ttail2 = thead;
}

static void
reset_incremental_usage (void)
{
  unsigned num_non_false;
  Lit * lit, ** q;
  Als *p;

  if (!assignments_and_failed_assumption_valid)
    return;

  LOG (fprintf (out, "c RESET incremental usage\n"));

  if (level)
    undo (0);

  if (als < alstail)
    {
      for (p = als; p < alstail && p->pos < 0; p++)
	;

      if (p < alstail)
	remove_assumptions_from_trail (trail + p->pos);
    }

  alstail = alshead = als;
  failed_assumption = 0;

  if (conflict)
    { 
      num_non_false = 0;
      for (q = conflict->lits; q < end_of_lits (conflict); q++)
	{
	  lit = *q;
	  if (lit->val != FALSE)
	    num_non_false++;
	}

      assert (num_non_false >= 2);
      conflict = 0;
    }

  assignments_and_failed_assumption_valid = 0;
}

static void
enter (void)
{
  entered = picosat_time_stamp ();
}


static void
leave (void)
{
  sflush ();
}

const char *
picosat_copyright (void)
{
  return "(C)opyright 2006-2007 by Armin Biere JKU Linz";
}

const char *
picosat_id (void)
{
  return id;
}

const char *
picosat_version (void)
{
  static char revision[20];
  const char * p;
  char  * q;

  for (p = id; *p && *p != ' '; p++)
    ;

  if (*p)
    {
      for (p++; *p && *p != ' '; p++)
      ;

      if (*p)
	{
	  for (p++; *p && *p != '.'; p++)
	    ;

	  if (*p)
	    {
	      p++;
	      for (q = revision; *p && *p != ' '; p++)
		{
		  if (q - revision + 1 < (long) sizeof (revision))
		    *q++ = *p;
		}

	      assert (q - revision < (long) sizeof (revision));
	      *q++ = 0;
	    }
	}
    }

  return revision;
}

const char *
picosat_config (void)
{
  return PICOSAT_OS "\n" PICOSAT_CC " " PICOSAT_CFLAGS "\n";
}

void
picosat_init (void)
{
  init ();
}

void
picosat_enable_trace_generation (void)
{
#ifdef TRACE
  ABORTIF (addedclauses, "trace generation enabled after adding clauses");
  trace = 1;
#endif
}

void
picosat_set_output (FILE * output_file)
{
  out = output_file;
}

void
picosat_set_seed (unsigned s)
{
  srng = s;
}

void
picosat_enable_verbosity (void)
{
  verbose = 1;
}

void
picosat_reset (void)
{
  reset ();
}

void
picosat_add (int int_lit)
{
  Lit *lit;

  enter ();
  reset_incremental_usage ();

  lit = import_lit (int_lit);

  if (int_lit)
    add_lit (lit);
  else
    {
      simplify_and_add_clause (0);
      if (!conflict)
	bcp ();
    }

  leave ();
}

void
picosat_assume (int int_lit)
{
  Lit *lit;

  reset_incremental_usage ();

  lit = import_lit (int_lit);
  if (alshead == eoals)
    {
      assert (alstail == als);
      ENLARGE (als, alshead, eoals);
      alstail = als;
    }

  alshead->lit = lit;
  alshead->pos = -1;
  alshead++;

  LOG (fprintf (out, "c assume %d\n", lit2int (lit)));
}

int
picosat_sat (int l)
{
  int res;
  char ch;

  calls++;
  LOG (fprintf (out, "c START call %u\n", calls));
  ABORTIF (added < ahead, "added clause not complete");

  enter ();
  reset_incremental_usage ();

  res = sat (l);

  if (verbose)
    {
      switch (res)
	{
	case PICOSAT_UNSATISFIABLE:
	  ch = '0';
	  break;
	case PICOSAT_SATISFIABLE:
	  ch = '1';
	  break;
	default:
	  ch = '?';
	  break;
	}

      report (ch);
      rheader ();
    }

  assignments_and_failed_assumption_valid = 1;

  leave ();
  LOG (fprintf (out, "c END call %u\n", calls));

  return res;
}

int
picosat_deref (int int_lit)
{
  Lit *lit;

  ABORTIF (!assignments_and_failed_assumption_valid, "assignment invalid");

  if (abs (int_lit) > max_var)
    return 0;

  lit = int2lit (int_lit);

  if (lit->val == TRUE)
    return 1;

  if (lit->val == FALSE)
    return -1;

  return 0;
}

void
picosat_trace (FILE * file)
{
  if (!mtcls && !failed_assumption)
    return;

  ABORTIF (!mtcls && failed_assumption, "not implemented");
  ABORTIF (!assignments_and_failed_assumption_valid, "tracing disabled");

#ifdef TRACE
  enter ();
  write_trace (file);
  leave ();
#else
  (void) file;
  ABORT ("compiled without trace support");
#endif
}

void
picosat_core (FILE * file)
{
#ifdef TRACE
  if (!mtcls && !failed_assumption)
    return;

  ABORTIF (!mtcls && failed_assumption, "not implemented");
  ABORTIF (!assignments_and_failed_assumption_valid, "tracing disabled");

  enter ();
  write_core (file);
  leave ();
#else
  (void) file;
  ABORT ("compiled without trace support");
#endif
}

size_t
picosat_max_bytes_allocated (void)
{
  return max_bytes;
}

unsigned
picosat_variables (void)
{
  return max_var;
}

unsigned
picosat_added_original_clauses (void)
{
  return oadded;
}

void
picosat_stats (void)
{
#ifdef STATS
  unsigned redlits;
#endif
  assert (sdecisions + rdecisions == decisions);

  fprintf (out, "c %u iterations, %u restarts", iterations, restarts);
  fprintf (out, "\n");
  fprintf (out,
	   "c recycled %.1f MB in %u reductions (%u forced)\n",
	   rrecycled / (double) (1 << 20), reductions, freductions);
  fprintf (out,
	   "c recycled %.1f MB in %u simplifications\n",
	   srecycled / (double) (1 << 20), simps);
  fprintf (out,
	   "c %u conflicts, %u decisions (%.1f%% random, %u assumptions)\n",
	   conflicts, decisions, PERCENT (rdecisions, decisions), assumptions);

#ifdef STATS
  assert (nonminimizedllits >= minimizedllits);
  redlits = nonminimizedllits - minimizedllits;

  fprintf (out,
	   "c %u learned literals, %u redundant (%.1f%%)\n",
	   llitsadded, redlits, PERCENT (redlits, nonminimizedllits));

  fprintf (out,
	   "c %llu antecedents, %.1f antecedents per clause",
	   antecedents, AVERAGE (antecedents, conflicts));

#ifdef TRACE
  if (trace)
    fprintf (out, " (%.1f bytes/antecedent)", AVERAGE (znts, antecedents));
#endif
  fputc ('\n', out);

  fprintf (out, "c %llu propagations\n", propagations);
  fprintf (out, "c %llu visits (%.2f per propagation)\n",
	   visits,
	   AVERAGE (visits, propagations));
  fprintf (out, "c %llu other true (%.1f%% of visited clauses)\n",
	   othertrue, PERCENT (othertrue, visits));
  fprintf (out, "c %llu traversals (%.2f per visit)\n",
	   traversals, AVERAGE (traversals, visits));

  fprintf (out, "c %llu assignments, %u fixed\n", assignments, fixed);
#else
  fprintf (out, "c %llu propagations\n", propagations);
#endif

  fprintf (out, "c %.2f seconds in library\n", seconds);

  fprintf (out, "c %.2f million propagations per second\n",
	   AVERAGE (propagations / 1e6f, seconds));
#ifdef STATS
  fprintf (out, "c %.2f million visits per second\n",
	   AVERAGE (visits / 1e6f, seconds));
  fprintf (out, "c %.2f million traversals per second\n",
	   AVERAGE (traversals / 1e6f, seconds));
#endif
}

#define HAVE_GETRUSAGE		/* do we have 'getrusage' ? */

#ifdef HAVE_GETRUSAGE
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/unistd.h>
#endif

double
picosat_time_stamp (void)
{
  double res = -1;
#ifdef HAVE_GETRUSAGE		/* BSD/Linux/SysV specific */
  struct rusage u;
  res = 0;
  if (!getrusage (RUSAGE_SELF, &u))
    {
      res += u.ru_utime.tv_sec + 1e-6 * u.ru_utime.tv_usec;
      res += u.ru_stime.tv_sec + 1e-6 * u.ru_stime.tv_usec;
    }
#endif
  return res;
}

double
picosat_seconds (void)
{
  return seconds;
}

void
picosat_print (FILE * file)
{
  Lit **q, **eol;
  Cls **p, *cls;

  fprintf (file, "p cnf %d %u\n", max_var, oclauses + lclauses);

  for (p = clauses; p < chead; p++)
    {
      cls = *p;
      if (!cls)
	continue;

      if (cls->collected)
	continue;

      eol = end_of_lits (cls);
      for (q = cls->lits; q < eol; q++)
	fprintf (file, "%d ", lit2int (*q));

      fputs ("0\n", file);
    }
}
