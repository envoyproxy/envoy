#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/thrift_proxy/app_exception_impl.h"

#include "test/extensions/filters/network/thrift_proxy/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::InSequence;
using testing::Ref;
using testing::StrictMock;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

TEST(AppExceptionImplTest, CopyConstructor) {
  AppException app_ex(AppExceptionType::InternalError, "msg");
  AppException copy(app_ex);

  EXPECT_EQ(app_ex.type_, copy.type_);
  EXPECT_STREQ("msg", copy.what());
}

TEST(AppExceptionImplTest, TestEncode) {
  AppException app_ex(AppExceptionType::InternalError, "msg");

  MessageMetadata metadata;
  metadata.setMethodName("method");
  metadata.setSequenceId(99);
  metadata.setMessageType(MessageType::Call);

  StrictMock<MockProtocol> proto;
  Buffer::OwnedImpl buffer;

  InSequence dummy;
  EXPECT_CALL(proto, writeMessageBegin(Ref(buffer), Ref(metadata)))
      .WillOnce(Invoke([&](Buffer::Instance&, const MessageMetadata& metadata) -> void {
        EXPECT_EQ("method", metadata.methodName());
        EXPECT_EQ(99, metadata.sequenceId());
        EXPECT_EQ(MessageType::Exception, metadata.messageType());
      }));
  EXPECT_CALL(proto, writeStructBegin(Ref(buffer), "TApplicationException"));
  EXPECT_CALL(proto, writeFieldBegin(Ref(buffer), "message", FieldType::String, 1));
  EXPECT_CALL(proto, writeString(Ref(buffer), "msg"));
  EXPECT_CALL(proto, writeFieldEnd(Ref(buffer)));
  EXPECT_CALL(proto, writeFieldBegin(Ref(buffer), "type", FieldType::I32, 2));
  EXPECT_CALL(proto, writeInt32(Ref(buffer), static_cast<int>(AppExceptionType::InternalError)));
  EXPECT_CALL(proto, writeFieldEnd(Ref(buffer)));
  EXPECT_CALL(proto, writeFieldBegin(Ref(buffer), "", FieldType::Stop, 0));
  EXPECT_CALL(proto, writeStructEnd(Ref(buffer)));
  EXPECT_CALL(proto, writeMessageEnd(Ref(buffer)));

  EXPECT_EQ(DirectResponse::ResponseType::Exception, app_ex.encode(metadata, proto, buffer));
}

TEST(AppExceptionImplTest, TestEncodeEmptyMetadata) {
  AppException app_ex(AppExceptionType::InternalError, "msg");

  MessageMetadata metadata;
  StrictMock<MockProtocol> proto;
  Buffer::OwnedImpl buffer;

  InSequence dummy;
  EXPECT_CALL(proto, writeMessageBegin(Ref(buffer), Ref(metadata)))
      .WillOnce(Invoke([&](Buffer::Instance&, const MessageMetadata& metadata) -> void {
        EXPECT_EQ("", metadata.methodName());
        EXPECT_EQ(0, metadata.sequenceId());
        EXPECT_EQ(MessageType::Exception, metadata.messageType());
      }));
  EXPECT_CALL(proto, writeStructBegin(Ref(buffer), "TApplicationException"));
  EXPECT_CALL(proto, writeFieldBegin(Ref(buffer), "message", FieldType::String, 1));
  EXPECT_CALL(proto, writeString(Ref(buffer), "msg"));
  EXPECT_CALL(proto, writeFieldEnd(Ref(buffer)));
  EXPECT_CALL(proto, writeFieldBegin(Ref(buffer), "type", FieldType::I32, 2));
  EXPECT_CALL(proto, writeInt32(Ref(buffer), static_cast<int>(AppExceptionType::InternalError)));
  EXPECT_CALL(proto, writeFieldEnd(Ref(buffer)));
  EXPECT_CALL(proto, writeFieldBegin(Ref(buffer), "", FieldType::Stop, 0));
  EXPECT_CALL(proto, writeStructEnd(Ref(buffer)));
  EXPECT_CALL(proto, writeMessageEnd(Ref(buffer)));

  EXPECT_EQ(DirectResponse::ResponseType::Exception, app_ex.encode(metadata, proto, buffer));
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
