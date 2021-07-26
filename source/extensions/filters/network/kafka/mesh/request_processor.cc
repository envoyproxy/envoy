#include "source/extensions/filters/network/kafka/mesh/request_processor.h"

#include "envoy/common/exception.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

// Helper function. Throws a nice message. Filter will react by closing the connection.
static void throwOnUnsupportedRequest(const std::string& reason, const RequestHeader& header) {
  throw EnvoyException(absl::StrCat(reason, " Kafka request (key=", header.api_key_, ", version=",
                                    header.api_version_, ", cid=", header.correlation_id_));
}

void RequestProcessor::onMessage(AbstractRequestSharedPtr arg) {
  // This will be replaced with switch on header's API key.
  throwOnUnsupportedRequest("unsupported (bad client API invoked?)", arg->request_header_);
}

// We got something that the parser could not handle.
void RequestProcessor::onFailedParse(RequestParseFailureSharedPtr arg) {
  throwOnUnsupportedRequest("unknown", arg->request_header_);
}

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
