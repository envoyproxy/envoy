#include "source/extensions/filters/network/kafka/mesh/request_processor.h"

#include "envoy/common/exception.h"

#include "source/extensions/filters/network/kafka/mesh/command_handlers/api_versions.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

RequestProcessor::RequestProcessor(AbstractRequestListener& origin) : origin_{origin} {}

// Helper function. Throws a nice message. Filter will react by closing the connection.
static void throwOnUnsupportedRequest(const std::string& reason, const RequestHeader& header) {
  throw EnvoyException(absl::StrCat(reason, " Kafka request (key=", header.api_key_, ", version=",
                                    header.api_version_, ", cid=", header.correlation_id_));
}

void RequestProcessor::onMessage(AbstractRequestSharedPtr arg) {
  switch (arg->request_header_.api_key_) {
  case API_VERSIONS_REQUEST_API_KEY:
    process(std::dynamic_pointer_cast<Request<ApiVersionsRequest>>(arg));
    break;
  default:
    // Client sent a request we cannot handle right now.
    throwOnUnsupportedRequest("unsupported (bad client API invoked?)", arg->request_header_);
    break;
  } // switch
}

void RequestProcessor::process(const std::shared_ptr<Request<ApiVersionsRequest>> request) const {
  auto res = std::make_shared<ApiVersionsRequestHolder>(origin_, request->request_header_);
  origin_.onRequest(res);
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
