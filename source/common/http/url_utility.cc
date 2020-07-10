#include "common/http/url_utility.h"

#include <sys/types.h>

#include <iostream>
#include <string>

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/utility.h"

#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Http {
namespace Utility {

bool Url::initialize(absl::string_view absolute_url, bool is_connect) {
  // TODO(dio): When we have access to base::StringPiece, probably we can convert absolute_url to
  // that instead.
  GURL parsed(std::string{absolute_url});
  if (is_connect) {
    return initializeForConnect(std::move(parsed));
  }

  // TODO(dio): Check if we need to accommodate to strictly validate only http(s) AND ws(s) schemes.
  // Currently, we only accept http(s).
  if (!parsed.is_valid() || !parsed.SchemeIsHTTPOrHTTPS()) {
    return false;
  }

  scheme_ = parsed.scheme();

  // Only non-default ports will be rendered as part of host_and_port_. For example,
  // http://www.host.com:80 has port component (i.e. 80). However, since 80 is a default port for
  // http scheme, host_and_port_ will be rendered as www.host.com (without port). The same case with
  // https scheme (with port 443) as well.
  host_and_port_ =
      absl::StrCat(parsed.host(), parsed.has_port() ? ":" : EMPTY_STRING, parsed.port());

  const int port = parsed.EffectiveIntPort();
  if (port <= 0 || port > std::numeric_limits<uint16_t>::max()) {
    return false;
  }
  port_ = static_cast<uint16_t>(port);

  // RFC allows the absolute URI to not end in "/", but the absolute path form must start with "/".
  path_and_query_params_ = parsed.PathForRequest();
  if (parsed.has_ref()) {
    absl::StrAppend(&path_and_query_params_, "#", parsed.ref());
  }

  return true;
}

bool Url::initializeForConnect(GURL&& url) {
  // CONNECT requests can only contain "hostname:port"
  // https://github.com/nodejs/http-parser/blob/d9275da4650fd1133ddc96480df32a9efe4b059b/http_parser.c#L2503-L2506.
  if (!url.is_valid() || url.IsStandard()) {
    return false;
  }

  const auto& parsed = url.parsed_for_possibly_invalid_spec();
  // The parsed.scheme contains the URL's hostname (stored by GURL). While host and port have -1
  // as its length.
  if (parsed.scheme.len <= 0 || parsed.host.len > 0 || parsed.port.len > 0) {
    return false;
  }

  host_and_port_ = url.possibly_invalid_spec();
  const auto& parts = StringUtil::splitToken(host_and_port_, ":", /*keep_empty_string=*/true,
                                             /*trim_whitespace=*/false);
  if (parts.size() != 2 || static_cast<size_t>(parsed.scheme.len) != parts.at(0).size() ||
      !validPortForConnect(parts.at(1))) {
    return false;
  }

  return true;
}

bool Url::validPortForConnect(absl::string_view port_string) {
  int port;
  const bool valid = absl::SimpleAtoi(port_string, &port);
  // Only a port value in valid range (1-65535) is allowed.
  if (!valid || port <= 0 || port > std::numeric_limits<uint16_t>::max()) {
    return false;
  }
  port_ = static_cast<uint16_t>(port);
  return true;
}

} // namespace Utility
} // namespace Http
} // namespace Envoy
