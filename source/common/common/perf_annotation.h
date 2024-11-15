#pragma once

#ifdef ENVOY_PERF_ANNOTATION

#include <chrono>
#include <cstdint>

#include "source/common/common/thread.h"
#include "source/common/common/utility.h"

#include "absl/container/node_hash_map.h"
#include "absl/strings/string_view.h"

// Performance Annotation system, enabled with
//   bazel --define=perf_annotation=enabled ...
// or, in individual .cc files:
//   #define ENVOY_PERF_ANNOTATION
// In the absence of such directives, the support classes are built and tested.
// However, the macros for instrumenting code for performance analysis will expand
// to nothing.
//
// See also: https://github.com/LLNL/Caliper -- it may be worth integrating with
// that for added functionality, particularly around loops.
//
// See also, for a much more comprehensive study in performance annotation:
// https://labs.vmware.com/vmtj/methodology-for-performance-analysis-of-vmware-vsphere-under-tier-1-applications
// https://dl.acm.org/citation.cfm?id=1899945&dl=ACM&coll=DL
//
// See also, source/common/common/perf_tracing.h adding support for "Perfetto" -
// an open-source stack for performance instrumentation and trace analysis.
// It can give a better insight into execution flows and ordering of events.

/**
 * Initiates a performance operation, storing its state in perf_var. A perf_var
 * can then be reported multiple times.
 */
#define PERF_OPERATION(perf_var) Envoy::PerfOperation perf_var

/**
 * Records performance data initiated with PERF_OPERATION. The category and description
 * are joined with in the library, but only if perf is enabled. This way, any concatenation
 * overhead is skipped when perf-annotation is disabled.
 */
#define PERF_RECORD(perf, category, description)                                                   \
  do {                                                                                             \
    perf.record(category, description);                                                            \
  } while (false)

/**
 * Declares a storage member for a performance operation inside an owner class.
 * A perf_var can then be instantiated in one of the owner's methods with
 * PERF_OWNED_OPERATION(perf_var) and reported multiple times in any method with
 * PERF_OWNED_RECORD(perf_var, category, description).
 */
#define PERF_OWNER(perf_var) std::unique_ptr<Envoy::PerfOperation> perf_var

/**
 * Instantiates a performance operation, initiates and stores it in the owner annotated with
 * PERF_OWNER.
 */
#define PERF_OWNED_OPERATION(perf_var)                                                             \
  do {                                                                                             \
    perf_var = std::make_unique<Envoy::PerfOperation>();                                           \
  } while (false)

/**
 * Records performance data initiated with PERF_OWNED_OPERATION. The category
 * and descriptions have the same meaning as in PERF_RECORD.
 */
#define PERF_OWNED_RECORD(perf_var, category, description)                                         \
  do {                                                                                             \
    perf_var->record(category, description);                                                       \
  } while (false)

/**
 * Dumps recorded performance data to stdout. Expands to nothing if not enabled.
 */
#define PERF_DUMP() Envoy::PerfAnnotationContext::dump()

/**
 * Returns the aggregated performance data as a formatted multi-line string, showing a
 * formatted table of values. Returns "" if perf-annotation is disabled.
 */
#define PERF_TO_STRING() Envoy::PerfAnnotationContext::toString()

/**
 * Clears all performance data.
 */
#define PERF_CLEAR() Envoy::PerfAnnotationContext::clear()

/**
 * Controls whether performances collection and reporting is thread safe. For now,
 * leaving this enabled for predictability across multiple applications, on the assumption
 * that an uncontended mutex lock has vanishingly small cost. In the future we may try
 * to make this system thread-unsafe if mutex contention disturbs the metrics.
 */
#define PERF_THREAD_SAFE true

namespace Envoy {

/**
 * Defines a context for collecting performance data. Note that this class is
 * fully declared and defined even if ENVOY_PERF_AUTOMATION is off. We depend on
 * the macros to disable performance collection for production.
 */
class PerfAnnotationContext {
public:
  /**
   * Records time consumed by a category and description, which are shown as separate
   * columns in the generated output table.
   *
   * @param duration the duration.
   * @param category the name of a category for the recording.
   * @param description the name of description for the recording.
   */
  void record(std::chrono::nanoseconds duration, absl::string_view category,
              absl::string_view description);

  /** @return MonotonicTime the current time */
  MonotonicTime currentTime() { return time_source_.monotonicTime(); }

  /**
   * Renders the aggregated statistics as a string.
   * @return std::string the performance data as a formatted string.
   */
  static std::string toString();

  /**
   * Dumps aggregated statistics (if any) to stdout.
   */
  static void dump();

  /**
   * Thread-safe lazy-initialization of a PerfAnnotationContext on first use.
   * @return PerfAnnotationContext* the context.
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

  struct CategoryDescription {
    std::string category;
    std::string description;

    bool operator==(const CategoryDescription& other) const {
      return category == other.category && description == other.description;
    }
  };

  struct DurationStats {
    std::chrono::nanoseconds total_{0};
    std::chrono::nanoseconds min_{0};
    std::chrono::nanoseconds max_{0};
    WelfordStandardDeviation stddev_;
  };

  struct Hash {
    size_t operator()(const CategoryDescription& a) const {
      return std::hash<std::string>()(a.category) + 13 * std::hash<std::string>()(a.description);
    }
  };

  using DurationStatsMap = absl::node_hash_map<CategoryDescription, DurationStats, Hash>;

  // Maps {category, description} to DurationStats.
#if PERF_THREAD_SAFE
  DurationStatsMap duration_stats_map_ ABSL_GUARDED_BY(mutex_);
  Thread::MutexBasicLockable mutex_;
#else
  DurationStatsMap duration_stats_map_;
#endif
  RealTimeSource time_source_;
};

/**
 * Represents an operation for reporting timing to the perf system. Usage:
 *
 * f() {
 *   PerfOperation perf_op;
 *   computeIntensiveWork();
 *   perf_op.record("category", "description");
 * }
 */
class PerfOperation {
public:
  PerfOperation();

  /**
   * Report an event relative to the operation in progress. Note report can be called
   * multiple times on a single PerfOperation, with distinct category/description combinations.
   * @param category the name of a category for the recording.
   * @param description the name of description for the recording.
   */
  void record(absl::string_view category, absl::string_view description);

private:
  PerfAnnotationContext* context_;
  MonotonicTime start_time_;
};

} // namespace Envoy

#else

// Macros that expand to nothing when performance collection is disabled. These are contrived to
// work syntactically as a C++ statement (e.g. if (foo) PERF_RECORD(...) else PERF_RECORD(...)).

#define PERF_OPERATION(perf_var)                                                                   \
  do {                                                                                             \
  } while (false)
#define PERF_RECORD(perf, category, description)                                                   \
  do {                                                                                             \
  } while (false)
#define PERF_OWNED_OPERATION(perf_var)                                                             \
  do {                                                                                             \
  } while (false)
#define PERF_OWNED_RECORD(perf, category, description)                                             \
  do {                                                                                             \
  } while (false)
#define PERF_DUMP()                                                                                \
  do {                                                                                             \
  } while (false)
#define PERF_TO_STRING() ""
#define PERF_CLEAR()                                                                               \
  do {                                                                                             \
  } while (false)

// Empty declaration used when performance collection is disabled.
#define PERF_OWNER(perf_var)

#endif
