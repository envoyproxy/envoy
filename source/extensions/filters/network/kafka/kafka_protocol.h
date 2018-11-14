#pragma once

#include <vector>

#include "common/common/macros.h"

#include "extensions/filters/network/kafka/kafka_types.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

/**
 * Kafka request type identifier
 * @see http://kafka.apache.org/protocol.html#protocol_api_keys
 */
enum RequestType : int16_t {
  OffsetCommit = 8,
};

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
