#pragma once

#include "envoy/server/filter_config.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/connect_grpc_bridge/end_stream_response.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ConnectGrpcBridge {

using HeaderPair = std::pair<Http::LowerCaseString, std::string>;

class ConnectGrpcBridgeFilter : public Http::PassThroughFilter,
                                public Logger::Loggable<Logger::Id::filter> {
public:
  ConnectGrpcBridgeFilter();

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;

  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool at_end) override;

  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;

  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool at_end) override;

  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;

  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }

  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override {
    encoder_callbacks_ = &callbacks;
  }

private:
  bool decoderBufferLimitReached(uint64_t buffer_length);
  bool encoderBufferLimitReached(uint64_t buffer_length);

  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{nullptr};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{nullptr};

  bool is_connect_unary_{false};
  bool is_connect_streaming_{false};
  bool is_grpc_response_{false};
  uint8_t unary_payload_frame_flags_{0};
  uint64_t drained_frame_header_bytes_{0};
  Buffer::OwnedImpl request_buffer_;
  Buffer::OwnedImpl response_buffer_;
  Http::ResponseHeaderMap* response_headers_;
};

} // namespace ConnectGrpcBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
