#pragma once

#ifdef ENVOY_PERFETTO

#include "perfetto.h"

// `Perfetto` is an open-source stack for performance instrumentation and trace
// analysis. In Envoy we use it only as a library for recording app-level
// traces which can be later analyzed online at https://ui.perfetto.dev/ or with
// custom tools.
//
// The support is enabled with
//   bazel --define=perf_tracing=enabled ...
// In the absence of such directives the macros for instrumenting code for
// performance analysis will expand to nothing.
//
// The supported `Perfetto` macros are TRACE_EVENT, TRACE_EVENT_BEGIN,
// TRACE_EVENT_END and TRACE_COUNTER.
//
// See https://perfetto.dev/docs/instrumentation/track-events for more details.
//
// See also, source/common/common/perf_annotation.h adding Performance
// Annotation system which is a better fit for micro-benchmarking and for the
// cases when an opening cross-scoped event (i.e. TRACE_EVENT_BEGIN) can be
// followed by the same opening event, not a closing one (this often happens
// for open-loop benchmarks).

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
