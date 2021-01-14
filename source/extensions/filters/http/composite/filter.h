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

  struct FilterAccumulator : public Http::FilterChainFactoryCallbacks {
    FilterAccumulator(Http::StreamDecoderFilterCallbacks& decoder_callbacks,
                      Http::StreamEncoderFilterCallbacks& encoder_callbacks)
        : decoder_callbacks_(decoder_callbacks), encoder_callbacks_(encoder_callbacks) {}
    Http::StreamDecoderFilterCallbacks& decoder_callbacks_;
    Http::StreamEncoderFilterCallbacks& encoder_callbacks_;
    void addStreamDecoderFilter(Http::StreamDecoderFilterSharedPtr filter) override {
      decoder_callbacks_.addStreamDecoderFilter(filter);
    }
    void addStreamDecoderFilter(Http::StreamDecoderFilterSharedPtr,
                                Matcher::MatchTreeSharedPtr<Http::HttpMatchingData>) override {
      // TODO(snowp): We should be able to support this.
      NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
    }

    void addStreamEncoderFilter(Http::StreamEncoderFilterSharedPtr filter) override {
      encoder_callbacks_.addStreamEncoderFilter(filter);
    }

    void addStreamEncoderFilter(Http::StreamEncoderFilterSharedPtr,
                                Matcher::MatchTreeSharedPtr<Http::HttpMatchingData>) override {
      // TODO(snowp): We should be able to support this.
      NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
    }

    void addStreamFilter(Http::StreamFilterSharedPtr filter) override {
      decoder_callbacks_.addStreamFilter(filter);
    }

    void addStreamFilter(Http::StreamFilterSharedPtr,
                         Matcher::MatchTreeSharedPtr<Http::HttpMatchingData>) override {
      // TODO(snowp): We should be able to support this.
      NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
    }

    void addAccessLogHandler(AccessLog::InstanceSharedPtr) override {
      NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
    }
  };

  void onMatchCallback(const Matcher::Action& action) override {
    ASSERT(!decoded_headers_);
    const auto& composite_action = action.getTyped<CompositeAction>();

    FilterAccumulator acc(*decoder_callbacks_, *encoder_callbacks_);
    composite_action.createFilters(acc);
  }

private:
  bool decoded_headers_ : 1;
  bool encoded_headers_ : 1;
};

} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy