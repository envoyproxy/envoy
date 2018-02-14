#ifndef ENVOY_PERF_ANNOTATION
#define ENVOY_PERF_ANNOTATION
#endif

#include "common/common/perf_annotation.h"

#include <chrono>
#include <unistd.h>
#include <iostream>
#include <string>

#include "absl/strings/str_cat.h"
#include "common/common/utility.h"

namespace Envoy {

PerfOperation::PerfOperation() :
    start_time_(ProdSystemTimeSource::instance_.currentTime()),
    context_(PerfAnnotationContext::getOrCreate()) {
}

void PerfOperation::report(absl::string_view category, absl::string_view description) {
  SystemTime end_time = ProdSystemTimeSource::instance_.currentTime();
  std::chrono::nanoseconds duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
      end_time - start_time_);
  context_->report(duration, category, description);
}

PerfAnnotationContext::PerfAnnotationContext() {
}

void PerfAnnotationContext::report(std::chrono::nanoseconds duration, absl::string_view category,
                                   absl::string_view description) {
  std::string key = absl::StrCat(category, " / ", description);
  {
#if PERF_THREAD_SAFE
    std::unique_lock<std::mutex> lock(mutex_);
#endif
    duration_map_[key] += duration;
  }
}

// TODO(jmarantz): Consider hooking up perf information-dump into admin console, if
// we find a performance problem we want to annotate with a live server.
void PerfAnnotationContext::dump() {
  std::cout << toString() << std::endl;
}

std::string PerfAnnotationContext::toString() {
  PerfAnnotationContext* context = getOrCreate();
  std::string out;
#if PERF_THREAD_SAFE
  std::unique_lock<std::mutex> lock(context->mutex_);
#endif
  for (const auto& p : context->duration_map_) {
    std::chrono::nanoseconds duration = p.second;
    absl::StrAppend(&out, std::to_string(
        std::chrono::duration_cast<std::chrono::microseconds>(duration).count()),
                    ": ", p.first, "\n");
  }
  return out;
}

void PerfAnnotationContext::clear() {
  PerfAnnotationContext* context = getOrCreate();
#if PERF_THREAD_SAFE
  std::unique_lock<std::mutex> lock(context->mutex_);
#endif
  context->duration_map_.clear();
}

PerfAnnotationContext* PerfAnnotationContext::getOrCreate() {
  static PerfAnnotationContext* context = new PerfAnnotationContext();
  return context;
}

}  // namespace Envoy
