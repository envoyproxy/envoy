#include "upstream_http_protocol.h"

#include <stdexcept>

namespace Envoy {
namespace Platform {

static const std::pair<UpstreamHttpProtocol, std::string> UPSTREAM_HTTP_PROTOCOL_LOOKUP[]{
    {UpstreamHttpProtocol::HTTP1, "http1"},
    {UpstreamHttpProtocol::HTTP2, "http2"},
};

std::string upstream_http_protocol_to_string(UpstreamHttpProtocol protocol) {
  for (const auto& pair : UPSTREAM_HTTP_PROTOCOL_LOOKUP) {
    if (pair.first == protocol) {
      return pair.second;
    }
  }
  throw std::invalid_argument("invalid upstream http protocol");
}

UpstreamHttpProtocol upstream_http_protocol_from_string(const std::string& str) {
  for (const auto& pair : UPSTREAM_HTTP_PROTOCOL_LOOKUP) {
    if (pair.second == str) {
      return pair.first;
    }
  }
  throw std::invalid_argument("invalid upstream http protocol");
}

} // namespace Platform
} // namespace Envoy
