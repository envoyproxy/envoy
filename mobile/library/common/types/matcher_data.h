#pragma once

// NOLINT(namespace-envoy)

#include <string>

struct MatcherData {
  std::string name;
  enum Type {
    EXACT = 0,
    SAFE_REGEX = 1,
  };
  Type type;
  std::string value;
  MatcherData(std::string name, Type type, std::string value)
      : name(name), type(type), value(value) {}
};
