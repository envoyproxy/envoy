#include "extensions/filters/network/kafka/kafka_request_parser.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

ParseResponse RequestStartParser::parse(absl::string_view&) {
  const RequestHeaderDeserializerPtr ptr = std::make_unique<RequestHeaderDeserializer>();
  return ParseResponse::stillWaiting();
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
