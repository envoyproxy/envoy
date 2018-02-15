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
    : start_time_(ProdSystemTimeSource::instance_.currentTime()),
      context_(PerfAnnotationContext::getOrCreate()) {}

void PerfOperation::record(absl::string_view category, absl::string_view description) {
  SystemTime end_time = ProdSystemTimeSource::instance_.currentTime();
  std::chrono::nanoseconds duration =
      std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time_);
  context_->record(duration, category, description);
}

PerfAnnotationContext::PerfAnnotationContext() {}

void PerfAnnotationContext::record(std::chrono::nanoseconds duration, absl::string_view category,
                                   absl::string_view description) {
  std::string key = absl::StrCat(category, " / ", description);
  {
#if PERF_THREAD_SAFE
    std::unique_lock<std::mutex> lock(mutex_);
#endif
    DurationCount& duration_count = duration_count_map_[key];
    duration_count.first += duration;
    ++duration_count.second;
  }
}

// TODO(jmarantz): Consider hooking up perf information-dump into admin console, if
// we find a performance problem we want to annotate with a live server.
void PerfAnnotationContext::dump() { std::cout << toString() << std::endl; }

std::string PerfAnnotationContext::toString() {
  PerfAnnotationContext* context = getOrCreate();
  std::string out;
#if PERF_THREAD_SAFE
  std::unique_lock<std::mutex> lock(context->mutex_);
#endif

  // The map is from category/description -> [duration, time].  Reverse-sort by duration.
  std::vector<const DurationCountMap::value_type*> sorted_values;
  sorted_values.reserve(context->duration_count_map_.size());
  for (const auto& iter : context->duration_count_map_) {
    sorted_values.push_back(&iter);
  }
  std::sort(sorted_values.begin(), sorted_values.end(), [](
      const DurationCountMap::value_type* a, const DurationCountMap::value_type* b) -> bool {
              return a->second.first > b->second.first;
            });


  // Organize the report so it lines up in columns.  Note that the widest duration comes first,
  // though that may not be descending order of calls or per_call time, so we need two passes
  // to compute column widths.  First collect the column headers and their widths.
  static const char* headers[] = {"Duration(us)", "# Calls", "per_call(ns)",
                                  "Category / Description"};
  constexpr int num_columns = ARRAY_SIZE(headers);
  size_t widths[num_columns];
  std::vector<std::string> columns[num_columns];
  for (size_t i = 0; i < num_columns; ++i) {
    columns[i].push_back(headers[i]);
    widths[i] = strlen(headers[i]);
  }

  // Compute all the column strings and their max widths.
  for (const auto& p : sorted_values) {
    const DurationCount& duration_count = p->second;
    std::chrono::nanoseconds duration = duration_count.first;
    uint64_t count = duration_count.second;
    columns[0].push_back(
        std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(duration).count()));
    columns[1].push_back(std::to_string(count));
    columns[2].push_back((count == 0) ? "NaN" : std::to_string(
        std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count() / count));
    columns[3].push_back(p->first);
    for (size_t i = 0; i < num_columns; ++i) {
      widths[i] = std::max(widths[i], columns[i].size());
    }
  }

  // Write out the table.
  for (size_t row = 0; row < columns[0].size(); ++row) {
    for (size_t i = 0; i < num_columns; ++i) {
      // Right-justify by appending the number of spaces needed to bring it inline with the largest.
      const std::string& str = columns[i][row];
      out.append(widths[i] - str.size(), ' ');
      absl::StrAppend(&out, str, (i == num_columns - 1) ? "\n" : "  ");
    }
  }
  return out;
}

void PerfAnnotationContext::clear() {
  PerfAnnotationContext* context = getOrCreate();
#if PERF_THREAD_SAFE
  std::unique_lock<std::mutex> lock(context->mutex_);
#endif
  context->duration_count_map_.clear();
}

PerfAnnotationContext* PerfAnnotationContext::getOrCreate() {
  static PerfAnnotationContext* context = new PerfAnnotationContext();
  return context;
}

} // namespace Envoy
