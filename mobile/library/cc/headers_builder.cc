#include "headers_builder.h"

namespace Envoy {
namespace Platform {

HeadersBuilder& HeadersBuilder::add(std::string name, std::string value) {
  if (isRestrictedHeader(name)) {
    return *this;
  }
  headers_[std::move(name)].push_back(std::move(value));
  return *this;
}

HeadersBuilder& HeadersBuilder::set(std::string name, std::vector<std::string> values) {
  if (isRestrictedHeader(name)) {
    return *this;
  }
  headers_[std::move(name)] = std::move(values);
  return *this;
}

HeadersBuilder& HeadersBuilder::remove(absl::string_view name) {
  if (isRestrictedHeader(name)) {
    return *this;
  }
  headers_.erase(name);
  return *this;
}

HeadersBuilder::HeadersBuilder() {}

HeadersBuilder& HeadersBuilder::internalSet(std::string name, std::vector<std::string> values) {
  headers_[std::move(name)] = std::move(values);
  return *this;
}

const RawHeaderMap& HeadersBuilder::allHeaders() const { return headers_; }

bool HeadersBuilder::isRestrictedHeader(absl::string_view name) const {
  return name.find(':') == 0 || name.find("x-envoy-mobile") == 0;
}

} // namespace Platform
} // namespace Envoy
