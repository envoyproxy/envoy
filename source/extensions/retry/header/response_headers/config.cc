#include "extensions/retry/header/response_headers/config.h"

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Header {

Upstream::RetryHeaderSharedPtr
ResponseHeadersRetryHeaderFactory::createRetryHeader(const Protobuf::Message&,
                                                     ProtobufMessage::ValidationVisitor&) {

  return std::make_shared<ResponseHeadersRetryHeader>();
}

REGISTER_FACTORY(ResponseHeadersRetryHeaderFactory, Upstream::RetryHeaderFactory);

} // namespace Header
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
