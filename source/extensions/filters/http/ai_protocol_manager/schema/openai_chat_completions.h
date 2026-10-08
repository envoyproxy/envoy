#pragma once

#include "source/extensions/filters/http/ai_protocol_manager/schema.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace OpenAI {

// Reusable sub-schemas and complete payload schema definition for OpenAI Chat Completions.
const Schema& toolCallSchema();
const Schema& chatMessageSchema();
const Schema& toolSchema();

// Returns the full OpenAI Chat Completions PayloadSchema.
PayloadSchema createPayloadSchema();

} // namespace OpenAI
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
