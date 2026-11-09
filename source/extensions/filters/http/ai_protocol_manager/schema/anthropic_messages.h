#pragma once

#include "source/extensions/filters/http/ai_protocol_manager/schema.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace Anthropic {

// Reusable sub-schemas and complete payload schema definition for the Anthropic Messages API.
const Schema& textBlockSchema();
const Schema& contentBlockSchema();
const Schema& messageSchema();
const Schema& toolSchema();

// Returns the full Anthropic Messages PayloadSchema.
PayloadSchema createPayloadSchema();

} // namespace Anthropic
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
