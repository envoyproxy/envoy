#ifndef ENVOY_PERF_ANNOTATION
#define ENVOY_PERF_ANNOTATION
#endif

#include "common/common/perf_annotation.h"

#include <unistd.h>

#include <chrono>
#include <iostream>
#include <string>

#include "common/common/utility.h"

#include "absl/strings/str_cat.h"

namespace Envoy {

PerfOperation::PerfOperation()
    : start_time_(ProdMonotonicTimeSource::instance_.currentTime()),
      context_(PerfAnnotationContext::getOrCreate()) {}

void PerfOperation::record(absl::string_view category, absl::string_view description) {
  const MonotonicTime end_time = ProdMonotonicTimeSource::instance_.currentTime();
  const std::chrono::nanoseconds duration =
      std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time_);
  context_->record(duration, category, description);
}

// The ctor is explicitly declared private to encourage clients to use getOrCreate(), at
// least for now. Given that it's declared it must be instantiated. It's not inlined
// because the contructor is non-trivial due to the contained unordered_map.
PerfAnnotationContext::PerfAnnotationContext() {}

void PerfAnnotationContext::record(std::chrono::nanoseconds duration, absl::string_view category,
                                   absl::string_view description) {
  CategoryDescription key((std::string(category)), (std::string(description)));
  {
#if PERF_THREAD_SAFE
    absl::MutexLock lock(&mutex_);
#endif
    DurationStats& stats = duration_stats_map_[key];
    stats.stddev_.update(static_cast<double>(duration.count()));
    if ((stats.stddev_.count() == 1) || (duration < stats.min_)) {
      stats.min_ = duration;
    }
    stats.max_ = std::max(stats.max_, duration);
    stats.total_ += duration;
  }
}

// TODO(jmarantz): Consider hooking up perf information-dump into admin console, if
// we find a performance problem we want to annotate with a live server.
void PerfAnnotationContext::dump() { std::cout << toString() << std::endl; }

std::string PerfAnnotationContext::toString() {
  PerfAnnotationContext* context = getOrCreate();
  std::string out;
#if PERF_THREAD_SAFE
  absl::MutexLock lock(&context->mutex_);
#endif

  // The map is from category/description -> [duration, time]. Reverse-sort by duration.
  std::vector<const DurationStatsMap::value_type*> sorted_values;
  sorted_values.reserve(context->duration_stats_map_.size());
  for (const auto& iter : context->duration_stats_map_) {
    sorted_values.push_back(&iter);
  }
  std::sort(
      sorted_values.begin(), sorted_values.end(),
      [](const DurationStatsMap::value_type* a, const DurationStatsMap::value_type* b) -> bool {
        const DurationStats& a_stats = a->second;
        const DurationStats& b_stats = b->second;
        return a_stats.total_ > b_stats.total_;
      });

  // Organize the report so it lines up in columns. Note that the widest duration comes first,
  // though that may not be descending order of calls or per_call time, so we need two passes
  // to compute column widths. First collect the column headers and their widths.
  //
  // TODO(jmarantz): Add a mechanism for dumping to HTML for viewing results in web browser.
  static const char* headers[] = {"Duration(us)", "# Calls", "Mean(ns)", "StdDev(ns)",
                                  "Min(ns)",      "Max(ns)", "Category", "Description"};
  constexpr int num_columns = ARRAY_SIZE(headers);
  size_t widths[num_columns];
  std::vector<std::string> columns[num_columns];
  for (size_t i = 0; i < num_columns; ++i) {
    std::string column(headers[i]);
    widths[i] = column.size();
    columns[i].emplace_back(column);
  }

  // Compute all the column strings and their max widths.
  for (const auto& p : sorted_values) {
    const DurationStats& stats = p->second;
    const auto microseconds_string = [](std::chrono::nanoseconds ns) -> std::string {
      return std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(ns).count());
    };
    const auto nanoseconds_string = [](std::chrono::nanoseconds ns) -> std::string {
      return std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(ns).count());
    };
    columns[0].push_back(microseconds_string(stats.total_));
    const uint64_t count = stats.stddev_.count();
    columns[1].push_back(std::to_string(count));
    columns[2].push_back(
        (count == 0)
            ? "NaN"
            : std::to_string(
                  std::chrono::duration_cast<std::chrono::nanoseconds>(stats.total_).count() /
                  count));
    columns[3].push_back(fmt::format("{}", stats.stddev_.computeStandardDeviation()));
    columns[4].push_back(nanoseconds_string(stats.min_));
    columns[5].push_back(nanoseconds_string(stats.max_));
    const CategoryDescription& category_description = p->first;
    columns[6].push_back(category_description.first);
    columns[7].push_back(category_description.second);
    for (size_t i = 0; i < num_columns; ++i) {
      widths[i] = std::max(widths[i], columns[i].back().size());
    }
  }

  // Create format-strings to right justify each column, e.g. {:>14} for a column of width 14.
  std::vector<std::string> formats;
  for (size_t i = 0; i < num_columns; ++i) {
    // left-justify category & description, but right-justify the numeric columns.
    const absl::string_view justify = (i < num_columns - 2) ? ">" : "<";
    formats.push_back(absl::StrCat("{:", justify, widths[i], "}"));
  }

  // Write out the table.
  for (size_t row = 0; row < columns[0].size(); ++row) {
    for (size_t i = 0; i < num_columns; ++i) {
      const std::string& str = columns[i][row];
      absl::StrAppend(&out, fmt::format(formats[i], str), (i != (num_columns - 1) ? "  " : "\n"));
    }
  }
  return out;
}

void PerfAnnotationContext::clear() {
  PerfAnnotationContext* context = getOrCreate();
#if PERF_THREAD_SAFE
  absl::MutexLock lock(&context->mutex_);
#endif
  context->duration_stats_map_.clear();
}

PerfAnnotationContext* PerfAnnotationContext::getOrCreate() {
  static PerfAnnotationContext* context = new PerfAnnotationContext();
  return context;
}

} // namespace Envoy
