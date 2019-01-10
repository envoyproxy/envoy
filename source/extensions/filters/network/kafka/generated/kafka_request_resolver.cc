// DO NOT EDIT - THIS FILE WAS GENERATED
// clang-format off
#include "extensions/filters/network/kafka/generated/requests.h"
#include "extensions/filters/network/kafka/kafka_request.h"
#include "extensions/filters/network/kafka/parser.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

const RequestParserResolver RequestParserResolver::INSTANCE;

ParserSharedPtr RequestParserResolver::createParser(int16_t api_key, int16_t api_version,
                                                    RequestContextSharedPtr context) const {

  if (8 == api_key && 0 == api_version) {
    return std::make_shared<OffsetCommitRequestV0Parser>(context);
  }
  if (8 == api_key && 1 == api_version) {
    return std::make_shared<OffsetCommitRequestV1Parser>(context);
  }
  if (8 == api_key && 2 == api_version) {
    return std::make_shared<OffsetCommitRequestV2Parser>(context);
  }
  if (8 == api_key && 3 == api_version) {
    return std::make_shared<OffsetCommitRequestV3Parser>(context);
  }
  return std::make_shared<SentinelParser>(context);
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
// clang-format on
