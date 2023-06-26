#pragma once

#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/grpc/codec.h"
#include "source/common/protobuf/stream_message.h"
#include "test/proto/apikeys.pb.h"
#include "src/message_data/message_data.h"
#include "src/message_data/cord_message_data.h"

namespace Envoy::ProtobufMessage {

// Ensures that gRPC data in the Envoy HTTP/2 data streams can be deserialized
// into the expected messages. Drains the underlying buffer.
template <class MessageType>
void CheckSerializedData(Envoy::Buffer::Instance& data,
                         std::vector<MessageType> expected_requests) {
  ::Envoy::Grpc::Decoder grpc_decoder;
  std::vector<::Envoy::Grpc::Frame> frames_after_processing;
  ASSERT_TRUE(grpc_decoder.decode(data, frames_after_processing));

  ASSERT_EQ(expected_requests.size(), frames_after_processing.size());
  for (unsigned  long i = 0; i < frames_after_processing.size(); i++) {
    MessageType request_after_processing;
    ASSERT_TRUE(request_after_processing.ParseFromString(
        frames_after_processing[i].data_->toString()));
  EXPECT_TRUE(google::protobuf::util::MessageDifferencer::Equals(request_after_processing,
                expected_requests[i]));
  }
}

apikeys::CreateApiKeyRequest ParseFromStreamMessage(
    StreamMessage& msg) {
  apikeys::CreateApiKeyRequest parsed_request;
  auto* c = dynamic_cast<google::protobuf::field_extraction::CordMessageData*>(
      msg.message());
  parsed_request.ParseFromCord(c->Cord());
  return parsed_request;
}


// Serialize the request message into a pre-existing ESF StreamMessage.
// Serialization overwrites pre-existing date in the ESF buffer.
void SerializeToStreamMessage(
    StreamMessage& msg,
    apikeys::CreateApiKeyRequest& request) {
  apikeys::CreateApiKeyRequest parsed_request;
  auto* c = dynamic_cast<google::protobuf::field_extraction::CordMessageData*>(
      msg.message());
  request.SerializeToCord(&(c->Cord()));
}
//
//// Parse an RawMessage into the proto message.
//template <class MessageType>
//void ParseFromRawMessage(const RawMessage& raw_message,
//                         MessageType* message_pb) {
//  DataBuffer* raw_buffer = raw_message.contents();
//  DataPosition pos{*raw_buffer};
//  EXPECT_TRUE(::proto2::bridge::ParseFromDataBuffer(&pos, message_pb));
//}
//
//// CheckRawoMessage
//template <class MessageType>
//void CheckRawMessage(const RawMessage& raw_message,
//                     const MessageType& expected_pb) {
//  MessageType out_pb;
//  ParseFromRawMessage(raw_message, &out_pb);
//  EXPECT_THAT(out_pb, ::testing::EqualsProto(expected_pb));
//}

}  //
