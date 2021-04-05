#include "extensions/filters/http/composite/factory_wrapper.h"

#include "extensions/filters/http/composite/filter.h"

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
void FactoryCallbacksWrapper::addStreamDecoderFilter(
    Http::StreamDecoderFilterSharedPtr, Matcher::MatchTreeSharedPtr<Http::HttpMatchingData>) {
  errors_.push_back(absl::InvalidArgumentError(
      "cannot delegate to decoder filter that instantiates a match tree"));
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

void FactoryCallbacksWrapper::addStreamEncoderFilter(
    Http::StreamEncoderFilterSharedPtr, Matcher::MatchTreeSharedPtr<Http::HttpMatchingData>) {
  errors_.push_back(absl::InvalidArgumentError(
      "cannot delegate to encoder filter that instantiates a match tree"));
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

void FactoryCallbacksWrapper::addStreamFilter(Http::StreamFilterSharedPtr,
                                              Matcher::MatchTreeSharedPtr<Http::HttpMatchingData>) {
  errors_.push_back(absl::InvalidArgumentError(
      "cannot delegate to stream filter that instantiates a match tree"));
}

void FactoryCallbacksWrapper::addAccessLogHandler(AccessLog::InstanceSharedPtr) {
  errors_.push_back(
      absl::InvalidArgumentError("cannot delegate to filter that adds an access log handler"));
}
} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
