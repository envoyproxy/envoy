#include "source/extensions/filters/http/ai_protocol_manager/schema/schema_registry.h"

#include "source/extensions/filters/http/ai_protocol_manager/schema/openai_chat_completions.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

const PayloadSchema* SchemaRegistry::getSchema(PerRouteProto::Schema schema) {
  switch (schema) {
  case PerRouteProto::OPENAI_CHAT_COMPLETIONS: {
    static const PayloadSchema openai_schema = OpenAi::createPayloadSchema();
    return &openai_schema;
  }
  case PerRouteProto::UNSPECIFIED:
  default:
    return nullptr;
  }
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
