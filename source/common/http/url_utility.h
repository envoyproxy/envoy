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
  bool initialize(absl::string_view absolute_url, bool is_connect);
  absl::string_view scheme() const { return scheme_; }
  absl::string_view hostAndPort() const { return host_and_port_; }
  absl::string_view pathAndQueryParams() const { return path_and_query_params_; }
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
