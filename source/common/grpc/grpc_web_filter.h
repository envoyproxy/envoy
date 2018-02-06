#pragma once

#include <unordered_set>

#include "envoy/http/filter.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/non_copyable.h"
#include "common/grpc/codec.h"

namespace Envoy {
namespace Grpc {

/**
 * See docs/configuration/http_filters/grpc_web_filter.rst
 */
class GrpcWebFilter : public Http::StreamFilter, NonCopyable {
public:
  GrpcWebFilter(Upstream::ClusterManager& cm) : cm_(cm) {}
  virtual ~GrpcWebFilter(){};

  // Http::StreamFilterBase
  void onDestroy() override { stream_destroyed_ = true; };

  // Implements StreamDecoderFilter.
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap&, bool) override;
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }

  // Implements StreamEncoderFilter.
  Http::FilterHeadersStatus encode100ContinueHeaders(Http::HeaderMap&) override {
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterHeadersStatus encodeHeaders(Http::HeaderMap&, bool) override;
  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override;
  Http::FilterTrailersStatus encodeTrailers(Http::HeaderMap& trailers) override;
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override {
    encoder_callbacks_ = &callbacks;
  }

private:
  friend class GrpcWebFilterTest;

  void chargeStat(const Http::HeaderMap& headers);
  void setupStatTracking(const Http::HeaderMap& headers);
  bool isGrpcWebRequest(const Http::HeaderMap& headers);

  static const uint8_t GRPC_WEB_TRAILER;
  const std::unordered_set<std::string>& gRpcWebContentTypes() const;

  Upstream::ClusterManager& cm_;
  Upstream::ClusterInfoConstSharedPtr cluster_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
  bool is_text_request_{};
  bool is_text_response_{};
  Buffer::OwnedImpl decoding_buffer_;
  Decoder decoder_;
  std::string grpc_service_;
  std::string grpc_method_;
  bool do_stat_tracking_{};
  bool stream_destroyed_{};
  bool is_grpc_web_request_{};
};

} // namespace Grpc
} // namespace Envoy
