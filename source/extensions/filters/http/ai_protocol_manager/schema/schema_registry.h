#pragma once

#include "source/extensions/filters/http/ai_protocol_manager/schema.h"
#include "source/extensions/filters/http/ai_protocol_manager/token_usage_extractor.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Registry mapping a route's declared wire API to its PayloadSchema, when one
// is defined. APIs without a registered schema (or an undeclared API) return
// nullptr and are not validated.
class SchemaRegistry {
public:
  static const PayloadSchema* getSchema(ApiProtocol protocol);
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
