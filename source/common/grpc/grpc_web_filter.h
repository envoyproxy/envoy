#ifndef SOURCE_COMMON_GRPC_GRPC_WEB_FILTER_H_
#define SOURCE_COMMON_GRPC_GRPC_WEB_FILTER_H_

#include "envoy/http/filter.h"

#include "common/buffer/buffer_impl.h"
#include "common/grpc/codec.h"

namespace Envoy {
namespace Grpc {

class GrpcWebFilter : public Http::StreamFilter {
public:
  GrpcWebFilter();
  virtual ~GrpcWebFilter();

  // GrpcWebFilter is neither copyable nor movable.
  GrpcWebFilter(const GrpcWebFilter&) = delete;
  GrpcWebFilter& operator=(const GrpcWebFilter&) = delete;

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
  Buffer::OwnedImpl encoding_buffer_trailers_;
  Decoder decoder_;
};
} // namespace Grpc
} // namespace Envoy

#endif // SOURCE_COMMON_GRPC_GRPC_WEB_FILTER_H_
