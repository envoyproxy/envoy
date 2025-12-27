#include "source/extensions/filters/http/composite/factory_wrapper.h"

#include "source/extensions/filters/http/composite/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Composite {
void FactoryCallbacksWrapper::addStreamDecoderFilter(Http::StreamDecoderFilterSharedPtr filter) {
  if (is_filter_chain_mode_) {
    // In filter chain mode, wrap the decoder filter as a stream filter.
    filters_to_inject_.push_back(std::make_shared<Filter::StreamFilterWrapper>(std::move(filter)));
    return;
  }

  if (filter_to_inject_) {
    errors_.push_back(absl::InvalidArgumentError(
        "cannot delegate to decoder filter that instantiates multiple filters"));
    return;
  }

  filter_to_inject_ = filter;
}

void FactoryCallbacksWrapper::addStreamEncoderFilter(Http::StreamEncoderFilterSharedPtr filter) {
  if (is_filter_chain_mode_) {
    // In filter chain mode, wrap the encoder filter as a stream filter.
    filters_to_inject_.push_back(std::make_shared<Filter::StreamFilterWrapper>(std::move(filter)));
    return;
  }

  if (filter_to_inject_) {
    errors_.push_back(absl::InvalidArgumentError(
        "cannot delegate to encoder filter that instantiates multiple filters"));
    return;
  }

  filter_to_inject_ = filter;
}

void FactoryCallbacksWrapper::addStreamFilter(Http::StreamFilterSharedPtr filter) {
  if (is_filter_chain_mode_) {
    // In filter chain mode, store the filter directly.
    filters_to_inject_.push_back(std::move(filter));
    return;
  }

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
