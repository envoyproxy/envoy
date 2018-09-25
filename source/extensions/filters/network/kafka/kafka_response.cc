#include "extensions/filters/network/kafka/kafka_response.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

ParseResponse ResponseStartParser::parse(const char*&, uint64_t&) {
  return ParseResponse::stillWaiting();
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
