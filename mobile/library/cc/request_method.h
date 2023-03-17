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

std::string requestMethodToString(RequestMethod method);
RequestMethod requestMethodFromString(const std::string& str);

} // namespace Platform
} // namespace Envoy
