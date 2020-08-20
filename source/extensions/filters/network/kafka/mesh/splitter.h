#pragma once

#include "common/common/logger.h"

#include "extensions/filters/network/kafka/external/requests.h"
#include "extensions/filters/network/kafka/mesh/abstract_command.h"
#include "extensions/filters/network/kafka/request_codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

class RequestProcessor : public RequestCallback, private Logger::Loggable<Logger::Id::kafka> {
public:
  RequestProcessor(AbstractRequestListener& origin,
                   const ClusteringConfiguration& clustering_configuration);

  // RequestCallback
  void onMessage(AbstractRequestSharedPtr arg) override;
  void onFailedParse(RequestParseFailureSharedPtr) override;

private:
  void process(const std::shared_ptr<Request<ProduceRequest>> request) const;
  void process(const std::shared_ptr<Request<MetadataRequest>> request) const;
  void process(const std::shared_ptr<Request<ApiVersionsRequest>> request) const;

  AbstractRequestListener& origin_;
  const ClusteringConfiguration& clustering_configuration_;
};

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
