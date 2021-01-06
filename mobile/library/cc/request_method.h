#pragma once

#include <string>

namespace Envoy {
namespace Platform {

enum RequestMethod {
  DELETE,
  GET,
  HEAD,
  OPTIONS,
  PATCH,
  POST,
  PUT,
  TRACE,
};

std::string request_method_to_string(RequestMethod method);
RequestMethod request_method_from_string(const std::string& str);

} // namespace Platform
} // namespace Envoy
