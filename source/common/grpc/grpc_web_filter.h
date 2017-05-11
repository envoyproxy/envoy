#ifndef SOURCE_COMMON_GRPC_GRPC_WEB_FILTER_H_
#define SOURCE_COMMON_GRPC_GRPC_WEB_FILTER_H_

#include "envoy/http/filter.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/buffer/buffer_impl.h"
#include "common/grpc/codec.h"

namespace Grpc {

class GrpcWebFilter : public Http::StreamFilter {
public:
  class Constants {
  public:
    static Constants& get() {
      static Constants instance;
      return instance;
    }

    // Constants is neither copyable nor movable.
    Constants(const Constants&) = delete;
    Constants& operator=(const Constants&) = delete;

    const std::string& CONTENT_TYPE_GRPC_WEB() { return content_type_grpc_web_; }

    const std::string& CONTENT_TYPE_GRPC_WEB_TEXT() { return content_type_grpc_web_text_; }

    const std::string& CONTENT_TYPE_GRPC() { return content_type_grpc_; }

    const Http::LowerCaseString& HTTP_KEY_TE() { return http_key_te_; }

    const std::string& HTTP_KEY_TE_VALUE() { return http_key_te_value_; }

    const Http::LowerCaseString& HTTP_KEY_GRPC_ACCEPT_ENCODING() {
      return http_key_grpc_accept_encoding;
    }

    const std::string& HTTP_KEY_GRPC_ACCEPT_ENCODING_VALUE() {
      return http_key_grpc_accept_encoding_value_;
    }

    const uint8_t GRPC_WEB_TRAILER = 0b10000000;

  private:
    Constants() {}

    const std::string content_type_grpc_web_ = "application/grpc-web";
    const std::string content_type_grpc_web_text_ = "application/grpc-web-text";
    const std::string content_type_grpc_ = "application/grpc";
    const Http::LowerCaseString http_key_te_{"te"};
    const std::string http_key_te_value_ = "trailers";
    const Http::LowerCaseString http_key_grpc_accept_encoding{"grpc-accept-encoding"};
    const std::string http_key_grpc_accept_encoding_value_ = "identity,deflate,gzip";
  };

  GrpcWebFilter();
  virtual ~GrpcWebFilter();

  // GrpcWebFilter is neither copyable nor movable.
  GrpcWebFilter(const GrpcWebFilter&) = delete;
  GrpcWebFilter& operator=(const GrpcWebFilter&) = delete;

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
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
  bool is_text_request_;
  bool is_text_response_;
  Buffer::OwnedImpl decoding_buffer_;
  Buffer::OwnedImpl encoding_buffer_trailers_;
  Decoder decoder_;
};
} // namespace Grpc

#endif // SOURCE_COMMON_GRPC_GRPC_WEB_FILTER_H_
