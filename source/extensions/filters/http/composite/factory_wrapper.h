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
  explicit FactoryCallbacksWrapper(Filter& filter) : filter_(filter) {}

  void addStreamDecoderFilter(Http::StreamDecoderFilterSharedPtr filter) override;
  void addStreamDecoderFilter(Http::StreamDecoderFilterSharedPtr,
                              Matcher::MatchTreeSharedPtr<Http::HttpMatchingData>) override;
  void addStreamEncoderFilter(Http::StreamEncoderFilterSharedPtr filter) override;
  void addStreamEncoderFilter(Http::StreamEncoderFilterSharedPtr,
                              Matcher::MatchTreeSharedPtr<Http::HttpMatchingData>) override;
  void addStreamFilter(Http::StreamFilterSharedPtr filter) override;
  void addStreamFilter(Http::StreamFilterSharedPtr,
                       Matcher::MatchTreeSharedPtr<Http::HttpMatchingData>) override;
  void addAccessLogHandler(AccessLog::InstanceSharedPtr) override;

  Filter& filter_;

  using FilterAlternative =
      absl::variant<Http::StreamDecoderFilterSharedPtr, Http::StreamEncoderFilterSharedPtr,
                    Http::StreamFilterSharedPtr>;
  absl::optional<FilterAlternative> filter_to_inject_;
  std::vector<absl::Status> errors_;
};
} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
