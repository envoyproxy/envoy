#pragma once

#include <memory>
#include <string>

#include "envoy/extensions/filters/http/grpc_http1_reverse_bridge/v3/config.pb.h"
#include "envoy/http/filter.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/grpc/status.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcHttp1ReverseBridge {

// When enabled, will downgrade an incoming gRPC http request into a h/1.1 request.
class Filter : public Envoy::Http::PassThroughFilter {
public:
  Filter(std::string upstream_content_type, bool withhold_grpc_frames,
         std::string response_size_header)
      : upstream_content_type_(std::move(upstream_content_type)),
        withhold_grpc_frames_(withhold_grpc_frames),
        response_size_header_(!response_size_header.empty()
                                  ? absl::make_optional(Http::LowerCaseString(response_size_header))
                                  : absl::nullopt) {}
  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& buffer, bool end_stream) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& buffer, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;

private:
  // Prepend the grpc frame into the buffer
  void buildGrpcFrameHeader(Buffer::Instance& buffer, uint32_t message_length);

  const std::string upstream_content_type_;
  const bool withhold_grpc_frames_;
  const absl::optional<Http::LowerCaseString> response_size_header_;

  bool enabled_{};
  bool prefix_stripped_{};

  // Tracking state for gRPC frame status when withholding gRPC frames from the
  // upstream and streaming responses.
  bool frame_header_added_{};
  // The content length reported by the upstream.
  uint32_t response_message_length_{};
  // The actual size of the response returned by the upstream so far.
  uint32_t upstream_response_bytes_{};

  std::string content_type_{};
  Grpc::Status::GrpcStatus grpc_status_{};
  // Normally we'd use the encoding buffer, but since we need to mutate the
  // buffer we instead maintain our own.
  Buffer::OwnedImpl buffer_{};
};

using FilterPtr = std::unique_ptr<Filter>;

class FilterConfigPerRoute : public Router::RouteSpecificFilterConfig {
public:
  FilterConfigPerRoute(
      const envoy::extensions::filters::http::grpc_http1_reverse_bridge::v3::FilterConfigPerRoute&
          config)
      : disabled_(config.disabled()) {}
  bool disabled() const { return disabled_; }

private:
  bool disabled_;
};

} // namespace GrpcHttp1ReverseBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
