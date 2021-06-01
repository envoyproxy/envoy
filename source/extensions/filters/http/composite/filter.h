#pragma once

#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/pass_through_filter.h"
#include "extensions/filters/http/composite/action.h"
#include "extensions/filters/http/composite/factory_wrapper.h"

#include "absl/types/variant.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Composite {

struct FactoryCallbacksWrapper;

#define ALL_COMPOSITE_FILTER_STATS(COUNTER)                                                        \
  COUNTER(filter_delegation_error)                                                                 \
  COUNTER(filter_delegation_success)

struct FilterStats {
  ALL_COMPOSITE_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

class Filter : public Http::StreamFilter, Logger::Loggable<Logger::Id::filter> {
public:
  explicit Filter(FilterStats& stats) : decoded_headers_(false), stats_(stats) {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;
  Http::FilterMetadataStatus decodeMetadata(Http::MetadataMap& metadata_map) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }
  void decodeComplete() override;

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
  void encodeComplete() override;

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
  friend FactoryCallbacksWrapper;

  // Use these to track whether we are allowed to insert a specific kind of filter. These mainly
  // serve to surface an easier to understand error, as attempting to insert a filter at a later
  // time will result in various FM assertions firing.
  // We should be protected against this by the match tree validation that only allows request
  // headers, this just provides some additional sanity checking.
  bool decoded_headers_ : 1;

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

  Http::StreamFilterSharedPtr delegated_filter_;
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  FilterStats& stats_;
};

} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
