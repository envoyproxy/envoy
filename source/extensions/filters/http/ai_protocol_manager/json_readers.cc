#include "source/extensions/filters/http/ai_protocol_manager/json_readers.h"

#include <cmath>

#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

std::optional<uint64_t> readCount(const nlohmann::json& json, const std::string& key,
                                  bool& malformed) {
  const auto it = json.find(key);
  if (it == json.end()) {
    return std::nullopt;
  }
  const nlohmann::json& value = *it;
  // JsonWithExtBufParser stores any literal that fits int64 as a *signed*
  // integer (is_number_unsigned() is true only above INT64_MAX), so probe the
  // signed representation first.
  if (value.is_number_integer()) {
    if (value.is_number_unsigned()) {
      const uint64_t count = value.get<uint64_t>();
      if (count > MaxSafeCount) {
        malformed = true;
        return std::nullopt;
      }
      return count;
    }
    const int64_t count = value.get<int64_t>();
    if (count < 0 || static_cast<uint64_t>(count) > MaxSafeCount) {
      malformed = true;
      return std::nullopt;
    }
    return static_cast<uint64_t>(count);
  }
  if (value.is_number_float()) {
    const double as_double = value.get<double>();
    // Range-check before the float-to-integer cast: converting an
    // out-of-range double to uint64_t is undefined behavior.
    if (!std::isfinite(as_double) || as_double < 0 ||
        as_double > static_cast<double>(MaxSafeCount) || std::trunc(as_double) != as_double) {
      malformed = true;
      return std::nullopt;
    }
    return static_cast<uint64_t>(as_double);
  }
  malformed = true; // Present with a non-numeric value (string, bool, null, container).
  return std::nullopt;
}

std::optional<std::string> readString(const nlohmann::json& json, const std::string& key) {
  const auto it = json.find(key);
  if (it == json.end() || !it->is_string()) {
    return std::nullopt;
  }
  const auto& value = it->get_ref<const std::string&>();
  if (value.empty() || value.size() > MaxStringValueSize) {
    return std::nullopt;
  }
  return value;
}

const nlohmann::json* readObject(const nlohmann::json& json, const std::string& key,
                                 bool& malformed, NullPolicy null_policy) {
  const auto it = json.find(key);
  if (it == json.end()) {
    return nullptr;
  }
  if (it->is_object()) {
    return &*it;
  }
  if (!it->is_null() || null_policy == NullPolicy::NullIsMalformed) {
    malformed = true;
  }
  return nullptr;
}

std::optional<uint64_t> addCounts(std::optional<uint64_t> base,
                                  const std::optional<uint64_t>& extra, bool& overflow) {
  if (!base.has_value() || !extra.has_value()) {
    return base;
  }
  const uint64_t sum = base.value() + extra.value();
  if (sum > MaxSafeCount) {
    overflow = true;
    return std::nullopt;
  }
  return sum;
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
