#include "headers_builder.h"

namespace Envoy {
namespace Platform {

HeadersBuilder& HeadersBuilder::add(const std::string& name, const std::string& value) {
  if (this->isRestrictedHeader(name)) {
    return *this;
  }
  this->headers_[name].push_back(value);
  return *this;
}

HeadersBuilder& HeadersBuilder::set(const std::string& name,
                                    const std::vector<std::string>& values) {
  if (this->isRestrictedHeader(name)) {
    return *this;
  }
  this->headers_[name] = values;
  return *this;
}

HeadersBuilder& HeadersBuilder::remove(const std::string& name) {
  if (this->isRestrictedHeader(name)) {
    return *this;
  }
  this->headers_.erase(name);
  return *this;
}

HeadersBuilder::HeadersBuilder() {}

HeadersBuilder& HeadersBuilder::internalSet(const std::string& name,
                                            const std::vector<std::string>& values) {
  this->headers_[name] = values;
  return *this;
}

const RawHeaderMap& HeadersBuilder::allHeaders() const { return this->headers_; }

bool HeadersBuilder::isRestrictedHeader(const std::string& name) const {
  return name.find(":") == 0 || name.find("x-envoy-mobile") == 0;
}

} // namespace Platform
} // namespace Envoy
