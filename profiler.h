#pragma once

#include "base.h"
#include "darray.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct profiler_snapshot profiler_snapshot_t;
typedef struct profiler_snapshot_entry profiler_snapshot_entry_t;
typedef struct profiler_time_entry profiler_time_entry_t;

/* ------------------------------------------------------------------------- */
/* Profiling */

EXPORT void profile_start(const char *name);
EXPORT void profile_end(const char *name);

EXPORT void profile_reenable_thread(void);

/* ------------------------------------------------------------------------- */
/* Profiler control */

/* ------------------------------------------------------------------------- */
/* Profiler name storage */

typedef struct profiler_name_store profiler_name_store_t;

#ifndef _MSC_VER
#define PRINTFATTR(f, a) __attribute__((__format__(__printf__, f, a)))
#else
#define PRINTFATTR(f, a)
#endif

/* ------------------------------------------------------------------------- */
/* Profiler data access */

struct profiler_time_entry {
	uint64_t time_delta;
	uint64_t count;
};

typedef DARRAY(profiler_time_entry_t) profiler_time_entries_t;

EXPORT profiler_snapshot_t *profile_snapshot_create(void);
EXPORT void profile_snapshot_free(profiler_snapshot_t *snap);

#ifdef __cplusplus
}
#endif
