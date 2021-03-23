#pragma once

#include "envoy/http/filter.h"


namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Composite {

class Filter;

// A FilterChainFactoryCallbacks that delegates filter creation to the filter callbacks.
// We make use of exceptions here for error handling since we're implementing a generic
// interface that doesn't failure. CompositeAction::createFilters catches these exceptions
// and translate it to an absl::Status, which is further translated into an error log.
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