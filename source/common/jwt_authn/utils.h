#pragma once

namespace Envoy {
namespace JwtAuthn {

// A convinence inline cast function.
inline const uint8_t* castToUChar(const std::string& str) {
  return reinterpret_cast<const uint8_t*>(str.c_str());
}

} // namespace JwtAuthn
} // namespace Envoy
