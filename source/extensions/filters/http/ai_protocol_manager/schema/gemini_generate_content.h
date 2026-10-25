#pragma once

#include "source/extensions/filters/http/ai_protocol_manager/schema.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace Gemini {

// A numeric field: the number itself, or its decimal string form.
Schema numberOrString();
Schema numberOrString(double min, double max);

// An enum field: the symbolic name, or the integer it maps to. Distinct from
// `numberOrString()` because the string side is a name, not a number.
Schema enumNameOrNumber();

// Returns a nullable copy of a shared sub-schema. Needed because the same schema backs both
// a singular field, where ProtoJSON permits null, and repeated field elements, where it
// does not.
Schema asNullable(const Schema& schema);

// Reusable sub-schemas and complete payload schema definition for the Gemini
// generateContent API.
const Schema& partSchema();
const Schema& contentSchema();
const Schema& functionDeclarationSchema();
const Schema& toolSchema();
const Schema& toolConfigSchema();
const Schema& safetySettingSchema();
const Schema& generationConfigSchema();

// Returns the full Gemini generateContent PayloadSchema.
PayloadSchema createPayloadSchema();

} // namespace Gemini
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
