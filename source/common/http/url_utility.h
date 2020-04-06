#pragma once

#include <string>

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Http {
namespace Utility {
/**
 * Given a fully qualified URL, splits the string_view provided into scheme, host, port and path
 * with query parameters components (including fragments).
 */
class Url {
public:
  bool initialize(absl::string_view absolute_url);
  absl::string_view getScheme() const { return scheme_; }
  absl::string_view getHostAndPort() const { return host_and_port_; }
  absl::string_view getPathAndQueryParams() const { return path_and_query_params_; }
  uint64_t getPort() const { return port_; }

private:
  std::string scheme_;
  std::string host_and_port_;
  std::string path_and_query_params_;
  uint16_t port_{0};
};

} // namespace Utility

} // namespace Http
} // namespace Envoy
