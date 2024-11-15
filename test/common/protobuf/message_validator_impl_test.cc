#include "envoy/common/exception.h"

#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/stats/isolated_store_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace ProtobufMessage {
namespace {

// The null validation visitor doesn't do anything on unknown fields.
TEST(NullValidationVisitorImpl, UnknownField) {
  NullValidationVisitorImpl null_validation_visitor;
  EXPECT_TRUE(null_validation_visitor.skipValidation());
  EXPECT_TRUE(null_validation_visitor.onUnknownField("foo").ok());
}

// The warning validation visitor logs and bumps stats on unknown fields
TEST(WarningValidationVisitorImpl, UnknownField) {
  Stats::TestUtil::TestStore stats;
  Stats::Counter& unknown_counter = stats.counter("unknown_counter");
  Stats::Counter& wip_counter = stats.counter("wip_counter");
  WarningValidationVisitorImpl warning_validation_visitor;
  // we want to be executed.
  EXPECT_FALSE(warning_validation_visitor.skipValidation());
  // First time around we should log.
  EXPECT_LOG_CONTAINS("warn", "Unknown field: foo",
                      EXPECT_TRUE(warning_validation_visitor.onUnknownField("foo").ok()));
  // Duplicate descriptions don't generate a log the second time around.
  EXPECT_LOG_NOT_CONTAINS("warn", "Unknown field: foo",
                          EXPECT_TRUE(warning_validation_visitor.onUnknownField("foo").ok()));
  // Unrelated variable increments.
  EXPECT_LOG_CONTAINS("warn", "Unknown field: bar",
                      EXPECT_TRUE(warning_validation_visitor.onUnknownField("bar").ok()));
  // When we set the stats counter, the above increments are transferred.
  EXPECT_EQ(0, unknown_counter.value());
  warning_validation_visitor.setCounters(unknown_counter, wip_counter);
  EXPECT_EQ(2, unknown_counter.value());
  // A third unknown field is tracked in stats post-initialization.
  EXPECT_LOG_CONTAINS("warn", "Unknown field: baz",
                      EXPECT_TRUE(warning_validation_visitor.onUnknownField("baz").ok()));
  EXPECT_EQ(3, unknown_counter.value());
}

// The strict validation visitor throws on unknown fields.
TEST(StrictValidationVisitorImpl, UnknownField) {
  StrictValidationVisitorImpl strict_validation_visitor;
  EXPECT_FALSE(strict_validation_visitor.skipValidation());
  EXPECT_EQ(strict_validation_visitor.onUnknownField("foo").message(),
            "Protobuf message (foo) has unknown fields");
}

} // namespace
} // namespace ProtobufMessage
} // namespace Envoy
