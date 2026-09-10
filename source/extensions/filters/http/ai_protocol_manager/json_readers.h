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

// Provider- and client-controlled strings; the cap keeps one record from
// turning into a multi-megabyte metadata value or access-log entry.
constexpr size_t MaxStringValueSize = 256;

// Whether a present-but-null object position is benignly absent or malformed.
// AllowNullAsAbsent is reserved for positions whose wire format documents
// null as a placeholder (OpenAI's `"usage": null`, nullable details objects);
// required structural members (`message`, a terminal event's `response`) are
// malformed when null.
enum class NullPolicy { AllowNullAsAbsent, NullIsMalformed };

// Read a token count (integer or JSON double). Nullopt for a missing key; a present
// but unusable key (wrong type, negative, fractional, out of range, or null unless
// `null_policy` allows it) also sets `malformed`, so a corrupt final cumulative
// update cannot leave an earlier value published as complete.
std::optional<uint64_t> readCount(const nlohmann::json& json, absl::string_view key,
                                  bool& malformed,
                                  NullPolicy null_policy = NullPolicy::NullIsMalformed);

// Read a non-empty string of at most MaxStringValueSize; anything else reads as
// absent, an offloaded external reference included. The overload reads the same
// value and flags a present but unusable one; null is absent, not malformed.
std::optional<std::string> readString(const nlohmann::json& json, absl::string_view key);
std::optional<std::string> readString(const nlohmann::json& json, absl::string_view key,
                                      bool& malformed);

// Read a boolean, or an array's direct element count. A present value of the
// wrong type is malformed; null is absent, which is how these wire formats
// spell "unset".
std::optional<bool> readBool(const nlohmann::json& json, absl::string_view key, bool& malformed);
std::optional<uint32_t> readArrayLength(const nlohmann::json& json, absl::string_view key,
                                        bool& malformed);

// Read a nested object, applying the null policy above.
const nlohmann::json* readObject(const nlohmann::json& json, absl::string_view key,
                                 bool& malformed,
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
