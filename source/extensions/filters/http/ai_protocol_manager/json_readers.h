#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "absl/strings/string_view.h"
#include "nlohmann/json_fwd.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Shared, dialect-agnostic readers over a parsed payload document (the
// nlohmann DOM produced by JsonWithExtBufParser). Every protocol adapter is
// built from these primitives; the fail-open semantics (a present-but-unusable
// known field reads as absent and flags `malformed`) are part of the shared
// extraction contract, not any one dialect.

// Sanity bound on provider-reported counts: values must survive an exact
// round trip through IEEE-double representations (JSON re-serialization,
// access-log pipelines) that downstream consumers commonly apply to the
// typed record, and summed counts must stay far from uint64_t overflow.
// Anything above this bound, or non-finite, negative, or fractional, is
// rejected rather than coerced -- real token counts sit many orders of
// magnitude below it.
constexpr uint64_t MaxSafeCount = (uint64_t(1) << 53) - 1;

// Caps peer-controlled strings so one record cannot become a multi-megabyte metadata value.
constexpr size_t MaxStringValueSize = 256;

// Whether a present-but-null object position is benignly absent or malformed.
// AllowNullAsAbsent is reserved for positions whose wire format documents
// null as a placeholder (OpenAI's `"usage": null`, nullable details objects);
// required structural members (`message`, a terminal event's `response`) are
// malformed when null.
enum class NullPolicy { AllowNullAsAbsent, NullIsMalformed };

// Reads a count (integer or integral double, at most MaxSafeCount). A present but unusable
// value, null included unless `null_policy` allows it, reads as absent and sets `malformed`,
// so a corrupt final update is never published as complete.
std::optional<uint64_t> readCount(const nlohmann::json& json, absl::string_view key,
                                  bool& malformed,
                                  NullPolicy null_policy = NullPolicy::NullIsMalformed);

// Reads a non-empty string of at most MaxStringValueSize. The second overload also flags a
// present non-string (an offloaded reference included) or oversized value; null and "" do not.
// The first stays silent for shape probes, where a mismatch means another dialect.
std::optional<std::string> readString(const nlohmann::json& json, absl::string_view key);
std::optional<std::string> readString(const nlohmann::json& json, absl::string_view key,
                                      bool& malformed);

// A present value of the wrong type sets `malformed`; null reads as absent.
std::optional<bool> readBool(const nlohmann::json& json, absl::string_view key, bool& malformed);
std::optional<uint32_t> readArrayLength(const nlohmann::json& json, absl::string_view key,
                                        bool& malformed);

// Read a nested object, applying the null policy above.
const nlohmann::json* readObject(const nlohmann::json& json, absl::string_view key, bool& malformed,
                                 NullPolicy null_policy = NullPolicy::NullIsMalformed);

// Adds an optional adjunct onto a base count. A sum above the metadata-safe
// bound is dropped rather than published imprecisely, and reported through
// `overflow` so the caller flags the record instead of silently omitting a
// canonical component.
std::optional<uint64_t> addCounts(std::optional<uint64_t> base,
                                  const std::optional<uint64_t>& extra, bool& overflow);

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
