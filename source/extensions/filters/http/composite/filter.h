#pragma once

#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/server/filter_config.h"

#include "source/common/json/json_loader.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/composite/action.h"
#include "source/extensions/filters/http/composite/factory_wrapper.h"

#include "absl/container/flat_hash_map.h"
#include "absl/types/variant.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Composite {

constexpr absl::string_view MatchedActionsFilterStateKey =
    "envoy.extensions.filters.http.composite.matched_actions";

struct FactoryCallbacksWrapper;

#define ALL_COMPOSITE_FILTER_STATS(COUNTER)                                                        \
  COUNTER(filter_delegation_error)                                                                 \
  COUNTER(filter_delegation_success)

struct FilterStats {
  ALL_COMPOSITE_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

class MatchedActionInfo : public StreamInfo::FilterState::Object {
public:
  MatchedActionInfo(const std::string& filter, const std::string& action) {
    actions_[filter] = action;
  }

  ProtobufTypes::MessagePtr serializeAsProto() const override { return buildProtoStruct(); }

  absl::optional<std::string> serializeAsString() const override {
    return Json::Factory::loadFromProtobufStruct(*buildProtoStruct())->asJsonString();
  }

  void setFilterAction(const std::string& filter, const std::string& action) {
    actions_[filter] = action;
  }

private:
  std::unique_ptr<ProtobufWkt::Struct> buildProtoStruct() const;

  absl::flat_hash_map<std::string, std::string> actions_;
};

class Filter : public Http::StreamFilter,
               public AccessLog::Instance,
               Logger::Loggable<Logger::Id::filter> {
public:
  Filter(FilterStats& stats, Event::Dispatcher& dispatcher, bool is_upstream)
      : dispatcher_(dispatcher), decoded_headers_(false), stats_(stats), is_upstream_(is_upstream) {
  }

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
  Http::Filter1xxHeadersStatus encode1xxHeaders(Http::ResponseHeaderMap& headers) override;
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

  void onStreamComplete() override {
    if (delegated_filter_) {
      // We need to explicitly specify which base class to the conversion via due
      // to the diamond inheritance between StreamFilter and StreamFilterBase.
      static_cast<Http::StreamDecoderFilter&>(*delegated_filter_).onStreamComplete();
    }
  }

  void onMatchCallback(const Matcher::Action& action) override;

  // AccessLog::Instance
  void log(const Formatter::HttpFormatterContext& log_context,
           const StreamInfo::StreamInfo& info) override {
    for (const auto& log : access_loggers_) {
      log->log(log_context, info);
    }
  }

  bool isUpstream() const { return is_upstream_; }

private:
  friend FactoryCallbacksWrapper;

  void updateFilterState(Http::StreamFilterCallbacks* callback, const std::string& filter_name,
                         const std::string& action_name);

  Event::Dispatcher& dispatcher_;
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
    Http::Filter1xxHeadersStatus encode1xxHeaders(Http::ResponseHeaderMap& headers) override;
    Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                            bool end_stream) override;
    Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
    Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;
    Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap& metadata_map) override;
    void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override;
    void encodeComplete() override;

    // Http::StreamFilterBase
    void onDestroy() override;
    void onStreamComplete() override;

  private:
    Http::StreamEncoderFilterSharedPtr encoder_filter_;
    Http::StreamDecoderFilterSharedPtr decoder_filter_;
  };
  std::vector<AccessLog::InstanceSharedPtr> access_loggers_;

  Http::StreamFilterSharedPtr delegated_filter_;
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  FilterStats& stats_;
  // Filter in the upstream filter chain.
  bool is_upstream_;
};

} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
