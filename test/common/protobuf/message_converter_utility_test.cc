#include "source/common/protobuf/message_converter_utility.h"

#include <memory>
#include <string>

#include "src/message_data/cord_message_data.h"
#include "test/common/protobuf/message_converter_test_lib.h"
#include "test/proto/apikeys.pb.h"
#include "gmock/gmock.h"
#include "test/test_common/status_utility.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/grpc/common.h"
#include "grpc_transcoding/message_reader.h"
#include "src/message_data/cord_message_data.h"
#include "ocpdiag/core/testing/status_matchers.h"

namespace Envoy::ProtobufMessage {
namespace {

using ::google::grpc::transcoding::kGrpcDelimiterByteSize;
using ::google::grpc::transcoding::MessageReader;
using ::apikeys::CreateApiKeyRequest;


CreateApiKeyRequest GetCreateApiKeyRequest() {
  CreateApiKeyRequest req;
  *req.mutable_parent() = "projects/cloud-api-proxy-test-client";
  *(*req.mutable_key()).mutable_display_name() = "my-api-key";
  return req;
}


TEST(MaybeParseGrpcMessage, ParseSingleMessageSingleFrame) {
  // Setup request data.
  CreateApiKeyRequest request = GetCreateApiKeyRequest();
    CreateMessageDataFunc factory = []() {
    return std::make_unique<google::protobuf::field_extraction::CordMessageData>();
  };
  Envoy::Buffer::InstancePtr request_data =
      Envoy::Grpc::Common::serializeToGrpcFrame(request);

  // Single message is parsed.
  Envoy::Buffer::OwnedImpl request_in;
  request_in.move(*request_data);
ASSERT_OK_AND_ASSIGN(auto parsed_output,
                       MaybeParseGrpcMessage(factory, request_in));
  EXPECT_FALSE(parsed_output.needs_more_data);
  ASSERT_NE(parsed_output.message, nullptr);
  ASSERT_NE(parsed_output.owned_bytes, nullptr);

  // Single message is correctly preserved.
  Envoy::Buffer::OwnedImpl request_out;
  request_out.move(parsed_output.frame_header);
  request_out.move(*parsed_output.owned_bytes);
  CheckSerializedData<CreateApiKeyRequest>(request_out, {request});
}

TEST(MaybeParseGrpcMessage, ParseSingleMessageSplitFrames) {
  constexpr uint start_data_size = 3;
  constexpr uint middle_data_size = 4;

  // Setup request data.
  CreateApiKeyRequest request = GetCreateApiKeyRequest();
  Envoy::Buffer::InstancePtr request_data =
      Envoy::Grpc::Common::serializeToGrpcFrame(request);
    CreateMessageDataFunc factory = []() {
    return std::make_unique<google::protobuf::field_extraction::CordMessageData>();
  };

   // Split into multiple buffers.
  Envoy::Buffer::OwnedImpl start_request_data;
  Envoy::Buffer::OwnedImpl middle_request_data;
  Envoy::Buffer::OwnedImpl end_request_data;
  start_request_data.move(*request_data, start_data_size);
  middle_request_data.move(*request_data, middle_data_size);
  end_request_data.move(*request_data);
  EXPECT_EQ(request_data->length(), 0);

  // Function under test. First 2 calls do not have the entire message, so they
  // are buffered internally.
  Envoy::Buffer::OwnedImpl request_in;
  request_in.move(start_request_data);
  ASSERT_OK_AND_ASSIGN(auto parsed_output,
                       MaybeParseGrpcMessage(factory, request_in));
  EXPECT_TRUE(parsed_output.needs_more_data);

  request_in.move(middle_request_data);
  ASSERT_OK_AND_ASSIGN(parsed_output,
                       MaybeParseGrpcMessage(factory, request_in));
  EXPECT_TRUE(parsed_output.needs_more_data);

  request_in.move(end_request_data);
  ASSERT_OK_AND_ASSIGN(parsed_output,
                       MaybeParseGrpcMessage(factory, request_in));
  EXPECT_FALSE(parsed_output.needs_more_data);
  ASSERT_NE(parsed_output.message, nullptr);
  ASSERT_NE(parsed_output.owned_bytes, nullptr);

  // When complete, data is preserved.
  Envoy::Buffer::OwnedImpl request_out;
  request_out.move(parsed_output.frame_header);
  request_out.move(*parsed_output.owned_bytes);
  CheckSerializedData<CreateApiKeyRequest>(request_out, {request});
}

TEST(MaybeParseGrpcMessage, ParseMultipleMessagesSplitFrames) {
  Envoy::Buffer::OwnedImpl request_out;

  // Setup request data.
    CreateApiKeyRequest request = GetCreateApiKeyRequest();
    CreateMessageDataFunc factory = []() {
    return std::make_unique<google::protobuf::field_extraction::CordMessageData>();
  };
  Envoy::Buffer::InstancePtr request_data1 =
      Envoy::Grpc::Common::serializeToGrpcFrame(request);
  Envoy::Buffer::InstancePtr request_data2 =
      Envoy::Grpc::Common::serializeToGrpcFrame(request);

 // Multiple messages are parsed individually.
  auto request_in = std::make_unique<Envoy::Buffer::OwnedImpl>();
  request_in->move(*request_data1);
  ASSERT_OK_AND_ASSIGN(auto parsed_output1,
                       MaybeParseGrpcMessage(factory, *request_in));
  EXPECT_FALSE(parsed_output1.needs_more_data);
  ASSERT_NE(parsed_output1.message, nullptr);
  ASSERT_NE(parsed_output1.owned_bytes, nullptr);
  request_out.move(parsed_output1.frame_header);
  request_out.move(*parsed_output1.owned_bytes);

  // Caller is expected to re-create transcoder objects once a message is
  // parsed (for the next message).
  request_in = std::make_unique<Envoy::Buffer::OwnedImpl>();
  request_in->move(*request_data2);
  ASSERT_OK_AND_ASSIGN(auto parsed_output2,
                       MaybeParseGrpcMessage(factory, *request_in));
  EXPECT_FALSE(parsed_output2.needs_more_data);
  ASSERT_NE(parsed_output2.message, nullptr);
  ASSERT_NE(parsed_output2.owned_bytes, nullptr);
  request_out.move(parsed_output2.frame_header);
  request_out.move(*parsed_output2.owned_bytes);

  // Messages are correctly preserved.
  CheckSerializedData<CreateApiKeyRequest>(request_out, {request, request});
}

TEST(MaybeParseGrpcMessage, ParseNeedsMoreData) {
  Envoy::Buffer::OwnedImpl request_in;

  // No data to parse.
  CreateMessageDataFunc factory = []() {
    return std::make_unique<google::protobuf::field_extraction::CordMessageData>();
  };
  ASSERT_OK_AND_ASSIGN(auto parsed_output,
                       MaybeParseGrpcMessage(factory, request_in));
  EXPECT_TRUE(parsed_output.needs_more_data);
}

TEST(SizeToDelimiter, VerifyDelimiterIsPreserved) {
  Envoy::Buffer::OwnedImpl request_in;

  // Setup request data.
   CreateApiKeyRequest request = GetCreateApiKeyRequest();
  Envoy::Buffer::InstancePtr request_data =
      Envoy::Grpc::Common::serializeToGrpcFrame(request);
  CreateMessageDataFunc factory = []() {
    return std::make_unique<google::protobuf::field_extraction::CordMessageData>();
  };

  // Single message is parsed, but `request_out` not provided.
  request_in.move(*request_data);
  ASSERT_OK_AND_ASSIGN(auto parsed_output,
                       MaybeParseGrpcMessage(factory, request_in));
  EXPECT_FALSE(parsed_output.needs_more_data);
  ASSERT_NE(parsed_output.message, nullptr);
  ASSERT_NE(parsed_output.owned_bytes, nullptr);

  // Function under test used to fill in `request_out`.
  Envoy::Buffer::OwnedImpl request_out;
  ASSERT_OK_AND_ASSIGN(std::string delimiter,
                       SizeToDelimiter(parsed_output.message->Size()));
  EXPECT_EQ(delimiter.length(), kGrpcDelimiterByteSize);
  request_out.add(delimiter);

  // Single message is correctly preserved.
  request_out.move(*parsed_output.owned_bytes);
  CheckSerializedData<CreateApiKeyRequest>(request_out, {request});
}

TEST(MaybeParseGrpcMessage, ParseZeroLengthMessage) {
  Envoy::Buffer::OwnedImpl request_in;
auto  delimiter = SizeToDelimiter(0);
ASSERT_OK(delimiter);
  EXPECT_EQ(delimiter->length(), kGrpcDelimiterByteSize);
  request_in.add(*delimiter);
    CreateMessageDataFunc factory = []() {
    return std::make_unique<google::protobuf::field_extraction::CordMessageData>();
  };

// Single message (of length 0) to be parsed.
  ASSERT_OK_AND_ASSIGN(auto parsed_output,
                       MaybeParseGrpcMessage(factory, request_in));
  EXPECT_FALSE(parsed_output.needs_more_data);
  ASSERT_NE(parsed_output.message, nullptr);
  ASSERT_NE(parsed_output.owned_bytes, nullptr);
  EXPECT_EQ(parsed_output.message->Size(), 0);
  EXPECT_EQ(parsed_output.owned_bytes->length(), 0);
}

}  // namespace
}  // namespace