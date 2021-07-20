#include "source/extensions/filters/network/kafka/mesh/command_handlers/api_versions.h"

#include "source/extensions/filters/network/kafka/external/responses.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

constexpr int16_t PRODUCE_API_KEY = 0;
// Introduced in Kafka 1.0, together with record format 2.
constexpr int16_t MIN_PRODUCE_SUPPORTED = 5;
constexpr int16_t MAX_PRODUCE_SUPPORTED = 8;

constexpr int16_t METADATA_KEY = 3;
constexpr int16_t MIN_METADATA_SUPPORTED = 1;
constexpr int16_t MAX_METADATA_SUPPORTED = 1;

ApiVersionsRequestHolder::ApiVersionsRequestHolder(
    AbstractRequestListener& filter, const std::shared_ptr<Request<ApiVersionsRequest>> request)
    : BaseInFlightRequest{filter}, request_{request} {}

// Api Versions requests are immediately ready for answer (as they do not need to reach upstream).
void ApiVersionsRequestHolder::startProcessing() { notifyFilter(); }

bool ApiVersionsRequestHolder::finished() const { return true; }

AbstractResponseSharedPtr ApiVersionsRequestHolder::computeAnswer() const {
  const auto& header = request_->request_header_;
  const ResponseMetadata metadata = {header.api_key_, header.api_version_, header.correlation_id_};

  const int16_t error_code = 0;
  const ApiVersionsResponseKey produce_key = {PRODUCE_API_KEY, MIN_PRODUCE_SUPPORTED,
                                              MAX_PRODUCE_SUPPORTED};
  const ApiVersionsResponseKey metadata_key = {METADATA_KEY, MIN_METADATA_SUPPORTED,
                                               MAX_METADATA_SUPPORTED};
  const ApiVersionsResponse real_response = {error_code, {produce_key, metadata_key}};

  return std::make_shared<Response<ApiVersionsResponse>>(metadata, real_response);
}

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
