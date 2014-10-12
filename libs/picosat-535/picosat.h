#ifndef picosat_h_INCLUDED
#define picosat_h_INCLUDED

#include <stdlib.h>
#include <stdio.h>

#define PICOSAT_UNKNOWN 0
#define PICOSAT_SATISFIABLE 10
#define PICOSAT_UNSATISFIABLE 20

const char *picosat_id (void);
const char *picosat_version (void);
const char *picosat_config (void);
const char *picosat_copyright (void);

void picosat_init (void);
void picosat_reset (void);

void picosat_enable_verbosity (void);
void picosat_enable_trace_generation (void);

void picosat_set_output (FILE *);
void picosat_set_seed (unsigned random_number_generator_seed);

size_t picosat_max_bytes_allocated (void);
unsigned picosat_variables (void);
unsigned picosat_added_original_clauses (void);
void picosat_stats (void);

void picosat_add (int lit);
void picosat_assume (int lit);

int picosat_sat (int decision_limit);

int picosat_deref (int lit);

void picosat_print (FILE *);
void picosat_trace (FILE * trace_file);
void picosat_core (FILE * core_file);

double picosat_seconds (void);
double picosat_time_stamp (void);

#endif
