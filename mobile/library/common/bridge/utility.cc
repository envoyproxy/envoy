#include "library/common/bridge/utility.h"

#include <cstdlib>
#include <string>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/empty_string.h"

namespace Envoy {
namespace Bridge {
namespace Utility {

envoy_error_code_t errorCodeFromLocalStatus(Http::Code status) {
  switch (status) {
  case Http::Code::RequestTimeout:
    return ENVOY_REQUEST_TIMEOUT;
  case Http::Code::PayloadTooLarge:
    return ENVOY_BUFFER_LIMIT_EXCEEDED;
  case Http::Code::ServiceUnavailable:
    return ENVOY_CONNECTION_FAILURE;
  default:
    return ENVOY_UNDEFINED_ERROR;
  }
}

envoy_error toBridgeError(const EnvoyError& error) {
  envoy_error error_bridge{};
  error_bridge.message = copyToBridgeData(error.message_);
  error_bridge.error_code = error.error_code_;
  if (error.attempt_count_.has_value()) {
    error_bridge.attempt_count = *error.attempt_count_;
  }
  return error_bridge;
}

void updateMaxBytes(uint32_t& max_bytes, const Buffer::Instance& data) {
  if (max_bytes == 0) {
    max_bytes = data.length();
  } else {
    max_bytes = std::min<uint32_t>(max_bytes, data.length());
  }
}

envoy_data toBridgeData(Buffer::Instance& data, uint32_t max_bytes) {
  updateMaxBytes(max_bytes, data);
  envoy_data bridge_data = copyToBridgeData(data, max_bytes);
  data.drain(bridge_data.length);
  return bridge_data;
}

envoy_data copyToBridgeData(absl::string_view str) {
  uint8_t* buffer = static_cast<uint8_t*>(safe_malloc(sizeof(uint8_t) * str.length()));
  memcpy(buffer, str.data(), str.length()); // NOLINT(safe-memcpy)
  return {str.length(), buffer, free, buffer};
}

envoy_data copyToBridgeData(const Buffer::Instance& data, uint32_t max_bytes) {
  updateMaxBytes(max_bytes, data);
  uint8_t* buffer = static_cast<uint8_t*>(safe_malloc(sizeof(uint8_t) * max_bytes));
  data.copyOut(0, max_bytes, buffer);
  return {static_cast<size_t>(max_bytes), buffer, free, buffer};
}

std::string copyToString(envoy_data data) {
  if (data.length == 0) {
    return EMPTY_STRING;
  }
  return std::string(const_cast<char*>(reinterpret_cast<const char*>((data.bytes))), data.length);
}

} // namespace Utility
} // namespace Bridge
} // namespace Envoy
