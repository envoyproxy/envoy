#include "contrib/kafka/filters/network/source/mesh/command_handlers/api_versions.h"

#include "contrib/kafka/filters/network/source/external/requests.h"
#include "contrib/kafka/filters/network/source/external/responses.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

// These constants define which versions of requests this "Kafka server" will understand.

// As we can process only record format 2 (which itself is pretty old coming from Kafka 1.0), we are
// going to handle only produce requests with versions higher than 5.
constexpr int16_t MIN_PRODUCE_SUPPORTED = 5;
constexpr int16_t MAX_PRODUCE_SUPPORTED = PRODUCE_REQUEST_MAX_VERSION; /* Generated value. */
// List-offsets version 0 uses old-style offsets.
constexpr int16_t MIN_LIST_OFFSETS_SUPPORTED = 1;
constexpr int16_t MAX_LIST_OFFSETS_SUPPORTED =
    LIST_OFFSETS_REQUEST_MAX_VERSION; /* Generated value. */
// Right now we do not want to handle old version 0 request, as it expects us to enumerate all
// topics if list of requested topics is empty.
// Impl note: if filter gains knowledge of upstream cluster topics (e.g. thru admin clients), we
// could decrease this value.
constexpr int16_t MIN_METADATA_SUPPORTED = 1;
constexpr int16_t MAX_METADATA_SUPPORTED = METADATA_REQUEST_MAX_VERSION; /* Generated value. */

ApiVersionsRequestHolder::ApiVersionsRequestHolder(AbstractRequestListener& filter,
                                                   const RequestHeader request_header)
    : BaseInFlightRequest{filter}, request_header_{request_header} {}

// Api Versions requests are immediately ready for answer (as they do not need to reach upstream).
void ApiVersionsRequestHolder::startProcessing() { notifyFilter(); }

// Because these requests can be trivially handled, the responses are okay to be sent downstream at
// any time.
bool ApiVersionsRequestHolder::finished() const { return true; }

AbstractResponseSharedPtr ApiVersionsRequestHolder::computeAnswer() const {
  const ResponseMetadata metadata = {request_header_.api_key_, request_header_.api_version_,
                                     request_header_.correlation_id_};

  const int16_t error_code = 0;
  const ApiVersion produce_entry = {PRODUCE_REQUEST_API_KEY, MIN_PRODUCE_SUPPORTED,
                                    MAX_PRODUCE_SUPPORTED};
  const ApiVersion list_offsets_entry = {LIST_OFFSETS_REQUEST_API_KEY, MIN_LIST_OFFSETS_SUPPORTED,
                                         MAX_LIST_OFFSETS_SUPPORTED};
  const ApiVersion metadata_entry = {METADATA_REQUEST_API_KEY, MIN_METADATA_SUPPORTED,
                                     MAX_METADATA_SUPPORTED};
  const ApiVersionsResponse real_response = {error_code,
                                             {produce_entry, list_offsets_entry, metadata_entry}};

  return std::make_shared<Response<ApiVersionsResponse>>(metadata, real_response);
}

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
