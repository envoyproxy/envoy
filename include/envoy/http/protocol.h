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

static const std::string DefaultString = "";
static const std::string Http10String = "HTTP/1.0";
static const std::string Http11String = "HTTP/1.1";
static const std::string Http2String = "HTTP/2";

inline const std::string& getProtocolString(const Protocol& p) {
  switch (p) {
  case Protocol::Http10:
    return Http10String;
  case Protocol::Http11:
    return Http11String;
  case Protocol::Http2:
    return Http2String;
  default:
    break;
  }
  return DefaultString;
}

} // namespace Http
} // namespace Envoy
