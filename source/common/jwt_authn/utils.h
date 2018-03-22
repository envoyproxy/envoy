#pragma once

namespace Envoy {
namespace JwtAuthn {

// Base64 URl Deocde.
// The Envoy Base64:decode is using "+" and "/"
// But JWT is UrlSafe encode with is using "-" and "_".
std::string decodeBase64Url(std::string input);

// A convinence inline cast function.
inline const uint8_t* castToUChar(const std::string& str) {
  return reinterpret_cast<const uint8_t*>(str.c_str());
}

} // namespace JwtAuthn
} // namespace Envoy
