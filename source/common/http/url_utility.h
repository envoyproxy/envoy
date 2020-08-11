#pragma once

#include <string>

#include "absl/strings/string_view.h"
#include "url/gurl.h"

namespace Envoy {
namespace Http {
namespace Utility {

/**
 * Given a fully qualified URL, splits the string_view provided into scheme, host and path with
 * query parameters components.
 */
class Url {
public:
  /**
   * Initializes a URL object from a URL string.
   * @param absolute_url URL string to be parsed.
   * @param is_connect whether to parse the absolute_url as CONNECT request URL or not.
   * @return bool if the initialization is successful.
   */
  bool initialize(absl::string_view absolute_url, bool is_connect);

  /**
   * @return absl::string_view the scheme of a URL.
   */
  absl::string_view scheme() const { return scheme_; }

  /**
   * @return absl::string_view the host and port part of a URL.
   */
  absl::string_view hostAndPort() const { return host_and_port_; }

  /**
   * @return absl::string_view the path and query params part of a URL.
   */
  absl::string_view pathAndQueryParams() const { return path_and_query_params_; }

  /**
   * @return uint64_t the effective port of a URL.
   */
  uint64_t port() const { return port_; }

private:
  bool initializeForConnect(GURL&& url);
  bool validPortForConnect(absl::string_view port_string);

  std::string scheme_;
  std::string host_and_port_;
  std::string path_and_query_params_;
  uint16_t port_{0};
};

} // namespace Utility
} // namespace Http
} // namespace Envoy
