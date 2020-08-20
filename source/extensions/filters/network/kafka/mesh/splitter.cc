#include "extensions/filters/network/kafka/mesh/splitter.h"

#include "extensions/filters/network/kafka/mesh/command_handlers/api_versions.h"
#include "extensions/filters/network/kafka/mesh/command_handlers/metadata.h"
#include "extensions/filters/network/kafka/mesh/command_handlers/produce.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

RequestProcessor::RequestProcessor(AbstractRequestListener& origin,
                                   const ClusteringConfiguration& clustering_configuration)
    : origin_{origin}, clustering_configuration_{clustering_configuration} {}

void RequestProcessor::onMessage(AbstractRequestSharedPtr arg) {
  switch (arg->request_header_.api_key_) {
  case /* Produce */ 0: {
    const std::shared_ptr<Request<ProduceRequest>> cast =
        std::dynamic_pointer_cast<Request<ProduceRequest>>(arg);
    process(cast);
    break;
  }
  case /* Metadata */ 3: {
    const std::shared_ptr<Request<MetadataRequest>> cast =
        std::dynamic_pointer_cast<Request<MetadataRequest>>(arg);
    process(cast);
    break;
  }
  case /* ApiVersions */ 18: {
    const std::shared_ptr<Request<ApiVersionsRequest>> cast =
        std::dynamic_pointer_cast<Request<ApiVersionsRequest>>(arg);
    process(cast);
    break;
  }
  default: {
    ENVOY_LOG(warn, "unknown request: {}/{}", arg->request_header_.api_key_,
              arg->request_header_.api_version_);
    break;
  }
  } // switch
}

void RequestProcessor::onFailedParse(RequestParseFailureSharedPtr) {
  ENVOY_LOG(warn, "got parse failure");
  // kill connection.
}

void RequestProcessor::process(const std::shared_ptr<Request<ProduceRequest>> request) const {
  ENVOY_LOG(warn, "RequestProcessor - create(Produce({})) for cid {}",
            request->request_header_.api_version_, request->request_header_.correlation_id_);
  auto res = std::make_shared<ProduceRequestHolder>(origin_, request);
  origin_.onRequest(res);
}

void RequestProcessor::process(const std::shared_ptr<Request<MetadataRequest>> request) const {
  ENVOY_LOG(warn, "RequestProcessor - create(Metadata) for cid {}",
            request->request_header_.correlation_id_);
  auto res = std::make_shared<MetadataRequestHolder>(origin_, clustering_configuration_, request);
  origin_.onRequest(res);
}

void RequestProcessor::process(const std::shared_ptr<Request<ApiVersionsRequest>> request) const {
  ENVOY_LOG(warn, "RequestProcessor - create(ApiVersions) for cid {}",
            request->request_header_.correlation_id_);
  auto res = std::make_shared<ApiVersionsRequestHolder>(origin_, request);
  origin_.onRequest(res);
}

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
