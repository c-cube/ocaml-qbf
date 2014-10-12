#include "picosat.h"

#include <assert.h>
#include <string.h>
#include <ctype.h>

#define GUNZIP "gunzip -c %s"
#define GZIP "gzip -c -f > %s"

static int lineno;
static FILE *input;
static FILE *output;
static int verbose;
static char buffer[100];
static char *bhead = buffer;
static const char *eob = buffer + 80;

static int
next (void)
{
  int res = getc (input);
  if (res == '\n')
    lineno++;
  return res;
}

static const char *
parse (int force)
{
  int ch, sign, lit, vars, clauses;

  lineno = 1;

SKIP_COMMENTS:
  ch = next ();
  if (ch == 'c')
    {
      while ((ch = next ()) != EOF && ch != '\n')
	;
      goto SKIP_COMMENTS;
    }

  if (isspace (ch))
    goto SKIP_COMMENTS;

  if (ch != 'p')
INVALID_HEADER:
    return "missing or invalid 'p cnf <variables> <clauses>' header";

  if (fscanf (input, " cnf %d %d\n", &vars, &clauses) != 2)
    goto INVALID_HEADER;

  if (verbose)
    {
      fprintf (output, "c parsed header 'p cnf %d %d'\n", vars, clauses);
      fflush (output);
    }

  lit = 0;
READ_LITERAL:
  ch = next ();

  if (ch == 'c')
    {
      while ((ch = next ()) != EOF && ch != '\n')
	;
      goto READ_LITERAL;
    }

  if (ch == EOF)
    {
      if (lit)
	return "trailing 0 missing";

      if (clauses && !force)
	return "clause missing";

      return 0;
    }

  if (isspace (ch))
    goto READ_LITERAL;

  sign = 1;
  if (ch == '-')
    {
      sign = -1;
      ch = next ();
    }

  if (!isdigit (ch))
    return "expected number";

  lit = ch - '0';
  while (isdigit (ch = next ()))
    lit = 10 * lit + (ch - '0');

  if (!clauses && !force)
    return "too many clauses";

  if (lit)
    {
      if (lit > vars && !force)
	return "maximal variable index exceeded";

      lit *= sign;
    }
  else
    clauses--;

  picosat_add (lit);

  goto READ_LITERAL;
}

static void
bflush (void)
{
  *bhead = 0;
  fputs (buffer, output);
  fputc ('\n', output);
  bhead = buffer;
}

static void
printi (int i)
{
  char *next;
  int l;

REENTER:
  if (bhead == buffer)
    *bhead++ = 'v';

  l = sprintf (bhead, " %d", i);
  next = bhead + l;

  if (next >= eob)
    {
      bflush ();
      goto REENTER;
    }
  else
    bhead = next;
}

static void
printa (void)
{
  int max_idx = picosat_variables (), i, lit;

  assert (bhead == buffer);

  for (i = 1; i <= max_idx; i++)
    {
      lit = (picosat_deref (i) > 0) ? i : -i;
      printi (lit);
    }

  printi (0);
  if (bhead > buffer)
    bflush ();
}

static int
has_suffix (const char *str, const char *suffix)
{
  const char *tmp = strstr (str, suffix);
  if (!tmp)
    return 0;

  return str + strlen (str) - strlen (suffix) == tmp;
}

static void
write_to_file (const char *name, const char *type, void (*writer) (FILE *))
{
  int pclose_file, zipped = has_suffix (name, ".gz");
  FILE *file;
  char *cmd;


  if (zipped)
    {
      cmd = malloc (strlen (GZIP) + strlen (name));
      sprintf (cmd, GZIP, name);
      file = popen (cmd, "w");
      free (cmd);
      pclose_file = 1;
    }
  else
    {
      file = fopen (name, "w");
      pclose_file = 0;
    }

  if (file)
    {
      if (verbose)
	fprintf (output,
		 "c\nc writing %s%s to '%s'\n",
		 zipped ? "gzipped " : "", type, name);

      writer (file);

      if (pclose_file)
	pclose (file);
      else
	fclose (file);
    }
  else
    fprintf (output, "*** picosat: can not write to '%s'\n", name);
}

#define USAGE \
"usage: picosat [ <option> ... ] [ <input> ]\n" \
"\n" \
"where <option> is one of the following\n" \
"\n" \
"  -h           print this command line option summary and exit\n" \
"  --version    print version and exit\n" \
"  --config     print build configuration and exit\n" \
"\n" \
"  -v           enable verbose output\n" \
"  -f           ignore invalid header\n" \
"  -n           do not print satisfying assignment\n" \
"  -a <lit>     start with an assumption\n" \
"  -l <limit>   set decision limit\n" \
"  -s <seed>    set random number generator seed\n" \
"  -o <output>  set output file\n" \
"  -t <trace>   generate proof trace file\n" \
"  -c <core>    generate core clauses file\n" \
"\n" \
"and <input> is an optional input file in DIMACS format.\n"

int
picosat_main (int argc, char **argv)
{
  const char *input_name, *output_name, *trace_name, *core_name;
  int res, done, err, print_satisfying_assignment, force;
  int close_input, pclose_input;
  int assumption, assumptions;
  int i, decision_limit;
  double start_time;
  unsigned seed;
  FILE *file;

  start_time = picosat_time_stamp ();

  output_name = 0;
  trace_name = 0;
  core_name = 0;
  close_input = 0;
  pclose_input = 0;
  input_name = "<stdin>";
  input = stdin;
  output = stdout;
  verbose = done = err = 0;
  decision_limit = -1;
  seed = 0;
  assumptions = 0;
  force = 0;

  print_satisfying_assignment = 1;

  for (i = 1; !done && !err && i < argc; i++)
    {
      if (!strcmp (argv[i], "-h"))
	{
	  fputs (USAGE, output);
	  done = 1;
	}
      else if (!strcmp (argv[i], "--version"))
	{
	  fprintf (output, "%s\n", picosat_version ());
	  done = 1;
	}
      else if (!strcmp (argv[i], "--config"))
	{
	  fprintf (output, "%s", picosat_config ());
	  done = 1;
	}
      else if (!strcmp (argv[i], "-v"))
	{
	  verbose = 1;
	}
      else if (!strcmp (argv[i], "-f"))
	{
	  force = 1;
	}
      else if (!strcmp (argv[i], "-n"))
	{
	  print_satisfying_assignment = 0;
	}
      else if (!strcmp (argv[i], "-l"))
	{
	  if (++i == argc)
	    {
	      fprintf (output, "*** picosat: argument to '-l' missing\n");
	      err = 1;
	    }
	  else
	    decision_limit = atoi (argv[i]);
	}
      else if (!strcmp (argv[i], "-a"))
	{
	  if (++i == argc)
	    {
	      fprintf (output, "*** picosat: argument to '-a' missing\n");
	      err = 1;
	    }
	  else if (!atoi (argv[i]))
	    {
	      fprintf (output, "*** picosat: argument to '-a' zero\n");
	      err = 1;
	    }
	  else
	    {
	      /* Handle assumptions further down
	       */
	      assumptions++;
	    }
	}
      else if (!strcmp (argv[i], "-s"))
	{
	  if (++i == argc)
	    {
	      fprintf (output, "*** picosat: argument to '-s' missing\n");
	      err = 1;
	    }
	  else
	    seed = atoi (argv[i]);
	}
      else if (!strcmp (argv[i], "-o"))
	{
	  if (output_name)
	    {
	      fprintf (output,
		       "*** picosat: "
		       "multiple output files '%s' and '%s'\n",
		       output_name, argv[i]);
	      err = 1;
	    }
	  else if (++i == argc)
	    {
	      fprintf (output, "*** picosat: argument ot '-o' missing\n");
	      err = 1;
	    }
	  else if (!(file = fopen (argv[i], "w")))
	    {
	      fprintf (output,
		       "*** picosat: "
		       "can not write output file '%s'\n", argv[i]);
	      err = 1;
	    }
	  else
	    {
	      output_name = argv[i];
	      output = file;
	    }
	}
      else if (!strcmp (argv[i], "-t"))
	{
	  if (trace_name)
	    {
	      fprintf (output,
		       "*** picosat: "
		       "multiple trace files '%s' and '%s'\n",
		       trace_name, argv[i]);
	      err = 1;
	    }
	  else if (++i == argc)
	    {
	      fprintf (output, "*** picosat: argument ot '-t' missing\n");
	      err = 1;
	    }
	  else
	    trace_name = argv[i];
	}
      else if (!strcmp (argv[i], "-c"))
	{
	  if (core_name)
	    {
	      fprintf (output,
		       "*** picosat: "
		       "multiple core files '%s' and '%s'\n",
		       core_name, argv[i]);
	      err = 1;
	    }
	  else if (++i == argc)
	    {
	      fprintf (output, "*** picosat: argument ot '-c' missing\n");
	      err = 1;
	    }
	  else
	    core_name = argv[i];
	}
      else if (argv[i][0] == '-')
	{
	  fprintf (output,
		   "*** picosat: "
		   "unknown command line option '%s' (try '-h')\n", argv[i]);
	  err = 1;
	}
      else if (close_input || pclose_input)
	{
	  fprintf (output,
		   "*** picosat: "
		   "multiple input files '%s' and '%s'\n",
		   input_name, argv[i]);
	  err = 1;
	}
      else if (has_suffix (argv[i], ".gz"))
	{
	  char *cmd = malloc (strlen (GUNZIP) + strlen (argv[i]));
	  sprintf (cmd, GUNZIP, argv[i]);
	  if ((file = popen (cmd, "r")))
	    {
	      input_name = argv[i];
	      pclose_input = 1;
	      input = file;
	    }
	  else
	    {
	      fprintf (output,
		       "*** picosat: "
		       "can not read compressed input file '%s'\n", argv[i]);
	      err = 1;
	    }
	  free (cmd);
	}
      else if (!(file = fopen (argv[i], "r")))	/* TODO .gz ? */
	{
	  fprintf (output,
		   "*** picosat: can not read input file '%s'\n", argv[i]);
	  err = 1;
	}
      else
	{
	  input_name = argv[i];
	  close_input = 1;
	  input = file;
	}
    }

  res = PICOSAT_UNKNOWN;

  if (!done && !err)
    {
      const char *err_msg;

      if (verbose)
	{
	  fprintf (output,
		   "c PicoSAT SAT Solver Version %s\n"
		   "c %s\n", picosat_version (), picosat_id ());

	  fprintf (output, "c %s\n", picosat_copyright ());
	}

      picosat_init ();
      if (trace_name || core_name)
	picosat_enable_trace_generation ();

      picosat_set_output (output);
      if (verbose)
	picosat_enable_verbosity ();

      if (verbose)
	fprintf (output, "c\nc parsing %s\n", input_name);

      if ((err_msg = parse (force)))
	{
	  fprintf (output, "%s:%d: %s\n", input_name, lineno, err_msg);
	  err = 1;
	}
      else
	{
	  if (verbose)
	    fprintf (output,
		     "c initialized %u variables\n"
		     "c found %u non trivial clauses\n",
		     picosat_variables (), picosat_added_original_clauses ());

	  picosat_set_seed (seed);
	  if (verbose)
	    fprintf (output, "c\nc random number generator seed %u\n", seed);

	  if (assumptions)
	    {
	      for (i = 1; i < argc; i++)
		{
		  if (!strcmp (argv[i], "-l") ||
		      !strcmp (argv[i], "-s") ||
		      !strcmp (argv[i], "-o") ||
		      !strcmp (argv[i], "-t") || !strcmp (argv[i], "-c"))
		    {
		      i++;
		    }
		  else if (!strcmp (argv[i], "-a"))
		    {
		      assert (i + 1 < argc);
		      assumption = atoi (argv[++i]);
		      assert (assumption);

		      picosat_assume (assumption);

		      if (verbose)
			fprintf (output, "c assumption %d\n", assumption);
		    }
		}
	    }

	  res = picosat_sat (decision_limit);

	  if (res == PICOSAT_UNSATISFIABLE)
	    {
	      fputs ("s UNSATISFIABLE\n", output);

	      if (trace_name)
		write_to_file (trace_name, "trace", picosat_trace);

	      if (core_name)
		write_to_file (core_name, "core", picosat_core);
	    }
	  else if (res == PICOSAT_SATISFIABLE)
	    {
	      fputs ("s SATISFIABLE\n", output);

	      if (print_satisfying_assignment)
		printa ();
	    }
	  else
	    fputs ("s UNKNOWN\n", output);
	}

      if (!err && verbose)
	{
	  fputs ("c\n", output);
	  picosat_stats ();
	  fputs ("c\n", output);
	  fprintf (output,
		   "c %.2f seconds, %.1f MB maximally allocated\n",
		   picosat_time_stamp () - start_time,
		   picosat_max_bytes_allocated () / (double) (1 << 20));
	}

      picosat_reset ();
    }

  if (close_input)
    fclose (input);

  if (pclose_input)
    pclose (input);

  if (output_name)
    fclose (output);

  return res;
}
