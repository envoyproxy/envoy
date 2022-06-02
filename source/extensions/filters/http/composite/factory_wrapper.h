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
struct FactoryCallbacksWrapper : public Http::FilterChainFactoryCallbacks {
  FactoryCallbacksWrapper(Filter& filter, Event::Dispatcher& dispatcher)
      : filter_(filter), dispatcher_(dispatcher) {}

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
  absl::optional<FilterAlternative> filter_to_inject_;
  std::vector<AccessLog::InstanceSharedPtr> access_loggers_;
  std::vector<absl::Status> errors_;
};
} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
