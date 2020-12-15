#pragma once

#include "envoy/config/core/v3/base.pb.h"

#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

extern void expectHttpHeader(const envoy::config::core::v3::HeaderMap& headers,
                             absl::string_view key, absl::string_view value) {
  for (auto it = headers.headers().cbegin(); it != headers.headers().cend(); it++) {
    if (it->key() == key) {
      EXPECT_EQ(it->value(), value);
      return;
    }
  }
  FAIL() << "Header " << key << " not found";
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy