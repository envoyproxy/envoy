#ifndef SOURCE_COMMON_GRPC_GRPC_WEB_FILTER_H_
#define SOURCE_COMMON_GRPC_GRPC_WEB_FILTER_H_

#include "envoy/http/filter.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/buffer/buffer_impl.h"
#include "common/grpc/codec.h"

namespace Grpc {

class GrpcWebFilter : public Http::StreamFilter {
public:
  GrpcWebFilter();
  virtual ~GrpcWebFilter();

  // GrpcWebFilter is neither copyable nor movable.
  GrpcWebFilter(const GrpcWebFilter&) = delete;
  GrpcWebFilter& operator=(const GrpcWebFilter&) = delete;

  // Implements StreamDecoderFilter.
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap&, bool) override;
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override;

  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap&) {
    return Http::FilterTrailersStatus::Continue;
  }
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
    decoder_callbacks_ = &callbacks;
  }

  // Implements StreamEncoderFilter.
  Http::FilterHeadersStatus encodeHeaders(Http::HeaderMap&, bool) override;
  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override;
  Http::FilterTrailersStatus encodeTrailers(Http::HeaderMap& trailers) override;
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) {
    encoder_callbacks_ = &callbacks;
  }

  static const std::string GRPC_WEB_CONTENT_TYPE;
  static const std::string GRPC_WEB_TEXT_CONTENT_TYPE;
  static const std::string GRPC_CONTENT_TYPE;
  static const uint8_t GRPC_WEB_TRAILER;
  static const Http::LowerCaseString HTTP_TE_KEY;
  static const std::string HTTP_TE_VALUE;
  static const Http::LowerCaseString GRPC_ACCEPT_ENCODING_KEY;
  static const std::string GRPC_ACCEPT_ENCODING_VALUE;

private:
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
  bool is_text_request_;
  bool is_text_response_;
  Buffer::OwnedImpl decoding_buffer_;
  Decoder decoder_;
};
} // namespace Grpc

#endif // SOURCE_COMMON_GRPC_GRPC_WEB_FILTER_H_
