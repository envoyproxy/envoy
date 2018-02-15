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

void PerfOperation::report(absl::string_view category, absl::string_view description) {
  SystemTime end_time = ProdSystemTimeSource::instance_.currentTime();
  std::chrono::nanoseconds duration =
      std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time_);
  context_->report(duration, category, description);
}

PerfAnnotationContext::PerfAnnotationContext() {}

void PerfAnnotationContext::report(std::chrono::nanoseconds duration, absl::string_view category,
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
  std::vector<const DurationCountMap::value_type*> sorted_values;
  sorted_values.reserve(context->duration_count_map_.size());
  for (const auto& iter : context->duration_count_map_) {
    sorted_values.push_back(&iter);
  }
  std::sort(
      sorted_values.begin(), sorted_values.end(),
      [](const DurationCountMap::value_type* a, const DurationCountMap::value_type* b) -> bool {
        return a->second.first > b->second.first;
      });
  size_t column_width = 0;
  for (const auto& p : sorted_values) {
    const DurationCount& duration_count = p->second;
    std::chrono::nanoseconds duration = duration_count.first;
    uint64_t count = duration_count.second;
    std::string duration_count_str = absl::StrCat(
        std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(duration).count()),
        "(", count, ")");
    if (column_width == 0) {
      column_width = duration_count_str.size();
    } else {
      out.append(column_width - duration_count_str.size(), ' ');
    }
    absl::StrAppend(&out, duration_count_str, ": ", p->first, "\n");
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
