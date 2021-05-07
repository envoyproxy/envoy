#pragma once

#include "headers.h"

namespace Envoy {
namespace Platform {

class HeadersBuilder {
public:
  virtual ~HeadersBuilder() {}

  HeadersBuilder& add(const std::string& name, const std::string& value);
  HeadersBuilder& set(const std::string& name, const std::vector<std::string>& values);
  HeadersBuilder& remove(const std::string& name);

protected:
  HeadersBuilder();
  HeadersBuilder& internalSet(const std::string& name, const std::vector<std::string>& values);
  const RawHeaderMap& allHeaders() const;

private:
  bool isRestrictedHeader(const std::string& name) const;

  RawHeaderMap headers_;
};

} // namespace Platform
} // namespace Envoy
