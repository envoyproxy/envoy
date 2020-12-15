#pragma once

// NOLINT(namespace-envoy)

#include "headers.h"

class HeadersBuilder {
public:
  HeadersBuilder& add(const std::string& name, const std::string& value);
  HeadersBuilder& set(const std::string& name, const std::vector<std::string>& values);
  HeadersBuilder& remove(const std::string& name);

protected:
  HeadersBuilder();
  HeadersBuilder& internal_set(const std::string&, const std::vector<std::string>& values);

private:
  bool is_restricted_header_(const std::string& name) const;

  RawHeaders headers_;
};
