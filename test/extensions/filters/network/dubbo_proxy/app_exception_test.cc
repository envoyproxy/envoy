#include "extensions/filters/network/dubbo_proxy/app_exception.h"
#include "extensions/filters/network/dubbo_proxy/deserializer_impl.h"
#include "extensions/filters/network/dubbo_proxy/dubbo_protocol_impl.h"
#include "extensions/filters/network/dubbo_proxy/filters/filter.h"
#include "extensions/filters/network/dubbo_proxy/hessian_deserializer_impl.h"
#include "extensions/filters/network/dubbo_proxy/hessian_utils.h"
#include "extensions/filters/network/dubbo_proxy/metadata.h"

#include "test/extensions/filters/network/dubbo_proxy/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

class AppExceptionTest : public testing::Test {
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
  size_t expect_body_size =
      HessianUtils::writeString(buffer, mock_message) +
      HessianUtils::writeInt(buffer, static_cast<uint8_t>(app_exception.response_type_));
  buffer.drain(buffer.length());

  metadata_->setSerializationType(SerializationType::Hessian);
  metadata_->setRequestId(0);

  EXPECT_EQ(app_exception.encode(*(metadata_.get()), protocol_, deserializer_, buffer),
            DubboFilters::DirectResponse::ResponseType::Exception);
  MessageMetadataSharedPtr metadata = std::make_shared<MessageMetadata>();
  EXPECT_TRUE(protocol_.decode(buffer, &context_, metadata));
  EXPECT_EQ(expect_body_size, context_.body_size_);
  EXPECT_EQ(metadata->message_type(), MessageType::Response);
  buffer.drain(context_.header_size_);

  // Verify the response type and content.
  size_t hessian_int_size;
  int type_value = HessianUtils::peekInt(buffer, &hessian_int_size);
  EXPECT_EQ(static_cast<uint8_t>(app_exception.response_type_), static_cast<uint8_t>(type_value));

  size_t hessian_string_size;
  std::string message = HessianUtils::peekString(buffer, &hessian_string_size, sizeof(uint8_t));
  EXPECT_EQ(mock_message, message);

  EXPECT_EQ(buffer.length(), hessian_int_size + hessian_string_size);

  auto result = deserializer_.deserializeRpcResult(buffer, context_.body_size_);
  EXPECT_TRUE(result->hasException());
  buffer.drain(buffer.length());

  AppException new_app_exception(app_exception);
  EXPECT_EQ(new_app_exception.status_, ResponseStatus::ServiceNotFound);

  MockProtocol mock_protocol;
  EXPECT_CALL(mock_protocol, encode(_, _, _)).WillOnce(Return(false));
  EXPECT_THROW(app_exception.encode(*(metadata_.get()), mock_protocol, deserializer_, buffer),
               EnvoyException);
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
