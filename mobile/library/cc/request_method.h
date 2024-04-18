#pragma once

#include "absl/strings/string_view.h"

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

absl::string_view requestMethodToString(RequestMethod method);
RequestMethod requestMethodFromString(absl::string_view str);

} // namespace Platform
} // namespace Envoy
