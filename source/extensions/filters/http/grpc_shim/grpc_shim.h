#pragma once

#include <string>

#include "envoy/http/filter.h"

#include "common/buffer/buffer_impl.h"
#include "common/grpc/status.h"

#include "extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcShim {

// When enabled, will downgrade an incoming gRPC http request into a h/1.1 request.
class GrpcShim : public Envoy::Http::PassThroughFilter {
public:
  explicit GrpcShim(std::string upstream_content_type)
      : upstream_content_type_(std::move(upstream_content_type)) {}
  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& buffer, bool end_stream) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& buffer, bool end_stream) override;

private:
  const std::string upstream_content_type_;
  bool enabled_{};
  bool prefix_stripped_{};
  std::string content_type_{};
  Grpc::Status::GrpcStatus grpc_status_{};
  // Normally we'd use the encoding buffer, but since we need to mutate the
  // buffer we instead maintain our own.
  Buffer::OwnedImpl buffer_{};
};
} // namespace GrpcShim
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
