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

// Readers shared by every protocol adapter: a present but unusable field reads as absent and
// sets `malformed`.

// Counts must round-trip exactly through double-backed metadata, and sums of them cannot
// overflow uint64_t, so callers may add them unchecked.
constexpr uint64_t MaxSafeCount = (uint64_t(1) << 53) - 1;

// Caps peer-controlled strings so one record cannot become a multi-megabyte metadata value.
constexpr size_t MaxStringValueSize = 256;

// AllowNullAsAbsent is only for positions whose wire format documents null as a placeholder,
// such as OpenAI's `"usage": null`.
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

const nlohmann::json* readObject(const nlohmann::json& json, absl::string_view key, bool& malformed,
                                 NullPolicy null_policy = NullPolicy::NullIsMalformed);

// A sum above MaxSafeCount is dropped and sets `overflow` rather than published imprecisely.
std::optional<uint64_t> addCounts(std::optional<uint64_t> base,
                                  const std::optional<uint64_t>& extra, bool& overflow);

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
