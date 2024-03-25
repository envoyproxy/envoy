#include "library/cc/headers.h"

namespace Envoy {
namespace Platform {

Headers::const_iterator Headers::begin() const {
  return Headers::const_iterator(allHeaders().begin());
}

Headers::const_iterator Headers::end() const { return Headers::const_iterator(allHeaders().end()); }

const std::vector<std::string>& Headers::operator[](absl::string_view key) const {
  return headers_.at(key);
}

const RawHeaderMap& Headers::allHeaders() const { return headers_; }

bool Headers::contains(const std::string& key) const { return headers_.contains(key); }

Headers::Headers(const RawHeaderMap& headers) : headers_(headers) {}

} // namespace Platform
} // namespace Envoy
