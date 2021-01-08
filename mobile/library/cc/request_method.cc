#include "request_method.h"

#include <stdexcept>
#include <vector>

namespace Envoy {
namespace Platform {

static const std::pair<RequestMethod, std::string> REQUEST_METHOD_LOOKUP[]{
    {RequestMethod::DELETE, "DELETE"}, {RequestMethod::GET, "GET"},
    {RequestMethod::HEAD, "HEAD"},     {RequestMethod::OPTIONS, "OPTIONS"},
    {RequestMethod::PATCH, "PATCH"},   {RequestMethod::POST, "POST"},
    {RequestMethod::PUT, "PUT"},       {RequestMethod::TRACE, "TRACE"},
};

std::string request_method_to_string(RequestMethod method) {
  for (const auto& pair : REQUEST_METHOD_LOOKUP) {
    if (pair.first == method) {
      return pair.second;
    }
  }

  throw std::out_of_range("unknown request method type");
}

RequestMethod request_method_from_string(const std::string& str) {
  for (const auto& pair : REQUEST_METHOD_LOOKUP) {
    if (pair.second == str) {
      return pair.first;
    }
  }

  throw std::out_of_range("unknown request method type");
}

} // namespace Platform
} // namespace Envoy
