#pragma once

#include "extensions/filters/network/kafka/parser.h"
#include "extensions/filters/network/kafka/serialization.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

// =============================================================================
// === RESPONSE ================================================================
// =============================================================================

struct ResponseContext {
  INT32 remaining_request_size_{0};
};

class ResponseStartParser : public Parser {
public:
  ResponseStartParser(ResponseContext&) {};
  ParseResponse parse(const char*& buffer, uint64_t& remaining);
};

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
