#pragma once

#include "envoy/matcher/matcher.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace CommonInputs {
namespace EnvironmentVariable {

class Input : public Matcher::CommonProtocolInput {
public:
  explicit Input(absl::optional<absl::string_view> value) : storage_(value) {}

  Matcher::DataInputGetResult get() override {
    return storage_ ? Matcher::DataInputGetResult::CreateStringView(*storage_)
                    : Matcher::DataInputGetResult::NoData();
  }

private:
  const absl::optional<absl::string_view> storage_;
};
} // namespace EnvironmentVariable
} // namespace CommonInputs
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
