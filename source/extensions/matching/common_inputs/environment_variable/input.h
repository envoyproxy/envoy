#pragma once

#include "envoy/matcher/matcher.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace CommonInputs {
namespace EnvironmentVariable {

class Input : public Matcher::CommonProtocolInput {
public:
  explicit Input(absl::optional<std::string>&& value) : storage_(std::move(value)) {}

  Matcher::InputValue get() override { return storage_ ? Matcher::InputValue(storage_.value()) : Matcher::InputValue(); }

private:
  const absl::optional<std::string> storage_;
};
} // namespace EnvironmentVariable
} // namespace CommonInputs
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
