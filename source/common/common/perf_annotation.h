#pragma once

#ifdef ENVOY_PERF_ANNOTATION

#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>

#include "common/common/utility.h"

#include "absl/strings/string_view.h"

// Performance Annotation system, enabled with
//   bazel --define=perf_annotation=enabled ...
// which defines ENVOY_PERF_ANNOTATION.  In the absense of that flag,
// the support classes are built and tested.  However, the supported
// macros for instrumenting code for performance analysis will expand
// to nothing.


// Defining ENVOY_PERF_ANNOTATION enables collections of named performance
// statistics during the run, which can be dumped when Envoy terminates.
// When not defined, empty class are provided for PerfAnnotationContext
// and PerfOperation, but all methods will inline to nothing. Further, the
// reporting methods should be called via macro, whch will also compile to
// nothing when not enabled.

#define PERF_OPERATION(perf_var) Envoy::PerfOperation(perf_var)
#define PERF_REPORT(perf, category, description)                                                   \
  do {                                                                                             \
    perf.report(category, description);                                                            \
  } while (false)
#define PERF_DUMP() Envoy::PerfAnnotationContext::dump()
#define PERF_TO_STRING() Envoy::PerfAnnotationContext::toString()
#define PERF_CLEAR() Envoy::PerfAnnotationContext::clear()

#define PERF_THREAD_SAFE 1

namespace Envoy {

/**
 * Defines a context for collecting performance data, which compiles but has
 * no cost when ENVOY_PERF_AUTOMATION is not defined.
 */
class PerfAnnotationContext {
public:
  /**
   * Reports time consumed by a category and description, which are just
   * joined together in the library with " / ".
   */
  void report(std::chrono::nanoseconds duration, absl::string_view category,
              absl::string_view description);

  /**
   * Renders the aggregated statistics as a string.
   */
  static std::string toString();

  /**
   * Dumps aggregated statistics (if any) to stdout.
   */
  static void dump();

  /**
   * Thread-safe lazy-initialization of a PerfAnnotationContext on first use.
   */
  static PerfAnnotationContext* getOrCreate();

  /**
   * Clears out all aggregated statistics.
   */
  static void clear();

private:
  /**
   * PerfAnnotationContext construction should be done via getOrCreate().
   */
  PerfAnnotationContext();

  typedef std::pair<std::chrono::nanoseconds, uint64_t> DurationCount;
  typedef std::map<std::string, DurationCount> DurationCountMap;

  DurationCountMap duration_count_map_; // Maps "$category / $description" to the duration.
#if PERF_THREAD_SAFE
  std::mutex mutex_;
#endif
};

/**
 * Represents an operation for reporting timing to the perf system. Usage:
 *
 * f() {
 *   PerfOperation perf_op;
 *   computeIntensiveWork();
 *   PERF_REPORT(perf_op, "category", "description");
 * }
 *
 * When ENVOY_PERF_ANNOTATION is not defined,, these statements will inline-optimize
 * away to nothing.
 */
class PerfOperation {
public:
  PerfOperation();

  /**
   * Report an event relative to the operation in progress. Note report can be called
   * multiple times on a single PerfOperation, with distinct category/description combinations.
   */
  void report(absl::string_view category, absl::string_view description);

private:
  SystemTime start_time_;
  PerfAnnotationContext* context_;
};

} // namespace Envoy

#else

#define PERF_OPERATION(perf_var)                                                                   \
  do {                                                                                             \
  } while (false)
#define PERF_REPORT(perf, category, description)                                                   \
  do {                                                                                             \
  } while (false)
#define PERF_DUMP()                                                                                \
  do {                                                                                             \
  } while (false)
#define PERF_TO_STRING() ""
#define PERF_CLEAR()                                                                               \
  do {                                                                                             \
  } while (false)

#endif
