#pragma once

#include "envoy/matcher/matcher.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace CommonInputs {
namespace EnvironmentVariable {

class Input : public Matcher::CommonProtocolInput {
public:
  explicit Input(Matcher::MatchingDataType&& value) : storage_(std::move(value)) {}

  Matcher::MatchingDataType get() override { return storage_; }

private:
  const Matcher::MatchingDataType storage_;
};
} // namespace EnvironmentVariable
} // namespace CommonInputs
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
