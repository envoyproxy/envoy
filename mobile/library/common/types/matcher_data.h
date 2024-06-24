#pragma once

// NOLINT(namespace-envoy)

#include <string>

/* This struct contains data to configure Envoy's envoy::config::route::v3::HeaderMatcher
 * We use a separate struct both to avoid the proliferation of protos but also
 * because only a limited set of HeaderMatcher arguments are supported through
 * the JNI interface */
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
