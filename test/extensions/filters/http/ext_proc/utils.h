#pragma once

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/http/header_map.h"

#include "absl/strings/str_format.h"
#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

class ExtProcTestUtility {
public:
  // Compare a reference header map to a proto
  static bool headerProtosEqualIgnoreOrder(const Http::HeaderMap& expected,
                                           const envoy::config::core::v3::HeaderMap& actual);
};

MATCHER_P(HeaderProtosEqual, expected, "HTTP header protos match") {
  return ExtProcTestUtility::headerProtosEqualIgnoreOrder(expected, arg);
}

MATCHER_P(HasNoHeader, key, absl::StrFormat("Headers have no value for \"%s\"", key)) {
  return arg.get(Http::LowerCaseString(std::string(key))).empty();
}

MATCHER_P2(SingleHeaderValueIs, key, value,
           absl::StrFormat("Header \"%s\" equals \"%s\"", key, value)) {
  const auto hdr = arg.get(Http::LowerCaseString(std::string(key)));
  if (hdr.size() != 1) {
    return false;
  }
  return hdr[0]->value() == value;
}

MATCHER_P2(SingleProtoHeaderValueIs, key, value,
           absl::StrFormat("Header \"%s\" equals \"%s\"", key, value)) {
  for (const auto& hdr : arg.headers()) {
    if (key == hdr.key()) {
      return value == hdr.value();
    }
  }
  return false;
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
