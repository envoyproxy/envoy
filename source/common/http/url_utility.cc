#include "common/http/url_utility.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"

#include "absl/strings/str_cat.h"
#include "url/gurl.h"

namespace Envoy {
namespace Http {
namespace Utility {

constexpr char COLON[] = ":";
constexpr char HASH[] = "#";

bool Url::initialize(absl::string_view absolute_url) {
  // TODO(dio): When we have access to base::StringPiece, probably we can convert absolute_url to
  // that instead.
  GURL parsed(std::string{absolute_url});
  if (!parsed.is_valid() || (!parsed.SchemeIsHTTPOrHTTPS())) {
    return false;
  }

  scheme_ = parsed.scheme();

  // Only non-default ports will be rendered as part of host_and_port_. For example,
  // http://www.host.com:80 has port component (i.e. 80). However, since 80 is a default port for
  // http scheme, host_and_port_ will be rendered as www.host.com (without port). The same case with
  // https scheme (with port 443) as well.
  host_and_port_ =
      absl::StrCat(parsed.host(), parsed.has_port() ? COLON : EMPTY_STRING, parsed.port());

  const int port = parsed.EffectiveIntPort();
  ASSERT(port <= std::numeric_limits<uint16_t>::max() && port >= 0);
  port_ = static_cast<uint16_t>(port);

  // RFC allows the absolute-uri to not end in "/", but the absolute path form must start with "/".
  path_and_query_params_ = parsed.PathForRequest();
  if (parsed.has_ref()) {
    absl::StrAppend(&path_and_query_params_, HASH, parsed.ref());
  }

  return true;
}

} // namespace Utility
} // namespace Http
} // namespace Envoy
