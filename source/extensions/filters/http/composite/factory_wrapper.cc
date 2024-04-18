#include "source/extensions/filters/http/composite/factory_wrapper.h"

#include "source/extensions/filters/http/composite/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Composite {
void FactoryCallbacksWrapper::addStreamDecoderFilter(Http::StreamDecoderFilterSharedPtr filter) {
  ASSERT(!filter_.decoded_headers_);
  if (filter_to_inject_) {
    errors_.push_back(absl::InvalidArgumentError(
        "cannot delegate to decoder filter that instantiates multiple filters"));
    return;
  }

  filter_to_inject_ = filter;
}

void FactoryCallbacksWrapper::addStreamEncoderFilter(Http::StreamEncoderFilterSharedPtr filter) {
  ASSERT(!filter_.decoded_headers_);
  if (filter_to_inject_) {
    errors_.push_back(absl::InvalidArgumentError(
        "cannot delegate to encoder filter that instantiates multiple filters"));
    return;
  }

  filter_to_inject_ = filter;
}

void FactoryCallbacksWrapper::addStreamFilter(Http::StreamFilterSharedPtr filter) {
  ASSERT(!filter_.decoded_headers_);
  if (filter_to_inject_) {
    errors_.push_back(absl::InvalidArgumentError(
        "cannot delegate to stream filter that instantiates multiple filters"));
    return;
  }

  filter_to_inject_ = filter;
}

void FactoryCallbacksWrapper::addAccessLogHandler(AccessLog::InstanceSharedPtr access_log) {
  access_loggers_.push_back(std::move(access_log));
}
} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
