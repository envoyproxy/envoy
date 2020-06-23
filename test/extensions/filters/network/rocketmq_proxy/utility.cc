#include "test/extensions/filters/network/rocketmq_proxy/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RocketmqProxy {

const std::string BufferUtility::topic_name_ = "test_topic";
const std::string BufferUtility::client_id_ = "test_client_id";
const std::string BufferUtility::producer_group_ = "test_pg";
const std::string BufferUtility::consumer_group_ = "test_cg";
const std::string BufferUtility::extra_info_ = "test_extra";
const std::string BufferUtility::msg_body_ = "_Apache_RocketMQ_";
const int BufferUtility::queue_id_ = 1;
int BufferUtility::opaque_ = 0;

void BufferUtility::fillRequestBuffer(Buffer::OwnedImpl& buffer, RequestCode code) {

  RemotingCommandPtr cmd = std::make_unique<RemotingCommand>();
  cmd->code(static_cast<int>(code));
  cmd->opaque(++opaque_);

  switch (code) {
  case RequestCode::SendMessage: {
    std::unique_ptr<SendMessageRequestHeader> header = std::make_unique<SendMessageRequestHeader>();
    header->topic(topic_name_);
    header->version(SendMessageRequestVersion::V1);
    std::string msg_body = msg_body_;
    cmd->body().add(msg_body);
    CommandCustomHeaderPtr ptr(header.release());
    cmd->customHeader(ptr);
  } break;

  case RequestCode::HeartBeat: {
    std::string heartbeat_data = R"EOF(
    {
      "clientID": "127.0.0.1@90330",
      "consumerDataSet": [
        {
          "consumeFromWhere": "CONSUME_FROM_FIRST_OFFSET",
          "consumeType": "CONSUME_PASSIVELY",
          "groupName": "test_cg",
          "messageModel": "CLUSTERING",
          "subscriptionDataSet": [
            {
              "classFilterMode": false,
              "codeSet": [],
              "expressionType": "TAG",
              "subString": "*",
              "subVersion": 1575630587925,
              "tagsSet": [],
              "topic": "test_topic"
            },
            {
              "classFilterMode": false,
              "codeSet": [],
              "expressionType": "TAG",
              "subString": "*",
              "subVersion": 1575630587945,
              "tagsSet": [],
              "topic": "%RETRY%please_rename_unique_group_name_4"
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
    cmd->body().add(heartbeat_data);
  } break;

  case RequestCode::UnregisterClient: {
    std::unique_ptr<UnregisterClientRequestHeader> header =
        std::make_unique<UnregisterClientRequestHeader>();
    header->clientId(client_id_);
    header->consumerGroup(consumer_group_);
    CommandCustomHeaderPtr ptr(header.release());
    cmd->customHeader(ptr);
    break;
  }

  case RequestCode::GetRouteInfoByTopic: {
    std::unique_ptr<GetRouteInfoRequestHeader> header =
        std::make_unique<GetRouteInfoRequestHeader>();
    header->topic(topic_name_);
    CommandCustomHeaderPtr ptr(header.release());
    cmd->customHeader(ptr);
    break;
  }

  case RequestCode::GetConsumerListByGroup: {
    std::unique_ptr<GetConsumerListByGroupRequestHeader> header =
        std::make_unique<GetConsumerListByGroupRequestHeader>();
    header->consumerGroup(consumer_group_);
    CommandCustomHeaderPtr ptr(header.release());
    cmd->customHeader(ptr);
    break;
  }

  case RequestCode::SendMessageV2: {
    std::unique_ptr<SendMessageRequestHeader> header = std::make_unique<SendMessageRequestHeader>();
    header->topic(topic_name_);
    header->version(SendMessageRequestVersion::V2);
    header->producerGroup(producer_group_);
    std::string msg_body = msg_body_;
    cmd->body().add(msg_body);
    CommandCustomHeaderPtr ptr(header.release());
    cmd->customHeader(ptr);
    break;
  }

  case RequestCode::PopMessage: {
    std::unique_ptr<PopMessageRequestHeader> header = std::make_unique<PopMessageRequestHeader>();
    header->consumerGroup(consumer_group_);
    header->topic(topic_name_);
    header->queueId(queue_id_);
    header->maxMsgNum(32);
    header->invisibleTime(6000);
    header->pollTime(3000);
    header->bornTime(1000);
    header->initMode(4);

    CommandCustomHeaderPtr ptr(header.release());
    cmd->customHeader(ptr);
    break;
  }

  case RequestCode::AckMessage: {
    std::unique_ptr<AckMessageRequestHeader> header = std::make_unique<AckMessageRequestHeader>();
    header->consumerGroup(consumer_group_);
    header->topic(topic_name_);
    header->queueId(queue_id_);
    header->extraInfo(extra_info_);
    header->offset(1);
    CommandCustomHeaderPtr ptr(header.release());
    cmd->customHeader(ptr);
    break;
  }

  default:
    break;
  }
  Encoder encoder_;
  buffer.drain(buffer.length());
  encoder_.encode(cmd, buffer);
}

void BufferUtility::fillResponseBuffer(Buffer::OwnedImpl& buffer, RequestCode req_code,
                                       ResponseCode resp_code) {
  RemotingCommandPtr cmd = std::make_unique<RemotingCommand>();
  cmd->code(static_cast<int>(resp_code));
  cmd->opaque(opaque_);

  switch (req_code) {
  case RequestCode::SendMessageV2: {
    std::unique_ptr<SendMessageResponseHeader> header =
        std::make_unique<SendMessageResponseHeader>();
    header->msgIdForTest("MSG_ID_01");
    header->queueId(1);
    header->queueOffset(100);
    header->transactionId("TX_01");
    break;
  }
  case RequestCode::PopMessage: {
    std::unique_ptr<PopMessageResponseHeader> header = std::make_unique<PopMessageResponseHeader>();
    header->popTime(1587386521445);
    header->invisibleTime(50000);
    header->reviveQid(5);
    std::string msg_offset_info = "0 6 147";
    header->msgOffsetInfo(msg_offset_info);
    std::string start_offset_info = "0 6 147";
    header->startOffsetInfo(start_offset_info);
    CommandCustomHeaderPtr ptr(header.release());
    cmd->customHeader(ptr);
    cmd->body().add(std::string({'\x00', '\x00', '\x00', '\xD5'}));
    cmd->body().add(std::string({'\xDA', '\xA3', '\x20', '\xA7'}));
    cmd->body().add(std::string({'\x01', '\xE5', '\x9A', '\x3E'}));
    cmd->body().add(std::string({'\x00', '\x00', '\x00', '\x06'}));
    cmd->body().add(std::string({'\x00', '\x00', '\x00', '\x00'}));
    cmd->body().add(std::string({'\x00', '\x00', '\x00', '\x00'}));
    cmd->body().add(std::string({'\x00', '\x00', '\x00', '\x93'}));
    cmd->body().add(std::string({'\x00', '\x00', '\x00', '\x00'}));
    cmd->body().add(std::string({'\x00', '\x4A', '\xE0', '\x46'}));
    cmd->body().add(std::string({'\x00', '\x00', '\x00', '\x00'}));
    cmd->body().add(std::string({'\x00', '\x00', '\x01', '\x71'}));
    cmd->body().add(std::string({'\x97', '\x98', '\x71', '\xB6'}));
    cmd->body().add(std::string({'\x0A', '\x65', '\xC4', '\x91'}));
    cmd->body().add(std::string({'\x00', '\x00', '\x1A', '\xF4'}));
    cmd->body().add(std::string({'\x00', '\x00', '\x01', '\x71'}));
    cmd->body().add(std::string({'\x97', '\x98', '\x71', '\xAF'}));
    cmd->body().add(std::string({'\x0A', '\x65', '\xC1', '\x2D'}));
    cmd->body().add(std::string({'\x00', '\x00', '\x1F', '\x53'}));
    cmd->body().add(std::string({'\x00', '\x00', '\x00', '\x00'}));
    cmd->body().add(std::string({'\x00', '\x00', '\x00', '\x00'}));
    cmd->body().add(std::string({'\x00', '\x00', '\x00', '\x00'}));
    cmd->body().add(std::string({'\x00', '\x00', '\x00', '\x11'}));
    cmd->body().add(std::string("Hello RocketMQ 52"));
    cmd->body().add(std::string({'\x04'}));
    cmd->body().add(std::string("mesh"));
    cmd->body().add(std::string({'\x00', '\x65'}));
    cmd->body().add(std::string("TRACE_ON"));
    cmd->body().add(std::string({'\x01'}));
    cmd->body().add(std::string("true"));
    cmd->body().add(std::string({'\x02'}));
    cmd->body().add(std::string("MSG_REGION"));
    cmd->body().add(std::string({'\x01'}));
    cmd->body().add(std::string("DefaultRegion"));
    cmd->body().add(std::string({'\x02'}));
    cmd->body().add(std::string("UNIQ_KEY"));
    cmd->body().add(std::string({'\x01'}));
    cmd->body().add(std::string("1EE10882893E18B4AAC2664649B60034"));
    cmd->body().add(std::string({'\x02'}));
    cmd->body().add(std::string("WAIT"));
    cmd->body().add(std::string({'\x01'}));
    cmd->body().add(std::string("true"));
    cmd->body().add(std::string({'\x02'}));
    cmd->body().add(std::string("TAGS"));
    cmd->body().add(std::string({'\x01'}));
    cmd->body().add(std::string("TagA"));
    cmd->body().add(std::string({'\x02'}));
    break;
  }
  default:
    break;
  }
  Encoder encoder_;
  buffer.drain(buffer.length());
  encoder_.encode(cmd, buffer);
}
} // namespace RocketmqProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy