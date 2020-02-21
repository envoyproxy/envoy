#include "extensions/retry/header/response_headers/config.h"

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Header {

Upstream::RetryHeaderSharedPtr ResponseHeadersRetryHeaderFactory::createRetryHeader(
    const Protobuf::Message&, ProtobufMessage::ValidationVisitor&, const uint32_t retry_on,
    const std::vector<uint32_t>& retriable_status_codes,
    const std::vector<Http::HeaderMatcherSharedPtr>& retriable_headers) {

  return std::make_shared<ResponseHeadersRetryHeader>(retry_on, retriable_status_codes,
                                                      retriable_headers);
}

REGISTER_FACTORY(ResponseHeadersRetryHeaderFactory, Upstream::RetryHeaderFactory);

} // namespace Header
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
