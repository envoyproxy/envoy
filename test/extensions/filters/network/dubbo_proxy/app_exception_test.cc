#include "extensions/filters/network/dubbo_proxy/app_exception.h"
#include "extensions/filters/network/dubbo_proxy/deserializer_impl.h"
#include "extensions/filters/network/dubbo_proxy/dubbo_protocol_impl.h"
#include "extensions/filters/network/dubbo_proxy/filters/filter.h"
#include "extensions/filters/network/dubbo_proxy/hessian_deserializer_impl.h"
#include "extensions/filters/network/dubbo_proxy/metadata.h"

#include "test/extensions/filters/network/dubbo_proxy/mocks.h"
#include "test/test_common/test_base.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

class AppExceptionTest : public TestBase {
public:
  AppExceptionTest() : metadata_(std::make_shared<MessageMetadata>()) {}

  HessianDeserializerImpl deserializer_;
  DubboProtocolImpl protocol_;
  MessageMetadataSharedPtr metadata_;
  Protocol::Context context_;
};

TEST_F(AppExceptionTest, Encode) {
  std::string mock_message("invalid method name 'Sub'");
  AppException app_exception(ResponseStatus::ServiceNotFound, mock_message);

  Buffer::OwnedImpl buffer;
  metadata_->setSerializationType(SerializationType::Hessian);
  metadata_->setRequestId(0);

  {
    EXPECT_EQ(app_exception.encode(*(metadata_.get()), protocol_, deserializer_, buffer),
              DubboFilters::DirectResponse::ResponseType::Exception);

    MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();
    EXPECT_TRUE(protocol_.decode(buffer, &context_, metadata));
    EXPECT_EQ(metadata->message_type(), MessageType::Response);
    buffer.drain(context_.header_size_);

    auto result = deserializer_.deserializeRpcResult(buffer, context_.body_size_);
    EXPECT_TRUE(result->hasException());
    buffer.drain(buffer.length());
  }

  {
    app_exception.response_type_ = RpcResponseType::ResponseValueWithAttachments;
    EXPECT_EQ(app_exception.encode(*(metadata_.get()), protocol_, deserializer_, buffer),
              DubboFilters::DirectResponse::ResponseType::Exception);

    MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();
    EXPECT_TRUE(protocol_.decode(buffer, &context_, metadata));
    EXPECT_EQ(metadata->message_type(), MessageType::Response);
    buffer.drain(context_.header_size_);

    auto result = deserializer_.deserializeRpcResult(buffer, context_.body_size_);
    EXPECT_FALSE(result->hasException());
    buffer.drain(buffer.length());
  }

  {
    app_exception.response_type_ = RpcResponseType::ResponseWithValue;
    EXPECT_EQ(app_exception.encode(*(metadata_.get()), protocol_, deserializer_, buffer),
              DubboFilters::DirectResponse::ResponseType::Exception);

    MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();
    EXPECT_TRUE(protocol_.decode(buffer, &context_, metadata));
    EXPECT_EQ(metadata->message_type(), MessageType::Response);
    buffer.drain(context_.header_size_);

    auto result = deserializer_.deserializeRpcResult(buffer, context_.body_size_);
    EXPECT_TRUE(result->hasException());
  }
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
