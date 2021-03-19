#include "extensions/filters/http/composite/factory_wrapper.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Composite {
void FactoryCallbacksWrapper::addStreamDecoderFilter(Http::StreamDecoderFilterSharedPtr filter) {
  ASSERT(!filter_.decoded_headers_);
  if (filter_to_inject_) {
    throw EnvoyException("cannot delegate to decoder filter that instantiates multiple filters");
  }

  filter_to_inject_ = filter;
}
void FactoryCallbacksWrapper::addStreamDecoderFilter(
    Http::StreamDecoderFilterSharedPtr, Matcher::MatchTreeSharedPtr<Http::HttpMatchingData>) {
  throw EnvoyException("cannot delegate to decoder filter that initializes a match tree");
}
void FactoryCallbacksWrapper::addStreamEncoderFilter(Http::StreamEncoderFilterSharedPtr filter) {
  ASSERT(!filter_.encoded_headers_);
  if (filter_to_inject_) {
    throw EnvoyException("cannot delegate to encoder filter that instantiates multiple filters");
  }

  filter_to_inject_ = filter;
}

void FactoryCallbacksWrapper::addStreamEncoderFilter(
    Http::StreamEncoderFilterSharedPtr, Matcher::MatchTreeSharedPtr<Http::HttpMatchingData>) {
  throw EnvoyException("cannot delegate to encoder filter that initializes a match tree");
}

void FactoryCallbacksWrapper::addStreamFilter(Http::StreamFilterSharedPtr filter) {
  ASSERT(!filter_.decoded_headers_);
  if (filter_to_inject_) {
    throw EnvoyException("cannot delegate to stream filter that instantiates multiple filters");
  }

  filter_to_inject_ = filter;
}

void FactoryCallbacksWrapper::addStreamFilter(Http::StreamFilterSharedPtr,
                                              Matcher::MatchTreeSharedPtr<Http::HttpMatchingData>) {
  throw EnvoyException("cannot delegate to stream filter that initializes a match tree");
}

void FactoryCallbacksWrapper::addAccessLogHandler(AccessLog::InstanceSharedPtr) {
  throw EnvoyException("cannot delegate to filter that adds an access log handler");
}
} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy