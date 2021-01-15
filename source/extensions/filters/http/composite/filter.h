#pragma once

#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Composite {

using FactoryMap = absl::flat_hash_map<std::string, Http::FilterFactoryCb>;

class Filter : public Http::PassThroughFilter {
public:
  Filter() : decoded_headers_(false), encoded_headers_(false) {}

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {
    decoded_headers_ = true;
    // TODO(snowp): Support buffering the request until a match result has been determined instead
    // of assuming it will only be header matching.
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterHeadersStatus encode100ContinueHeaders(Http::ResponseHeaderMap&) override {
    encoded_headers_ = true;

    // TODO(snowp): Support buffering the response until a match result has been determined instead
    // of assuming it will only be header matching.
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool) override {
    encoded_headers_ = true;

    // TODO(snowp): Support buffering the response until a match result has been determined instead
    // of assuming it will only be header matching.
    return Http::FilterHeadersStatus::Continue;
  }

  // A FilterChainFactoryCallbacks that delegates filter creation to the filter callbacks.
  struct FactoryCallbacksWrapper : public Http::FilterChainFactoryCallbacks {
    explicit FactoryCallbacksWrapper(Filter& filter) : filter_(filter) {}

    void addStreamDecoderFilter(Http::StreamDecoderFilterSharedPtr filter) override {
      ASSERT(!filter_.decoded_headers_);

      filter_.decoder_callbacks_->addStreamDecoderFilter(filter);
    }
    void addStreamDecoderFilter(Http::StreamDecoderFilterSharedPtr,
                                Matcher::MatchTreeSharedPtr<Http::HttpMatchingData>) override {
      // TODO(snowp): We should be able to support this.
      NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
    }

    void addStreamEncoderFilter(Http::StreamEncoderFilterSharedPtr filter) override {
      ASSERT(!filter_.encoded_headers_);
      filter_.encoder_callbacks_->addStreamEncoderFilter(filter);
    }

    void addStreamEncoderFilter(Http::StreamEncoderFilterSharedPtr,
                                Matcher::MatchTreeSharedPtr<Http::HttpMatchingData>) override {
      // TODO(snowp): We should be able to support this.
      NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
    }

    void addStreamFilter(Http::StreamFilterSharedPtr filter) override {
      ASSERT(!filter_.decoded_headers_);
      filter_.decoder_callbacks_->addStreamFilter(filter);
    }

    void addStreamFilter(Http::StreamFilterSharedPtr,
                         Matcher::MatchTreeSharedPtr<Http::HttpMatchingData>) override {
      // TODO(snowp): We should be able to support this.
      NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
    }

    void addAccessLogHandler(AccessLog::InstanceSharedPtr) override {
      // TODO(snowp): Should we support adding an access log as well? Wouldn't be hard to do.
      NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
    }

    Filter& filter_;
  };

  void onMatchCallback(const Matcher::Action& action) override {
    const auto& composite_action = action.getTyped<CompositeAction>();

    FactoryCallbacksWrapper wrapper(*this);
    composite_action.createFilters(wrapper);
  }

private:
  // Use these to track whether we are allowed to insert a specific kind of filter. These mainly
  // serve to surface an easier to understand error, as attempting to insert a filter at a later
  // time will result in various FM assertions firing.
  // TODO(snowp): Instead of validating this via ASSERTs, we should be able to validate that the
  // match tree is only going to fire when we can actually inject a filter.
  bool decoded_headers_ : 1;
  bool encoded_headers_ : 1;
};

} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
