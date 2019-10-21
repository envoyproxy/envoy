#pragma once

#include <string>

#include "envoy/config/filter/http/grpc_http1_reverse_bridge/v2alpha1/config.pb.h"
#include "envoy/http/filter.h"

#include "common/buffer/buffer_impl.h"
#include "common/grpc/status.h"

#include "extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcHttp1ReverseBridge {

// When enabled, will downgrade an incoming gRPC http request into a h/1.1 request.
class Filter : public Envoy::Http::PassThroughFilter {
public:
  Filter(std::string upstream_content_type, bool withhold_grpc_frames)
      : upstream_content_type_(std::move(upstream_content_type)),
        withhold_grpc_frames_(withhold_grpc_frames) {}
  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& buffer, bool end_stream) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& buffer, bool end_stream) override;

private:
  const std::string upstream_content_type_;
  const bool withhold_grpc_frames_;

  bool enabled_{};
  bool prefix_stripped_{};
  std::string content_type_{};
  Grpc::Status::GrpcStatus grpc_status_{};
  // Normally we'd use the encoding buffer, but since we need to mutate the
  // buffer we instead maintain our own.
  Buffer::OwnedImpl buffer_{};
};

class FilterConfigPerRoute : public Router::RouteSpecificFilterConfig {
public:
  FilterConfigPerRoute(
      const envoy::config::filter::http::grpc_http1_reverse_bridge::v2alpha1::FilterConfigPerRoute&
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
