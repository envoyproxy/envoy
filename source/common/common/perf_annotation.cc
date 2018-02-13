#include "common/common/perf_annotation.h"

#include <chrono>
#include <unistd.h>
#include <iostream>
#include <string>

#include "absl/strings/str_cat.h"
#include "common/common/utility.h"

#ifdef ENVOY_PERF_ANNOTATION
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
    std::unique_lock<std::mutex> lock(mutex_);
    duration_map_[key] += duration;
  }
}

// TODO(jmarantz): hook up perf information-dump into admin console.
void PerfAnnotationContext::dump() {
  PerfAnnotationContext* context = getOrCreate();
  std::unique_lock<std::mutex> lock(context->mutex_);
  for (const auto& p : context->duration_map_) {
    std::chrono::nanoseconds duration = p.second;
    std::cout << std::to_string(
        std::chrono::duration_cast<std::chrono::microseconds>(duration).count())
              << ": " << p.first << std::endl;
  }
}

PerfAnnotationContext* PerfAnnotationContext::getOrCreate() {
  static PerfAnnotationContext* context = new PerfAnnotationContext();
  return context;
}

}  // namespace Envoy

#endif
