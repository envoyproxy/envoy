#include "extensions/filters/network/thrift_proxy/metadata.h"

#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

TEST(MessageMetadataTest, Fields) {
  MessageMetadata metadata;

  EXPECT_FALSE(metadata.hasFrameSize());
  EXPECT_THROW(metadata.frameSize(), absl::bad_optional_access);
  metadata.setFrameSize(100);
  EXPECT_TRUE(metadata.hasFrameSize());
  EXPECT_EQ(100, metadata.frameSize());

  EXPECT_FALSE(metadata.hasProtocol());
  EXPECT_THROW(metadata.protocol(), absl::bad_optional_access);
  metadata.setProtocol(ProtocolType::Binary);
  EXPECT_TRUE(metadata.hasProtocol());
  EXPECT_EQ(ProtocolType::Binary, metadata.protocol());

  EXPECT_FALSE(metadata.hasMethodName());
  EXPECT_THROW(metadata.methodName(), absl::bad_optional_access);
  metadata.setMethodName("method");
  EXPECT_TRUE(metadata.hasMethodName());
  EXPECT_EQ("method", metadata.methodName());

  EXPECT_FALSE(metadata.hasMessageType());
  EXPECT_THROW(metadata.messageType(), absl::bad_optional_access);
  metadata.setMessageType(MessageType::Call);
  EXPECT_TRUE(metadata.hasMessageType());
  EXPECT_EQ(MessageType::Call, metadata.messageType());

  EXPECT_FALSE(metadata.hasSequenceId());
  EXPECT_THROW(metadata.sequenceId(), absl::bad_optional_access);
  metadata.setSequenceId(101);
  EXPECT_TRUE(metadata.hasSequenceId());
  EXPECT_EQ(101, metadata.sequenceId());

  EXPECT_FALSE(metadata.hasAppException());
  EXPECT_THROW(metadata.appExceptionType(), absl::bad_optional_access);
  EXPECT_THROW(metadata.appExceptionMessage(), absl::bad_optional_access);
  metadata.setAppException(AppExceptionType::InternalError, "oops");
  EXPECT_TRUE(metadata.hasAppException());
  EXPECT_EQ(AppExceptionType::InternalError, metadata.appExceptionType());
  EXPECT_EQ("oops", metadata.appExceptionMessage());
}

TEST(MessageMetadataTest, Headers) {
  MessageMetadata metadata;

  EXPECT_EQ(metadata.headers().size(), 0);
  metadata.headers().addCopy(Http::LowerCaseString("k"), "v");
  EXPECT_EQ(metadata.headers().size(), 1);
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
