#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/grpc/common.h"
#include "source/extensions/filters/http/grpc_field_extraction/message_converter/message_converter.h"

#include "test/extensions/filters/http/grpc_field_extraction/message_converter/message_converter_test_lib.h"
#include "test/proto/apikeys.pb.h"
#include "test/test_common/status_utility.h"

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "grpc_transcoding/message_reader.h"
#include "gtest/gtest.h"
#include "ocpdiag/core/testing/status_matchers.h"
#include "proto_field_extraction/message_data/cord_message_data.h"
#include "proto_field_extraction/message_data/message_data.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcFieldExtraction {
namespace {

using ::apikeys::CreateApiKeyRequest;
using ::google::grpc::transcoding::kGrpcDelimiterByteSize;
using Protobuf::util::MessageDifferencer;
using StatusHelpers::StatusIs;
using ::testing::Bool;
using ::testing::Combine;
using ::testing::Values;

CreateApiKeyRequest getCreateApiKeyRequest() {
  CreateApiKeyRequest req;
  *req.mutable_parent() = "projects/test";
  *(*req.mutable_key()).mutable_display_name() = "my-api-key";
  return req;
}
constexpr absl::string_view kAlternativeParent = "projects/test";

// https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md
// `Compressed-Flag` = byte 1 = 0, Message-Length = bytes 2-5 = 0
constexpr char kZeroSizeFrame[] = "\0\0\0\0\0";
// 'Compressed-Flag' = bytes 1 != 0, this is invalid.
constexpr char kInvalidFrame[] = "\1\0\0\0\0";

std::string createOriginalRequestParent(int index) {
  return absl::StrCat("projects/original-", index);
}

std::string createModifiedRequestParent(int index) {
  return absl::StrCat("projects/modified-", index);
}

// Creates request messages with each `parent` set based on the index.
//
// `message_size` determines how large to make the request message by adding
// additional characters to the request key.
//
// If `is_modified=true`, the request slightly changes to emulate the
// modifications done by `MutateMessage` and `BatchMutatesStreamMessages`.
std::vector<CreateApiKeyRequest> createRequestMessages(int num_requests, int message_size,
                                                       bool is_modified) {
  std::vector<CreateApiKeyRequest> requests;
  requests.reserve(num_requests);
  for (int i = 0; i < num_requests; i++) {
    requests.emplace_back(getCreateApiKeyRequest());

    if (is_modified) {
      requests.back().set_parent(createModifiedRequestParent(i));
      requests.back().mutable_key()->set_current_key("removed");
      requests.back().mutable_key()->set_location(std::string(message_size, 'z'));
    } else {
      requests.back().set_parent(createOriginalRequestParent(i));
      requests.back().mutable_key()->set_current_key(std::string(message_size, 'A'));
    }
  }
  return requests;
}

// Create buffers with each buffer contains exactly 1 full gRPC frame. 1 buffer
// per request message.
std::vector<Buffer::InstancePtr>
createOneFramePerBuffer(const std::vector<CreateApiKeyRequest>& request_messages) {
  std::vector<Buffer::InstancePtr> buffers;
  buffers.reserve(request_messages.size());
  for (auto& request_message : request_messages) {
    buffers.emplace_back(Grpc::Common::serializeToGrpcFrame(request_message));
  }
  return buffers;
}

// Creates a single buffer that contains all request data.
// There is no guarantee on the slice format in the buffer due to internal
// optimizations in the `Buffer::move` API.
std::vector<Buffer::InstancePtr>
consolidateBuffers(const std::vector<Buffer::InstancePtr> request_buffers) {
  Buffer::InstancePtr consolidated_buffer = std::make_unique<Buffer::OwnedImpl>();
  for (auto& request_buffer : request_buffers) {
    consolidated_buffer->move(*request_buffer);
  }

  std::vector<Buffer::InstancePtr> buffers;
  buffers.push_back(std::move(consolidated_buffer));
  return buffers;
}

// Create a list of exactly `num_splits` buffers from the given
// list of buffers. This results in the consolidated buffers not being split by
// frame - the start and end of each buffer is "random". This results in gRPC
// frames and messages being split across multiple buffers.
//
// Input buffers should be a single consolidated buffer.
std::vector<Buffer::InstancePtr>
consolidateBuffersIntoSplitFrames(const std::vector<Buffer::InstancePtr> request_buffers,
                                  int num_splits) {
  std::vector<Buffer::InstancePtr> buffers;
  buffers.reserve(num_splits);

  // Calculate how much data we need for each buffer.
  EXPECT_EQ(request_buffers.size(), 1);
  uint64_t request_length = request_buffers[0]->length();
  auto data_per_buffer = request_length / num_splits;

  // If there is not enough data, create as many buffers as possible (by moving
  // 1 byte to each buffer).
  if (data_per_buffer == 0) {
    while (request_buffers[0]->length() > 0) {
      buffers.push_back(std::make_unique<Buffer::OwnedImpl>());
      buffers.back()->move(*request_buffers[0], 1);
    }
    return buffers;
  }

  // Move enough data into each individual buffer.
  for (int i = 0; i < num_splits; i++) {
    buffers.push_back(std::make_unique<Buffer::OwnedImpl>());
    buffers.back()->move(*request_buffers[0], data_per_buffer);
  }

  // Move all leftover data to the last buffer.
  buffers.back()->move(*request_buffers[0]);
  return buffers;
}

// Creates a single buffer that contains a single slice with all request data.
// The slice has an allocation that matches the exact size all the gRPC request
// data.
std::vector<Buffer::InstancePtr>
consolidateBuffersSingleSliceExactAlloc(const std::vector<Buffer::InstancePtr> request_buffers) {
  // Calculate size we need to allocate for the single slice.
  uint64_t alloc_size = 0;
  for (auto& request_buffer : request_buffers) {
    alloc_size += request_buffer->length();
  }

  // Make reservation.
  Buffer::InstancePtr consolidated_buffer = std::make_unique<Buffer::OwnedImpl>();
  auto reservation = consolidated_buffer->reserveSingleSlice(alloc_size);
  EXPECT_GE(reservation.slice().len_, alloc_size);

  // Copy each slice from each individual buffer into this consolidated slice.
  uint8_t* dst = reinterpret_cast<uint8_t*>(reservation.slice().mem_);
  for (auto& request_buffer : request_buffers) {
    EXPECT_EQ(request_buffer->getRawSlices().size(), 1);
    memcpy(dst, request_buffer->frontSlice().mem_, request_buffer->length());
    dst += request_buffer->length();
  }

  // Finalize and return.
  reservation.commit(alloc_size);
  std::vector<Buffer::InstancePtr> buffers;
  buffers.push_back(std::move(consolidated_buffer));
  return buffers;
}

// Creates a single buffer that contains exactly 1 slice per request buffer.
// However, the slices are reserved with large amounts of extra capacity.
std::vector<Buffer::InstancePtr> consolidateBuffersSlicePerRequestExtraAlloc(
    const std::vector<Buffer::InstancePtr> request_buffers) {
  Buffer::InstancePtr consolidated_buffer = std::make_unique<Buffer::OwnedImpl>();

  // Copy each slice from each individual buffer into a new (larger)
  // reservation.
  for (auto& single_slice_request_data : request_buffers) {
    auto reservation = consolidated_buffer->reserveSingleSlice(
        single_slice_request_data->length() + (1 << 16), true);
    uint8_t* dst = reinterpret_cast<uint8_t*>(reservation.slice().mem_);
    EXPECT_EQ(single_slice_request_data->getRawSlices().size(), 1);
    memcpy(dst, single_slice_request_data->frontSlice().mem_, single_slice_request_data->length());
    reservation.commit(single_slice_request_data->length());
  }

  std::vector<Buffer::InstancePtr> buffers;
  buffers.push_back(std::move(consolidated_buffer));
  return buffers;
}

// Verifies a single parsed `EnvoyStreamMessage` matches the corresponding
// request message.
void verifyParsedStreamMessage(StreamMessage& message_data,
                               const CreateApiKeyRequest& request_message) {
  EXPECT_TRUE(MessageDifferencer::Equals(parseFromStreamMessage(message_data), request_message));
}

// Read-only batch verification of the `StreamMessage` list. Ensures all the
// stream messages match the original request messages.
void batchVerifyParsedStreamMessages(const std::vector<StreamMessagePtr>& message_datas,
                                     const std::vector<CreateApiKeyRequest>& request_messages) {
  EXPECT_EQ(message_datas.size(), request_messages.size());
  for (unsigned long i = 0; i < message_datas.size(); i++) {
    ASSERT_NE(message_datas[i], nullptr);
    verifyParsedStreamMessage(*message_datas[i], request_messages[i]);
  }
}

// Mutates the underlying proto in the given `StreamMessage` to match the
// expected modified request message.
void mutateMessage(StreamMessage& message_data, int index, int message_size) {
  // When a single message is mutated multiple times, we change the previous
  // mutation to ensure another mutation does occur.
  auto parsed_request = parseFromStreamMessage(message_data);
  parsed_request.mutable_key()->set_location(std::string(message_size / 2, 'X'));
  serializeToStreamMessage(message_data, parsed_request);

  // Real mutation here.
  parsed_request = parseFromStreamMessage(message_data);
  parsed_request.set_parent(createModifiedRequestParent(index));
  parsed_request.mutable_key()->set_current_key("removed");
  parsed_request.mutable_key()->set_location(std::string(message_size, 'z'));
  serializeToStreamMessage(message_data, parsed_request);
}

// Batch mutation of all underlying proto messages in `StreamMessage` list.
void batchMutateStreamMessages(const std::vector<StreamMessagePtr>& message_datas,
                               int message_size) {
  for (unsigned long i = 0; i < message_datas.size(); i++) {
    ASSERT_NE(message_datas[i], nullptr);
    mutateMessage(*message_datas[i], i, message_size);
  }
}

std::unique_ptr<CreateMessageDataFunc> factory() {
  return std::make_unique<CreateMessageDataFunc>(
      []() { return std::make_unique<Protobuf::field_extraction::CordMessageData>(); });
}

// Base class that all tests in this file inherit from.
class MessageConverterTest : public ::testing::Test {
public:
  MessageConverterTest() = default;
};

// ================================================
// MessageConverterReadOnlyBehavior
// Tests for basic behavior of `MessageConverter`
// `StreamMessages` are read-only
// ================================================
class MessageConverterReadOnlyBehavior : public MessageConverterTest {};

TEST_F(MessageConverterReadOnlyBehavior, SingleMessageSingleFrame) {
  CreateApiKeyRequest request = getCreateApiKeyRequest();
  Buffer::InstancePtr request_data = Grpc::Common::serializeToGrpcFrame(request);

  // Function under test.
  MessageConverter converter(factory());
  ASSERT_OK_AND_ASSIGN(auto message_data, converter.accumulateMessage(*request_data, true));
  ASSERT_NE(message_data, nullptr);
  EXPECT_EQ(request_data->length(), 0);
  EXPECT_TRUE(MessageDifferencer::Equals(parseFromStreamMessage(*message_data), request));
  EXPECT_TRUE(message_data->isFirstMessage());
  EXPECT_TRUE(message_data->isFinalMessage());

  // Function under test.
  ASSERT_OK_AND_ASSIGN(auto final_data, converter.convertBackToBuffer(std::move(message_data)));
  ASSERT_NE(final_data, nullptr);

  // Verify converted data is correctly preserved.
  EXPECT_GT(converter.bytesBuffered(), 0);
  checkSerializedData<CreateApiKeyRequest>(*final_data, {request});
  EXPECT_EQ(converter.bytesBuffered(), 0);
}

TEST_F(MessageConverterReadOnlyBehavior, SingleMessageSplitFrame) {
  const uint32_t start_data_size = 3;
  const uint32_t middle_data_size = 4;

  CreateApiKeyRequest request = getCreateApiKeyRequest();
  Buffer::InstancePtr request_data = Grpc::Common::serializeToGrpcFrame(request);

  // Split into multiple buffers.
  Buffer::OwnedImpl start_request_data;
  Buffer::OwnedImpl middle_request_data;
  Buffer::OwnedImpl end_request_data;
  start_request_data.move(*request_data, start_data_size);
  middle_request_data.move(*request_data, middle_data_size);
  end_request_data.move(*request_data);
  EXPECT_EQ(request_data->length(), 0);

  // Function under test. First 2 calls do not have the entire message, so
  // they are buffered internally.
  MessageConverter converter(factory());
  ASSERT_OK_AND_ASSIGN(auto message_data, converter.accumulateMessage(start_request_data, false));
  EXPECT_EQ(message_data, nullptr);
  EXPECT_EQ(start_request_data.length(), 0);

  ASSERT_OK_AND_ASSIGN(message_data, converter.accumulateMessage(middle_request_data, false));
  EXPECT_EQ(message_data, nullptr);
  EXPECT_EQ(middle_request_data.length(), 0);

  // Last call has the remaining message.
  ASSERT_OK_AND_ASSIGN(message_data, converter.accumulateMessage(end_request_data, true));
  ASSERT_NE(message_data, nullptr);
  EXPECT_EQ(end_request_data.length(), 0);
  EXPECT_TRUE(MessageDifferencer::Equals(parseFromStreamMessage(*message_data), request));

  // Function under test.
  ASSERT_OK_AND_ASSIGN(auto final_data, converter.convertBackToBuffer(std::move(message_data)));
  ASSERT_NE(final_data, nullptr);

  // Verify converted data is correctly preserved.
  EXPECT_GT(converter.bytesBuffered(), 0);
  checkSerializedData<CreateApiKeyRequest>(*final_data, {request});
  EXPECT_EQ(converter.bytesBuffered(), 0);
}

TEST_F(MessageConverterReadOnlyBehavior, MultipleMessagesSingleFrame) {
  CreateApiKeyRequest request1 = getCreateApiKeyRequest();
  request1.set_parent(kAlternativeParent);
  CreateApiKeyRequest request2 = getCreateApiKeyRequest();
  Buffer::InstancePtr request_data1 = Grpc::Common::serializeToGrpcFrame(request1);
  Buffer::InstancePtr request_data2 = Grpc::Common::serializeToGrpcFrame(request2);

  // Move all data to one buffer.
  MessageConverter converter(factory());
  Buffer::OwnedImpl request_in;
  Buffer::OwnedImpl final_data;
  request_in.move(*request_data1);
  request_in.move(*request_data2);

  // Convert the first message.
  ASSERT_OK_AND_ASSIGN(auto message_data, converter.accumulateMessage(request_in, false));
  ASSERT_NE(message_data, nullptr);
  EXPECT_EQ(request_in.length(), 0);
  EXPECT_TRUE(MessageDifferencer::Equals(parseFromStreamMessage(*message_data), request1));
  EXPECT_TRUE(message_data->isFirstMessage());
  EXPECT_FALSE(message_data->isFinalMessage());
  ASSERT_OK_AND_ASSIGN(auto converted_buffer,
                       converter.convertBackToBuffer(std::move(message_data)));
  ASSERT_NE(converted_buffer, nullptr);
  final_data.move(*converted_buffer);

  // Convert the second message.
  ASSERT_OK_AND_ASSIGN(message_data, converter.accumulateMessage(request_in, true));
  ASSERT_NE(message_data, nullptr);
  EXPECT_EQ(request_in.length(), 0);
  EXPECT_TRUE(MessageDifferencer::Equals(parseFromStreamMessage(*message_data), request2));
  EXPECT_FALSE(message_data->isFirstMessage());
  EXPECT_TRUE(message_data->isFinalMessage());
  ASSERT_OK_AND_ASSIGN(converted_buffer, converter.convertBackToBuffer(std::move(message_data)));
  ASSERT_NE(converted_buffer, nullptr);
  final_data.move(*converted_buffer);

  // Verify converted data is correctly preserved.
  EXPECT_GT(converter.bytesBuffered(), 0);
  checkSerializedData<CreateApiKeyRequest>(final_data, {request1, request2});
  EXPECT_EQ(converter.bytesBuffered(), 0);
}

TEST_F(MessageConverterReadOnlyBehavior, ReachBufferLimit) {
  CreateApiKeyRequest request1 = getCreateApiKeyRequest();
  request1.set_parent(kAlternativeParent);
  Buffer::InstancePtr request_data1 = Grpc::Common::serializeToGrpcFrame(request1);

  // Move all data to one buffer.
  MessageConverter converter(factory(), 0);
  Buffer::OwnedImpl request_in;
  request_in.move(*request_data1);

  // Convert the message which is larger than the buffer_limit=0;
  auto result = converter.accumulateMessage(request_in, true);
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().ToString(),
            "FAILED_PRECONDITION: Rejected because internal buffer limits are exceeded.");
}

// Tests a streaming RPC request with only data (no trailers) where the stream
// ends after the last message is already sent to the upstream.
// In this case, Envoy calls `decodeData` with `end_stream=true` but an empty
// data buffer. This test emulates the corresponding calls to
// MessageConverter.
TEST_F(MessageConverterReadOnlyBehavior, MultipleMessagesSingleFrameEndStreamAfterData) {
  CreateApiKeyRequest request1 = getCreateApiKeyRequest();
  request1.set_parent(kAlternativeParent);
  CreateApiKeyRequest request2 = getCreateApiKeyRequest();
  Buffer::InstancePtr request_data1 = Grpc::Common::serializeToGrpcFrame(request1);
  Buffer::InstancePtr request_data2 = Grpc::Common::serializeToGrpcFrame(request2);

  // Move all data to one buffer.
  MessageConverter converter(factory());
  Buffer::OwnedImpl request_in;
  Buffer::OwnedImpl final_data;
  request_in.move(*request_data1);
  request_in.move(*request_data2);

  // Convert the first message.
  ASSERT_OK_AND_ASSIGN(auto message_data, converter.accumulateMessage(request_in, false));
  ASSERT_NE(message_data, nullptr);
  EXPECT_EQ(request_in.length(), 0);
  EXPECT_TRUE(MessageDifferencer::Equals(parseFromStreamMessage(*message_data), request1));
  EXPECT_TRUE(message_data->isFirstMessage());
  EXPECT_FALSE(message_data->isFinalMessage());
  ASSERT_OK_AND_ASSIGN(auto converted_buffer,
                       converter.convertBackToBuffer(std::move(message_data)));
  ASSERT_NE(converted_buffer, nullptr);
  final_data.move(*converted_buffer);

  // Convert the second message.
  ASSERT_OK_AND_ASSIGN(message_data, converter.accumulateMessage(request_in, false));
  ASSERT_NE(message_data, nullptr);
  EXPECT_EQ(request_in.length(), 0);
  EXPECT_TRUE(MessageDifferencer::Equals(parseFromStreamMessage(*message_data), request2));
  EXPECT_FALSE(message_data->isFirstMessage());
  EXPECT_FALSE(message_data->isFinalMessage());
  ASSERT_OK_AND_ASSIGN(converted_buffer, converter.convertBackToBuffer(std::move(message_data)));
  ASSERT_NE(converted_buffer, nullptr);
  final_data.move(*converted_buffer);

  // Verify first 2 messages of converted data is correctly preserved.
  EXPECT_GT(converter.bytesBuffered(), 0);
  checkSerializedData<CreateApiKeyRequest>(final_data, {request1, request2});
  EXPECT_EQ(converter.bytesBuffered(), 0);

  // Third message emulates realistic end stream, where data length = 0.
  ASSERT_OK_AND_ASSIGN(message_data, converter.accumulateMessage(request_in, true));
  ASSERT_NE(message_data, nullptr);
  EXPECT_EQ(message_data->message(), nullptr);
  EXPECT_EQ(request_in.length(), 0);
  EXPECT_FALSE(message_data->isFirstMessage());
  EXPECT_TRUE(message_data->isFinalMessage());
  ASSERT_OK_AND_ASSIGN(converted_buffer, converter.convertBackToBuffer(std::move(message_data)));

  // Verify last conversion did not add any data.
  ASSERT_NE(converted_buffer, nullptr);
  EXPECT_EQ(converted_buffer->length(), 0);
  EXPECT_EQ(converter.bytesBuffered(), 0);
}

TEST_F(MessageConverterReadOnlyBehavior, SingleMessageNotEnoughData) {
  CreateApiKeyRequest request = getCreateApiKeyRequest();
  Buffer::InstancePtr request_data = Grpc::Common::serializeToGrpcFrame(request);
  Buffer::OwnedImpl partial_data;
  partial_data.move(*request_data, request_data->length() / 2);

  // First call with `end_stream=true` returns error due to not enough data.
  MessageConverter converter(factory());
  EXPECT_THAT(converter.accumulateMessage(partial_data, true),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_EQ(partial_data.length(), 0);

  // Second call has enough data.
  ASSERT_OK_AND_ASSIGN(auto message_data, converter.accumulateMessage(*request_data, true));
  ASSERT_NE(message_data, nullptr);
  EXPECT_EQ(request_data->length(), 0);
  EXPECT_TRUE(MessageDifferencer::Equals(parseFromStreamMessage(*message_data), request));
  ASSERT_OK_AND_ASSIGN(auto final_data, converter.convertBackToBuffer(std::move(message_data)));
  ASSERT_NE(final_data, nullptr);

  // Single message is correctly preserved.
  EXPECT_GT(converter.bytesBuffered(), 0);
  checkSerializedData<CreateApiKeyRequest>(*final_data, {request});
  EXPECT_EQ(converter.bytesBuffered(), 0);
}

TEST_F(MessageConverterReadOnlyBehavior, EmptyFrame) {
  Buffer::OwnedImpl request_data;
  request_data.add(kZeroSizeFrame, kGrpcDelimiterByteSize);

  // Function under test.
  MessageConverter converter(factory());
  ASSERT_OK_AND_ASSIGN(auto message_data, converter.accumulateMessage(request_data, true));
  ASSERT_NE(message_data, nullptr);
  EXPECT_EQ(request_data.length(), 0);

  ASSERT_OK_AND_ASSIGN(auto final_data, converter.convertBackToBuffer(std::move(message_data)));
  EXPECT_NE(final_data, nullptr);

  // gRPC frame delimiter is preserved.
  EXPECT_EQ(final_data->toString(), std::string(kZeroSizeFrame, kGrpcDelimiterByteSize));
}

TEST_F(MessageConverterReadOnlyBehavior, InvalidFrame) {
  Buffer::OwnedImpl request_data;
  request_data.add(kInvalidFrame, kGrpcDelimiterByteSize);

  // Function under test.
  MessageConverter converter(factory());
  EXPECT_THAT(converter.accumulateMessage(request_data, true),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(MessageConverterReadOnlyBehavior, OutOfOrderConversionSucceeds) {
  CreateApiKeyRequest request1 = getCreateApiKeyRequest();
  request1.set_parent(kAlternativeParent);
  CreateApiKeyRequest request2 = getCreateApiKeyRequest();
  Buffer::InstancePtr request_data1 = Grpc::Common::serializeToGrpcFrame(request1);
  Buffer::InstancePtr request_data2 = Grpc::Common::serializeToGrpcFrame(request2);

  // Move all data to one buffer.
  MessageConverter converter(factory());
  Buffer::OwnedImpl request_in;
  Buffer::OwnedImpl final_data;
  request_in.move(*request_data1);
  request_in.move(*request_data2);

  // Convert the messages to StreamMessage in order.
  ASSERT_OK_AND_ASSIGN(auto message_data1, converter.accumulateMessage(request_in, false));
  ASSERT_OK_AND_ASSIGN(auto message_data2, converter.accumulateMessage(request_in, true));

  // We can convert them back out of order.
  ASSERT_OK_AND_ASSIGN(auto converted_buffer2,
                       converter.convertBackToBuffer(std::move(message_data2)));
  ASSERT_OK_AND_ASSIGN(auto converted_buffer1,
                       converter.convertBackToBuffer(std::move(message_data1)));

  // Verify converted data is correctly preserved.
  checkSerializedData<CreateApiKeyRequest>(*converted_buffer2, {request2});
  checkSerializedData<CreateApiKeyRequest>(*converted_buffer1, {request1});
  EXPECT_EQ(converter.bytesBuffered(), 0);
}

TEST_F(MessageConverterReadOnlyBehavior, OutOfOrderCallsFail) {
  CreateApiKeyRequest request = getCreateApiKeyRequest();
  Buffer::InstancePtr request_data = Grpc::Common::serializeToGrpcFrame(request);

  // Get a StreamMessage from the first converter.
  MessageConverter converter1(factory());
  ASSERT_OK_AND_ASSIGN(auto message_data, converter1.accumulateMessage(*request_data, false));

  // We fail when we convert back with a different MessageConverter.
  // In the view of the second converter, we are calling `convertBackToBuffer`
  // before `AccumulateMessage`.
  MessageConverter converter2(factory());
  EXPECT_THAT(converter2.convertBackToBuffer(std::move(message_data)),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST_F(MessageConverterReadOnlyBehavior, SingleMessageOutOfOrderDrainingWorks) {
  CreateApiKeyRequest request = getCreateApiKeyRequest();
  std::string kAlternativeParent1(16 * 1024, '*');
  request.set_parent(kAlternativeParent1);
  request.mutable_key()->set_current_key(kAlternativeParent1);

  Buffer::InstancePtr request_data = Grpc::Common::serializeToGrpcFrame(request);
  Buffer::OwnedImpl for_split_message;

  for_split_message.move(*request_data, 16 * 1024);
  for_split_message.move(*request_data);
  request_data->move(for_split_message);

  // Function under test.
  MessageConverter converter(factory());
  ASSERT_OK_AND_ASSIGN(auto message_data, converter.accumulateMessage(*request_data, true));
  ASSERT_NE(message_data, nullptr);
  EXPECT_EQ(request_data->length(), 0);
  EXPECT_TRUE(MessageDifferencer::Equals(parseFromStreamMessage(*message_data), request));
  EXPECT_TRUE(message_data->isFirstMessage());
  EXPECT_TRUE(message_data->isFinalMessage());

  // Function under test.
  ASSERT_OK_AND_ASSIGN(auto final_data, converter.convertBackToBuffer(std::move(message_data)));
  ASSERT_NE(final_data, nullptr);

  // There are three slices in final_data. Move the first two slices to
  // final_data1; move the last slice to final_data2.
  Buffer::OwnedImpl final_data1;
  Buffer::OwnedImpl final_data2;
  final_data1.move(*final_data, 16384);
  final_data2.move(*final_data);

  // Make a copy and release the underlying final_data2.
  Buffer::OwnedImpl copy_out;
  copy_out.add(final_data2.toString());
  final_data2.drain(final_data2.length());

  // Verify converted data is correctly preserved.
  final_data->move(final_data1);
  final_data->move(copy_out);
  EXPECT_GT(converter.bytesBuffered(), 0);
  checkSerializedData<CreateApiKeyRequest>(*final_data, {request});
  EXPECT_EQ(converter.bytesBuffered(), 0);
}

TEST_F(MessageConverterReadOnlyBehavior, MultipleMessagesOutOfOrderDrainingWorks) {
  CreateApiKeyRequest request = getCreateApiKeyRequest();
  std::string kAlternativeParent1(16 * 1024, '*');
  request.set_parent(kAlternativeParent1);
  request.mutable_key()->set_current_key(kAlternativeParent1);
  Buffer::InstancePtr request_data1 = Grpc::Common::serializeToGrpcFrame(request);
  Buffer::InstancePtr request_data2 = Grpc::Common::serializeToGrpcFrame(request);

  // Move all data to one buffer.
  MessageConverter converter(factory());

  // Convert the first message.
  ASSERT_OK_AND_ASSIGN(auto message_data, converter.accumulateMessage(*request_data1, false));
  ASSERT_OK_AND_ASSIGN(auto converted_buffer1,
                       converter.convertBackToBuffer(std::move(message_data)));

  // Convert the second message.
  ASSERT_OK_AND_ASSIGN(message_data, converter.accumulateMessage(*request_data2, true));
  ASSERT_OK_AND_ASSIGN(auto converted_buffer2,
                       converter.convertBackToBuffer(std::move(message_data)));

  // Drain out the 2nd buffer before draining the 1st one.
  converted_buffer2.reset();

  // Verify converted data in buffer 1 is correctly preserved.
  EXPECT_GT(converter.bytesBuffered(), 0);
  checkSerializedData<CreateApiKeyRequest>(*converted_buffer1, {request});
  EXPECT_EQ(converter.bytesBuffered(), 0);
}

// ================================================
// MessageConverterMutableBehavior
// Tests for basic behavior of `MessageConverter`
// `StreamMessages` are mutated between conversions
// ================================================
class MessageConverterMutableBehavior : public MessageConverterTest {};

TEST_F(MessageConverterMutableBehavior, SingleMessageEmptyMutation) {
  CreateApiKeyRequest request = getCreateApiKeyRequest();
  Buffer::InstancePtr request_data = Grpc::Common::serializeToGrpcFrame(request);

  // Convert to StreamMessage.
  MessageConverter converter(factory());
  ASSERT_OK_AND_ASSIGN(auto message_data, converter.accumulateMessage(*request_data, true));
  ASSERT_NE(message_data, nullptr);
  EXPECT_EQ(request_data->length(), 0);

  message_data->message()->RemoveSuffix(message_data->message()->Size());

  // Convert back to Envoy buffer.
  ASSERT_OK_AND_ASSIGN(auto final_data, converter.convertBackToBuffer(std::move(message_data)));
  ASSERT_NE(final_data, nullptr);

  // Only gRPC frame delimiter is present.
  EXPECT_EQ(final_data->toString(), std::string(kZeroSizeFrame, kGrpcDelimiterByteSize));
}

// ================================================
// MessageConverterLifetime
// Tests various construction/destruction orders of
// the converter, data buffers, and dependencies.
// `StreamMessages` are read-only
// ================================================

// RunConversionOnMessage converts, mutates, and converts back a single
// request message.
//
// - `converter` is the current `MessageConverter` under test.
// - `input_buffers` are the Envoy buffers that contain all the serialized
// `input_requests`. They are not 1:1 sizes due to different methods for
// creating the buffers.
// - `num_conversions` is the number of times to run the same request through
// the `MessageConverters` - AKA the number of `MessageConverters` in the
// chain.
// - `message_size` is the extra size appended to each input request during
// mutation.
void runConversionOnMessage(MessageConverter& converter,
                            const std::vector<Buffer::InstancePtr>& input_buffers,
                            const std::vector<CreateApiKeyRequest>&, int message_size) {
  ASSERT_FALSE(input_buffers.empty());
  ASSERT_OK_AND_ASSIGN(auto message_data, converter.accumulateMessage(*input_buffers[0], true));

  // Mutation
  ASSERT_NE(message_data, nullptr);
  EXPECT_EQ(input_buffers[0]->length(), 0);
  mutateMessage(*message_data, 0, message_size);

  // Convert back to same input buffer.
  ASSERT_OK_AND_ASSIGN(auto converted_buffer,
                       converter.convertBackToBuffer(std::move(message_data)));
  ASSERT_NE(converted_buffer, nullptr);
  input_buffers[0]->move(*converted_buffer);
}

// After each iteration, the converter is destructed and a new one is created.
// The internal parsing buffers go out of scope after each message.
//
// - `input_buffers` are the Envoy buffers that contain all the serialized
// `input_requests`. They are not 1:1 sizes due to different methods for
// creating the buffers.
// - `num_conversions` is the number of times to run the same request through
// the `MessageConverters` - AKA the number of `MessageConverters` in the
// chain.
// - `message_size` is the extra size appended to each input request during
// mutation.
void runOutOfScope(const std::vector<Buffer::InstancePtr>& input_buffers,
                   const std::vector<CreateApiKeyRequest>& input_requests, int num_conversions,
                   int message_size) {
  ASSERT_EQ(input_buffers.size(), 1);
  ASSERT_EQ(input_requests.size(), 1);
  ASSERT_GT(num_conversions, 0);
  auto output_requests = createRequestMessages(1, message_size, true);

  for (int i = 0; i < num_conversions; i++) {
    // It is testing "Out of scope"
    // since each converter is destructed every iteration.
    MessageConverter converter(factory());
    runConversionOnMessage(converter, input_buffers, input_requests, message_size);
  }

  // Converter goes out of scope here. But data is expected to be valid.
  // Single message is correctly preserved.
  checkSerializedData<CreateApiKeyRequest>(*input_buffers[0], output_requests);
}

// All message converters are destructed after data is iterated through the
// chain. This ensures all internal parsing buffers in each `MessageConverter`
// stays in scope for the duration of the request.
//
// - `input_buffers` are the Envoy buffers that contain all the serialized
// `input_requests`. They are not 1:1 sizes due to different methods for
// creating the buffers.
// - `num_conversions` is the number of times to run the same request through
// the `MessageConverters` - AKA the number of `MessageConverters` in the
// chain.
// - `message_size` is the extra size appended to each input request during
// mutation.
void runInScope(const std::vector<Buffer::InstancePtr>& input_buffers,
                const std::vector<CreateApiKeyRequest>& input_requests, int num_conversions,
                int message_size) {
  ASSERT_EQ(input_buffers.size(), 1);
  ASSERT_EQ(input_requests.size(), 1);
  ASSERT_GT(num_conversions, 0);
  auto output_requests = createRequestMessages(1, message_size, true);

  {
    std::vector<std::unique_ptr<MessageConverter>> converters;
    converters.reserve(num_conversions);

    for (int i = 0; i < num_conversions; i++) {
      auto converter = std::make_unique<MessageConverter>(factory());
      runConversionOnMessage(*converter, input_buffers, input_requests, message_size);
      converters.push_back(std::move(converter));
    }
  }

  // All converters go out of scope here. But data is expected to be valid.
  // Single message is correctly preserved.
  checkSerializedData<CreateApiKeyRequest>(*input_buffers[0], output_requests);
}

// Runs the mutable lifetime tests via a chain of `MessageConverters`.
//
// Parameters are the following:
// 1) Whether to keep the `MessageConverter` in scope for all iterations. If
//    false, the `MessageConverter` will be destroyed after each iteration.
// 2) Number of `MessageConverter` instances to feed the single message
// through. 3) Message size
class RunChained : public ::testing::TestWithParam<std::tuple<bool, int, int>> {
public:
  RunChained() = default;
};

std::string runChainedParamsString(const ::testing::TestParamInfo<RunChained::ParamType>& info) {
  return absl::StrCat(std::get<0>(info.param) ? "InScope_" : "OutOfScope_", "NumConverters_",
                      std::get<1>(info.param), "_MessageSize_", std::get<2>(info.param));
}

TEST_P(RunChained, OneFramePerBuffer) {
  auto [test_in_scope, num_conversions, message_size] = GetParam();

  auto input_requests = createRequestMessages(1, message_size, false);
  auto input_buffers = createOneFramePerBuffer(input_requests);

  if (test_in_scope) {
    runInScope(input_buffers, input_requests, num_conversions, message_size);
  } else {
    runOutOfScope(input_buffers, input_requests, num_conversions, message_size);
  }
}

TEST_P(RunChained, ConsolidatedBuffer) {
  auto [test_in_scope, num_conversions, message_size] = GetParam();

  auto input_requests = createRequestMessages(1, message_size, false);
  auto input_buffers = consolidateBuffers(createOneFramePerBuffer(input_requests));

  if (test_in_scope) {
    runInScope(input_buffers, input_requests, num_conversions, message_size);
  } else {
    runOutOfScope(input_buffers, input_requests, num_conversions, message_size);
  }
}

TEST_P(RunChained, ConsolidateBuffersSingleSliceExactAlloc) {
  auto [test_in_scope, num_conversions, message_size] = GetParam();

  auto input_requests = createRequestMessages(1, message_size, false);
  auto input_buffers =
      consolidateBuffersSingleSliceExactAlloc(createOneFramePerBuffer(input_requests));

  if (test_in_scope) {
    runInScope(input_buffers, input_requests, num_conversions, message_size);
  } else {
    runOutOfScope(input_buffers, input_requests, num_conversions, message_size);
  }
}

TEST_P(RunChained, ConsolidateBuffersSlicePerRequestExtraAlloc) {
  auto [test_in_scope, num_conversions, message_size] = GetParam();

  auto input_requests = createRequestMessages(1, message_size, false);
  auto input_buffers =
      consolidateBuffersSlicePerRequestExtraAlloc(createOneFramePerBuffer(input_requests));

  if (test_in_scope) {
    runInScope(input_buffers, input_requests, num_conversions, message_size);
  } else {
    runOutOfScope(input_buffers, input_requests, num_conversions, message_size);
  }
}

// ================================================
// MessageConverterDataIntegrity
// Tests various data consumption/production models
// to catch data corruption issues.
// ================================================

// RunStreaming converts, mutates, and converts back each `input_buffer` one
// at a time in a chained fashion.
//
// - `converter` is the current `MessageConverter` under test.
// - `input_buffers` are the Envoy buffers that contain all the serialized
// `input_requests`. They are not 1:1 sizes due to different methods for
// creating the buffers.
// - `message_size` is the extra size appended to each input request during
// mutation.
void runStreaming(const std::vector<Buffer::InstancePtr>& input_buffers,
                  const std::vector<CreateApiKeyRequest>& input_requests, int message_size) {
  ASSERT_FALSE(input_buffers.empty());
  auto num_messages = input_requests.size();
  auto output_requests = createRequestMessages(num_messages, message_size, true);

  MessageConverter converter(factory());
  int request_index = 0;

  // Process input.
  for (auto& input_buffer : input_buffers) {
    ASSERT_OK_AND_ASSIGN(auto message_datas, converter.accumulateMessages(*input_buffer, false));

    // Mutate each processed request message (with absolute ordering).
    for (auto& message_data : message_datas) {
      ASSERT_NE(message_data, nullptr);
      verifyParsedStreamMessage(*message_data, input_requests[request_index]);
      mutateMessage(*message_data, request_index, message_size);

      // Convert back and verify.
      ASSERT_OK_AND_ASSIGN(auto converted_buffer,
                           converter.convertBackToBuffer(std::move(message_data)));
      ASSERT_NE(converted_buffer, nullptr);
      EXPECT_GT(converter.bytesBuffered(), 0);
      checkSerializedData<CreateApiKeyRequest>(*converted_buffer, {output_requests[request_index]});
      request_index++;
    }
  }

  // Ensure there is no lingering data in the converter.
  Buffer::OwnedImpl empty_buffer;
  ASSERT_OK_AND_ASSIGN(auto final_messages, converter.accumulateMessages(empty_buffer, true));
  ASSERT_EQ(final_messages.size(), 1);
  EXPECT_TRUE(final_messages[0]->isFinalMessage());
  ASSERT_OK(converter.convertBackToBuffer(std::move(final_messages[0])));
  EXPECT_EQ(converter.bytesBuffered(), 0);
}

// RunBatch converts, mutates, and converts back all `input_buffers` in a
// batch fashion. I.e. is converts ALL buffers first, mutates ALL buffers
// next, and then converts them ALL back.
//
// - `input_buffers` are the Envoy buffers that contain all the serialized
// `input_requests`. They are not 1:1 sizes due to different methods for
// creating the buffers.
// - `message_size` is the extra size appended to each input request during
// mutation.
// - `output_requests` is the expected requests (after mutation) the test
// wants.
// - `output_data` is an output variable that will contain all the converted
// back data after (for verification by caller).
void runBatch(const std::vector<Buffer::InstancePtr>& input_buffers,
              const std::vector<CreateApiKeyRequest>& input_requests,
              const std::vector<CreateApiKeyRequest>&, int message_size,
              std::vector<Buffer::InstancePtr>& output_data) {
  // Data should outlive the converter.
  std::vector<StreamMessagePtr> message_datas;
  {
    // Process input.
    MessageConverter converter(factory());
    for (auto& input_buffer : input_buffers) {
      ASSERT_OK_AND_ASSIGN(auto intermediates, converter.accumulateMessages(*input_buffer, false));
      message_datas.insert(message_datas.end(), std::make_move_iterator(intermediates.begin()),
                           std::make_move_iterator(intermediates.end()));
    }

    // Ensure there is no lingering data in the converter.
    Buffer::OwnedImpl empty_buffer;
    ASSERT_OK_AND_ASSIGN(auto final_messages, converter.accumulateMessages(empty_buffer, true));
    ASSERT_EQ(final_messages.size(), 1);
    EXPECT_TRUE(final_messages[0]->isFinalMessage());

    // Verification and mutation.
    batchVerifyParsedStreamMessages(message_datas, input_requests);
    batchMutateStreamMessages(message_datas, message_size);

    // Convert Back
    for (auto& message_data : message_datas) {
      ASSERT_OK_AND_ASSIGN(auto converted_buffer,
                           converter.convertBackToBuffer(std::move(message_data)));
      ASSERT_NE(converted_buffer, nullptr);
      output_data.push_back(std::move(converted_buffer));
    }

    // Ensure there is no lingering data in the converter.
    ASSERT_OK(converter.convertBackToBuffer(std::move(final_messages[0])));
  }
}

void runBatchAndVerifyInOrder(const std::vector<Buffer::InstancePtr>& input_buffers,
                              const std::vector<CreateApiKeyRequest>& input_requests,
                              int message_size) {
  auto num_messages = input_requests.size();
  auto output_requests = createRequestMessages(num_messages, message_size, true);
  std::vector<Buffer::InstancePtr> output_data;
  output_data.reserve(num_messages);

  // Run test.
  runBatch(input_buffers, input_requests, output_requests, message_size, output_data);

  // Verification of data integrity in regular destruction order.
  // Buffer data is freed in absolute ordering.
  Buffer::OwnedImpl merged_data;
  for (auto& data : output_data) {
    merged_data.move(*data);
  }
  checkSerializedData<CreateApiKeyRequest>(merged_data, output_requests);
}

// Runs the mutable data integrity tests.
//
// Parameters are the following:
// 1) Whether streaming mode is enabled (batch when false)
// 2) Number of request messages
// 3) Per-message size
class RunNoSplits : public ::testing::TestWithParam<std::tuple<bool, int, int>> {
public:
  RunNoSplits() = default;
};

std::string noSplitsParamsString(const ::testing::TestParamInfo<RunNoSplits::ParamType>& info) {
  return absl::StrCat(std::get<0>(info.param) ? "Streaming_" : "Batch_", "NumMessages_",
                      std::get<1>(info.param), "_MessageSize_", std::get<2>(info.param));
}

TEST_P(RunNoSplits, OneFramePerBuffer) {
  auto [is_streaming_enabled, num_messages, message_size] = GetParam();

  auto input_requests = createRequestMessages(num_messages, message_size, false);
  auto input_buffers = createOneFramePerBuffer(input_requests);

  if (is_streaming_enabled) {
    runStreaming(input_buffers, input_requests, message_size);
  } else {
    runBatchAndVerifyInOrder(input_buffers, input_requests, message_size);
  }
}

TEST_P(RunNoSplits, ConsolidatedBuffer) {
  auto [is_streaming_enabled, num_messages, message_size] = GetParam();

  auto input_requests = createRequestMessages(num_messages, message_size, false);
  auto input_buffers = consolidateBuffers(createOneFramePerBuffer(input_requests));

  if (is_streaming_enabled) {
    runStreaming(input_buffers, input_requests, message_size);
  } else {
    runBatchAndVerifyInOrder(input_buffers, input_requests, message_size);
  }
}

TEST_P(RunNoSplits, ConsolidateBuffersSingleSliceExactAlloc) {
  auto [is_streaming_enabled, num_messages, message_size] = GetParam();

  auto input_requests = createRequestMessages(num_messages, message_size, false);
  auto input_buffers =
      consolidateBuffersSingleSliceExactAlloc(createOneFramePerBuffer(input_requests));

  if (is_streaming_enabled) {
    runStreaming(input_buffers, input_requests, message_size);
  } else {
    runBatchAndVerifyInOrder(input_buffers, input_requests, message_size);
  }
}

TEST_P(RunNoSplits, ConsolidateBuffersSlicePerRequestExtraAlloc) {
  auto [is_streaming_enabled, num_messages, message_size] = GetParam();

  auto input_requests = createRequestMessages(num_messages, message_size, false);
  auto input_buffers =
      consolidateBuffersSlicePerRequestExtraAlloc(createOneFramePerBuffer(input_requests));

  if (is_streaming_enabled) {
    runStreaming(input_buffers, input_requests, message_size);
  } else {
    runBatchAndVerifyInOrder(input_buffers, input_requests, message_size);
  }
}

// Runs the mutable data integrity tests, with additional cross-frame splits
// across the buffers. Made a separate test to allow configurable split
// sizes.
//
// Parameters are the following:
// 1) Whether streaming mode is enabled (batch when false)
// 2) Number of request messages
// 3) Per-message size
// 4) Number of cross-frame splits
class RunWithSplits : public ::testing::TestWithParam<std::tuple<bool, int, int, int>> {
public:
  RunWithSplits() = default;
};

std::string withSplitsParamsString(const ::testing::TestParamInfo<RunWithSplits::ParamType>& info) {
  return absl::StrCat(std::get<0>(info.param) ? "Streaming_" : "Batch_", "NumMessages_",
                      std::get<1>(info.param), "_MessageSize_", std::get<2>(info.param),
                      "_NumSplits_", std::get<3>(info.param));
}

TEST_P(RunWithSplits, ConsolidateBuffersIntoSplitFrames) {
  auto [is_streaming_enabled, num_messages, message_size, num_splits] = GetParam();

  auto input_requests = createRequestMessages(num_messages, message_size, false);
  auto input_buffers = consolidateBuffersIntoSplitFrames(
      consolidateBuffers(createOneFramePerBuffer(input_requests)), num_splits);

  if (is_streaming_enabled) {
    runStreaming(input_buffers, input_requests, message_size);
  } else {
    runBatchAndVerifyInOrder(input_buffers, input_requests, message_size);
  }
}

// Runs the mutable data integrity tests, but this emulates incoming data
// while the converter is in the middle of parsing messages.
//
// This test supports ONLY streaming, no batch.
//
// Parameters are the following:
// 1) Number of iterations to wait before adding in the last half of data.
// 2) Number of request messages
// 3) Per-message size
class RunWithIncomingStreamingData : public ::testing::TestWithParam<std::tuple<int, int, int>> {
public:
  RunWithIncomingStreamingData() = default;
};

std::string incomingWithStreamingDataParamsString(
    const ::testing::TestParamInfo<RunWithIncomingStreamingData::ParamType>& info) {
  return absl::StrCat("NumWaitIterations_", std::get<0>(info.param), "_NumMessages_",
                      std::get<1>(info.param), "_MessageSize_", std::get<2>(info.param));
}
// RunIncomingStreamingData is similar to RunStreaming, but it adds extra
// data for conversion to the `MessageConverter` input buffer while it is in
// the middle of parsing/converting requests.
//
// - `input_buffers` are the Envoy buffers that contain all the serialized
// `input_requests`. They are not 1:1 sizes due to different methods for
// creating the buffers.
// - `message_size` is the extra size appended to each input request during
// mutation.
// - `num_wait_iterations` is the number of conversion iterations to wait
// before adding in the last half of data (i.e. the incoming streaming data).
void runIncomingStreamingData(const std::vector<Buffer::InstancePtr>& input_buffers,
                              const std::vector<CreateApiKeyRequest>& input_requests,
                              int message_size, int num_wait_iterations) {
  ASSERT_FALSE(input_buffers.empty());

  Buffer::OwnedImpl first_half_requests;
  Buffer::OwnedImpl last_half_requests;
  first_half_requests.move(*input_buffers[0], input_buffers[0]->length() / 2);
  last_half_requests.move(*input_buffers[0]);

  auto num_messages = input_requests.size();
  auto output_requests = createRequestMessages(num_messages, message_size, true);

  MessageConverter converter(factory());
  unsigned long request_index = 0;
  for (int iterations = 0; request_index < num_messages; iterations++) {
    if (iterations == num_wait_iterations) {
      // We waited long enough, move the remaining incoming data back to the
      // buffer that is currently being processed.
      first_half_requests.move(last_half_requests);
    }

    ASSERT_OK_AND_ASSIGN(auto message_data,
                         converter.accumulateMessage(first_half_requests, false));
    if (message_data == nullptr) {
      // Waiting for more iterations before adding in remaining data.
      ASSERT_LT(iterations, num_wait_iterations);
      continue;
    }

    verifyParsedStreamMessage(*message_data, input_requests[request_index]);
    mutateMessage(*message_data, request_index, message_size);

    // Convert back and verify.
    ASSERT_OK_AND_ASSIGN(auto converted_buffer,
                         converter.convertBackToBuffer(std::move(message_data)));
    ASSERT_NE(converted_buffer, nullptr);
    checkSerializedData<CreateApiKeyRequest>(*converted_buffer, {output_requests[request_index]});
    request_index++;
  }

  // Ensure there is no lingering data in the converter.
  Buffer::OwnedImpl empty_buffer;
  ASSERT_OK_AND_ASSIGN(auto final_messages, converter.accumulateMessages(empty_buffer, true));
  ASSERT_EQ(final_messages.size(), 1);
  EXPECT_TRUE(final_messages[0]->isFinalMessage());
  ASSERT_OK(converter.convertBackToBuffer(std::move(final_messages[0])));
  EXPECT_EQ(converter.bytesBuffered(), 0);
}

TEST_P(RunWithIncomingStreamingData, ConsolidatedBuffer) {
  auto [num_wait_iterations, num_messages, message_size] = GetParam();

  auto input_requests = createRequestMessages(num_messages, message_size, false);
  auto input_buffers = consolidateBuffers(createOneFramePerBuffer(input_requests));

  runIncomingStreamingData(input_buffers, input_requests, message_size, num_wait_iterations);
}

// Runs the mutable lifetime tests via out of order draining (of the converted
// back) buffers.
//
// Parameters are the following:
// 1) Number of request messages
// 2) Per-message size
class RunOutOfOrderDrain : public ::testing::TestWithParam<std::tuple<int, int>> {
public:
  RunOutOfOrderDrain() = default;
};

std::string runOutOfOrderDrainParamsString(
    const ::testing::TestParamInfo<RunOutOfOrderDrain::ParamType>& info) {
  return absl::StrCat("Batch_", "NumMessages_", std::get<0>(info.param), "_MessageSize_",
                      std::get<1>(info.param));
}

void runBatchAndDrainBackwards(const std::vector<Buffer::InstancePtr>& input_buffers,
                               const std::vector<CreateApiKeyRequest>& input_requests,
                               int message_size) {
  auto num_messages = input_requests.size();
  auto output_requests = createRequestMessages(num_messages, message_size, true);
  std::vector<Buffer::InstancePtr> output_data;
  output_data.reserve(num_messages);

  // Run test.
  runBatch(input_buffers, input_requests, output_requests, message_size, output_data);

  // Test that we can drain the underlying data in any order.
  // Specifically, we will drain backwards from `output_data`.
  for (auto it = output_data.rbegin(); it != output_data.rend(); it++) {
    checkSerializedData<CreateApiKeyRequest>(**it, {output_requests.back()});
    EXPECT_EQ((**it).length(), 0);
    output_requests.pop_back();
  }
}

TEST_P(RunOutOfOrderDrain, OneFramePerBuffer) {
  auto [num_messages, message_size] = GetParam();

  auto input_requests = createRequestMessages(num_messages, message_size, false);
  auto input_buffers = createOneFramePerBuffer(input_requests);

  runBatchAndDrainBackwards(input_buffers, input_requests, message_size);
}

TEST_P(RunOutOfOrderDrain, ConsolidatedBuffer) {
  auto [num_messages, message_size] = GetParam();

  auto input_requests = createRequestMessages(num_messages, message_size, false);
  auto input_buffers = consolidateBuffers(createOneFramePerBuffer(input_requests));

  runBatchAndDrainBackwards(input_buffers, input_requests, message_size);
}

TEST_P(RunOutOfOrderDrain, ConsolidateBuffersSingleSliceExactAlloc) {
  auto [num_messages, message_size] = GetParam();

  auto input_requests = createRequestMessages(num_messages, message_size, false);
  auto input_buffers =
      consolidateBuffersSingleSliceExactAlloc(createOneFramePerBuffer(input_requests));

  runBatchAndDrainBackwards(input_buffers, input_requests, message_size);
}

TEST_P(RunOutOfOrderDrain, ConsolidateBuffersSlicePerRequestExtraAlloc) {
  auto [num_messages, message_size] = GetParam();

  auto input_requests = createRequestMessages(num_messages, message_size, false);
  auto input_buffers =
      consolidateBuffersSlicePerRequestExtraAlloc(createOneFramePerBuffer(input_requests));

  runBatchAndDrainBackwards(input_buffers, input_requests, message_size);
}

// Note: Make sure EVERY test suite has a test case with
// message_size > page_size (1 << 12 usually).
// This triggers extra memory allocations in the underlying buffers.

INSTANTIATE_TEST_SUITE_P(MessageConverterLifetime, RunChained,
                         Combine(Bool(),                    // Is converter in scope?
                                 Values(1, 2, 7, 1 << 8),   // Num conversions
                                 Values(0, 1 << 6, 1 << 13) // Message size
                                 ),
                         runChainedParamsString);

INSTANTIATE_TEST_SUITE_P(MessageConverterLifetime, RunOutOfOrderDrain,
                         Combine(Values(1, 2, 7, 1 << 10),  // Num messages
                                 Values(0, 1 << 6, 1 << 13) // Message size
                                 ),
                         runOutOfOrderDrainParamsString);

INSTANTIATE_TEST_SUITE_P(MessageConverterDataIntegrity, RunNoSplits,
                         Combine(Bool(),                    // Is streaming enabled?
                                 Values(1, 2, 7, 1 << 10),  // Num messages
                                 Values(0, 1 << 6, 1 << 13) // Message size
                                 ),
                         noSplitsParamsString);

INSTANTIATE_TEST_SUITE_P(MessageConverterDataIntegrity, RunWithSplits,
                         Combine(Bool(),                     // Is streaming enabled?
                                 Values(1, 2, 7, 1 << 10),   // Num messages
                                 Values(0, 1 << 6, 1 << 13), // Message size
                                 Values(1, 2, 3, 1 << 6,
                                        1 << 10) // Num cross-frame splits
                                 ),
                         withSplitsParamsString);

INSTANTIATE_TEST_SUITE_P(MessageConverterDataIntegrity, RunWithIncomingStreamingData,
                         Combine(Values(1, 2, 3, 4, 5, 6,
                                        7),                 // Num wait iterations
                                 Values(1, 2, 7, 1 << 10),  // Num messages
                                 Values(0, 1 << 6, 1 << 13) // Message size
                                 ),
                         incomingWithStreamingDataParamsString);
} // namespace

} // namespace GrpcFieldExtraction
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
