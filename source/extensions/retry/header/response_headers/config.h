#pragma once

#include "envoy/config/retry/response_headers/v2/response_headers_config.pb.h"
#include "envoy/upstream/retry.h"

#include "common/protobuf/protobuf.h"

#include "extensions/retry/header/response_headers/response_headers.h"
#include "extensions/retry/header/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Header {

class ResponseHeadersRetryHeaderFactory : public Upstream::RetryHeaderFactory {
public:
  std::string name() const override { return RetryHeaderValues::get().ResponseHeadersRetryHeader; }

  Upstream::RetryHeaderSharedPtr
  createRetryHeader(const Protobuf::Message& config,
                    ProtobufMessage::ValidationVisitor& validation_visitor, const uint32_t retry_on,
                    const std::vector<uint32_t>& retriable_status_codes,
                    const std::vector<Http::HeaderMatcherSharedPtr>& retriable_headers) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr(
        new envoy::config::retry::response_headers::v2::ResponseHeadersConfig());
  }
};

} // namespace Header
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
