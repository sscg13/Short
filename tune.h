#ifndef TUNE_H
#define TUNE_H

/* Single source for search defaults and OpenBench SPSA input. Bounds and
   C_end are starting suggestions, not the result of a strength experiment.
   Q8 LMR values mean base/256 + scale/256 * ln(depth)*ln(move_count). */
#define SEARCH_TUNABLES(X) \
    X(RFP_MARGIN,         100, 50, 200, 8) \
    X(NMP_MARGIN,          60,  0, 200, 10) \
    X(SEE_QUIET_MARGIN,    80,  0, 160, 8) \
    X(SEE_CAPTURE_MARGIN,  20,  0,  80, 4) \
    X(ASP_DELTA,           50, 10, 200, 10) \
    X(LMR_BASE_Q8,        192,  0, 384, 16) \
    X(LMR_SCALE_Q8,       128, 64, 192, 6)

#ifdef TUNE
#define TUNE_DECLARE(name, value, min, max, step) extern i16 name;
SEARCH_TUNABLES(TUNE_DECLARE)
#undef TUNE_DECLARE
void search_tune_features(void);
void search_tune_spsa(void);
/* 0: unknown name, 1: accepted, -1: invalid value (no state changed). */
i16 search_tune_option(const char *name, const char *value);
#else
/* No parameter storage, registry, parsing or option strings in DOS release. */
#define TUNE_DECLARE(name, value, min, max, step) enum { name = value };
SEARCH_TUNABLES(TUNE_DECLARE)
#undef TUNE_DECLARE
#endif

#endif
