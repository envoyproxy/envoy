#pragma once

#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/pass_through_filter.h"
#include "extensions/filters/http/composite/action.h"

#include "absl/types/variant.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Composite {

class Filter : public Http::StreamFilter {
public:
  Filter() : decoded_headers_(false), encoded_headers_(false) {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encode100ContinueHeaders(Http::ResponseHeaderMap& headers) override;
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap& metadata_map) override;
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override {
    encoder_callbacks_ = &callbacks;
  }

  // Http::StreamFilterBase
  void onDestroy() override {
    if (delegated_filter_) {
      // We need to explicitly specify which base class to the conversion via due
      // to the diamond inheritance between StreamFilter and StreamFilterBase.
      static_cast<Http::StreamDecoderFilter&>(*delegated_filter_).onDestroy();
    }
  }

  void onMatchCallback(const Matcher::Action& action) override;

private:
  // Use these to track whether we are allowed to insert a specific kind of filter. These mainly
  // serve to surface an easier to understand error, as attempting to insert a filter at a later
  // time will result in various FM assertions firing.
  // TODO(snowp): Instead of validating this via ASSERTs, we should be able to validate that the
  // match tree is only going to fire when we can actually inject a filter.
  bool decoded_headers_ : 1;
  bool encoded_headers_ : 1;

  // Wraps a stream encoder OR a stream decoder filter into a stream filter, making it easier to
  // delegate calls.
  struct StreamFilterWrapper : public Http::StreamFilter {
  public:
    explicit StreamFilterWrapper(Http::StreamEncoderFilterSharedPtr encoder_filter)
        : encoder_filter_(encoder_filter) {}
    explicit StreamFilterWrapper(Http::StreamDecoderFilterSharedPtr decoder_filter)
        : decoder_filter_(decoder_filter) {}

    // Http::StreamDecoderFilter
    Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                            bool end_stream) override;
    Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
    Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;
    Http::FilterMetadataStatus decodeMetadata(Http::MetadataMap& metadata_map) override;
    void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;
    void decodeComplete() override;

    // Http::StreamEncoderFilter
    Http::FilterHeadersStatus encode100ContinueHeaders(Http::ResponseHeaderMap& headers) override;
    Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                            bool end_stream) override;
    Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
    Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;
    Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap& metadata_map) override;
    void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override;
    void encodeComplete() override;

    // Http::StreamFilterBase
    void onDestroy() override;

  private:
    Http::StreamEncoderFilterSharedPtr encoder_filter_;
    Http::StreamDecoderFilterSharedPtr decoder_filter_;
  };

  // A FilterChainFactoryCallbacks that delegates filter creation to the filter callbacks.
  struct FactoryCallbacksWrapper : public Http::FilterChainFactoryCallbacks {
    explicit FactoryCallbacksWrapper(Filter& filter) : filter_(filter) {}

    void addStreamDecoderFilter(Http::StreamDecoderFilterSharedPtr filter) override {
      ASSERT(!filter_.decoded_headers_);
      ASSERT(!filter_to_inject_);

      filter_to_inject_ = filter;
    }
    void addStreamDecoderFilter(Http::StreamDecoderFilterSharedPtr,
                                Matcher::MatchTreeSharedPtr<Http::HttpMatchingData>) override {
      NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
    }
    void addStreamEncoderFilter(Http::StreamEncoderFilterSharedPtr filter) override {
      ASSERT(!filter_.encoded_headers_);
      ASSERT(!filter_to_inject_);

      filter_to_inject_ = filter;
    }

    void addStreamEncoderFilter(Http::StreamEncoderFilterSharedPtr,
                                Matcher::MatchTreeSharedPtr<Http::HttpMatchingData>) override {
      NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
    }

    void addStreamFilter(Http::StreamFilterSharedPtr filter) override {
      ASSERT(!filter_.decoded_headers_);
      ASSERT(!filter_to_inject_);

      filter_to_inject_ = filter;
    }

    void addStreamFilter(Http::StreamFilterSharedPtr,
                         Matcher::MatchTreeSharedPtr<Http::HttpMatchingData>) override {
      NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
    }

    void addAccessLogHandler(AccessLog::InstanceSharedPtr) override {
      NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
    }

    Filter& filter_;

    using FilterAlternative =
        absl::variant<Http::StreamDecoderFilterSharedPtr, Http::StreamEncoderFilterSharedPtr,
                      Http::StreamFilterSharedPtr>;
    absl::optional<FilterAlternative> filter_to_inject_;
  };

  Http::StreamFilterSharedPtr delegated_filter_;
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_;
};

} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
