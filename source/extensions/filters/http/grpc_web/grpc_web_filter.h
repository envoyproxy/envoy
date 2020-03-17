#pragma once

#include <unordered_set>

#include "envoy/http/filter.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/non_copyable.h"
#include "common/grpc/codec.h"
#include "common/grpc/context_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcWeb {

/**
 * See docs/configuration/http_filters/grpc_web_filter.rst
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
  Http::FilterHeadersStatus encode100ContinueHeaders(Http::ResponseHeaderMap&) override {
    return Http::FilterHeadersStatus::Continue;
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

  bool doStatTracking() const { return request_names_.has_value(); }

private:
  friend class GrpcWebFilterTest;

  void chargeStat(const Http::ResponseHeaderOrTrailerMap& headers);
  void setupStatTracking(const Http::RequestHeaderMap& headers);
  bool isGrpcWebRequest(const Http::RequestHeaderMap& headers);

  static const uint8_t GRPC_WEB_TRAILER;
  const absl::flat_hash_set<std::string>& gRpcWebContentTypes() const;

  Upstream::ClusterInfoConstSharedPtr cluster_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
  bool is_text_request_{};
  bool is_text_response_{};
  Buffer::OwnedImpl decoding_buffer_;
  Grpc::Decoder decoder_;
  absl::optional<Grpc::Context::RequestNames> request_names_;
  bool is_grpc_web_request_{};
  Grpc::Context& context_;
};

} // namespace GrpcWeb
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
