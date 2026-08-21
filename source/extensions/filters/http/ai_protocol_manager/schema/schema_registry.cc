#include "source/extensions/filters/http/ai_protocol_manager/schema/schema_registry.h"

#include "source/extensions/filters/http/ai_protocol_manager/schema/openai_chat_completions.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

const PayloadSchema* SchemaRegistry::getSchema(ApiProtocol protocol) {
  static const PayloadSchema openai_schema = OpenAI::createPayloadSchema();
  switch (protocol) {
  case ApiProtocol::OpenAiChatCompletions:
    return &openai_schema;
  default:
    return nullptr;
  }
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
