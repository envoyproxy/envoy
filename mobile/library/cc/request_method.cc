#include "request_method.h"

#include <stdexcept>

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

  throw std::out_of_range("unknown request method type");
}

RequestMethod requestMethodFromString(absl::string_view str) {
  for (const auto& pair : REQUEST_METHOD_LOOKUP) {
    if (pair.second == str) {
      return pair.first;
    }
  }

  throw std::out_of_range("unknown request method type");
}

} // namespace Platform
} // namespace Envoy
