#pragma once

#include "envoy/http/filter.h"

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
  GrpcWebFilter();
  virtual ~GrpcWebFilter();

  void onDestroy() override{};

  // Implements StreamDecoderFilter.
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap&, bool) override;
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override;

  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }

  // Implements StreamEncoderFilter.
  Http::FilterHeadersStatus encodeHeaders(Http::HeaderMap&, bool) override;
  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override;
  Http::FilterTrailersStatus encodeTrailers(Http::HeaderMap& trailers) override;
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override {
    encoder_callbacks_ = &callbacks;
  }

private:
  static const uint8_t GRPC_WEB_TRAILER;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
  bool is_text_request_;
  bool is_text_response_;
  Buffer::OwnedImpl decoding_buffer_;
  Decoder decoder_;
};
} // namespace Grpc
} // namespace Envoy
