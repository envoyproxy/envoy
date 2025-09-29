#include "source/extensions/filters/http/proto_api_scrubber/scrubbing_util/field_checker.h"

#include "test/test_common/utility.h"

#include "absl/memory/memory.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using proto_processing_lib::proto_scrubber::FieldFilters;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoApiScrubber {
namespace {

class FieldCheckerTest : public ::testing::Test {
protected:
  FieldCheckerTest() : field_checker_(&parent_type_) { parent_type_.set_name("some.MessageType"); }

  FieldChecker field_checker_;
  Protobuf::Type parent_type_;
};

// With the current basic implementation, all fields are included. This test
// verifies that behavior for a few different field paths. Once field mask
// logic is added, this test suite should be expanded to cover exclusion,
// wildcards, and other scenarios.
TEST_F(FieldCheckerTest, IncludesSimpleField) {
  Protobuf::Field simple_field;
  simple_field.set_name("name");
  EXPECT_EQ(field_checker_.CheckField({"name"}, &simple_field), FieldCheckResults::kInclude);
}

TEST_F(FieldCheckerTest, IncludesNestedField) {
  Protobuf::Field nested_field;
  nested_field.set_name("name");
  EXPECT_EQ(field_checker_.CheckField({"user", "name"}, &nested_field),
            FieldCheckResults::kInclude);
}

TEST_F(FieldCheckerTest, IncludesFieldWithIndex) {
  Protobuf::Field simple_field;
  simple_field.set_name("name");
  EXPECT_EQ(field_checker_.CheckField({"name"}, &simple_field, 0, &parent_type_),
            FieldCheckResults::kInclude);
}

TEST_F(FieldCheckerTest, IncludesType) {
  EXPECT_EQ(field_checker_.CheckType(&parent_type_), FieldCheckResults::kInclude);
}

TEST_F(FieldCheckerTest, SupportAny) { EXPECT_FALSE(field_checker_.SupportAny()); }

TEST_F(FieldCheckerTest, FilterName) {
  EXPECT_EQ(field_checker_.FilterName(), FieldFilters::FieldMaskFilter);
}

} // namespace
} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
