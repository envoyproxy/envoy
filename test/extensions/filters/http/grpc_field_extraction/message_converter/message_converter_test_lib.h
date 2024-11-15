#pragma once

#include <vector>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/grpc/codec.h"
#include "source/extensions/filters/http/grpc_field_extraction/message_converter/stream_message.h"

#include "test/proto/apikeys.pb.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "proto_field_extraction/message_data/cord_message_data.h"
#include "proto_field_extraction/message_data/message_data.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcFieldExtraction {

// Ensures that gRPC data in the Envoy HTTP/2 data streams can be deserialized
// into the expected messages. Drains the underlying buffer.
template <class MessageType>
void checkSerializedData(Envoy::Buffer::Instance& data,
                         std::vector<MessageType> expected_requests) {
  ::Envoy::Grpc::Decoder grpc_decoder;
  std::vector<::Envoy::Grpc::Frame> frames_after_processing;
  ASSERT_TRUE(grpc_decoder.decode(data, frames_after_processing).ok());

  ASSERT_EQ(expected_requests.size(), frames_after_processing.size());
  for (unsigned long i = 0; i < frames_after_processing.size(); i++) {
    MessageType request_after_processing;
    ASSERT_TRUE(
        request_after_processing.ParseFromString(frames_after_processing[i].data_->toString()));
    EXPECT_TRUE(
        Protobuf::util::MessageDifferencer::Equals(request_after_processing, expected_requests[i]));
  }
}

apikeys::CreateApiKeyRequest parseFromStreamMessage(StreamMessage& msg) {
  apikeys::CreateApiKeyRequest parsed_request;
  auto* c = dynamic_cast<Protobuf::field_extraction::CordMessageData*>(msg.message());
  parsed_request.ParseFromCord(c->Cord());
  return parsed_request;
}

// Serialize the request message into a pre-existing StreamMessage.
// Serialization overwrites pre-existing date in the buffer.
void serializeToStreamMessage(StreamMessage& msg, apikeys::CreateApiKeyRequest& request) {
  apikeys::CreateApiKeyRequest parsed_request;
  auto* c = dynamic_cast<Protobuf::field_extraction::CordMessageData*>(msg.message());
  request.SerializeToCord(&(c->Cord()));
}
} // namespace GrpcFieldExtraction
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
