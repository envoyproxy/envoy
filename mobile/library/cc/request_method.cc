#include "request_method.h"

#include <stdexcept>

#include "source/common/common/assert.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Platform {

namespace {

const std::pair<RequestMethod, absl::string_view> REQUEST_METHOD_LOOKUP[]{
    {RequestMethod::DELETE, "DELETE"}, {RequestMethod::GET, "GET"},
    {RequestMethod::HEAD, "HEAD"},     {RequestMethod::OPTIONS, "OPTIONS"},
    {RequestMethod::PATCH, "PATCH"},   {RequestMethod::POST, "POST"},
    {RequestMethod::PUT, "PUT"},       {RequestMethod::TRACE, "TRACE"},
};

} // namespace

absl::string_view requestMethodToString(RequestMethod method) {
  for (const auto& pair : REQUEST_METHOD_LOOKUP) {
    if (pair.first == method) {
      return pair.second;
    }
  }

  IS_ENVOY_BUG("unknown method");
  return "";
}

RequestMethod requestMethodFromString(absl::string_view str) {
  for (const auto& pair : REQUEST_METHOD_LOOKUP) {
    if (pair.second == str) {
      return pair.first;
    }
  }

  IS_ENVOY_BUG("unknown method");
  return REQUEST_METHOD_LOOKUP[0].first;
}

} // namespace Platform
} // namespace Envoy
