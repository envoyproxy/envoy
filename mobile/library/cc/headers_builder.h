#pragma once

#include "headers.h"

namespace Envoy {
namespace Platform {

class HeadersBuilder {
public:
  virtual ~HeadersBuilder() {}

  HeadersBuilder& add(std::string name, std::string value);
  HeadersBuilder& set(std::string name, std::vector<std::string> values);
  HeadersBuilder& remove(const std::string& name);

protected:
  HeadersBuilder();
  HeadersBuilder& internalSet(std::string name, std::vector<std::string> values);
  const RawHeaderMap& allHeaders() const;

private:
  bool isRestrictedHeader(absl::string_view name) const;

  RawHeaderMap headers_;
};

} // namespace Platform
} // namespace Envoy
