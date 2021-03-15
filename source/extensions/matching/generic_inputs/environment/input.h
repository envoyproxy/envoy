#pragma once

#include "envoy/matcher/matcher.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace GenericInputs {
namespace Environment {

class Input : public Envoy::Matcher::GenericDataInput {
public:
  explicit Input(absl::optional<std::string>&& value) : storage_(std::move(value)) {}

  absl::optional<absl::string_view> get() override { return storage_; }

private:
  const absl::optional<std::string> storage_;
};
} // namespace Environment
} // namespace GenericInputs
} // namespace Matching
} // namespace Extensions
} // namespace Envoy