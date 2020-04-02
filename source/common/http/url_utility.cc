#include "common/http/url_utility.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"

#include "absl/strings/str_cat.h"

#ifndef WIN32
#include "url/gurl.h"
#else
#include <http_parser.h>
#endif

namespace Envoy {
namespace Http {
namespace Utility {

#ifndef WIN32
constexpr char COLON[] = ":";
constexpr char HASH[] = "#";
#else
constexpr char DEFAULT_PATH[] = "/";
constexpr char HTTP[] = "http";
constexpr char HTTPS[] = "https";
#endif

bool Url::initialize(absl::string_view absolute_url) {
#ifndef WIN32
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
#else
  struct http_parser_url u;
  const bool is_connect = false;
  http_parser_url_init(&u);
  const int result =
      http_parser_parse_url(absolute_url.data(), absolute_url.length(), is_connect, &u);

  if (result != 0) {
    return false;
  }
  if ((u.field_set & (1 << UF_HOST)) != (1 << UF_HOST) &&
      (u.field_set & (1 << UF_SCHEMA)) != (1 << UF_SCHEMA)) {
    return false;
  }
  scheme_ = absl::string_view(absolute_url.data() + u.field_data[UF_SCHEMA].off,
                              u.field_data[UF_SCHEMA].len);

  uint16_t authority_len = u.field_data[UF_HOST].len;
  uint16_t authority_and_port_len = u.field_data[UF_HOST].len;

  bool has_port = (u.field_set & (1 << UF_PORT)) == (1 << UF_PORT);
  if (has_port) {
    int port = 0;
    const bool converted_to_int =
        absl::SimpleAtoi(absl::string_view(absolute_url.data() + u.field_data[UF_PORT].off,
                                           u.field_data[UF_PORT].len),
                         &port);
    ASSERT(converted_to_int);
    ASSERT(port <= std::numeric_limits<uint16_t>::max() && port >= 0);
    port_ = static_cast<uint16_t>(port);
    authority_and_port_len = authority_and_port_len + u.field_data[UF_PORT].len + 1;
  } else {
    // When there is no port specified in the input URL, we set the port_ value by default ports of
    // the known schemes.
    if (scheme_ == HTTP) {
      port_ = 80;
    } else if (scheme_ == HTTPS) {
      port_ = 443;
    } else {
      port_ = 0;
    }
  }

  const bool has_http_default_port = port_ == 80 && scheme_ == HTTP;
  const bool has_https_default_port = port_ == 443 && scheme_ == HTTPS;
  if (!has_http_default_port && !has_https_default_port) {
    authority_len = authority_len + u.field_data[UF_PORT].len + 1;
  }
  host_and_port_ =
      absl::string_view(absolute_url.data() + u.field_data[UF_HOST].off, authority_len);

  // RFC allows the absolute-uri to not end in "/", but the absolute path form must start with "/".
  const uint64_t path_len =
      absolute_url.length() - (u.field_data[UF_HOST].off + authority_and_port_len);
  if (path_len > 0) {
    uint64_t path_beginning = u.field_data[UF_HOST].off + authority_and_port_len;
    const auto slash = absl::string_view(absolute_url.data() + path_beginning, 1);
    path_and_query_params_ =
        absl::StrCat(slash != DEFAULT_PATH ? DEFAULT_PATH : EMPTY_STRING,
                     absl::string_view(absolute_url.data() + path_beginning, path_len));
  } else {
    path_and_query_params_ = DEFAULT_PATH;
  }
#endif

  return true;
}

} // namespace Utility
} // namespace Http
} // namespace Envoy