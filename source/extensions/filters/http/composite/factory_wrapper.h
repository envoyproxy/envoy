#pragma once

#include "envoy/http/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Composite {

class Filter;

// A FilterChainFactoryCallbacks that delegates filter creation to the filter callbacks.
// Since we are unable to handle all the different callbacks, we track errors seen throughout
// the lifetime of this wrapper by appending them to the errors_ field. This should be checked
// afterwards to determine whether invalid callbacks were called.
//
// This wrapper supports following two modes:
// 1. Single filter mode (default): Only one filter can be added. Adding more than one filter
//    will result in an error.
// 2. Filter chain mode: Multiple filters can be added. Filters are stored in order and will
//    be executed in a chain sequence.
struct FactoryCallbacksWrapper : public Http::FilterChainFactoryCallbacks {
  // Creates a wrapper in single filter mode.
  FactoryCallbacksWrapper(Filter& filter, Event::Dispatcher& dispatcher)
      : filter_(filter), dispatcher_(dispatcher), is_filter_chain_mode_(false) {}

  // Creates a wrapper in filter chain mode.
  FactoryCallbacksWrapper(Filter& filter, Event::Dispatcher& dispatcher, bool filter_chain_mode)
      : filter_(filter), dispatcher_(dispatcher), is_filter_chain_mode_(filter_chain_mode) {}

  void addStreamDecoderFilter(Http::StreamDecoderFilterSharedPtr filter) override;
  void addStreamEncoderFilter(Http::StreamEncoderFilterSharedPtr filter) override;
  void addStreamFilter(Http::StreamFilterSharedPtr filter) override;
  void addAccessLogHandler(AccessLog::InstanceSharedPtr) override;
  Event::Dispatcher& dispatcher() override { return dispatcher_; }

  Filter& filter_;
  Event::Dispatcher& dispatcher_;

  using FilterAlternative =
      absl::variant<Http::StreamDecoderFilterSharedPtr, Http::StreamEncoderFilterSharedPtr,
                    Http::StreamFilterSharedPtr>;
  // For single filter mode we store the filter to inject.
  absl::optional<FilterAlternative> filter_to_inject_;
  // For filter chain mode we store all filters in order.
  std::vector<Http::StreamFilterSharedPtr> filters_to_inject_;
  AccessLog::InstanceSharedPtrVector access_loggers_;
  std::vector<absl::Status> errors_;
  const bool is_filter_chain_mode_;
};
} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
