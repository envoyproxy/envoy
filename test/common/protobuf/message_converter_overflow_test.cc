#include <limits>
#include <string>
#include <utility>

#include "source/common/protobuf/message_converter.h"
#include "test/common/protobuf/message_converter_test_lib.h"
#include "test/proto/apikeys.pb.h"
#include "src/message_data/cord_message_data.h"
#include "gmock/gmock.h"
#include "test/test_common/status_utility.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/grpc/common.h"
#include "ocpdiag/core/testing/status_matchers.h"

namespace Envoy::ProtobufMessage {
namespace {
using ::apikeys::CreateApiKeyRequest;
using ::Envoy::StatusHelpers::StatusIs;
using :: google::protobuf::util::MessageDifferencer;
using ::google::protobuf::field_extraction::CordMessageData;

// Request body to populate runtime messages with.
CreateApiKeyRequest GetCreateApiKeyRequest() {
  CreateApiKeyRequest req;
  *req.mutable_parent() = "projects/cloud-api-proxy-test-client";
  *(*req.mutable_key()).mutable_display_name() = "my-api-key";
  return req;
}

std::unique_ptr<CreateMessageDataFunc>
Factory() {
  return std::make_unique<
      CreateMessageDataFunc>([]() {
    return std::make_unique<google::protobuf::field_extraction::CordMessageData>();
  });
}

TEST(MessageConverterReadOnly, MessageLengthDoesNotOverflowFrame) {
  // We create a message that is just under the proto limit.
  // http://google3/third_party/protobuf/message_lite.h;l=104;rcl=436867703
  const std::string kLargeRequestBody =
      std::string(std::numeric_limits<int32_t>::max() - 100, 'A');

  CreateApiKeyRequest request = GetCreateApiKeyRequest();
  request.set_parent(kLargeRequestBody);
  Envoy::Buffer::InstancePtr request_data =
      Envoy::Grpc::Common::serializeToGrpcFrame(request);

  // Function under test.
  MessageConverter converter(Factory());
  ASSERT_OK_AND_ASSIGN(auto message_data,
                       converter.AccumulateMessage(*request_data, true));
  ASSERT_NE(message_data, nullptr);
  EXPECT_EQ(request_data->length(), 0);
  EXPECT_TRUE(MessageDifferencer::Equals(ParseFromStreamMessage(*message_data),request));

  // Function under test.
  ASSERT_OK_AND_ASSIGN(auto final_data, converter.ConvertBackToBuffer(
                                            std::move(message_data)));
  ASSERT_NE(final_data, nullptr);

  // Verify converted data is correctly preserved.
  CheckSerializedData<CreateApiKeyRequest>(*final_data, {request});
}


TEST(MessageConverterMutable, SingleMessageOverflowMutation) {
  CreateApiKeyRequest request = GetCreateApiKeyRequest();
  Envoy::Buffer::InstancePtr request_data =
      Envoy::Grpc::Common::serializeToGrpcFrame(request);

  // Convert to StreamMessage.
  MessageConverter converter(Factory());
  ASSERT_OK_AND_ASSIGN(auto message_data,
                       converter.AccumulateMessage(*request_data, true));
  ASSERT_NE(message_data, nullptr);
  EXPECT_EQ(request_data->length(), 0);

  std::string s(std::numeric_limits<uint32_t>::max(), 'a');
  dynamic_cast<CordMessageData*>(message_data->message())->Cord().Append(s);

  // Convert back to Envoy buffer.
  EXPECT_THAT(converter.ConvertBackToBuffer(std::move(message_data)),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

}  // namespace
}  // namespace experimental::taoxuy