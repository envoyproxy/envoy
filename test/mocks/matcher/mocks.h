#pragma once

#include "source/common/common/matchers.h"
#include "source/common/matcher/matcher.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Matcher {

template <class DataType>
class MockMatchTreeValidationVisitor : public MatchTreeValidationVisitor<DataType> {
public:
  // Normally we'd define the ctor/dtor in the .cc file, but because this is templated that
  // won't work.

  MOCK_METHOD(absl::Status, performDataInputValidation,
              (const Matcher::DataInputFactory<DataType>&, absl::string_view));
};

} // namespace Matcher

namespace Matchers {
class MockStringMatcher : public StringMatcher {
public:
  MOCK_METHOD(bool, match, (absl::string_view), (const, override));
};
} // namespace Matchers
} // namespace Envoy
