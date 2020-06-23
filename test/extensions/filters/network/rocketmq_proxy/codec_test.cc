#include "common/network/address_impl.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/network/rocketmq_proxy/codec.h"

#include "test/extensions/filters/network/rocketmq_proxy/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RocketmqProxy {

class RocketmqCodecTest : public testing::Test {
public:
  RocketmqCodecTest() = default;
  ~RocketmqCodecTest() override = default;
};

TEST_F(RocketmqCodecTest, DecodeWithMinFrameSize) {
  Buffer::OwnedImpl buffer;

  buffer.add(std::string({'\x00', '\x00', '\x01', '\x8b'}));
  buffer.add(std::string({'\x00', '\x00', '\x01', '\x76'}));

  bool underflow = false;
  bool has_error = false;

  auto cmd = Decoder::decode(buffer, underflow, has_error);

  EXPECT_TRUE(underflow);
  EXPECT_FALSE(has_error);
  EXPECT_TRUE(nullptr == cmd);
}

TEST_F(RocketmqCodecTest, DecodeWithOverMaxFrameSizeData) {
  Buffer::OwnedImpl buffer;

  buffer.add(std::string({'\x00', '\x40', '\x00', '\x01'}));
  buffer.add(std::string({'\x00', '\x20', '\x00', '\x00', '\x00'}));

  bool underflow = false;
  bool has_error = false;

  auto cmd = Decoder::decode(buffer, underflow, has_error);

  EXPECT_FALSE(underflow);
  EXPECT_TRUE(has_error);
  EXPECT_TRUE(nullptr == cmd);
}

TEST_F(RocketmqCodecTest, DecodeUnsupportHeaderSerialization) {
  Buffer::OwnedImpl buffer;
  std::string header = "random text suffices";

  buffer.writeBEInt<int32_t>(4 + 4 + header.size());
  uint32_t mark = header.size();
  mark |= (1u << 24u);
  buffer.writeBEInt<uint32_t>(mark);
  buffer.add(header);

  bool underflow = false;
  bool has_error = false;

  auto cmd = Decoder::decode(buffer, underflow, has_error);

  EXPECT_FALSE(underflow);
  EXPECT_TRUE(has_error);
  EXPECT_TRUE(nullptr == cmd);
}

TEST_F(RocketmqCodecTest, DecodeInvalidJson) {
  Buffer::OwnedImpl buffer;
  // Invalid json string.
  std::string invalid_json = R"EOF({a: 3)EOF";

  buffer.writeBEInt<int32_t>(4 + 4 + invalid_json.size());
  buffer.writeBEInt<int32_t>(invalid_json.size());
  buffer.add(invalid_json);

  bool underflow = false;
  bool has_error = false;

  auto cmd = Decoder::decode(buffer, underflow, has_error);

  EXPECT_FALSE(underflow);
  EXPECT_TRUE(has_error);
  EXPECT_TRUE(cmd == nullptr);
}

TEST_F(RocketmqCodecTest, DecodeCodeMissing) {
  Buffer::OwnedImpl buffer;
  // Invalid json string.
  std::string invalid_json = R"EOF({"a": 3})EOF";

  buffer.writeBEInt<int32_t>(4 + 4 + invalid_json.size());
  buffer.writeBEInt<int32_t>(invalid_json.size());
  buffer.add(invalid_json);

  bool underflow = false;
  bool has_error = false;

  auto cmd = Decoder::decode(buffer, underflow, has_error);

  EXPECT_FALSE(underflow);
  EXPECT_TRUE(has_error);
  EXPECT_TRUE(cmd == nullptr);
}

TEST_F(RocketmqCodecTest, DecodeVersionMissing) {
  Buffer::OwnedImpl buffer;
  // Invalid json string.
  std::string invalid_json = R"EOF({"code": 3})EOF";

  buffer.writeBEInt<int32_t>(4 + 4 + invalid_json.size());
  buffer.writeBEInt<int32_t>(invalid_json.size());
  buffer.add(invalid_json);

  bool underflow = false;
  bool has_error = false;

  auto cmd = Decoder::decode(buffer, underflow, has_error);

  EXPECT_FALSE(underflow);
  EXPECT_TRUE(has_error);
  EXPECT_TRUE(cmd == nullptr);
}

TEST_F(RocketmqCodecTest, DecodeOpaqueMissing) {
  Buffer::OwnedImpl buffer;
  // Invalid json string.
  std::string invalid_json = R"EOF(
  {
    "code": 3,
    "version": 1
  }
  )EOF";

  buffer.writeBEInt<int32_t>(4 + 4 + invalid_json.size());
  buffer.writeBEInt<int32_t>(invalid_json.size());
  buffer.add(invalid_json);

  bool underflow = false;
  bool has_error = false;

  auto cmd = Decoder::decode(buffer, underflow, has_error);

  EXPECT_FALSE(underflow);
  EXPECT_TRUE(has_error);
  EXPECT_TRUE(cmd == nullptr);
}

TEST_F(RocketmqCodecTest, DecodeFlagMissing) {
  Buffer::OwnedImpl buffer;
  // Invalid json string.
  std::string invalid_json = R"EOF(
  {
    "code": 3,
    "version": 1,
    "opaque": 1
  }
  )EOF";

  buffer.writeBEInt<int32_t>(4 + 4 + invalid_json.size());
  buffer.writeBEInt<int32_t>(invalid_json.size());
  buffer.add(invalid_json);

  bool underflow = false;
  bool has_error = false;

  auto cmd = Decoder::decode(buffer, underflow, has_error);

  EXPECT_FALSE(underflow);
  EXPECT_TRUE(has_error);
  EXPECT_TRUE(cmd == nullptr);
}

TEST_F(RocketmqCodecTest, DecodeRequestSendMessage) {
  Buffer::OwnedImpl buffer;
  BufferUtility::fillRequestBuffer(buffer, RequestCode::SendMessage);

  bool underflow = false;
  bool has_error = false;

  RemotingCommandPtr request = Decoder::decode(buffer, underflow, has_error);

  EXPECT_FALSE(underflow || has_error);
  EXPECT_EQ(request->opaque(), BufferUtility::opaque_);
  Buffer::Instance& body = request->body();
  EXPECT_EQ(body.toString(), BufferUtility::msg_body_);

  auto header = request->typedCustomHeader<SendMessageRequestHeader>();

  EXPECT_EQ(header->topic(), BufferUtility::topic_name_);
  EXPECT_EQ(header->version(), SendMessageRequestVersion::V1);
  EXPECT_EQ(header->queueId(), -1);
}

TEST_F(RocketmqCodecTest, DecodeRequestSendMessageV2) {
  Buffer::OwnedImpl buffer;

  BufferUtility::fillRequestBuffer(buffer, RequestCode::SendMessageV2);

  bool underflow = false;
  bool has_error = false;

  RemotingCommandPtr request = Decoder::decode(buffer, underflow, has_error);

  EXPECT_FALSE(underflow || has_error);
  EXPECT_EQ(request->opaque(), BufferUtility::opaque_);

  Buffer::Instance& body = request->body();

  EXPECT_EQ(body.toString(), BufferUtility::msg_body_);

  auto header = request->typedCustomHeader<SendMessageRequestHeader>();

  EXPECT_EQ(header->topic(), BufferUtility::topic_name_);
  EXPECT_EQ(header->version(), SendMessageRequestVersion::V2);
  EXPECT_EQ(header->queueId(), -1);
}

TEST_F(RocketmqCodecTest, DecodeRequestSendMessageV1) {
  std::string json = R"EOF(
  {
    "code": 10,
    "version": 1,
    "opaque": 1,
    "flag": 0,
    "extFields": {
      "batch": false,
      "bornTimestamp": 1575872212297,
      "defaultTopic": "TBW102",
      "defaultTopicQueueNums": 3,
      "flag": 124,
      "producerGroup": "FooBarGroup",
      "queueId": 1,
      "reconsumeTimes": 0,
      "sysFlag": 0,
      "topic": "FooBar",
      "unitMode": false,
      "properties": "mock_properties",
      "maxReconsumeTimes": 32
      }
  }
  )EOF";
  Buffer::OwnedImpl buffer;

  buffer.writeBEInt<int32_t>(4 + 4 + json.size());
  buffer.writeBEInt<int32_t>(json.size());
  buffer.add(json);

  bool underflow = false;
  bool has_error = false;

  auto cmd = Decoder::decode(buffer, underflow, has_error);

  EXPECT_FALSE(underflow);
  EXPECT_FALSE(has_error);
  EXPECT_TRUE(nullptr != cmd);
  EXPECT_EQ(10, cmd->code());
  EXPECT_EQ(1, cmd->version());
  EXPECT_EQ(1, cmd->opaque());
}

TEST_F(RocketmqCodecTest, DecodeSendMessageResponseWithSystemError) {
  std::string json = R"EOF(
  {
    "code": 1,
    "language": "JAVA",
    "version": 2,
    "opaque": 1,
    "flag": 1,
    "remark": "System error",
    "serializeTypeCurrentRPC": "JSON"
  }
  )EOF";
  Buffer::OwnedImpl buffer;

  buffer.writeBEInt<int32_t>(4 + 4 + json.size());
  buffer.writeBEInt<int32_t>(json.size());
  buffer.add(json);

  bool underflow = false;
  bool has_error = false;

  auto cmd =
      Decoder::decode(buffer, underflow, has_error, static_cast<int>(RequestCode::SendMessage));

  EXPECT_FALSE(has_error);
  EXPECT_FALSE(underflow);
  EXPECT_TRUE(nullptr != cmd);
  EXPECT_STREQ("JAVA", cmd->language().c_str());
  EXPECT_STREQ("JSON", cmd->serializeTypeCurrentRPC().c_str());
  EXPECT_STREQ("System error", cmd->remark().c_str());
  EXPECT_TRUE(nullptr == cmd->customHeader());
}

TEST_F(RocketmqCodecTest, DecodeSendMessageResponseWithSystemBusy) {
  std::string json = R"EOF(
  {
    "code": 2,
    "language": "JAVA",
    "version": 2,
    "opaque": 1,
    "flag": 1,
    "remark": "System busy",
    "serializeTypeCurrentRPC": "JSON"
  }
  )EOF";
  Buffer::OwnedImpl buffer;

  buffer.writeBEInt<int32_t>(4 + 4 + json.size());
  buffer.writeBEInt<int32_t>(json.size());
  buffer.add(json);

  bool underflow = false;
  bool has_error = false;

  auto cmd =
      Decoder::decode(buffer, underflow, has_error, static_cast<int>(RequestCode::SendMessage));

  EXPECT_FALSE(has_error);
  EXPECT_FALSE(underflow);
  EXPECT_TRUE(nullptr != cmd);
  EXPECT_STREQ("JAVA", cmd->language().c_str());
  EXPECT_STREQ("JSON", cmd->serializeTypeCurrentRPC().c_str());
  EXPECT_STREQ("System busy", cmd->remark().c_str());
  EXPECT_TRUE(nullptr == cmd->customHeader());
}

TEST_F(RocketmqCodecTest, DecodeSendMessageResponseWithCodeNotSupported) {
  std::string json = R"EOF(
  {
    "code": 3,
    "language": "JAVA",
    "version": 2,
    "opaque": 1,
    "flag": 1,
    "remark": "Code not supported",
    "serializeTypeCurrentRPC": "JSON"
  }
  )EOF";
  Buffer::OwnedImpl buffer;

  buffer.writeBEInt<int32_t>(4 + 4 + json.size());
  buffer.writeBEInt<int32_t>(json.size());
  buffer.add(json);

  bool underflow = false;
  bool has_error = false;

  auto cmd =
      Decoder::decode(buffer, underflow, has_error, static_cast<int>(RequestCode::SendMessage));

  EXPECT_FALSE(has_error);
  EXPECT_FALSE(underflow);
  EXPECT_TRUE(nullptr != cmd);
  EXPECT_STREQ("JAVA", cmd->language().c_str());
  EXPECT_STREQ("JSON", cmd->serializeTypeCurrentRPC().c_str());
  EXPECT_STREQ("Code not supported", cmd->remark().c_str());
  EXPECT_TRUE(nullptr == cmd->customHeader());
}

TEST_F(RocketmqCodecTest, DecodeSendMessageResponseNormal) {
  std::string json = R"EOF(
  {
    "code": 0,
    "language": "JAVA",
    "version": 2,
    "opaque": 1,
    "flag": 1,
    "remark": "OK",
    "serializeTypeCurrentRPC": "JSON",
    "extFields": {
      "msgId": "A001",
      "queueId": "10",
      "queueOffset": "2",
      "transactionId": ""
    }
  }
  )EOF";
  Buffer::OwnedImpl buffer;

  buffer.writeBEInt<int32_t>(4 + 4 + json.size());
  buffer.writeBEInt<int32_t>(json.size());
  buffer.add(json);

  bool underflow = false;
  bool has_error = false;

  auto cmd =
      Decoder::decode(buffer, underflow, has_error, static_cast<int>(RequestCode::SendMessage));

  EXPECT_FALSE(has_error);
  EXPECT_FALSE(underflow);
  EXPECT_TRUE(nullptr != cmd);
  EXPECT_STREQ("JAVA", cmd->language().c_str());
  EXPECT_STREQ("JSON", cmd->serializeTypeCurrentRPC().c_str());
  EXPECT_STREQ("OK", cmd->remark().c_str());
  EXPECT_TRUE(nullptr != cmd->customHeader());

  auto extHeader = cmd->typedCustomHeader<SendMessageResponseHeader>();

  EXPECT_STREQ("A001", extHeader->msgId().c_str());
  EXPECT_EQ(10, extHeader->queueId());
  EXPECT_EQ(2, extHeader->queueOffset());
}

TEST_F(RocketmqCodecTest, DecodePopMessageResponseNormal) {
  std::string json = R"EOF(
  {
    "code": 0,
    "language": "JAVA",
    "version": 2,
    "opaque": 1,
    "flag": 1,
    "remark": "OK",
    "serializeTypeCurrentRPC": "JSON",
    "extFields": {
      "popTime": "1234",
      "invisibleTime": "10",
      "reviveQid": "2",
      "restNum": "10",
      "startOffsetInfo": "3",
      "msgOffsetInfo": "mock_msg_offset_info",
      "orderCountInfo": "mock_order_count_info"
    }
  }
  )EOF";
  Buffer::OwnedImpl buffer;

  buffer.writeBEInt<int32_t>(4 + 4 + json.size());
  buffer.writeBEInt<int32_t>(json.size());
  buffer.add(json);

  bool underflow = false;
  bool has_error = false;

  auto cmd =
      Decoder::decode(buffer, underflow, has_error, static_cast<int>(RequestCode::PopMessage));

  EXPECT_FALSE(has_error);
  EXPECT_FALSE(underflow);
  EXPECT_TRUE(nullptr != cmd);
  EXPECT_STREQ("JAVA", cmd->language().c_str());
  EXPECT_STREQ("JSON", cmd->serializeTypeCurrentRPC().c_str());
  EXPECT_STREQ("OK", cmd->remark().c_str());
  EXPECT_TRUE(nullptr != cmd->customHeader());

  auto extHeader = cmd->typedCustomHeader<PopMessageResponseHeader>();

  EXPECT_EQ(1234, extHeader->popTimeForTest());
  EXPECT_EQ(10, extHeader->invisibleTime());
  EXPECT_EQ(2, extHeader->reviveQid());
  EXPECT_EQ(10, extHeader->restNum());
  EXPECT_STREQ("3", extHeader->startOffsetInfo().c_str());
  EXPECT_STREQ("mock_msg_offset_info", extHeader->msgOffsetInfo().c_str());
  EXPECT_STREQ("mock_order_count_info", extHeader->orderCountInfo().c_str());
}

TEST_F(RocketmqCodecTest, DecodeRequestSendMessageV2underflow) {
  Buffer::OwnedImpl buffer;

  buffer.add(std::string({'\x00', '\x00', '\x01', '\x8b'}));
  buffer.add(std::string({'\x00', '\x00', '\x01', '\x76'}));

  std::string header_json = R"EOF(
  {
    "code": 310,
    "extFields": {
      "a": "GID_LINGCHU_TEST_0"
  }
  )EOF";

  buffer.add(header_json);
  buffer.add(std::string{"_Apache_RocketMQ_"});

  bool underflow = false;
  bool has_error = false;

  RemotingCommandPtr request = Decoder::decode(buffer, underflow, has_error);

  EXPECT_EQ(underflow, true);
  EXPECT_EQ(has_error, false);
}

TEST_F(RocketmqCodecTest, EncodeResponseSendMessageSuccess) {
  const int version = 285;
  const int opaque = 4;
  const std::string msg_id = "1E05789ABD1F18B4AAC2895B8BE60003";

  RemotingCommandPtr response =
      std::make_unique<RemotingCommand>(static_cast<int>(ResponseCode::Success), version, opaque);

  response->markAsResponse();

  const int queue_id = 0;
  const int queue_offset = 0;

  std::unique_ptr<SendMessageResponseHeader> sendMessageResponseHeader =
      std::make_unique<SendMessageResponseHeader>(msg_id, queue_id, queue_offset, EMPTY_STRING);
  CommandCustomHeaderPtr extHeader(sendMessageResponseHeader.release());
  response->customHeader(extHeader);

  Buffer::OwnedImpl response_buffer;
  Encoder::encode(response, response_buffer);

  uint32_t frame_length = response_buffer.peekBEInt<uint32_t>();
  uint32_t header_length =
      response_buffer.peekBEInt<uint32_t>(Decoder::FRAME_HEADER_LENGTH_FIELD_SIZE);

  EXPECT_EQ(header_length + Decoder::FRAME_HEADER_LENGTH_FIELD_SIZE, frame_length);

  std::unique_ptr<char[]> header_data = std::make_unique<char[]>(header_length);
  const uint32_t frame_header_content_offset =
      Decoder::FRAME_LENGTH_FIELD_SIZE + Decoder::FRAME_HEADER_LENGTH_FIELD_SIZE;
  response_buffer.copyOut(frame_header_content_offset, header_length, header_data.get());
  std::string header_json(header_data.get(), header_length);
  ProtobufWkt::Struct doc;
  MessageUtil::loadFromJson(header_json, doc);
  const auto& members = doc.fields();

  EXPECT_EQ(members.at("code").number_value(), 0);
  EXPECT_EQ(members.at("version").number_value(), version);
  EXPECT_EQ(members.at("opaque").number_value(), opaque);

  const auto& extFields = members.at("extFields").struct_value().fields();

  EXPECT_EQ(extFields.at("msgId").string_value(), msg_id);
  EXPECT_EQ(extFields.at("queueId").number_value(), queue_id);
  EXPECT_EQ(extFields.at("queueOffset").number_value(), queue_offset);
}

TEST_F(RocketmqCodecTest, DecodeQueueIdWithIncompleteBuffer) {
  Buffer::OwnedImpl buffer;
  // incomplete buffer
  buffer.add(std::string({'\x00'}));

  EXPECT_EQ(Decoder::decodeQueueId(buffer, 0), -1);
}

TEST_F(RocketmqCodecTest, DecodeQueueIdSuccess) {
  Buffer::OwnedImpl buffer;
  // frame length
  buffer.writeBEInt(16);

  for (int i = 0; i < 3; i++) {
    buffer.writeBEInt(i);
  }
  EXPECT_EQ(Decoder::decodeQueueId(buffer, 0), 2);
}

TEST_F(RocketmqCodecTest, DecodeQueueIdFailure) {
  Buffer::OwnedImpl buffer;
  buffer.writeBEInt(128);

  // Some random data, but incomplete frame
  buffer.writeBEInt(12);

  EXPECT_EQ(Decoder::decodeQueueId(buffer, 0), -1);
}

TEST_F(RocketmqCodecTest, DecodeQueueOffsetSuccess) {
  Buffer::OwnedImpl buffer;
  // frame length
  buffer.writeBEInt(28);

  // frame data
  for (int i = 0; i < 4; i++) {
    buffer.writeBEInt(i);
  }
  // write queue offset which takes up 8 bytes
  buffer.writeBEInt<int64_t>(4);

  EXPECT_EQ(Decoder::decodeQueueOffset(buffer, 0), 4);
}

TEST_F(RocketmqCodecTest, DecodeQueueOffsetFailure) {
  Buffer::OwnedImpl buffer;

  // Define length of the frame as 128 bytes
  buffer.writeBEInt(128);

  // some random data, just make sure the frame is incomplete
  for (int i = 0; i < 6; i++) {
    buffer.writeBEInt<int32_t>(i);
  }

  EXPECT_EQ(Decoder::decodeQueueOffset(buffer, 0), -1);
}

TEST_F(RocketmqCodecTest, DecodeMsgIdSuccess) {
  Buffer::OwnedImpl buffer;

  // frame length
  buffer.writeBEInt<int32_t>(64);

  // magic code
  buffer.writeBEInt<int32_t>(0);

  // body CRC
  buffer.writeBEInt<int32_t>(1);

  // queue Id
  buffer.writeBEInt<int32_t>(2);

  // flag
  buffer.writeBEInt<int32_t>(3);

  // queue offset
  buffer.writeBEInt<int64_t>(4);

  // physical offset
  buffer.writeBEInt<int64_t>(5);

  // system flag
  buffer.writeBEInt<int32_t>(6);

  // born timestamp
  buffer.writeBEInt<int64_t>(7);

  // born host
  buffer.writeBEInt<int32_t>(8);

  // born host port
  buffer.writeBEInt<int32_t>(9);

  // store timestamp
  buffer.writeBEInt<int64_t>(10);

  // store host address ip:port --> long
  Network::Address::Ipv4Instance host_address("127.0.0.1", 10911);
  const sockaddr_in* sock_addr = reinterpret_cast<const sockaddr_in*>(host_address.sockAddr());
  buffer.writeBEInt<int32_t>(sock_addr->sin_addr.s_addr);
  buffer.writeBEInt<int32_t>(sock_addr->sin_port);
  EXPECT_EQ(Decoder::decodeMsgId(buffer, 0).empty(), false);
}

TEST_F(RocketmqCodecTest, DecodeMsgIdFailure) {
  Buffer::OwnedImpl buffer;

  // frame length
  buffer.writeBEInt<int32_t>(101);

  // magic code
  buffer.writeBEInt<int32_t>(0);
  EXPECT_EQ(Decoder::decodeMsgId(buffer, 0).empty(), true);
}

TEST_F(RocketmqCodecTest, DecodeTopicSuccessV1) {
  Buffer::OwnedImpl buffer;

  // frame length
  buffer.writeBEInt<int32_t>(98);

  // magic code
  buffer.writeBEInt<int32_t>(enumToSignedInt(MessageVersion::V1));

  // body CRC
  buffer.writeBEInt<int32_t>(1);

  // queue Id
  buffer.writeBEInt<int32_t>(2);

  // flag
  buffer.writeBEInt<int32_t>(3);

  // queue offset
  buffer.writeBEInt<int64_t>(4);

  // physical offset
  buffer.writeBEInt<int64_t>(5);

  // system flag
  buffer.writeBEInt<int32_t>(6);

  // born timestamp
  buffer.writeBEInt<int64_t>(7);

  // born host
  buffer.writeBEInt<int32_t>(8);

  // born host port
  buffer.writeBEInt<int32_t>(9);

  // store timestamp
  buffer.writeBEInt<int64_t>(10);

  // store host address ip:port --> long
  Network::Address::Ipv4Instance host_address("127.0.0.1", 10911);
  const sockaddr_in* sock_addr = reinterpret_cast<const sockaddr_in*>(host_address.sockAddr());
  buffer.writeBEInt<int32_t>(sock_addr->sin_addr.s_addr);
  buffer.writeBEInt<int32_t>(sock_addr->sin_port);

  // re-consume times
  buffer.writeBEInt<int32_t>(11);

  // transaction offset
  buffer.writeBEInt<int64_t>(12);

  // body size
  buffer.writeBEInt<int32_t>(0);

  const std::string topic = "TopicTest";

  // topic length
  buffer.writeBEInt<int8_t>(topic.length());

  // topic data
  buffer.add(topic);

  EXPECT_STREQ(Decoder::decodeTopic(buffer, 0).c_str(), topic.c_str());
}

TEST_F(RocketmqCodecTest, DecodeTopicSuccessV2) {
  Buffer::OwnedImpl buffer;

  // frame length
  buffer.writeBEInt<int32_t>(99);

  // magic code
  buffer.writeBEInt<int32_t>(enumToSignedInt(MessageVersion::V2));

  // body CRC
  buffer.writeBEInt<int32_t>(1);

  // queue Id
  buffer.writeBEInt<int32_t>(2);

  // flag
  buffer.writeBEInt<int32_t>(3);

  // queue offset
  buffer.writeBEInt<int64_t>(4);

  // physical offset
  buffer.writeBEInt<int64_t>(5);

  // system flag
  buffer.writeBEInt<int32_t>(6);

  // born timestamp
  buffer.writeBEInt<int64_t>(7);

  // born host
  buffer.writeBEInt<int32_t>(8);

  // born host port
  buffer.writeBEInt<int32_t>(9);

  // store timestamp
  buffer.writeBEInt<int64_t>(10);

  // store host address ip:port --> long
  Network::Address::Ipv4Instance host_address("127.0.0.1", 10911);
  const sockaddr_in* sock_addr = reinterpret_cast<const sockaddr_in*>(host_address.sockAddr());
  buffer.writeBEInt<int32_t>(sock_addr->sin_addr.s_addr);
  buffer.writeBEInt<int32_t>(sock_addr->sin_port);

  // re-consume times
  buffer.writeBEInt<int32_t>(11);

  // transaction offset
  buffer.writeBEInt<int64_t>(12);

  // body size
  buffer.writeBEInt<int32_t>(0);

  const std::string topic = "TopicTest";

  // topic length
  buffer.writeBEInt<int16_t>(topic.length());

  // topic data
  buffer.add(topic);

  EXPECT_STREQ(Decoder::decodeTopic(buffer, 0).c_str(), topic.c_str());
}

TEST_F(RocketmqCodecTest, DecodeTopicFailure) {
  Buffer::OwnedImpl buffer;

  // frame length
  buffer.writeBEInt<int32_t>(64);

  // magic code
  buffer.writeBEInt<int32_t>(0);
  EXPECT_EQ(Decoder::decodeTopic(buffer, 0).empty(), true);
}

} // namespace RocketmqProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy