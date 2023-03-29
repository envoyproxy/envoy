#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"

namespace Envoy {
class MetadataControlFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "metadata-control-filter";

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool end_stream) override {
    return end_stream ? Http::FilterHeadersStatus::Continue
                      : Http::FilterHeadersStatus::StopIteration;
  }

  Http::FilterDataStatus decodeData(Buffer::Instance&, bool end_stream) override {
    return end_stream ? Http::FilterDataStatus::Continue
                      : Http::FilterDataStatus::StopIterationAndWatermark;
  }

  // Adds new metadata to metadata_map directly
  Http::FilterMetadataStatus decodeMetadata(Http::MetadataMap& metadata_map) override {
    if (metadata_map.contains("should_continue")) {
      return Http::FilterMetadataStatus::ContinueAll;
    } else if (metadata_map.contains("local_reply")) {
      decoder_callbacks_->sendLocalReply(Http::Code::BadRequest, "baaaad", nullptr, absl::nullopt,
                                         "reallybad");
      return Http::FilterMetadataStatus::StopIterationForLocalReply;
    }
    return Http::FilterMetadataStatus::Continue;
  }

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool end_stream) override {
    return end_stream ? Http::FilterHeadersStatus::Continue
                      : Http::FilterHeadersStatus::StopIteration;
  }
  Http::FilterDataStatus encodeData(Buffer::Instance&, bool end_stream) override {
    return end_stream ? Http::FilterDataStatus::Continue
                      : Http::FilterDataStatus::StopIterationAndWatermark;
  }
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap& metadata_map) override {
    if (metadata_map.contains("should_continue")) {
      return Http::FilterMetadataStatus::ContinueAll;
    } else if (metadata_map.contains("local_reply")) {
      encoder_callbacks_->sendLocalReply(Http::Code::BadRequest, "baaaad", nullptr, absl::nullopt,
                                         "reallybad");
      return Http::FilterMetadataStatus::StopIterationForLocalReply;
    }
    return Http::FilterMetadataStatus::Continue;
  }
};

constexpr char MetadataControlFilter::name[];
static Registry::RegisterFactory<SimpleFilterConfig<MetadataControlFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;
static Registry::RegisterFactory<SimpleFilterConfig<MetadataControlFilter>,
                                 Server::Configuration::UpstreamHttpFilterConfigFactory>
    register_upstream_;

} // namespace Envoy
