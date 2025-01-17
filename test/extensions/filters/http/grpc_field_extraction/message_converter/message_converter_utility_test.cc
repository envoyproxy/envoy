#include <memory>
#include <string>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/grpc/common.h"
#include "source/extensions/filters/http/grpc_field_extraction/message_converter/message_converter_utility.h"

#include "test/extensions/filters/http/grpc_field_extraction/message_converter/message_converter_test_lib.h"
#include "test/proto/apikeys.pb.h"
#include "test/test_common/status_utility.h"

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "grpc_transcoding/message_reader.h"
#include "gtest/gtest.h"
#include "ocpdiag/core/testing/status_matchers.h"
#include "proto_field_extraction/message_data/cord_message_data.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcFieldExtraction {
namespace {

using ::apikeys::CreateApiKeyRequest;
using ::google::grpc::transcoding::kGrpcDelimiterByteSize;

CreateApiKeyRequest getCreateApiKeyRequest() {
  CreateApiKeyRequest req;
  *req.mutable_parent() = "projects/cloud-api-proxy-test-client";
  *(*req.mutable_key()).mutable_display_name() = "my-api-key";
  return req;
}

TEST(ParseGrpcMessage, ParseSingleMessageSingleFrame) {
  // Setup request data.
  CreateApiKeyRequest request = getCreateApiKeyRequest();
  CreateMessageDataFunc factory = []() {
    return std::make_unique<Protobuf::field_extraction::CordMessageData>();
  };
  Buffer::InstancePtr request_data = Grpc::Common::serializeToGrpcFrame(request);

  // Single message is parsed.
  Buffer::OwnedImpl request_in;
  request_in.move(*request_data);
  ASSERT_OK_AND_ASSIGN(auto parsed_output, parseGrpcMessage(factory, request_in));
  EXPECT_FALSE(parsed_output.needs_more_data);
  ASSERT_NE(parsed_output.message, nullptr);
  ASSERT_NE(parsed_output.owned_bytes, nullptr);

  // Single message is correctly preserved.
  Buffer::OwnedImpl request_out;
  request_out.move(parsed_output.frame_header);
  request_out.move(*parsed_output.owned_bytes);
  checkSerializedData<CreateApiKeyRequest>(request_out, {request});
}

TEST(ParseGrpcMessage, ParseSingleMessageSplitFrames) {
  const uint32_t start_data_size = 3;
  const uint32_t middle_data_size = 4;

  // Setup request data.
  CreateApiKeyRequest request = getCreateApiKeyRequest();
  Buffer::InstancePtr request_data = Grpc::Common::serializeToGrpcFrame(request);
  CreateMessageDataFunc factory = []() {
    return std::make_unique<Protobuf::field_extraction::CordMessageData>();
  };

  // Split into multiple buffers.
  Buffer::OwnedImpl start_request_data;
  Buffer::OwnedImpl middle_request_data;
  Buffer::OwnedImpl end_request_data;
  start_request_data.move(*request_data, start_data_size);
  middle_request_data.move(*request_data, middle_data_size);
  end_request_data.move(*request_data);
  EXPECT_EQ(request_data->length(), 0);

  // Function under test. First 2 calls do not have the entire message, so they
  // are buffered internally.
  Buffer::OwnedImpl request_in;
  request_in.move(start_request_data);
  ASSERT_OK_AND_ASSIGN(auto parsed_output, parseGrpcMessage(factory, request_in));
  EXPECT_TRUE(parsed_output.needs_more_data);

  request_in.move(middle_request_data);
  ASSERT_OK_AND_ASSIGN(parsed_output, parseGrpcMessage(factory, request_in));
  EXPECT_TRUE(parsed_output.needs_more_data);

  request_in.move(end_request_data);
  ASSERT_OK_AND_ASSIGN(parsed_output, parseGrpcMessage(factory, request_in));
  EXPECT_FALSE(parsed_output.needs_more_data);
  ASSERT_NE(parsed_output.message, nullptr);
  ASSERT_NE(parsed_output.owned_bytes, nullptr);

  // When complete, data is preserved.
  Buffer::OwnedImpl request_out;
  request_out.move(parsed_output.frame_header);
  request_out.move(*parsed_output.owned_bytes);
  checkSerializedData<CreateApiKeyRequest>(request_out, {request});
}

TEST(ParseGrpcMessage, ParseMultipleMessagesSplitFrames) {
  Buffer::OwnedImpl request_out;

  // Setup request data.
  CreateApiKeyRequest request = getCreateApiKeyRequest();
  CreateMessageDataFunc factory = []() {
    return std::make_unique<Protobuf::field_extraction::CordMessageData>();
  };
  Buffer::InstancePtr request_data1 = Grpc::Common::serializeToGrpcFrame(request);
  Buffer::InstancePtr request_data2 = Grpc::Common::serializeToGrpcFrame(request);

  // Multiple messages are parsed individually.
  auto request_in = std::make_unique<Buffer::OwnedImpl>();
  request_in->move(*request_data1);
  ASSERT_OK_AND_ASSIGN(auto parsed_output1, parseGrpcMessage(factory, *request_in));
  EXPECT_FALSE(parsed_output1.needs_more_data);
  ASSERT_NE(parsed_output1.message, nullptr);
  ASSERT_NE(parsed_output1.owned_bytes, nullptr);
  request_out.move(parsed_output1.frame_header);
  request_out.move(*parsed_output1.owned_bytes);

  // Caller is expected to re-create transcoder objects once a message is
  // parsed (for the next message).
  request_in = std::make_unique<Buffer::OwnedImpl>();
  request_in->move(*request_data2);
  ASSERT_OK_AND_ASSIGN(auto parsed_output2, parseGrpcMessage(factory, *request_in));
  EXPECT_FALSE(parsed_output2.needs_more_data);
  ASSERT_NE(parsed_output2.message, nullptr);
  ASSERT_NE(parsed_output2.owned_bytes, nullptr);
  request_out.move(parsed_output2.frame_header);
  request_out.move(*parsed_output2.owned_bytes);

  // Messages are correctly preserved.
  checkSerializedData<CreateApiKeyRequest>(request_out, {request, request});
}

TEST(ParseGrpcMessage, ParseNeedsMoreData) {
  Buffer::OwnedImpl request_in;

  // No data to parse.
  CreateMessageDataFunc factory = []() {
    return std::make_unique<Protobuf::field_extraction::CordMessageData>();
  };
  ASSERT_OK_AND_ASSIGN(auto parsed_output, parseGrpcMessage(factory, request_in));
  EXPECT_TRUE(parsed_output.needs_more_data);
}

TEST(SizeToDelimiter, VerifyDelimiterIsPreserved) {
  Buffer::OwnedImpl request_in;

  // Setup request data.
  CreateApiKeyRequest request = getCreateApiKeyRequest();
  Buffer::InstancePtr request_data = Grpc::Common::serializeToGrpcFrame(request);
  CreateMessageDataFunc factory = []() {
    return std::make_unique<Protobuf::field_extraction::CordMessageData>();
  };

  // Single message is parsed, but `request_out` not provided.
  request_in.move(*request_data);
  ASSERT_OK_AND_ASSIGN(auto parsed_output, parseGrpcMessage(factory, request_in));
  EXPECT_FALSE(parsed_output.needs_more_data);
  ASSERT_NE(parsed_output.message, nullptr);
  ASSERT_NE(parsed_output.owned_bytes, nullptr);

  // Function under test used to fill in `request_out`.
  Buffer::OwnedImpl request_out;
  ASSERT_OK_AND_ASSIGN(std::string delimiter, sizeToDelimiter(parsed_output.message->Size()));
  EXPECT_EQ(delimiter.length(), kGrpcDelimiterByteSize);
  request_out.add(delimiter);

  // Single message is correctly preserved.
  request_out.move(*parsed_output.owned_bytes);
  checkSerializedData<CreateApiKeyRequest>(request_out, {request});
}

TEST(ParseGrpcMessage, ParseZeroLengthMessage) {
  Buffer::OwnedImpl request_in;
  auto delimiter = sizeToDelimiter(0);
  ASSERT_OK(delimiter);
  EXPECT_EQ(delimiter->length(), kGrpcDelimiterByteSize);
  request_in.add(*delimiter);
  CreateMessageDataFunc factory = []() {
    return std::make_unique<Protobuf::field_extraction::CordMessageData>();
  };

  // Single message (of length 0) to be parsed.
  ASSERT_OK_AND_ASSIGN(auto parsed_output, parseGrpcMessage(factory, request_in));
  EXPECT_FALSE(parsed_output.needs_more_data);
  ASSERT_NE(parsed_output.message, nullptr);
  ASSERT_NE(parsed_output.owned_bytes, nullptr);
  EXPECT_EQ(parsed_output.message->Size(), 0);
  EXPECT_EQ(parsed_output.owned_bytes->length(), 0);
}

TEST(SizeToDelimiter, Oversize) {
  auto delimiter = sizeToDelimiter(std::numeric_limits<uint64_t>::max());
  ASSERT_FALSE(delimiter.ok());
  EXPECT_EQ(delimiter.status().ToString(),
            "FAILED_PRECONDITION: Current input message size 18446744073709551615 is larger than "
            "max allowed gRPC message length of 4294967295");
}

} // namespace
} // namespace GrpcFieldExtraction
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
