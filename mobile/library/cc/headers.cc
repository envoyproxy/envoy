#include "headers.h"

namespace Envoy {
namespace Platform {

Headers::const_iterator Headers::begin() const {
  return Headers::const_iterator(this->allHeaders().begin());
}

Headers::const_iterator Headers::end() const {
  return Headers::const_iterator(this->allHeaders().end());
}

const std::vector<std::string>& Headers::operator[](const std::string& key) const {
  return this->headers_.at(key);
}

const RawHeaderMap& Headers::allHeaders() const { return this->headers_; }

bool Headers::contains(const std::string& key) const { return this->headers_.contains(key); }

Headers::Headers(const RawHeaderMap& headers) : headers_(headers) {}

} // namespace Platform
} // namespace Envoy
