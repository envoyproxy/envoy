#include "contrib/kafka/filters/network/source/mesh/request_processor.h"

#include "envoy/common/exception.h"

#include "contrib/kafka/filters/network/source/mesh/command_handlers/api_versions.h"
#include "contrib/kafka/filters/network/source/mesh/command_handlers/fetch.h"
#include "contrib/kafka/filters/network/source/mesh/command_handlers/list_offsets.h"
#include "contrib/kafka/filters/network/source/mesh/command_handlers/metadata.h"
#include "contrib/kafka/filters/network/source/mesh/command_handlers/produce.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

RequestProcessor::RequestProcessor(AbstractRequestListener& origin,
                                   const UpstreamKafkaConfiguration& configuration,
                                   UpstreamKafkaFacade& upstream_kafka_facade,
                                   RecordCallbackProcessor& record_callback_processor)
    : origin_{origin}, configuration_{configuration}, upstream_kafka_facade_{upstream_kafka_facade},
      record_callback_processor_{record_callback_processor} {}

// Helper function. Throws a nice message. Filter will react by closing the connection.
static void throwOnUnsupportedRequest(const std::string& reason, const RequestHeader& header) {
  throw EnvoyException(absl::StrCat(reason, " Kafka request (key=", header.api_key_, ", version=",
                                    header.api_version_, ", cid=", header.correlation_id_));
}

void RequestProcessor::onMessage(AbstractRequestSharedPtr arg) {
  switch (arg->request_header_.api_key_) {
  case PRODUCE_REQUEST_API_KEY:
    process(std::dynamic_pointer_cast<Request<ProduceRequest>>(arg));
    break;
  case FETCH_REQUEST_API_KEY:
    process(std::dynamic_pointer_cast<Request<FetchRequest>>(arg));
    break;
  case LIST_OFFSETS_REQUEST_API_KEY:
    process(std::dynamic_pointer_cast<Request<ListOffsetsRequest>>(arg));
    break;
  case METADATA_REQUEST_API_KEY:
    process(std::dynamic_pointer_cast<Request<MetadataRequest>>(arg));
    break;
  case API_VERSIONS_REQUEST_API_KEY:
    process(std::dynamic_pointer_cast<Request<ApiVersionsRequest>>(arg));
    break;
  default:
    // Client sent a request we cannot handle right now.
    throwOnUnsupportedRequest("unsupported (bad client API invoked?)", arg->request_header_);
    break;
  } // switch
}

void RequestProcessor::process(const std::shared_ptr<Request<ProduceRequest>> request) const {
  auto res = std::make_shared<ProduceRequestHolder>(origin_, upstream_kafka_facade_, request);
  origin_.onRequest(res);
}

void RequestProcessor::process(const std::shared_ptr<Request<FetchRequest>> request) const {
  auto res = std::make_shared<FetchRequestHolder>(origin_, record_callback_processor_, request);
  origin_.onRequest(res);
}

void RequestProcessor::process(const std::shared_ptr<Request<ListOffsetsRequest>> request) const {
  auto res = std::make_shared<ListOffsetsRequestHolder>(origin_, request);
  origin_.onRequest(res);
}

void RequestProcessor::process(const std::shared_ptr<Request<MetadataRequest>> request) const {
  auto res = std::make_shared<MetadataRequestHolder>(origin_, configuration_, request);
  origin_.onRequest(res);
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
