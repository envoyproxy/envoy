#include "source/extensions/filters/network/dubbo_proxy/app_exception.h"
#include "source/extensions/filters/network/dubbo_proxy/dubbo_hessian2_serializer_impl.h"
#include "source/extensions/filters/network/dubbo_proxy/dubbo_protocol_impl.h"
#include "source/extensions/filters/network/dubbo_proxy/filters/filter.h"
#include "source/extensions/filters/network/dubbo_proxy/hessian_utils.h"
#include "source/extensions/filters/network/dubbo_proxy/message_impl.h"
#include "source/extensions/filters/network/dubbo_proxy/metadata.h"

#include "test/extensions/filters/network/dubbo_proxy/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

namespace {

class AppExceptionTest : public testing::Test {
public:
  AppExceptionTest() : metadata_(std::make_shared<MessageMetadata>()) {
    protocol_.initSerializer(SerializationType::Hessian2);
  }

  DubboProtocolImpl protocol_;
  MessageMetadataSharedPtr metadata_;
};

TEST_F(AppExceptionTest, Encode) {
  std::string mock_message("invalid method name 'Sub'");
  AppException app_exception(ResponseStatus::ServiceNotFound, mock_message);

  Buffer::OwnedImpl buffer;

  Hessian2::Encoder encoder(std::make_unique<BufferWriter>(buffer));

  encoder.encode(mock_message);
  encoder.encode(static_cast<uint8_t>(app_exception.response_type_));

  size_t expect_body_size = buffer.length();

  buffer.drain(buffer.length());

  metadata_->setSerializationType(SerializationType::Hessian2);
  metadata_->setRequestId(0);

  EXPECT_EQ(app_exception.encode(*(metadata_.get()), protocol_, buffer),
            DubboFilters::DirectResponse::ResponseType::Exception);
  MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();
  auto result = protocol_.decodeHeader(buffer, metadata);
  EXPECT_TRUE(result.second);

  const ContextImpl* context = static_cast<const ContextImpl*>(result.first.get());
  EXPECT_EQ(expect_body_size, context->bodySize());
  EXPECT_EQ(metadata->messageType(), MessageType::Response);
  buffer.drain(context->headerSize());

  // Verify the response type and content.
  Hessian2::Decoder decoder(std::make_unique<BufferReader>(buffer));

  int type_value = *decoder.decode<int32_t>();
  EXPECT_EQ(static_cast<uint8_t>(app_exception.response_type_), static_cast<uint8_t>(type_value));

  std::string message = *decoder.decode<std::string>();
  EXPECT_EQ(mock_message, message);

  EXPECT_EQ(buffer.length(), decoder.offset());

  auto rpc_result = protocol_.serializer()->deserializeRpcResult(buffer, result.first);
  EXPECT_TRUE(rpc_result.second);
  EXPECT_TRUE(rpc_result.first->hasException());
  buffer.drain(buffer.length());

  AppException new_app_exception(app_exception);
  EXPECT_EQ(new_app_exception.status_, ResponseStatus::ServiceNotFound);

  MockProtocol mock_protocol;
  EXPECT_CALL(mock_protocol, encode(_, _, _, _)).WillOnce(Return(false));
  EXPECT_THROW(app_exception.encode(*(metadata_.get()), mock_protocol, buffer), EnvoyException);
}

class AppExceptionTestWithMock : public testing::Test {
public:
  AppExceptionTestWithMock() : metadata_(std::make_shared<MessageMetadata>()) {}

  NiceMock<MockProtocol> mock_protocol_;
  MessageMetadataSharedPtr metadata_;
};

TEST_F(AppExceptionTestWithMock, AppExceptionTestWithMock) {
  std::string mock_message("MOCK_MESSAGE");
  AppException app_exception(ResponseStatus::ServiceNotFound, mock_message);

  Buffer::OwnedImpl buffer;

  ON_CALL(mock_protocol_, encode(_, _, _, _)).WillByDefault(Return(false));

  EXPECT_THROW_WITH_MESSAGE(app_exception.encode(*metadata_, mock_protocol_, buffer),
                            EnvoyException, "Failed to encode local reply message");
}

} // namespace

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
