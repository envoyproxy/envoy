#pragma once
#include <string>

namespace Envoy {
namespace Http {

/**
 * Possible HTTP connection/request protocols. The parallel NumProtocols constant allows defining
 * fixed arrays for each protocol, but does not pollute the enum.
 */
enum class Protocol { Http10, Http11, Http2 };
const size_t NumProtocols = 3;

inline std::string getProtocolString(const Protocol& p) {
  switch (p) {
  case Protocol::Http10:
    return std::string("Http1.0");
  case Protocol::Http11:
    return std::string("Http1.1");
  case Protocol::Http2:
    return std::string("Http2");
  default:
    break;
  }
  return "";
}

} // namespace Http
} // namespace Envoy
