#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>

#include "absl/strings/string_view.h"
#include "common/common/utility.h"

// Defining ENVOY_PERF_ANNOTATION enables collections of named performance
// statistics during the run, which can be dumped when Envoy terminates.
// When not defined, empty class are provided for PerfAnnotationContext
// and PerfOperation, but all methods will inline to nothing.  Further, the
// reporting methods should be called via macro, whch will also compile to
// nothing when not enabled.
//
//     #define ENVOY_PERF_ANNOTATION
//
// TODO(jmarantz): in a follow-up PR I will integratrate the setting of
// this flag into the build system like ENVOY_HOT_RESTART.

namespace Envoy {

/**
 * Defines a context for collecting performance data, which compiles but has
 * no cost when ENVOY_PERF_AUTOMATION is not defined.
 */
class PerfAnnotationContext {
 public:
#ifdef ENVOY_PERF_ANNOTATION

  /**
   * Reports time consumed by a category and description, which are just
   * joined together in the library with " / ".
   */
  void report(std::chrono::nanoseconds duration, absl::string_view category,
              absl::string_view description);

  /**
   * Dumps aggregated statistics (if any) to stdout.
   */
  static void dump();

  /**
   * Thread-safe lazy-initialization of a PerfAnnotationContext on first use.
   */
  static PerfAnnotationContext* getOrCreate();

 private:
  /**
   * PerfAnnotationContext construction should be done via getOrCreate().
   */
  PerfAnnotationContext();

  typedef std::map<std::string, std::chrono::nanoseconds> DurationMap;

  DurationMap duration_map_;  // Maps "$category / $description" to the duration.
  std::mutex mutex_;

#else

  static void dump() {}

 private:
  PerfAnnotationContext() {}

#endif
};

/**
 * Represents an operation for reporting timing to the perf system.  Usage:
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

#ifdef ENVOY_PERF_ANNOTATION

#define PERF_REPORT(perf, category, description) \
  do { perf.report(category, description); } while (false)

  PerfOperation();

  /**
   * Report an event relative to the operation in progress.  Note report can be called
   * multiple times on a single PerfOperation, with distinct category/description combinations.
   */
   void report(absl::string_view category, absl::string_view description);

 private:
  SystemTime start_time_;
  PerfAnnotationContext* context_;

#else

#define PERF_REPORT(perf, category, description) do { } while (false)

  PerfOperation() {}

#endif
};

}  // namespace Envoy
