#pragma once

#include <map>
#include <utility>

#include "envoy/common/pure.h"
#include "envoy/common/time.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/protobuf/protobuf.h"

#include "absl/strings/string_view.h"
#include "contrib/rocketmq_proxy/filters/network/source/metadata.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RocketmqProxy {

/**
 * Retry topic prefix
 */
constexpr absl::string_view RetryTopicPrefix = "%RETRY%";

/**
 * RocketMQ supports two versions of sending message protocol. These two versions are identical in
 * terms of functionality. But they do differ in encoding scheme. See SendMessageRequestHeader
 * encode/decode functions for specific differences.
 */
enum class SendMessageRequestVersion : uint32_t {
  V1 = 0,
  V2 = 1,
  // Only for test purpose
  V3 = 2,
};

/**
 * Command custom header are used in combination with RemotingCommand::code, to provide further
 * instructions and data for the operation defined by the protocol.
 * In addition to the shared encode/decode functions, this class also defines target-broker-name and
 * target-broker-id fields, which are helpful if the associated remoting command should be delivered
 * to specific host according to the semantics of the previous command.
 */
class CommandCustomHeader {
public:
  CommandCustomHeader() = default;

  virtual ~CommandCustomHeader() = default;

  virtual void encode(ProtobufWkt::Value& root) PURE;

  virtual void decode(const ProtobufWkt::Value& ext_fields) PURE;

  const std::string& targetBrokerName() const { return target_broker_name_; }

  void targetBrokerName(absl::string_view broker_name) {
    target_broker_name_ = std::string(broker_name.data(), broker_name.length());
  }

  int32_t targetBrokerId() const { return target_broker_id_; }

  void targetBrokerId(int32_t broker_id) { target_broker_id_ = broker_id; }

protected:
  /**
   * If this field is not empty, RDS will employ this field and target-broker-id to direct the
   * associated request to a subset of the chosen cluster.
   */
  std::string target_broker_name_;

  /**
   * Used along with target-broker-name field.
   */
  int32_t target_broker_id_;
};

using CommandCustomHeaderPtr = CommandCustomHeader*;

/**
 * This class extends from CommandCustomHeader, adding a commonly used field by various custom
 * command headers which participate the process of request routing.
 */
class RoutingCommandCustomHeader : public CommandCustomHeader {
public:
  virtual const std::string& topic() const { return topic_; }

  virtual void topic(absl::string_view t) { topic_ = std::string(t.data(), t.size()); }

protected:
  std::string topic_;
};

/**
 * This class defines basic request/response forms used by RocketMQ among all its components.
 */
class RemotingCommand {
public:
  RemotingCommand() : RemotingCommand(0, 0, 0) {}

  RemotingCommand(int code, int version, int opaque)
      : code_(code), version_(version), opaque_(opaque), flag_(0) {}

  ~RemotingCommand() { delete custom_header_; }

  int32_t code() const { return code_; }

  void code(int code) { code_ = code; }

  const std::string& language() const { return language_; }

  void language(absl::string_view lang) { language_ = std::string(lang.data(), lang.size()); }

  int32_t version() const { return version_; }

  void opaque(int opaque) { opaque_ = opaque; }

  int32_t opaque() const { return opaque_; }

  uint32_t flag() const { return flag_; }

  void flag(uint32_t f) { flag_ = f; }

  void customHeader(CommandCustomHeaderPtr custom_header) { custom_header_ = custom_header; }

  CommandCustomHeaderPtr customHeader() const { return custom_header_; }

  template <typename T> T* typedCustomHeader() {
    if (!custom_header_) {
      return nullptr;
    }

    return dynamic_cast<T*>(custom_header_);
  }

  uint32_t bodyLength() const { return body_.length(); }

  Buffer::Instance& body() { return body_; }

  const std::string& remark() const { return remark_; }

  void remark(absl::string_view remark) { remark_ = std::string(remark.data(), remark.length()); }

  const std::string& serializeTypeCurrentRPC() const { return serialize_type_current_rpc_; }

  void serializeTypeCurrentRPC(absl::string_view serialization_type) {
    serialize_type_current_rpc_ = std::string(serialization_type.data(), serialization_type.size());
  }

  bool isOneWay() const {
    uint32_t marker = 1u << SHIFT_ONEWAY;
    return (flag_ & marker) == marker;
  }

  void markAsResponse() { flag_ |= (1u << SHIFT_RPC); }

  void markAsOneway() { flag_ |= (1u << SHIFT_ONEWAY); }

  static bool isResponse(uint32_t flag) { return (flag & (1u << SHIFT_RPC)) == (1u << SHIFT_RPC); }

private:
  /**
   * Action code of this command. Possible values are defined in RequestCode enumeration.
   */
  int32_t code_;

  /**
   * Language used by the client.
   */
  std::string language_{"CPP"};

  /**
   * Version of the client SDK.
   */
  int32_t version_;

  /**
   * Request ID. If the RPC is request-response form, this field is used to establish the
   * association.
   */
  int32_t opaque_;

  /**
   * Bit-wise flag indicating RPC type, including whether it is one-way or request-response;
   * a request or response command.
   */
  uint32_t flag_;

  /**
   * Remark is used to deliver text message in addition to code. Urgent scenarios may use this field
   * to transfer diagnostic message to the counterparts when a full-fledged response is impossible.
   */
  std::string remark_;

  /**
   * Indicate how the custom command header is serialized.
   */
  std::string serialize_type_current_rpc_{"JSON"};

  /**
   * The custom command header works with command code to provide additional protocol
   * implementation.
   * Generally speaking, each code has pair of request/response custom command header.
   */
  CommandCustomHeaderPtr custom_header_{nullptr};

  /**
   * The command body, in form of binary.
   */
  Buffer::OwnedImpl body_;

  static constexpr uint32_t SHIFT_RPC = 0;

  static constexpr uint32_t SHIFT_ONEWAY = 1;

  friend class Encoder;
  friend class Decoder;
};

using RemotingCommandPtr = std::unique_ptr<RemotingCommand>;

/**
 * Command codes used when sending requests. Meaning of each field is self-explanatory.
 */
enum class RequestCode : uint32_t {
  SendMessage = 10,
  HeartBeat = 34,
  UnregisterClient = 35,
  GetConsumerListByGroup = 38,
  PopMessage = 50,
  AckMessage = 51,
  GetRouteInfoByTopic = 105,
  SendMessageV2 = 310,
  // Only for test purpose
  Unsupported = 999,
};

/**
 * Command code used when sending responses. Meaning of each enum is self-explanatory.
 */
enum class ResponseCode : uint32_t {
  Success = 0,
  SystemError = 1,
  SystemBusy = 2,
  RequestCodeNotSupported = 3,
  ReplicaNotAvailable = 11,
};

/**
 * Custom command header for sending messages.
 */
class SendMessageRequestHeader : public RoutingCommandCustomHeader,
                                 Logger::Loggable<Logger::Id::rocketmq> {
public:
  ~SendMessageRequestHeader() override = default;

  int32_t queueId() const { return queue_id_; }

  /**
   * TODO(lizhanhui): Remove this write API after adding queue-id-aware route logic
   * @param queue_id target queue Id.
   */
  void queueId(int32_t queue_id) { queue_id_ = queue_id; }

  void producerGroup(std::string producer_group) { producer_group_ = std::move(producer_group); }

  void encode(ProtobufWkt::Value& root) override;

  void decode(const ProtobufWkt::Value& ext_fields) override;

  const std::string& producerGroup() const { return producer_group_; }

  const std::string& defaultTopic() const { return default_topic_; }

  int32_t defaultTopicQueueNumber() const { return default_topic_queue_number_; }

  int32_t sysFlag() const { return sys_flag_; }

  int32_t flag() const { return flag_; }

  int64_t bornTimestamp() const { return born_timestamp_; }

  const std::string& properties() const { return properties_; }

  int32_t reconsumeTimes() const { return reconsume_time_; }

  bool unitMode() const { return unit_mode_; }

  bool batch() const { return batch_; }

  int32_t maxReconsumeTimes() const { return max_reconsume_time_; }

  void properties(absl::string_view props) {
    properties_ = std::string(props.data(), props.size());
  }

  void reconsumeTimes(int32_t reconsume_times) { reconsume_time_ = reconsume_times; }

  void unitMode(bool unit_mode) { unit_mode_ = unit_mode; }

  void batch(bool batch) { batch_ = batch; }

  void maxReconsumeTimes(int32_t max_reconsume_times) { max_reconsume_time_ = max_reconsume_times; }

  void version(SendMessageRequestVersion version) { version_ = version; }

  SendMessageRequestVersion version() const { return version_; }

private:
  std::string producer_group_;
  std::string default_topic_;
  int32_t default_topic_queue_number_{0};
  int32_t queue_id_{-1};
  int32_t sys_flag_{0};
  int64_t born_timestamp_{0};
  int32_t flag_{0};
  std::string properties_;
  int32_t reconsume_time_{0};
  bool unit_mode_{false};
  bool batch_{false};
  int32_t max_reconsume_time_{0};
  SendMessageRequestVersion version_{SendMessageRequestVersion::V1};

  friend class Decoder;
};

/**
 * Custom command header to respond to a send-message-request.
 */
class SendMessageResponseHeader : public CommandCustomHeader {
public:
  SendMessageResponseHeader() = default;

  SendMessageResponseHeader(std::string msg_id, int32_t queue_id, int64_t queue_offset,
                            std::string transaction_id)
      : msg_id_(std::move(msg_id)), queue_id_(queue_id), queue_offset_(queue_offset),
        transaction_id_(std::move(transaction_id)) {}

  void encode(ProtobufWkt::Value& root) override;

  void decode(const ProtobufWkt::Value& ext_fields) override;

  const std::string& msgId() const { return msg_id_; }

  int32_t queueId() const { return queue_id_; }

  int64_t queueOffset() const { return queue_offset_; }

  const std::string& transactionId() const { return transaction_id_; }

  // This function is for testing only.
  void msgIdForTest(absl::string_view msg_id) {
    msg_id_ = std::string(msg_id.data(), msg_id.size());
  }

  void queueId(int32_t queue_id) { queue_id_ = queue_id; }

  void queueOffset(int64_t queue_offset) { queue_offset_ = queue_offset; }

  void transactionId(absl::string_view transaction_id) {
    transaction_id_ = std::string(transaction_id.data(), transaction_id.size());
  }

private:
  std::string msg_id_;
  int32_t queue_id_{0};
  int64_t queue_offset_{0};
  std::string transaction_id_;
};

/**
 * Classic RocketMQ needs to known addresses of each broker to work with. To resolve the addresses,
 * client SDK uses this command header to query name servers.
 *
 * This header is kept for compatible purpose only.
 */
class GetRouteInfoRequestHeader : public RoutingCommandCustomHeader {
public:
  void encode(ProtobufWkt::Value& root) override;

  void decode(const ProtobufWkt::Value& ext_fields) override;
};

/**
 * When a client wishes to consume messages stored in brokers, it sends a pop command to brokers.
 * Brokers would send a batch of messages to the client. At the same time, the broker keeps the
 * batch invisible for a configured period of time, waiting for acknowledgments from the client.
 *
 * If the client manages to consume the messages within promised time interval and sends ack command
 * back to the broker, the broker will mark the acknowledged ones as consumed. Otherwise, the
 * previously sent messages are visible again and would be consumable for other client instances.
 *
 * Through this approach, we achieves stateless message-pulling, comparing to classic offset-based
 * consuming progress management. This models brings about some extra workload to broker side, but
 * it fits Envoy well.
 */
class PopMessageRequestHeader : public RoutingCommandCustomHeader {
public:
  friend class Decoder;

  void encode(ProtobufWkt::Value& root) override;

  void decode(const ProtobufWkt::Value& ext_fields) override;

  const std::string& consumerGroup() const { return consumer_group_; }

  void consumerGroup(absl::string_view consumer_group) {
    consumer_group_ = std::string(consumer_group.data(), consumer_group.size());
  }

  int32_t queueId() const { return queue_id_; }

  void queueId(int32_t queue_id) { queue_id_ = queue_id; }

  int32_t maxMsgNum() const { return max_msg_nums_; }

  void maxMsgNum(int32_t max_msg_num) { max_msg_nums_ = max_msg_num; }

  int64_t invisibleTime() const { return invisible_time_; }

  void invisibleTime(int64_t invisible_time) { invisible_time_ = invisible_time; }

  int64_t pollTime() const { return poll_time_; }

  void pollTime(int64_t poll_time) { poll_time_ = poll_time; }

  int64_t bornTime() const { return born_time_; }

  void bornTime(int64_t born_time) { born_time_ = born_time; }

  int32_t initMode() const { return init_mode_; }

  void initMode(int32_t init_mode) { init_mode_ = init_mode; }

  const std::string& expType() const { return exp_type_; }

  void expType(absl::string_view exp_type) {
    exp_type_ = std::string(exp_type.data(), exp_type.size());
  }

  const std::string& exp() const { return exp_; }

  void exp(absl::string_view exp) { exp_ = std::string(exp.data(), exp.size()); }

private:
  std::string consumer_group_;
  int32_t queue_id_{-1};
  int32_t max_msg_nums_{32};
  int64_t invisible_time_{0};
  int64_t poll_time_{0};
  int64_t born_time_{0};
  int32_t init_mode_{0};
  std::string exp_type_;
  std::string exp_;
  bool order_{false};
};

/**
 * The pop response command header. See pop request header for how-things-work explanation.
 */
class PopMessageResponseHeader : public CommandCustomHeader {
public:
  void decode(const ProtobufWkt::Value& ext_fields) override;

  void encode(ProtobufWkt::Value& root) override;

  // This function is for testing only.
  int64_t popTimeForTest() const { return pop_time_; }

  void popTime(int64_t pop_time) { pop_time_ = pop_time; }

  int64_t invisibleTime() const { return invisible_time_; }

  void invisibleTime(int64_t invisible_time) { invisible_time_ = invisible_time; }

  int32_t reviveQid() const { return revive_qid_; }

  void reviveQid(int32_t revive_qid) { revive_qid_ = revive_qid; }

  int64_t restNum() const { return rest_num_; }

  void restNum(int64_t rest_num) { rest_num_ = rest_num; }

  const std::string& startOffsetInfo() const { return start_offset_info_; }

  void startOffsetInfo(absl::string_view start_offset_info) {
    start_offset_info_ = std::string(start_offset_info.data(), start_offset_info.size());
  }

  const std::string& msgOffsetInfo() const { return msg_off_set_info_; }

  void msgOffsetInfo(absl::string_view msg_offset_info) {
    msg_off_set_info_ = std::string(msg_offset_info.data(), msg_offset_info.size());
  }

  const std::string& orderCountInfo() const { return order_count_info_; }

  void orderCountInfo(absl::string_view order_count_info) {
    order_count_info_ = std::string(order_count_info.data(), order_count_info.size());
  }

private:
  int64_t pop_time_{0};
  int64_t invisible_time_{0};
  int32_t revive_qid_{0};
  int64_t rest_num_{0};
  std::string start_offset_info_;
  std::string msg_off_set_info_;
  std::string order_count_info_;
};

/**
 * This command is used by the client to acknowledge message(s) that has been successfully consumed.
 * Once the broker received this request, the associated message will formally marked as consumed.
 *
 * Note: the ack request has to be sent the exactly same broker where messages are popped from.
 */
class AckMessageRequestHeader : public RoutingCommandCustomHeader {
public:
  void decode(const ProtobufWkt::Value& ext_fields) override;

  void encode(ProtobufWkt::Value& root) override;

  absl::string_view consumerGroup() const { return consumer_group_; }

  int64_t offset() const { return offset_; }

  void consumerGroup(absl::string_view consumer_group) {
    consumer_group_ = std::string(consumer_group.data(), consumer_group.size());
  }

  int32_t queueId() const { return queue_id_; }
  void queueId(int32_t queue_id) { queue_id_ = queue_id; }

  absl::string_view extraInfo() const { return extra_info_; }
  void extraInfo(absl::string_view extra_info) {
    extra_info_ = std::string(extra_info.data(), extra_info.size());
  }

  void offset(int64_t offset) { offset_ = offset; }

  const std::string& directiveKey() {
    if (key_.empty()) {
      key_ = fmt::format("{}-{}-{}-{}", consumer_group_, topic_, queue_id_, offset_);
    }
    return key_;
  }

private:
  std::string consumer_group_;
  int32_t queue_id_{0};
  std::string extra_info_;
  int64_t offset_{0};
  std::string key_;
};

/**
 * When a client shuts down gracefully, it notifies broker(now envoy) this event.
 */
class UnregisterClientRequestHeader : public CommandCustomHeader {
public:
  void encode(ProtobufWkt::Value& root) override;

  void decode(const ProtobufWkt::Value& ext_fields) override;

  void clientId(absl::string_view client_id) {
    client_id_ = std::string(client_id.data(), client_id.length());
  }

  const std::string& clientId() const { return client_id_; }

  void producerGroup(absl::string_view producer_group) {
    producer_group_ = std::string(producer_group.data(), producer_group.length());
  }

  const std::string& producerGroup() const { return producer_group_; }

  void consumerGroup(absl::string_view consumer_group) {
    consumer_group_ = std::string(consumer_group.data(), consumer_group.length());
  }

  const std::string& consumerGroup() const { return consumer_group_; }

private:
  std::string client_id_;
  std::string producer_group_;
  std::string consumer_group_;
};

/**
 * Classic SDK clients use client-side load balancing. This header is kept for compatibility.
 */
class GetConsumerListByGroupRequestHeader : public CommandCustomHeader {
public:
  void encode(ProtobufWkt::Value& root) override;

  void decode(const ProtobufWkt::Value& ext_fields) override;

  void consumerGroup(absl::string_view consumer_group) {
    consumer_group_ = std::string(consumer_group.data(), consumer_group.length());
  }

  const std::string& consumerGroup() const { return consumer_group_; }

private:
  std::string consumer_group_;
};

/**
 * The response body.
 */
class GetConsumerListByGroupResponseBody {
public:
  void encode(ProtobufWkt::Struct& root);

  void add(absl::string_view consumer_id) {
    consumer_id_list_.emplace_back(consumer_id.data(), consumer_id.length());
  }

private:
  std::vector<std::string> consumer_id_list_;
};

/**
 * Client periodically sends heartbeat to servers to maintain alive status.
 */
class HeartbeatData : public Logger::Loggable<Logger::Id::rocketmq> {
public:
  bool decode(ProtobufWkt::Struct& doc);

  const std::string& clientId() const { return client_id_; }

  const std::vector<std::string>& consumerGroups() const { return consumer_groups_; }

  void encode(ProtobufWkt::Struct& root);

  void clientId(absl::string_view client_id) {
    client_id_ = std::string(client_id.data(), client_id.size());
  }

private:
  std::string client_id_;
  std::vector<std::string> consumer_groups_;
};

class MetadataHelper {
public:
  MetadataHelper() = delete;

  static void parseRequest(RemotingCommandPtr& request, MessageMetadataSharedPtr metadata);
};

/**
 * Directive to ensure entailing ack requests are routed to the same broker host where pop
 * requests are made.
 */
struct AckMessageDirective {

  AckMessageDirective(absl::string_view broker_name, int32_t broker_id, MonotonicTime create_time)
      : broker_name_(broker_name), broker_id_(broker_id), creation_time_(create_time) {}

  std::string broker_name_;
  int32_t broker_id_;
  MonotonicTime creation_time_;
};

} // namespace RocketmqProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
