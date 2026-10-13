#pragma once

#include "envoy/extensions/filters/http/grpc_http1_bridge/v3/config.pb.h"
#include "envoy/extensions/filters/http/grpc_http1_bridge/v3/config.pb.validate.h"
#include "envoy/http/filter.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/logger.h"
#include "source/common/grpc/context_impl.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcHttp1Bridge {
/**
 * See
 * https://envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/grpc_http1_bridge_filter
 */
class Http1BridgeFilter : public Http::StreamFilter, Logger::Loggable<Logger::Id::http> {
public:
  explicit Http1BridgeFilter(
      Grpc::Context& context,
      const envoy::extensions::filters::http::grpc_http1_bridge::v3::Config& proto_config)
      : context_(context), upgrade_protobuf_(proto_config.upgrade_protobuf_to_grpc()),
        ignore_query_parameters_(proto_config.ignore_query_parameters()) {}

  // Http::StreamFilterBase
  void onDestroy() override {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;

  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }

  // Http::StreamEncoderFilter
  Http::Filter1xxHeadersStatus encode1xxHeaders(Http::ResponseHeaderMap&) override {
    return Http::Filter1xxHeadersStatus::Continue;
  }
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap&) override {
    return Http::FilterMetadataStatus::Continue;
  }
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override {
    encoder_callbacks_ = &callbacks;
  }

private:
  void setupStatTracking(const Http::RequestHeaderMap& headers);
  void ignoreQueryParams(Http::RequestHeaderMap& headers);

  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
  Http::ResponseHeaderMap* response_headers_{};
  bool do_bridging_{};
  bool do_framing_{};
  Grpc::Context& context_;
  bool upgrade_protobuf_{};
  bool ignore_query_parameters_{};
};

} // namespace GrpcHttp1Bridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
