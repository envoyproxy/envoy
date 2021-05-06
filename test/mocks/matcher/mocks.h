#pragma once

#include "common/matcher/matcher.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Matcher {

template <class DataType>
class MockMatchTreeValidationVisitor : public MatchTreeValidationVisitor<DataType> {
public:
  // Normally we'd define the ctor/dtor in the .cc file, but because this is templated that
  // won't work.

  MOCK_METHOD(absl::Status, performDataInputValidation,
              (const Matcher::DataInput<DataType>&, absl::string_view));
};

} // namespace Matcher
} // namespace Envoy
