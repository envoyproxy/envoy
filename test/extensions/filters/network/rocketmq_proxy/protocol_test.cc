#include "common/protobuf/utility.h"

#include "extensions/filters/network/rocketmq_proxy/protocol.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RocketmqProxy {

class UnregisterClientRequestHeaderTest : public testing::Test {
public:
  std::string client_id_{"SampleClient_01"};
  std::string producer_group_{"PG_Example_01"};
  std::string consumer_group_{"CG_001"};
};

TEST_F(UnregisterClientRequestHeaderTest, Encode) {
  UnregisterClientRequestHeader request_header;
  request_header.clientId(client_id_);
  request_header.producerGroup(producer_group_);
  request_header.consumerGroup(consumer_group_);

  ProtobufWkt::Value doc;
  request_header.encode(doc);

  const auto& members = doc.struct_value().fields();
  EXPECT_STREQ(client_id_.c_str(), members.at("clientID").string_value().c_str());
  EXPECT_STREQ(producer_group_.c_str(), members.at("producerGroup").string_value().c_str());
  EXPECT_STREQ(consumer_group_.c_str(), members.at("consumerGroup").string_value().c_str());
}

TEST_F(UnregisterClientRequestHeaderTest, Decode) {

  std::string json = R"EOF(
  {
    "clientID": "SampleClient_01",
    "producerGroup": "PG_Example_01",
    "consumerGroup": "CG_001"
  }
  )EOF";

  ProtobufWkt::Value doc;
  MessageUtil::loadFromJson(json, *(doc.mutable_struct_value()));
  UnregisterClientRequestHeader unregister_client_request_header;
  unregister_client_request_header.decode(doc);
  EXPECT_STREQ(client_id_.c_str(), unregister_client_request_header.clientId().c_str());
  EXPECT_STREQ(producer_group_.c_str(), unregister_client_request_header.producerGroup().c_str());
  EXPECT_STREQ(consumer_group_.c_str(), unregister_client_request_header.consumerGroup().c_str());
}

TEST(GetConsumerListByGroupResponseBodyTest, Encode) {
  GetConsumerListByGroupResponseBody response_body;
  response_body.add("localhost@1");
  response_body.add("localhost@2");

  ProtobufWkt::Struct doc;
  response_body.encode(doc);

  const auto& members = doc.fields();
  EXPECT_TRUE(members.contains("consumerIdList"));
  EXPECT_EQ(2, members.at("consumerIdList").list_value().values_size());
}

class AckMessageRequestHeaderTest : public testing::Test {
public:
  std::string consumer_group{"CG_Unit_Test"};
  std::string topic{"T_UnitTest"};
  int32_t queue_id{1};
  std::string extra_info{"extra_info_UT"};
  int64_t offset{100};
};

TEST_F(AckMessageRequestHeaderTest, Encode) {
  AckMessageRequestHeader ack_header;
  ack_header.consumerGroup(consumer_group);
  ack_header.topic(topic);
  ack_header.queueId(queue_id);
  ack_header.extraInfo(extra_info);
  ack_header.offset(offset);

  ProtobufWkt::Value doc;
  ack_header.encode(doc);

  const auto& members = doc.struct_value().fields();

  EXPECT_TRUE(members.contains("consumerGroup"));
  EXPECT_STREQ(consumer_group.c_str(), members.at("consumerGroup").string_value().c_str());

  EXPECT_TRUE(members.contains("topic"));
  EXPECT_STREQ(topic.c_str(), members.at("topic").string_value().c_str());

  EXPECT_TRUE(members.contains("queueId"));
  EXPECT_EQ(queue_id, members.at("queueId").number_value());

  EXPECT_TRUE(members.contains("extraInfo"));
  EXPECT_STREQ(extra_info.c_str(), members.at("extraInfo").string_value().c_str());

  EXPECT_TRUE(members.contains("offset"));
  EXPECT_EQ(offset, members.at("offset").number_value());
}

TEST_F(AckMessageRequestHeaderTest, Decode) {
  std::string json = R"EOF(
  {
    "consumerGroup": "CG_Unit_Test",
    "topic": "T_UnitTest",
    "queueId": 1,
    "extraInfo": "extra_info_UT",
    "offset": 100
  }
  )EOF";

  ProtobufWkt::Value doc;
  MessageUtil::loadFromJson(json, *(doc.mutable_struct_value()));

  AckMessageRequestHeader ack_header;
  ack_header.decode(doc);
  ASSERT_STREQ(consumer_group.c_str(), ack_header.consumerGroup().data());
  ASSERT_STREQ(topic.c_str(), ack_header.topic().c_str());
  ASSERT_EQ(queue_id, ack_header.queueId());
  ASSERT_STREQ(extra_info.c_str(), ack_header.extraInfo().data());
  ASSERT_EQ(offset, ack_header.offset());
}

TEST_F(AckMessageRequestHeaderTest, DecodeNumSerializedAsString) {
  std::string json = R"EOF(
  {
    "consumerGroup": "CG_Unit_Test",
    "topic": "T_UnitTest",
    "queueId": "1",
    "extraInfo": "extra_info_UT",
    "offset": "100"
  }
  )EOF";

  ProtobufWkt::Value doc;
  MessageUtil::loadFromJson(json, *(doc.mutable_struct_value()));

  AckMessageRequestHeader ack_header;
  ack_header.decode(doc);
  ASSERT_STREQ(consumer_group.c_str(), ack_header.consumerGroup().data());
  ASSERT_STREQ(topic.c_str(), ack_header.topic().c_str());
  ASSERT_EQ(queue_id, ack_header.queueId());
  ASSERT_STREQ(extra_info.c_str(), ack_header.extraInfo().data());
  ASSERT_EQ(offset, ack_header.offset());
}

class PopMessageRequestHeaderTest : public testing::Test {
public:
  std::string consumer_group{"CG_UT"};
  std::string topic{"T_UT"};
  int32_t queue_id{1};
  int32_t max_msg_nums{2};
  int64_t invisible_time{3};
  int64_t poll_time{4};
  int64_t born_time{5};
  int32_t init_mode{6};

  std::string exp_type{"exp_type_UT"};
  std::string exp{"exp_UT"};
};

TEST_F(PopMessageRequestHeaderTest, Encode) {
  PopMessageRequestHeader pop_request_header;
  pop_request_header.consumerGroup(consumer_group);
  pop_request_header.topic(topic);
  pop_request_header.queueId(queue_id);
  pop_request_header.maxMsgNum(max_msg_nums);
  pop_request_header.invisibleTime(invisible_time);
  pop_request_header.pollTime(poll_time);
  pop_request_header.bornTime(born_time);
  pop_request_header.initMode(init_mode);
  pop_request_header.expType(exp_type);
  pop_request_header.exp(exp);

  ProtobufWkt::Value doc;
  pop_request_header.encode(doc);

  const auto& members = doc.struct_value().fields();

  EXPECT_TRUE(members.contains("consumerGroup"));
  EXPECT_STREQ(consumer_group.c_str(), members.at("consumerGroup").string_value().c_str());

  EXPECT_TRUE(members.contains("topic"));
  EXPECT_STREQ(topic.c_str(), members.at("topic").string_value().c_str());

  EXPECT_TRUE(members.contains("queueId"));
  EXPECT_EQ(queue_id, members.at("queueId").number_value());

  EXPECT_TRUE(members.contains("maxMsgNums"));
  EXPECT_EQ(max_msg_nums, members.at("maxMsgNums").number_value());

  EXPECT_TRUE(members.contains("invisibleTime"));
  EXPECT_EQ(invisible_time, members.at("invisibleTime").number_value());

  EXPECT_TRUE(members.contains("pollTime"));
  EXPECT_EQ(poll_time, members.at("pollTime").number_value());

  EXPECT_TRUE(members.contains("bornTime"));
  EXPECT_EQ(born_time, members.at("bornTime").number_value());

  EXPECT_TRUE(members.contains("initMode"));
  EXPECT_EQ(init_mode, members.at("initMode").number_value());

  EXPECT_TRUE(members.contains("expType"));
  EXPECT_STREQ(exp_type.c_str(), members.at("expType").string_value().c_str());

  EXPECT_TRUE(members.contains("exp"));
  EXPECT_STREQ(exp.c_str(), members.at("exp").string_value().c_str());
}

TEST_F(PopMessageRequestHeaderTest, Decode) {
  std::string json = R"EOF(
  {
    "consumerGroup": "CG_UT",
    "topic": "T_UT",
    "queueId": 1,
    "maxMsgNums": 2,
    "invisibleTime": 3,
    "pollTime": 4,
    "bornTime": 5,
    "initMode": 6,
    "expType": "exp_type_UT",
    "exp": "exp_UT"
  }
  )EOF";

  ProtobufWkt::Value doc;
  MessageUtil::loadFromJson(json, *(doc.mutable_struct_value()));
  PopMessageRequestHeader pop_request_header;
  pop_request_header.decode(doc);

  ASSERT_STREQ(consumer_group.c_str(), pop_request_header.consumerGroup().data());
  ASSERT_STREQ(topic.c_str(), pop_request_header.topic().c_str());
  ASSERT_EQ(queue_id, pop_request_header.queueId());
  ASSERT_EQ(max_msg_nums, pop_request_header.maxMsgNum());
  ASSERT_EQ(invisible_time, pop_request_header.invisibleTime());
  ASSERT_EQ(poll_time, pop_request_header.pollTime());
  ASSERT_EQ(born_time, pop_request_header.bornTime());
  ASSERT_EQ(init_mode, pop_request_header.initMode());
  ASSERT_STREQ(exp_type.c_str(), pop_request_header.expType().c_str());
  ASSERT_STREQ(exp.c_str(), pop_request_header.exp().c_str());
}

TEST_F(PopMessageRequestHeaderTest, DecodeNumSerializedAsString) {
  std::string json = R"EOF(
  {
    "consumerGroup": "CG_UT",
    "topic": "T_UT",
    "queueId": "1",
    "maxMsgNums": "2",
    "invisibleTime": "3",
    "pollTime": "4",
    "bornTime": "5",
    "initMode": "6",
    "expType": "exp_type_UT",
    "exp": "exp_UT"
  }
  )EOF";

  ProtobufWkt::Value doc;
  MessageUtil::loadFromJson(json, *(doc.mutable_struct_value()));
  PopMessageRequestHeader pop_request_header;
  pop_request_header.decode(doc);

  ASSERT_STREQ(consumer_group.c_str(), pop_request_header.consumerGroup().data());
  ASSERT_STREQ(topic.c_str(), pop_request_header.topic().c_str());
  ASSERT_EQ(queue_id, pop_request_header.queueId());
  ASSERT_EQ(max_msg_nums, pop_request_header.maxMsgNum());
  ASSERT_EQ(invisible_time, pop_request_header.invisibleTime());
  ASSERT_EQ(poll_time, pop_request_header.pollTime());
  ASSERT_EQ(born_time, pop_request_header.bornTime());
  ASSERT_EQ(init_mode, pop_request_header.initMode());
  ASSERT_STREQ(exp_type.c_str(), pop_request_header.expType().c_str());
  ASSERT_STREQ(exp.c_str(), pop_request_header.exp().c_str());
}

class PopMessageResponseHeaderTest : public testing::Test {
public:
  int64_t pop_time{1};
  int64_t invisible_time{2};
  int32_t revive_qid{3};
  int64_t rest_num{4};

  std::string start_offset_info{"start"};
  std::string msg_offset_info{"msg"};
  std::string order_count_info{"order"};
};

TEST_F(PopMessageResponseHeaderTest, Encode) {
  PopMessageResponseHeader pop_response_header;
  pop_response_header.popTime(pop_time);
  pop_response_header.invisibleTime(invisible_time);
  pop_response_header.reviveQid(revive_qid);
  pop_response_header.restNum(rest_num);
  pop_response_header.startOffsetInfo(start_offset_info);
  pop_response_header.msgOffsetInfo(msg_offset_info);
  pop_response_header.orderCountInfo(order_count_info);

  ProtobufWkt::Value doc;
  pop_response_header.encode(doc);

  const auto& members = doc.struct_value().fields();

  EXPECT_TRUE(members.contains("popTime"));
  EXPECT_TRUE(members.contains("invisibleTime"));
  EXPECT_TRUE(members.contains("reviveQid"));
  EXPECT_TRUE(members.contains("restNum"));
  EXPECT_TRUE(members.contains("startOffsetInfo"));
  EXPECT_TRUE(members.contains("msgOffsetInfo"));
  EXPECT_TRUE(members.contains("orderCountInfo"));

  EXPECT_EQ(pop_time, members.at("popTime").number_value());
  EXPECT_EQ(invisible_time, members.at("invisibleTime").number_value());
  EXPECT_EQ(revive_qid, members.at("reviveQid").number_value());
  EXPECT_EQ(rest_num, members.at("restNum").number_value());
  EXPECT_STREQ(start_offset_info.c_str(), members.at("startOffsetInfo").string_value().c_str());
  EXPECT_STREQ(msg_offset_info.c_str(), members.at("msgOffsetInfo").string_value().c_str());
  EXPECT_STREQ(order_count_info.c_str(), members.at("orderCountInfo").string_value().c_str());
}

TEST_F(PopMessageResponseHeaderTest, Decode) {
  std::string json = R"EOF(
  {
    "popTime": 1,
    "invisibleTime": 2,
    "reviveQid": 3,
    "restNum": 4,
    "startOffsetInfo": "start",
    "msgOffsetInfo": "msg",
     "orderCountInfo": "order"
  }
  )EOF";

  ProtobufWkt::Value doc;
  MessageUtil::loadFromJson(json, *(doc.mutable_struct_value()));

  PopMessageResponseHeader header;
  header.decode(doc);

  EXPECT_EQ(pop_time, header.popTimeForTest());
  EXPECT_EQ(invisible_time, header.invisibleTime());
  EXPECT_EQ(revive_qid, header.reviveQid());
  EXPECT_EQ(rest_num, header.restNum());

  EXPECT_STREQ(start_offset_info.c_str(), header.startOffsetInfo().data());
  EXPECT_STREQ(msg_offset_info.c_str(), header.msgOffsetInfo().data());
  EXPECT_STREQ(order_count_info.c_str(), header.orderCountInfo().data());
}

TEST_F(PopMessageResponseHeaderTest, DecodeNumSerializedAsString) {
  std::string json = R"EOF(
  {
    "popTime": "1",
    "invisibleTime": "2",
    "reviveQid": "3",
    "restNum": "4",
    "startOffsetInfo": "start",
    "msgOffsetInfo": "msg",
    "orderCountInfo": "order"
  }
  )EOF";

  ProtobufWkt::Value doc;
  MessageUtil::loadFromJson(json, *(doc.mutable_struct_value()));

  PopMessageResponseHeader header;
  header.decode(doc);

  EXPECT_EQ(pop_time, header.popTimeForTest());
  EXPECT_EQ(invisible_time, header.invisibleTime());
  EXPECT_EQ(revive_qid, header.reviveQid());
  EXPECT_EQ(rest_num, header.restNum());

  EXPECT_STREQ(start_offset_info.c_str(), header.startOffsetInfo().data());
  EXPECT_STREQ(msg_offset_info.c_str(), header.msgOffsetInfo().data());
  EXPECT_STREQ(order_count_info.c_str(), header.orderCountInfo().data());
}

class SendMessageResponseHeaderTest : public testing::Test {
public:
  SendMessageResponseHeader response_header_;
};

TEST_F(SendMessageResponseHeaderTest, Encode) {
  response_header_.msgIdForTest("MSG_ID_01");
  response_header_.queueId(1);
  response_header_.queueOffset(100);
  response_header_.transactionId("TX_01");
  ProtobufWkt::Value doc;
  response_header_.encode(doc);

  const auto& members = doc.struct_value().fields();
  EXPECT_TRUE(members.contains("msgId"));
  EXPECT_TRUE(members.contains("queueId"));
  EXPECT_TRUE(members.contains("queueOffset"));
  EXPECT_TRUE(members.contains("transactionId"));

  EXPECT_STREQ("MSG_ID_01", members.at("msgId").string_value().c_str());
  EXPECT_STREQ("TX_01", members.at("transactionId").string_value().c_str());
  EXPECT_EQ(1, members.at("queueId").number_value());
  EXPECT_EQ(100, members.at("queueOffset").number_value());
}

TEST_F(SendMessageResponseHeaderTest, Decode) {
  std::string json = R"EOF(
  {
    "msgId": "abc",
    "queueId": 1,
    "queueOffset": 10,
    "transactionId": "TX_1"
  }
  )EOF";
  ProtobufWkt::Value doc;
  MessageUtil::loadFromJson(json, *(doc.mutable_struct_value()));
  response_header_.decode(doc);
  EXPECT_STREQ("abc", response_header_.msgId().c_str());
  EXPECT_EQ(1, response_header_.queueId());
  EXPECT_EQ(10, response_header_.queueOffset());
  EXPECT_STREQ("TX_1", response_header_.transactionId().c_str());
}

TEST_F(SendMessageResponseHeaderTest, DecodeNumSerializedAsString) {
  std::string json = R"EOF(
  {
    "msgId": "abc",
    "queueId": "1",
    "queueOffset": "10",
    "transactionId": "TX_1"
   }
  )EOF";
  ProtobufWkt::Value doc;
  MessageUtil::loadFromJson(json, *(doc.mutable_struct_value()));
  response_header_.decode(doc);
  EXPECT_STREQ("abc", response_header_.msgId().c_str());
  EXPECT_EQ(1, response_header_.queueId());
  EXPECT_EQ(10, response_header_.queueOffset());
  EXPECT_STREQ("TX_1", response_header_.transactionId().c_str());
}

class SendMessageRequestHeaderTest : public testing::Test {};

TEST_F(SendMessageRequestHeaderTest, EncodeDefault) {
  SendMessageRequestHeader header;
  ProtobufWkt::Value doc;
  header.encode(doc);
  const auto& members = doc.struct_value().fields();
  EXPECT_TRUE(members.contains("producerGroup"));
  EXPECT_TRUE(members.contains("topic"));
  EXPECT_TRUE(members.contains("defaultTopic"));
  EXPECT_TRUE(members.contains("defaultTopicQueueNums"));
  EXPECT_TRUE(members.contains("queueId"));
  EXPECT_TRUE(members.contains("sysFlag"));
  EXPECT_TRUE(members.contains("bornTimestamp"));
  EXPECT_TRUE(members.contains("flag"));
  EXPECT_FALSE(members.contains("properties"));
  EXPECT_FALSE(members.contains("reconsumeTimes"));
  EXPECT_FALSE(members.contains("unitMode"));
  EXPECT_FALSE(members.contains("batch"));
  EXPECT_FALSE(members.contains("maxReconsumeTimes"));
}

TEST_F(SendMessageRequestHeaderTest, EncodeOptional) {
  SendMessageRequestHeader header;
  header.properties("mock");
  header.reconsumeTimes(1);
  header.unitMode(true);
  header.batch(true);
  header.maxReconsumeTimes(32);
  ProtobufWkt::Value doc;
  header.encode(doc);
  const auto& members = doc.struct_value().fields();
  EXPECT_TRUE(members.contains("producerGroup"));
  EXPECT_TRUE(members.contains("topic"));
  EXPECT_TRUE(members.contains("defaultTopic"));
  EXPECT_TRUE(members.contains("defaultTopicQueueNums"));
  EXPECT_TRUE(members.contains("queueId"));
  EXPECT_TRUE(members.contains("sysFlag"));
  EXPECT_TRUE(members.contains("bornTimestamp"));
  EXPECT_TRUE(members.contains("flag"));
  EXPECT_TRUE(members.contains("properties"));
  EXPECT_TRUE(members.contains("reconsumeTimes"));
  EXPECT_TRUE(members.contains("unitMode"));
  EXPECT_TRUE(members.contains("batch"));
  EXPECT_TRUE(members.contains("maxReconsumeTimes"));

  EXPECT_STREQ("mock", members.at("properties").string_value().c_str());
  EXPECT_EQ(1, members.at("reconsumeTimes").number_value());
  EXPECT_TRUE(members.at("unitMode").bool_value());
  EXPECT_TRUE(members.at("batch").bool_value());
  EXPECT_EQ(32, members.at("maxReconsumeTimes").number_value());
}

TEST_F(SendMessageRequestHeaderTest, EncodeDefaultV2) {
  SendMessageRequestHeader header;
  header.version(SendMessageRequestVersion::V2);
  ProtobufWkt::Value doc;
  header.encode(doc);
  const auto& members = doc.struct_value().fields();
  EXPECT_TRUE(members.contains("a"));
  EXPECT_TRUE(members.contains("b"));
  EXPECT_TRUE(members.contains("c"));
  EXPECT_TRUE(members.contains("d"));
  EXPECT_TRUE(members.contains("e"));
  EXPECT_TRUE(members.contains("f"));
  EXPECT_TRUE(members.contains("g"));
  EXPECT_TRUE(members.contains("h"));
  EXPECT_FALSE(members.contains("i"));
  EXPECT_FALSE(members.contains("j"));
  EXPECT_FALSE(members.contains("k"));
  EXPECT_FALSE(members.contains("l"));
  EXPECT_FALSE(members.contains("m"));
}

TEST_F(SendMessageRequestHeaderTest, EncodeOptionalV2) {
  SendMessageRequestHeader header;
  header.properties("mock");
  header.reconsumeTimes(1);
  header.unitMode(true);
  header.batch(true);
  header.maxReconsumeTimes(32);
  header.version(SendMessageRequestVersion::V2);
  ProtobufWkt::Value doc;
  header.encode(doc);

  const auto& members = doc.struct_value().fields();
  EXPECT_TRUE(members.contains("a"));
  EXPECT_TRUE(members.contains("b"));
  EXPECT_TRUE(members.contains("c"));
  EXPECT_TRUE(members.contains("d"));
  EXPECT_TRUE(members.contains("e"));
  EXPECT_TRUE(members.contains("f"));
  EXPECT_TRUE(members.contains("g"));
  EXPECT_TRUE(members.contains("h"));
  EXPECT_TRUE(members.contains("i"));
  EXPECT_TRUE(members.contains("j"));
  EXPECT_TRUE(members.contains("k"));
  EXPECT_TRUE(members.contains("l"));
  EXPECT_TRUE(members.contains("m"));

  EXPECT_STREQ("mock", members.at("i").string_value().c_str());
  EXPECT_EQ(1, members.at("j").number_value());
  EXPECT_TRUE(members.at("k").bool_value());
  EXPECT_TRUE(members.at("m").bool_value());
  EXPECT_EQ(32, members.at("l").number_value());
}

TEST_F(SendMessageRequestHeaderTest, EncodeV3) {
  SendMessageRequestHeader header;
  header.version(SendMessageRequestVersion::V3);
  ProtobufWkt::Value doc;
  header.encode(doc);
}

TEST_F(SendMessageRequestHeaderTest, DecodeV1) {
  std::string json = R"EOF(
  {
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
    "unitMode": false
  }
  )EOF";

  SendMessageRequestHeader header;
  ProtobufWkt::Value doc;
  MessageUtil::loadFromJson(json, *(doc.mutable_struct_value()));
  header.decode(doc);
  EXPECT_STREQ("FooBar", header.topic().c_str());
  EXPECT_EQ(1, header.queueId());
  EXPECT_STREQ("FooBarGroup", header.producerGroup().c_str());
  EXPECT_STREQ("TBW102", header.defaultTopic().c_str());
  EXPECT_EQ(3, header.defaultTopicQueueNumber());
  EXPECT_EQ(0, header.sysFlag());
  EXPECT_EQ(1575872212297, header.bornTimestamp());
  EXPECT_EQ(124, header.flag());
  EXPECT_STREQ("", header.properties().c_str());
  EXPECT_EQ(0, header.reconsumeTimes());
  EXPECT_FALSE(header.unitMode());
  EXPECT_FALSE(header.batch());
  EXPECT_EQ(0, header.maxReconsumeTimes());
}

TEST_F(SendMessageRequestHeaderTest, DecodeV1Optional) {
  std::string json = R"EOF(
  {
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
  )EOF";

  SendMessageRequestHeader header;
  ProtobufWkt::Value doc;
  MessageUtil::loadFromJson(json, *(doc.mutable_struct_value()));
  header.decode(doc);
  EXPECT_STREQ("FooBar", header.topic().c_str());
  EXPECT_EQ(1, header.queueId());
  EXPECT_STREQ("FooBarGroup", header.producerGroup().c_str());
  EXPECT_STREQ("TBW102", header.defaultTopic().c_str());
  EXPECT_EQ(3, header.defaultTopicQueueNumber());
  EXPECT_EQ(0, header.sysFlag());
  EXPECT_EQ(1575872212297, header.bornTimestamp());
  EXPECT_EQ(124, header.flag());
  EXPECT_STREQ("mock_properties", header.properties().c_str());
  EXPECT_EQ(0, header.reconsumeTimes());
  EXPECT_FALSE(header.unitMode());
  EXPECT_FALSE(header.batch());
  EXPECT_EQ(32, header.maxReconsumeTimes());
}

TEST_F(SendMessageRequestHeaderTest, DecodeV1OptionalNumSerializedAsString) {
  std::string json = R"EOF(
  {
    "batch": "false",
    "bornTimestamp": "1575872212297",
    "defaultTopic": "TBW102",
    "defaultTopicQueueNums": "3",
    "flag": "124",
    "producerGroup": "FooBarGroup",
    "queueId": "1",
    "reconsumeTimes": "0",
    "sysFlag": "0",
    "topic": "FooBar",
    "unitMode": "false",
    "properties": "mock_properties",
    "maxReconsumeTimes": "32"
  }
  )EOF";

  SendMessageRequestHeader header;
  ProtobufWkt::Value doc;
  MessageUtil::loadFromJson(json, *(doc.mutable_struct_value()));
  header.decode(doc);
  EXPECT_STREQ("FooBar", header.topic().c_str());
  EXPECT_EQ(1, header.queueId());
  EXPECT_STREQ("FooBarGroup", header.producerGroup().c_str());
  EXPECT_STREQ("TBW102", header.defaultTopic().c_str());
  EXPECT_EQ(3, header.defaultTopicQueueNumber());
  EXPECT_EQ(0, header.sysFlag());
  EXPECT_EQ(1575872212297, header.bornTimestamp());
  EXPECT_EQ(124, header.flag());
  EXPECT_STREQ("mock_properties", header.properties().c_str());
  EXPECT_EQ(0, header.reconsumeTimes());
  EXPECT_FALSE(header.unitMode());
  EXPECT_FALSE(header.batch());
  EXPECT_EQ(32, header.maxReconsumeTimes());
}

TEST_F(SendMessageRequestHeaderTest, DecodeV2) {
  std::string json = R"EOF(
  {
    "a": "FooBarGroup",
    "b": "FooBar",
    "c": "TBW102",
    "d": 3,
    "e": 1,
    "f": 0,
    "g": 1575872563203,
    "h": 124,
    "j": 0,
    "k": false,
    "m": false
  }
  )EOF";

  SendMessageRequestHeader header;
  header.version(SendMessageRequestVersion::V2);
  ProtobufWkt::Value doc;
  MessageUtil::loadFromJson(json, *(doc.mutable_struct_value()));
  header.decode(doc);
  EXPECT_STREQ("FooBar", header.topic().c_str());
  EXPECT_EQ(1, header.queueId());
  EXPECT_STREQ("FooBarGroup", header.producerGroup().c_str());
  EXPECT_STREQ("TBW102", header.defaultTopic().c_str());
  EXPECT_EQ(3, header.defaultTopicQueueNumber());
  EXPECT_EQ(0, header.sysFlag());
  EXPECT_EQ(1575872563203, header.bornTimestamp());
  EXPECT_EQ(124, header.flag());
  EXPECT_STREQ("", header.properties().c_str());
  EXPECT_EQ(0, header.reconsumeTimes());
  EXPECT_FALSE(header.unitMode());
  EXPECT_FALSE(header.batch());
  EXPECT_EQ(0, header.maxReconsumeTimes());
}

TEST_F(SendMessageRequestHeaderTest, DecodeV2Optional) {
  std::string json = R"EOF(
  {
    "a": "FooBarGroup",
    "b": "FooBar",
    "c": "TBW102",
    "d": 3,
    "e": 1,
    "f": 0,
    "g": 1575872563203,
    "h": 124,
    "i": "mock_properties",
    "j": 0,
    "k": false,
    "l": 1,
    "m": false
  }
  )EOF";

  SendMessageRequestHeader header;
  header.version(SendMessageRequestVersion::V2);
  ProtobufWkt::Value doc;
  MessageUtil::loadFromJson(json, *(doc.mutable_struct_value()));
  header.decode(doc);
  EXPECT_STREQ("FooBar", header.topic().c_str());
  EXPECT_EQ(1, header.queueId());
  EXPECT_STREQ("FooBarGroup", header.producerGroup().c_str());
  EXPECT_STREQ("TBW102", header.defaultTopic().c_str());
  EXPECT_EQ(3, header.defaultTopicQueueNumber());
  EXPECT_EQ(0, header.sysFlag());
  EXPECT_EQ(1575872563203, header.bornTimestamp());
  EXPECT_EQ(124, header.flag());
  EXPECT_STREQ("mock_properties", header.properties().c_str());
  EXPECT_EQ(0, header.reconsumeTimes());
  EXPECT_FALSE(header.unitMode());
  EXPECT_FALSE(header.batch());
  EXPECT_EQ(1, header.maxReconsumeTimes());
}

TEST_F(SendMessageRequestHeaderTest, DecodeV2OptionalNumSerializedAsString) {
  std::string json = R"EOF(
  {
    "a": "FooBarGroup",
    "b": "FooBar",
    "c": "TBW102",
    "d": "3",
    "e": "1",
    "f": "0",
    "g": "1575872563203",
    "h": "124",
    "i": "mock_properties",
    "j": "0",
    "k": "false",
    "l": "1",
    "m": "false"
  }
  )EOF";

  SendMessageRequestHeader header;
  header.version(SendMessageRequestVersion::V2);
  ProtobufWkt::Value doc;
  MessageUtil::loadFromJson(json, *(doc.mutable_struct_value()));
  header.decode(doc);
  EXPECT_STREQ("FooBar", header.topic().c_str());
  EXPECT_EQ(1, header.queueId());
  EXPECT_STREQ("FooBarGroup", header.producerGroup().c_str());
  EXPECT_STREQ("TBW102", header.defaultTopic().c_str());
  EXPECT_EQ(3, header.defaultTopicQueueNumber());
  EXPECT_EQ(0, header.sysFlag());
  EXPECT_EQ(1575872563203, header.bornTimestamp());
  EXPECT_EQ(124, header.flag());
  EXPECT_STREQ("mock_properties", header.properties().c_str());
  EXPECT_EQ(0, header.reconsumeTimes());
  EXPECT_FALSE(header.unitMode());
  EXPECT_FALSE(header.batch());
  EXPECT_EQ(1, header.maxReconsumeTimes());
}

TEST_F(SendMessageRequestHeaderTest, DecodeV3) {
  std::string json = R"EOF(
  {
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
    "unitMode": false
  }
  )EOF";

  SendMessageRequestHeader header;
  ProtobufWkt::Value doc;
  MessageUtil::loadFromJson(json, *(doc.mutable_struct_value()));
  header.version(SendMessageRequestVersion::V3);
  header.decode(doc);
}

class HeartbeatDataTest : public testing::Test {
public:
  HeartbeatData data_;
};

TEST_F(HeartbeatDataTest, Decoding) {
  std::string json = R"EOF(
  {
    "clientID": "127.0.0.1@23606",
    "consumerDataSet": [
      {
        "consumeFromWhere": "CONSUME_FROM_LAST_OFFSET",
        "consumeType": "CONSUME_ACTIVELY",
        "groupName": "please_rename_unique_group_name_4",
        "messageModel": "CLUSTERING",
        "subscriptionDataSet": [
          {
            "classFilterMode": false,
            "codeSet": [],
            "expressionType": "TAG",
            "subString": "*",
            "subVersion": 0,
            "tagsSet": [],
            "topic": "test_topic"
          }
        ],
        "unitMode": false
      }
    ],
    "producerDataSet": [
      {
        "groupName": "CLIENT_INNER_PRODUCER"
      }
    ]
  }
  )EOF";

  const char* clientId = "127.0.0.1@23606";
  const char* consumerGroup = "please_rename_unique_group_name_4";

  HeartbeatData heart_beat_data;
  ProtobufWkt::Struct doc;
  MessageUtil::loadFromJson(json, doc);

  heart_beat_data.decode(doc);
  EXPECT_STREQ(clientId, heart_beat_data.clientId().c_str());
  EXPECT_EQ(1, heart_beat_data.consumerGroups().size());
  EXPECT_STREQ(consumerGroup, heart_beat_data.consumerGroups()[0].c_str());
}

TEST_F(HeartbeatDataTest, DecodeClientIdMissing) {
  std::string json = R"EOF(
  {
    "consumerDataSet": [
      {
        "consumeFromWhere": "CONSUME_FROM_LAST_OFFSET",
        "consumeType": "CONSUME_ACTIVELY",
        "groupName": "please_rename_unique_group_name_4",
        "messageModel": "CLUSTERING",
        "subscriptionDataSet": [
          {
            "classFilterMode": false,
            "codeSet": [],
            "expressionType": "TAG",
            "subString": "*",
            "subVersion": 0,
            "tagsSet": [],
            "topic": "test_topic"
          }
        ],
        "unitMode": false
      }
    ],
    "producerDataSet": [
      {
        "groupName": "CLIENT_INNER_PRODUCER"
      }
    ]
  }
  )EOF";

  ProtobufWkt::Struct doc;
  MessageUtil::loadFromJson(json, doc);
  EXPECT_FALSE(data_.decode(doc));
}

TEST_F(HeartbeatDataTest, Encode) {
  data_.clientId("CID_01");
  ProtobufWkt::Struct doc;
  data_.encode(doc);
  const auto& members = doc.fields();
  EXPECT_TRUE(members.contains("clientID"));
  EXPECT_STREQ("CID_01", members.at("clientID").string_value().c_str());
}

class RemotingCommandTest : public testing::Test {
public:
  RemotingCommand cmd_;
};

TEST_F(RemotingCommandTest, FlagResponse) {
  cmd_.markAsResponse();
  EXPECT_EQ(1, cmd_.flag());
}

TEST_F(RemotingCommandTest, FlagOneway) {
  cmd_.markAsOneway();
  EXPECT_EQ(2, cmd_.flag());
}

TEST_F(RemotingCommandTest, Remark) {
  const char* remark = "OK";
  cmd_.remark(remark);
  EXPECT_STREQ(remark, cmd_.remark().c_str());
}

} // namespace RocketmqProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy