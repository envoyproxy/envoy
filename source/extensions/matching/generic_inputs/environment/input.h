#pragma once

#include "envoy/matcher/matcher.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace GenericInputs {
namespace Environment {

class Input : public Envoy::Matcher::GenericDataInput {
public:
  explicit Input(absl::string_view name) {
    // We read the env variable at construction time to avoid repeat lookups.
    // This assumes that the environment remains stable during the process lifetime.
    auto* value = getenv(name.data());
    if (value != nullptr) {
      storage_ = std::string(value);
    }
  }

  absl::optional<absl::string_view> get() override { return storage_; }

private:
  absl::optional<std::string> storage_;
};
} // namespace Environment
} // namespace GenericInputs
} // namespace Matching
} // namespace Extensions
} // namespace Envoy