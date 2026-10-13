#pragma once

#ifdef ENVOY_PERFETTO

#include "perfetto.h"

// `Perfetto` is an open-source stack for performance instrumentation and trace
// analysis. In Envoy we use it only as a library for recording app-level
// traces which can be later analyzed online at https://ui.perfetto.dev/ or with
// custom tools.
//
// See https://perfetto.dev/docs/instrumentation/track-events for more details.
//
// The support is enabled with
//   bazel --define=perf_tracing=enabled ...
// In the absence of such directives the macros for instrumenting code for
// performance analysis will expand to nothing.
//
// The supported `Perfetto` macros are TRACE_EVENT, TRACE_COUNTER,
// TRACE_EVENT_BEGIN and TRACE_EVENT_END. Please be careful with the last two:
// all events on a given thread share the same stack. This means that it's not
// recommended to have a matching pair of TRACE_EVENT_BEGIN and TRACE_EVENT_END
// markers in separate functions, since an unrelated event might terminate the
// original event unexpectedly; for events that cross function boundaries it's
// usually best to emit them on a separate track. Unfortunately this may lead to
// excessive number of tracks if they are unique for every pair of emitted
// events. The existing visualization tools may not work well if the number of
// tracks is too big. In this case the resulting trace data needs to be processed
// differently. Alternatively, if you are interested in benchmarking only and don't
// need any tracing capabilities, then you can resort to the Performance Annotation
// system which supports cross-scoped events too, but doesn't require any
// post-processing to get a benchmark's final report.
// See source/common/common/perf_annotation.h for details.

// NOLINT(namespace-envoy)

PERFETTO_DEFINE_CATEGORIES(
    perfetto::Category("core").SetDescription("Events from core modules"),
    perfetto::Category("extensions").SetDescription("Events from extensions"));

#else

// Macros that expand to nothing when performance collection is disabled. These are contrived to
// work syntactically as a C++ statement (e.g. if (foo) TRACE_COUNTER(...) else TRACE_COUNTER(...)).

#define TRACE_EVENT(category, name, ...)                                                           \
  do {                                                                                             \
  } while (false)
#define TRACE_EVENT_BEGIN(category, name, ...)                                                     \
  do {                                                                                             \
  } while (false)
#define TRACE_EVENT_END(category, ...)                                                             \
  do {                                                                                             \
  } while (false)
#define TRACE_COUNTER(category, track, ...)                                                        \
  do {                                                                                             \
  } while (false)

#endif
