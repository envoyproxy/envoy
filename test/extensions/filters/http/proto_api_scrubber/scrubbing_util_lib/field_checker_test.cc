#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/proto_api_scrubber/scrubbing_util_lib/field_checker.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "proto_processing_lib/proto_scrubber/field_checker_interface.h"

using proto_processing_lib::proto_scrubber::FieldCheckResults;
using proto_processing_lib::proto_scrubber::FieldFilters;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoApiScrubber {
namespace {

class FieldCheckerTest : public ::testing::Test {};

// With the current basic implementation, all fields are included. This test
// verifies that behavior for a few different field paths. Once field mask
// logic is added, this test suite should be expanded to cover exclusion,
// wildcards, and other scenarios.
TEST_F(FieldCheckerTest, IncludesSimpleField) {
  FieldChecker field_checker;
  Protobuf::Field simple_field;
  simple_field.set_name("name");
  EXPECT_EQ(field_checker.CheckField({"name"}, &simple_field), FieldCheckResults::kInclude);
}

TEST_F(FieldCheckerTest, IncludesType) {
  FieldChecker field_checker;
  Protobuf::Type type;
  EXPECT_EQ(field_checker.CheckType(&type), FieldCheckResults::kInclude);
}

TEST_F(FieldCheckerTest, SupportAny) {
  FieldChecker field_checker;
  EXPECT_FALSE(field_checker.SupportAny());
}

TEST_F(FieldCheckerTest, FilterName) {
  FieldChecker field_checker;
  EXPECT_EQ(field_checker.FilterName(), FieldFilters::FieldMaskFilter);
}

} // namespace
} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
