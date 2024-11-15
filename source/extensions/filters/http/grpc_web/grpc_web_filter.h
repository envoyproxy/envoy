#pragma once

#include "envoy/http/filter.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/non_copyable.h"
#include "source/common/grpc/codec.h"
#include "source/common/grpc/context_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcWeb {

/**
 * See https://envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/grpc_web_filter
 */
class GrpcWebFilter : public Http::StreamFilter, NonCopyable {
public:
  explicit GrpcWebFilter(Grpc::Context& context) : context_(context) {}
  ~GrpcWebFilter() override = default;

  // Http::StreamFilterBase
  void onDestroy() override {}

  // Implements StreamDecoderFilter.
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override;
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }

  // Implements StreamEncoderFilter.
  Http::Filter1xxHeadersStatus encode1xxHeaders(Http::ResponseHeaderMap&) override {
    return Http::Filter1xxHeadersStatus::Continue;
  }
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool) override;
  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap&) override {
    return Http::FilterMetadataStatus::Continue;
  }
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override {
    encoder_callbacks_ = &callbacks;
  }

  bool doStatTracking() const { return request_stat_names_.has_value(); }

private:
  friend class GrpcWebFilterTest;

  void chargeStat(const Http::ResponseHeaderOrTrailerMap& headers);
  void setupStatTracking(const Http::RequestHeaderMap& headers);
  bool isGrpcWebRequest(const Http::RequestHeaderMap& headers);
  bool isProtoEncodedGrpcWebResponseHeaders(const Http::ResponseHeaderMap& headers) const;
  bool hasProtoEncodedGrpcWebContentType(const Http::RequestOrResponseHeaderMap& headers) const;
  bool needsTransformationForNonProtoEncodedResponse(Http::ResponseHeaderMap& headers,
                                                     bool end_stream) const;
  void mergeAndLimitNonProtoEncodedResponseData(Buffer::OwnedImpl& output,
                                                Buffer::Instance* last_data);
  void setTransformedNonProtoEncodedResponseHeaders(Buffer::Instance* data);

  static const uint8_t GRPC_WEB_TRAILER;
  const absl::flat_hash_set<std::string>& gRpcWebContentTypes() const;

  Upstream::ClusterInfoConstSharedPtr cluster_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
  bool is_text_request_{};
  bool is_text_response_{};
  bool needs_transformation_for_non_proto_encoded_response_{};
  Buffer::OwnedImpl decoding_buffer_;
  Grpc::Decoder decoder_;
  absl::optional<Grpc::Context::RequestStatNames> request_stat_names_;
  bool is_grpc_web_request_{};
  Grpc::Context& context_;
  Http::ResponseHeaderMap* response_headers_{nullptr};
};

} // namespace GrpcWeb
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
