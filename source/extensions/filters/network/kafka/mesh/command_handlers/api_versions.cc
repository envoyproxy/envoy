#include "extensions/filters/network/kafka/mesh/command_handlers/api_versions.h"

#include "extensions/filters/network/kafka/external/responses.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

ApiVersionsRequestHolder::ApiVersionsRequestHolder(
    AbstractRequestListener& filter, const std::shared_ptr<Request<ApiVersionsRequest>> request)
    : AbstractInFlightRequest{filter}, request_{request} {}

// Api Versions requests are immediately ready for answer (as they do not need to reach upstream).
void ApiVersionsRequestHolder::invoke(UpstreamKafkaFacade&) { notifyFilter(); }

bool ApiVersionsRequestHolder::finished() const { return true; }

AbstractResponseSharedPtr ApiVersionsRequestHolder::computeAnswer() const {
  const auto& header = request_->request_header_;
  ResponseMetadata metadata = {header.api_key_, header.api_version_, header.correlation_id_};

  ApiVersionsResponseKey produce_key = {0, 5, 5};
  ApiVersionsResponseKey metadata_key = {3, 1, 1};
  ApiVersionsResponse data = {0, {produce_key, metadata_key}};
  return std::make_shared<Response<ApiVersionsResponse>>(metadata, data);
}

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
