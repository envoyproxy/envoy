#pragma once

#include "envoy/common/pure.h"

#include "source/extensions/filters/http/ai_protocol_manager/token_usage.h"

#include "nlohmann/json_fwd.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

class PayloadSchema;

// Everything the filter needs to know about one wire API, behind one
// interface: adapters answer "how does this dialect express X" -- its payload
// schema, where usage lives, which streaming events end extraction, and how
// native counts canonicalize. Feature and policy logic ("what do we do to
// this traffic") stays outside, in the filter and, later, a payload filter
// chain: supporting a new wire API means implementing one adapter, and every
// feature works on it unchanged.
//
// Adapters are stateless singletons owned by AdapterRegistry; all per-stream
// state lives in the caller (TokenUsage accumulation, detection lock).
class ApiProtocolAdapter {
public:
  virtual ~ApiProtocolAdapter() = default;

  virtual ApiProtocol protocol() const PURE;

  // The declarative payload contract for this dialect, or nullptr when none
  // is defined yet. Drives request validation (and, later, offload planning
  // and transforms).
  virtual const PayloadSchema* schema() const PURE;

  // Extract the partial *native* usage one parsed document carries -- an SSE
  // event's data payload, one streamed-array element, or a whole JSON body
  // (oversized strings appear as external-reference nodes and read as
  // absent). Absent fields stay unset; the caller accumulates via
  // TokenUsage::merge() and canonicalizes once in TokenUsage::finalize().
  // Non-virtual: the shared prologue stamps the result with the adapter's own
  // protocol, and the dialect fills in its counts via extractUsageInto().
  ExtractionResult extractUsage(const nlohmann::json& json) const {
    ExtractionResult result;
    result.usage.api_protocol = protocol();
    extractUsageInto(json, result);
    return result;
  }

  // Rewrite the accumulated native counts onto the canonical inclusive
  // contract (e.g. Anthropic sums its disjoint cache buckets into input;
  // Gemini adds tool-use and thoughts). A sum exceeding the metadata-safe
  // bound is dropped and reported through `overflow`. Called exactly once,
  // by TokenUsage::finalize().
  virtual void canonicalizeUsage(TokenUsage& usage, bool& overflow) const PURE;

  // True when this document marks the dialect's logical end of extraction
  // (Anthropic `message_stop`, OpenAI Responses terminal lifecycle events).
  // Callers must extractUsage() first: terminal Responses events also carry
  // the usage. Chat Completions' non-JSON `[DONE]` is handled before parsing.
  virtual bool isTerminalEvent(const nlohmann::json& json) const PURE;

protected:
  // The dialect's usage reads, onto a result whose usage is already stamped
  // with protocol(). A known field with an unusable value reads as absent and
  // sets `result.malformed`; an in-band stream error sets
  // `result.stream_error`.
  virtual void extractUsageInto(const nlohmann::json& json, ExtractionResult& result) const PURE;
};

// The registry mapping each ApiProtocol to its adapter, plus shape detection.
// get() is total: undefined and Unspecified protocols resolve to a no-op
// adapter (no schema, no usage, no terminal events), so callers need no null
// checks.
class AdapterRegistry {
public:
  static const ApiProtocolAdapter& get(ApiProtocol protocol);

  // Detect the API dialect from a response document's shape. Detection is
  // stream-global once locked, so only strongly shaped, value-validated
  // markers decide; anything else stays Unspecified for a later document.
  // Marker checks are ordered from most to least structurally distinctive.
  static ApiProtocol detect(const nlohmann::json& json);
};

// Canonicalizes a finalized accumulation with its own protocol's adapter --
// the one-liner every caller wants; see TokenUsage::finalize().
void finalizeUsage(TokenUsage& usage);

// Whether an SSE `event:` name is an OpenAI Responses terminal lifecycle
// event (the usage carriers). Shared by the adapter's isTerminalEvent() and
// the SSE handler's pre-parse classification so the event-name list has one
// owner.
bool isOpenAiResponsesTerminalEventType(absl::string_view event_type);

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
