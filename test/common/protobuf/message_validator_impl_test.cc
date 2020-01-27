#include "envoy/common/exception.h"

#include "common/protobuf/message_validator_impl.h"
#include "common/stats/isolated_store_impl.h"

#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace ProtobufMessage {
namespace {

// The null validation visitor doesn't do anything on unknown fields.
TEST(NullValidationVisitorImpl, UnknownField) {
  NullValidationVisitorImpl null_validation_visitor;
  EXPECT_NO_THROW(null_validation_visitor.onUnknownField("foo"));
}

// The warning validation visitor logs and bumps stats on unknown fields
TEST(WarningValidationVisitorImpl, UnknownField) {
  Stats::IsolatedStoreImpl stats;
  Stats::Counter& counter = stats.counter("counter");
  WarningValidationVisitorImpl warning_validation_visitor;
  // First time around we should log.
  EXPECT_LOG_CONTAINS("warn", "Unknown field: foo",
                      warning_validation_visitor.onUnknownField("foo"));
  // Duplicate descriptions don't generate a log the second time around.
  EXPECT_LOG_NOT_CONTAINS("warn", "Unknown field: foo",
                          warning_validation_visitor.onUnknownField("foo"));
  // Unrelated variable increments.
  EXPECT_LOG_CONTAINS("warn", "Unknown field: bar",
                      warning_validation_visitor.onUnknownField("bar"));
  // When we set the stats counter, the above increments are transferred.
  EXPECT_EQ(0, counter.value());
  warning_validation_visitor.setCounter(counter);
  EXPECT_EQ(2, counter.value());
  // A third unknown field is tracked in stats post-initialization.
  EXPECT_LOG_CONTAINS("warn", "Unknown field: baz",
                      warning_validation_visitor.onUnknownField("baz"));
  EXPECT_EQ(3, counter.value());
}

// The strict validation visitor throws on unknown fields.
TEST(StrictValidationVisitorImpl, UnknownField) {
  StrictValidationVisitorImpl strict_validation_visitor;
  EXPECT_THROW_WITH_MESSAGE(strict_validation_visitor.onUnknownField("foo"),
                            UnknownProtoFieldException,
                            "Protobuf message (foo) has unknown fields");
}

} // namespace
} // namespace ProtobufMessage
} // namespace Envoy
