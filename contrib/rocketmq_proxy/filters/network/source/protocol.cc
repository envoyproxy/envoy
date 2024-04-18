#include "contrib/rocketmq_proxy/filters/network/source/protocol.h"

#include "source/common/common/assert.h"
#include "source/common/common/enum_to_int.h"

#include "contrib/rocketmq_proxy/filters/network/source/constant.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RocketmqProxy {

void SendMessageRequestHeader::encode(ProtobufWkt::Value& root) {
  auto& members = *(root.mutable_struct_value()->mutable_fields());

  switch (version_) {
  case SendMessageRequestVersion::V1: {
    ProtobufWkt::Value producer_group_v;
    producer_group_v.set_string_value(producer_group_);
    members["producerGroup"] = producer_group_v;

    ProtobufWkt::Value topic_v;
    topic_v.set_string_value(topic_.c_str(), topic_.length());
    members["topic"] = topic_v;

    ProtobufWkt::Value default_topic_v;
    default_topic_v.set_string_value(default_topic_);
    members["defaultTopic"] = default_topic_v;

    ProtobufWkt::Value default_topic_queue_number_v;
    default_topic_queue_number_v.set_number_value(default_topic_queue_number_);
    members["defaultTopicQueueNums"] = default_topic_queue_number_v;

    ProtobufWkt::Value queue_id_v;
    queue_id_v.set_number_value(queue_id_);
    members["queueId"] = queue_id_v;

    ProtobufWkt::Value sys_flag_v;
    sys_flag_v.set_number_value(sys_flag_);
    members["sysFlag"] = sys_flag_v;

    ProtobufWkt::Value born_timestamp_v;
    born_timestamp_v.set_number_value(born_timestamp_);
    members["bornTimestamp"] = born_timestamp_v;

    ProtobufWkt::Value flag_v;
    flag_v.set_number_value(flag_);
    members["flag"] = flag_v;

    if (!properties_.empty()) {
      ProtobufWkt::Value properties_v;
      properties_v.set_string_value(properties_.c_str(), properties_.length());
      members["properties"] = properties_v;
    }

    if (reconsume_time_ > 0) {
      ProtobufWkt::Value reconsume_times_v;
      reconsume_times_v.set_number_value(reconsume_time_);
      members["reconsumeTimes"] = reconsume_times_v;
    }

    if (unit_mode_) {
      ProtobufWkt::Value unit_mode_v;
      unit_mode_v.set_bool_value(unit_mode_);
      members["unitMode"] = unit_mode_v;
    }

    if (batch_) {
      ProtobufWkt::Value batch_v;
      batch_v.set_bool_value(batch_);
      members["batch"] = batch_v;
    }

    if (max_reconsume_time_ > 0) {
      ProtobufWkt::Value max_reconsume_time_v;
      max_reconsume_time_v.set_number_value(max_reconsume_time_);
      members["maxReconsumeTimes"] = max_reconsume_time_v;
    }
    break;
  }
  case SendMessageRequestVersion::V2: {
    ProtobufWkt::Value producer_group_v;
    producer_group_v.set_string_value(producer_group_.c_str(), producer_group_.length());
    members["a"] = producer_group_v;

    ProtobufWkt::Value topic_v;
    topic_v.set_string_value(topic_.c_str(), topic_.length());
    members["b"] = topic_v;

    ProtobufWkt::Value default_topic_v;
    default_topic_v.set_string_value(default_topic_.c_str(), default_topic_.length());
    members["c"] = default_topic_v;

    ProtobufWkt::Value default_topic_queue_number_v;
    default_topic_queue_number_v.set_number_value(default_topic_queue_number_);
    members["d"] = default_topic_queue_number_v;

    ProtobufWkt::Value queue_id_v;
    queue_id_v.set_number_value(queue_id_);
    members["e"] = queue_id_v;

    ProtobufWkt::Value sys_flag_v;
    sys_flag_v.set_number_value(sys_flag_);
    members["f"] = sys_flag_v;

    ProtobufWkt::Value born_timestamp_v;
    born_timestamp_v.set_number_value(born_timestamp_);
    members["g"] = born_timestamp_v;

    ProtobufWkt::Value flag_v;
    flag_v.set_number_value(flag_);
    members["h"] = flag_v;

    if (!properties_.empty()) {
      ProtobufWkt::Value properties_v;
      properties_v.set_string_value(properties_.c_str(), properties_.length());
      members["i"] = properties_v;
    }

    if (reconsume_time_ > 0) {
      ProtobufWkt::Value reconsume_times_v;
      reconsume_times_v.set_number_value(reconsume_time_);
      members["j"] = reconsume_times_v;
    }

    if (unit_mode_) {
      ProtobufWkt::Value unit_mode_v;
      unit_mode_v.set_bool_value(unit_mode_);
      members["k"] = unit_mode_v;
    }

    if (batch_) {
      ProtobufWkt::Value batch_v;
      batch_v.set_bool_value(batch_);
      members["m"] = batch_v;
    }

    if (max_reconsume_time_ > 0) {
      ProtobufWkt::Value max_reconsume_time_v;
      max_reconsume_time_v.set_number_value(max_reconsume_time_);
      members["l"] = max_reconsume_time_v;
    }
    break;
  }
  default:
    break;
  }
}

void SendMessageRequestHeader::decode(const ProtobufWkt::Value& ext_fields) {
  const auto& members = ext_fields.struct_value().fields();
  switch (version_) {
  case SendMessageRequestVersion::V1: {
    ASSERT(members.contains("producerGroup"));
    ASSERT(members.contains("topic"));
    ASSERT(members.contains("defaultTopic"));
    ASSERT(members.contains("defaultTopicQueueNums"));
    ASSERT(members.contains("queueId"));
    ASSERT(members.contains("sysFlag"));
    ASSERT(members.contains("bornTimestamp"));
    ASSERT(members.contains("flag"));

    producer_group_ = members.at("producerGroup").string_value();
    topic_ = members.at("topic").string_value();
    default_topic_ = members.at("defaultTopic").string_value();

    if (members.at("defaultTopicQueueNums").kind_case() == ProtobufWkt::Value::kNumberValue) {
      default_topic_queue_number_ = members.at("defaultTopicQueueNums").number_value();
    } else {
      default_topic_queue_number_ = std::stoi(members.at("defaultTopicQueueNums").string_value());
    }

    if (members.at("queueId").kind_case() == ProtobufWkt::Value::kNumberValue) {
      queue_id_ = members.at("queueId").number_value();
    } else {
      queue_id_ = std::stoi(members.at("queueId").string_value());
    }

    if (members.at("sysFlag").kind_case() == ProtobufWkt::Value::kNumberValue) {
      sys_flag_ = static_cast<int32_t>(members.at("sysFlag").number_value());
    } else {
      sys_flag_ = std::stoi(members.at("sysFlag").string_value());
    }

    if (members.at("bornTimestamp").kind_case() == ProtobufWkt::Value::kNumberValue) {
      born_timestamp_ = static_cast<int64_t>(members.at("bornTimestamp").number_value());
    } else {
      born_timestamp_ = std::stoll(members.at("bornTimestamp").string_value());
    }

    if (members.at("flag").kind_case() == ProtobufWkt::Value::kNumberValue) {
      flag_ = static_cast<int32_t>(members.at("flag").number_value());
    } else {
      flag_ = std::stoi(members.at("flag").string_value());
    }

    if (members.contains("properties")) {
      properties_ = members.at("properties").string_value();
    }

    if (members.contains("reconsumeTimes")) {
      if (members.at("reconsumeTimes").kind_case() == ProtobufWkt::Value::kNumberValue) {
        reconsume_time_ = members.at("reconsumeTimes").number_value();
      } else {
        reconsume_time_ = std::stoi(members.at("reconsumeTimes").string_value());
      }
    }

    if (members.contains("unitMode")) {
      if (members.at("unitMode").kind_case() == ProtobufWkt::Value::kBoolValue) {
        unit_mode_ = members.at("unitMode").bool_value();
      } else {
        unit_mode_ = (members.at("unitMode").string_value() == std::string("true"));
      }
    }

    if (members.contains("batch")) {
      if (members.at("batch").kind_case() == ProtobufWkt::Value::kBoolValue) {
        batch_ = members.at("batch").bool_value();
      } else {
        batch_ = (members.at("batch").string_value() == std::string("true"));
      }
    }

    if (members.contains("maxReconsumeTimes")) {
      if (members.at("maxReconsumeTimes").kind_case() == ProtobufWkt::Value::kNumberValue) {
        max_reconsume_time_ = static_cast<int32_t>(members.at("maxReconsumeTimes").number_value());
      } else {
        max_reconsume_time_ = std::stoi(members.at("maxReconsumeTimes").string_value());
      }
    }
    break;
  }

  case SendMessageRequestVersion::V2: {
    ASSERT(members.contains("a"));
    ASSERT(members.contains("b"));
    ASSERT(members.contains("c"));
    ASSERT(members.contains("d"));
    ASSERT(members.contains("e"));
    ASSERT(members.contains("f"));
    ASSERT(members.contains("g"));
    ASSERT(members.contains("h"));

    producer_group_ = members.at("a").string_value();
    topic_ = members.at("b").string_value();
    default_topic_ = members.at("c").string_value();

    if (members.at("d").kind_case() == ProtobufWkt::Value::kNumberValue) {
      default_topic_queue_number_ = members.at("d").number_value();
    } else {
      default_topic_queue_number_ = std::stoi(members.at("d").string_value());
    }

    if (members.at("e").kind_case() == ProtobufWkt::Value::kNumberValue) {
      queue_id_ = members.at("e").number_value();
    } else {
      queue_id_ = std::stoi(members.at("e").string_value());
    }

    if (members.at("f").kind_case() == ProtobufWkt::Value::kNumberValue) {
      sys_flag_ = static_cast<int32_t>(members.at("f").number_value());
    } else {
      sys_flag_ = std::stoi(members.at("f").string_value());
    }

    if (members.at("g").kind_case() == ProtobufWkt::Value::kNumberValue) {
      born_timestamp_ = static_cast<int64_t>(members.at("g").number_value());
    } else {
      born_timestamp_ = std::stoll(members.at("g").string_value());
    }

    if (members.at("h").kind_case() == ProtobufWkt::Value::kNumberValue) {
      flag_ = static_cast<int32_t>(members.at("h").number_value());
    } else {
      flag_ = std::stoi(members.at("h").string_value());
    }

    if (members.contains("i")) {
      properties_ = members.at("i").string_value();
    }

    if (members.contains("j")) {
      if (members.at("j").kind_case() == ProtobufWkt::Value::kNumberValue) {
        reconsume_time_ = members.at("j").number_value();
      } else {
        reconsume_time_ = std::stoi(members.at("j").string_value());
      }
    }

    if (members.contains("k")) {
      if (members.at("k").kind_case() == ProtobufWkt::Value::kBoolValue) {
        unit_mode_ = members.at("k").bool_value();
      } else {
        unit_mode_ = (members.at("k").string_value() == std::string("true"));
      }
    }

    if (members.contains("m")) {
      if (members.at("m").kind_case() == ProtobufWkt::Value::kBoolValue) {
        batch_ = members.at("m").bool_value();
      } else {
        batch_ = (members.at("m").string_value() == std::string("true"));
      }
    }

    if (members.contains("l")) {
      if (members.at("l").kind_case() == ProtobufWkt::Value::kNumberValue) {
        max_reconsume_time_ = members.at("l").number_value();
      } else {
        max_reconsume_time_ = std::stoi(members.at("l").string_value());
      }
    }
    break;
  }
  default:
    ENVOY_LOG(error, "Unknown SendMessageRequestVersion: {}", static_cast<int>(version_));
    break;
  }
}

void SendMessageResponseHeader::encode(ProtobufWkt::Value& root) {
  auto& members = *(root.mutable_struct_value()->mutable_fields());

  ASSERT(!msg_id_.empty());
  ProtobufWkt::Value msg_id_v;
  msg_id_v.set_string_value(msg_id_.c_str(), msg_id_.length());
  members["msgId"] = msg_id_v;

  ASSERT(queue_id_ >= 0);
  ProtobufWkt::Value queue_id_v;
  queue_id_v.set_number_value(queue_id_);
  members["queueId"] = queue_id_v;

  ASSERT(queue_offset_ >= 0);
  ProtobufWkt::Value queue_offset_v;
  queue_offset_v.set_number_value(queue_offset_);
  members["queueOffset"] = queue_offset_v;

  if (!transaction_id_.empty()) {
    ProtobufWkt::Value transaction_id_v;
    transaction_id_v.set_string_value(transaction_id_.c_str(), transaction_id_.length());
    members["transactionId"] = transaction_id_v;
  }
}

void SendMessageResponseHeader::decode(const ProtobufWkt::Value& ext_fields) {
  const auto& members = ext_fields.struct_value().fields();
  ASSERT(members.contains("msgId"));
  ASSERT(members.contains("queueId"));
  ASSERT(members.contains("queueOffset"));

  msg_id_ = members.at("msgId").string_value();

  if (members.at("queueId").kind_case() == ProtobufWkt::Value::kNumberValue) {
    queue_id_ = members.at("queueId").number_value();
  } else {
    queue_id_ = std::stoi(members.at("queueId").string_value());
  }

  if (members.at("queueOffset").kind_case() == ProtobufWkt::Value::kNumberValue) {
    queue_offset_ = members.at("queueOffset").number_value();
  } else {
    queue_offset_ = std::stoll(members.at("queueOffset").string_value());
  }

  if (members.contains("transactionId")) {
    transaction_id_ = members.at("transactionId").string_value();
  }
}

void GetRouteInfoRequestHeader::encode(ProtobufWkt::Value& root) {
  auto& members = *(root.mutable_struct_value()->mutable_fields());

  ProtobufWkt::Value topic_v;
  topic_v.set_string_value(topic_.c_str(), topic_.length());
  members["topic"] = topic_v;
}

void GetRouteInfoRequestHeader::decode(const ProtobufWkt::Value& ext_fields) {
  const auto& members = ext_fields.struct_value().fields();
  ASSERT(members.contains("topic"));
  topic_ = members.at("topic").string_value();
}

void PopMessageRequestHeader::encode(ProtobufWkt::Value& root) {
  auto& members = *(root.mutable_struct_value()->mutable_fields());

  ASSERT(!consumer_group_.empty());
  ProtobufWkt::Value consumer_group_v;
  consumer_group_v.set_string_value(consumer_group_.c_str(), consumer_group_.size());
  members["consumerGroup"] = consumer_group_v;

  ASSERT(!topic_.empty());
  ProtobufWkt::Value topicNode;
  topicNode.set_string_value(topic_.c_str(), topic_.length());
  members["topic"] = topicNode;

  ProtobufWkt::Value queue_id_v;
  queue_id_v.set_number_value(queue_id_);
  members["queueId"] = queue_id_v;

  ProtobufWkt::Value max_msg_nums_v;
  max_msg_nums_v.set_number_value(max_msg_nums_);
  members["maxMsgNums"] = max_msg_nums_v;

  ProtobufWkt::Value invisible_time_v;
  invisible_time_v.set_number_value(invisible_time_);
  members["invisibleTime"] = invisible_time_v;

  ProtobufWkt::Value poll_time_v;
  poll_time_v.set_number_value(poll_time_);
  members["pollTime"] = poll_time_v;

  ProtobufWkt::Value born_time_v;
  born_time_v.set_number_value(born_time_);
  members["bornTime"] = born_time_v;

  ProtobufWkt::Value init_mode_v;
  init_mode_v.set_number_value(init_mode_);
  members["initMode"] = init_mode_v;

  if (!exp_type_.empty()) {
    ProtobufWkt::Value exp_type_v;
    exp_type_v.set_string_value(exp_type_.c_str(), exp_type_.size());
    members["expType"] = exp_type_v;
  }

  if (!exp_.empty()) {
    ProtobufWkt::Value exp_v;
    exp_v.set_string_value(exp_.c_str(), exp_.size());
    members["exp"] = exp_v;
  }
}

void PopMessageRequestHeader::decode(const ProtobufWkt::Value& ext_fields) {
  const auto& members = ext_fields.struct_value().fields();
  ASSERT(members.contains("consumerGroup"));
  ASSERT(members.contains("topic"));
  ASSERT(members.contains("queueId"));
  ASSERT(members.contains("maxMsgNums"));
  ASSERT(members.contains("invisibleTime"));
  ASSERT(members.contains("pollTime"));
  ASSERT(members.contains("bornTime"));
  ASSERT(members.contains("initMode"));

  consumer_group_ = members.at("consumerGroup").string_value();
  topic_ = members.at("topic").string_value();

  if (members.at("queueId").kind_case() == ProtobufWkt::Value::kNumberValue) {
    queue_id_ = members.at("queueId").number_value();
  } else {
    queue_id_ = std::stoi(members.at("queueId").string_value());
  }

  if (members.at("maxMsgNums").kind_case() == ProtobufWkt::Value::kNumberValue) {
    max_msg_nums_ = members.at("maxMsgNums").number_value();
  } else {
    max_msg_nums_ = std::stoi(members.at("maxMsgNums").string_value());
  }

  if (members.at("invisibleTime").kind_case() == ProtobufWkt::Value::kNumberValue) {
    invisible_time_ = members.at("invisibleTime").number_value();
  } else {
    invisible_time_ = std::stoll(members.at("invisibleTime").string_value());
  }

  if (members.at("pollTime").kind_case() == ProtobufWkt::Value::kNumberValue) {
    poll_time_ = members.at("pollTime").number_value();
  } else {
    poll_time_ = std::stoll(members.at("pollTime").string_value());
  }

  if (members.at("bornTime").kind_case() == ProtobufWkt::Value::kNumberValue) {
    born_time_ = members.at("bornTime").number_value();
  } else {
    born_time_ = std::stoll(members.at("bornTime").string_value());
  }

  if (members.at("initMode").kind_case() == ProtobufWkt::Value::kNumberValue) {
    init_mode_ = members.at("initMode").number_value();
  } else {
    init_mode_ = std::stol(members.at("initMode").string_value());
  }

  if (members.contains("expType")) {
    exp_type_ = members.at("expType").string_value();
  }

  if (members.contains("exp")) {
    exp_ = members.at("exp").string_value();
  }
}

void PopMessageResponseHeader::encode(ProtobufWkt::Value& root) {
  auto& members = *(root.mutable_struct_value()->mutable_fields());

  ProtobufWkt::Value pop_time_v;
  pop_time_v.set_number_value(pop_time_);
  members["popTime"] = pop_time_v;

  ProtobufWkt::Value invisible_time_v;
  invisible_time_v.set_number_value(invisible_time_);
  members["invisibleTime"] = invisible_time_v;

  ProtobufWkt::Value revive_qid_v;
  revive_qid_v.set_number_value(revive_qid_);
  members["reviveQid"] = revive_qid_v;

  ProtobufWkt::Value rest_num_v;
  rest_num_v.set_number_value(rest_num_);
  members["restNum"] = rest_num_v;

  if (!start_offset_info_.empty()) {
    ProtobufWkt::Value start_offset_info_v;
    start_offset_info_v.set_string_value(start_offset_info_.c_str(), start_offset_info_.size());
    members["startOffsetInfo"] = start_offset_info_v;
  }

  if (!msg_off_set_info_.empty()) {
    ProtobufWkt::Value msg_offset_info_v;
    msg_offset_info_v.set_string_value(msg_off_set_info_.c_str(), msg_off_set_info_.size());
    members["msgOffsetInfo"] = msg_offset_info_v;
  }

  if (!order_count_info_.empty()) {
    ProtobufWkt::Value order_count_info_v;
    order_count_info_v.set_string_value(order_count_info_.c_str(), order_count_info_.size());
    members["orderCountInfo"] = order_count_info_v;
  }
}

void PopMessageResponseHeader::decode(const ProtobufWkt::Value& ext_fields) {
  const auto& members = ext_fields.struct_value().fields();
  ASSERT(members.contains("popTime"));
  ASSERT(members.contains("invisibleTime"));
  ASSERT(members.contains("reviveQid"));
  ASSERT(members.contains("restNum"));

  if (members.at("popTime").kind_case() == ProtobufWkt::Value::kNumberValue) {
    pop_time_ = members.at("popTime").number_value();
  } else {
    pop_time_ = std::stoull(members.at("popTime").string_value());
  }

  if (members.at("invisibleTime").kind_case() == ProtobufWkt::Value::kNumberValue) {
    invisible_time_ = members.at("invisibleTime").number_value();
  } else {
    invisible_time_ = std::stoull(members.at("invisibleTime").string_value());
  }

  if (members.at("reviveQid").kind_case() == ProtobufWkt::Value::kNumberValue) {
    revive_qid_ = members.at("reviveQid").number_value();
  } else {
    revive_qid_ = std::stoul(members.at("reviveQid").string_value());
  }

  if (members.at("restNum").kind_case() == ProtobufWkt::Value::kNumberValue) {
    rest_num_ = members.at("restNum").number_value();
  } else {
    rest_num_ = std::stoull(members.at("restNum").string_value());
  }

  if (members.contains("startOffsetInfo")) {
    start_offset_info_ = members.at("startOffsetInfo").string_value();
  }

  if (members.contains("msgOffsetInfo")) {
    msg_off_set_info_ = members.at("msgOffsetInfo").string_value();
  }

  if (members.contains("orderCountInfo")) {
    order_count_info_ = members.at("orderCountInfo").string_value();
  }
}

void AckMessageRequestHeader::encode(ProtobufWkt::Value& root) {
  auto& members = *(root.mutable_struct_value()->mutable_fields());

  ASSERT(!consumer_group_.empty());
  ProtobufWkt::Value consumer_group_v;
  consumer_group_v.set_string_value(consumer_group_.c_str(), consumer_group_.size());
  members["consumerGroup"] = consumer_group_v;

  ASSERT(!topic_.empty());
  ProtobufWkt::Value topic_v;
  topic_v.set_string_value(topic_.c_str(), topic_.size());
  members["topic"] = topic_v;

  ASSERT(queue_id_ >= 0);
  ProtobufWkt::Value queue_id_v;
  queue_id_v.set_number_value(queue_id_);
  members["queueId"] = queue_id_v;

  ASSERT(!extra_info_.empty());
  ProtobufWkt::Value extra_info_v;
  extra_info_v.set_string_value(extra_info_.c_str(), extra_info_.size());
  members["extraInfo"] = extra_info_v;

  ASSERT(offset_ >= 0);
  ProtobufWkt::Value offset_v;
  offset_v.set_number_value(offset_);
  members["offset"] = offset_v;
}

void AckMessageRequestHeader::decode(const ProtobufWkt::Value& ext_fields) {
  const auto& members = ext_fields.struct_value().fields();
  ASSERT(members.contains("consumerGroup"));
  ASSERT(members.contains("topic"));
  ASSERT(members.contains("queueId"));
  ASSERT(members.contains("extraInfo"));
  ASSERT(members.contains("offset"));

  consumer_group_ = members.at("consumerGroup").string_value();

  topic_ = members.at("topic").string_value();

  if (members.at("queueId").kind_case() == ProtobufWkt::Value::kNumberValue) {
    queue_id_ = members.at("queueId").number_value();
  } else {
    queue_id_ = std::stoi(members.at("queueId").string_value());
  }

  extra_info_ = members.at("extraInfo").string_value();

  if (members.at("offset").kind_case() == ProtobufWkt::Value::kNumberValue) {
    offset_ = members.at("offset").number_value();
  } else {
    offset_ = std::stoll(members.at("offset").string_value());
  }
}

void UnregisterClientRequestHeader::encode(ProtobufWkt::Value& root) {
  auto& members = *(root.mutable_struct_value()->mutable_fields());

  ASSERT(!client_id_.empty());
  ProtobufWkt::Value client_id_v;
  client_id_v.set_string_value(client_id_.c_str(), client_id_.size());
  members["clientID"] = client_id_v;

  ASSERT(!producer_group_.empty() || !consumer_group_.empty());
  if (!producer_group_.empty()) {
    ProtobufWkt::Value producer_group_v;
    producer_group_v.set_string_value(producer_group_.c_str(), producer_group_.size());
    members["producerGroup"] = producer_group_v;
  }

  if (!consumer_group_.empty()) {
    ProtobufWkt::Value consumer_group_v;
    consumer_group_v.set_string_value(consumer_group_.c_str(), consumer_group_.size());
    members["consumerGroup"] = consumer_group_v;
  }
}

void UnregisterClientRequestHeader::decode(const ProtobufWkt::Value& ext_fields) {
  const auto& members = ext_fields.struct_value().fields();
  ASSERT(members.contains("clientID"));
  ASSERT(members.contains("producerGroup") || members.contains("consumerGroup"));

  client_id_ = members.at("clientID").string_value();

  if (members.contains("consumerGroup")) {
    consumer_group_ = members.at("consumerGroup").string_value();
  }

  if (members.contains("producerGroup")) {
    producer_group_ = members.at("producerGroup").string_value();
  }
}

void GetConsumerListByGroupResponseBody::encode(ProtobufWkt::Struct& root) {
  auto& members = *(root.mutable_fields());

  ProtobufWkt::Value consumer_id_list_v;
  auto member_list = consumer_id_list_v.mutable_list_value();
  for (const auto& consumerId : consumer_id_list_) {
    auto consumer_id_v = new ProtobufWkt::Value;
    consumer_id_v->set_string_value(consumerId.c_str(), consumerId.size());
    member_list->mutable_values()->AddAllocated(consumer_id_v);
  }
  members["consumerIdList"] = consumer_id_list_v;
}

bool HeartbeatData::decode(ProtobufWkt::Struct& doc) {
  const auto& members = doc.fields();
  if (!members.contains("clientID")) {
    return false;
  }

  client_id_ = members.at("clientID").string_value();

  if (members.contains("consumerDataSet")) {
    auto& consumer_data_list = members.at("consumerDataSet").list_value().values();
    for (const auto& it : consumer_data_list) {
      if (it.struct_value().fields().contains("groupName")) {
        consumer_groups_.push_back(it.struct_value().fields().at("groupName").string_value());
      }
    }
  }
  return true;
}

void HeartbeatData::encode(ProtobufWkt::Struct& root) {
  auto& members = *(root.mutable_fields());

  ProtobufWkt::Value client_id_v;
  client_id_v.set_string_value(client_id_.c_str(), client_id_.size());
  members["clientID"] = client_id_v;
}

void GetConsumerListByGroupRequestHeader::encode(ProtobufWkt::Value& root) {
  auto& members = *(root.mutable_struct_value()->mutable_fields());

  ProtobufWkt::Value consumer_group_v;
  consumer_group_v.set_string_value(consumer_group_.c_str(), consumer_group_.size());
  members["consumerGroup"] = consumer_group_v;
}

void GetConsumerListByGroupRequestHeader::decode(const ProtobufWkt::Value& ext_fields) {
  const auto& members = ext_fields.struct_value().fields();
  ASSERT(members.contains("consumerGroup"));

  consumer_group_ = members.at("consumerGroup").string_value();
}

void MetadataHelper::parseRequest(RemotingCommandPtr& request, MessageMetadataSharedPtr metadata) {
  metadata->setOneWay(request->isOneWay());
  CommandCustomHeader* custom_header = request->customHeader();

  auto route_command_custom_header = request->typedCustomHeader<RoutingCommandCustomHeader>();
  if (route_command_custom_header != nullptr) {
    metadata->setTopicName(route_command_custom_header->topic());
  }

  const uint64_t code = request->code();
  metadata->headers().addCopy(Http::LowerCaseString("code"), code);

  if (enumToInt(RequestCode::AckMessage) == code) {
    metadata->headers().addCopy(Http::LowerCaseString(RocketmqConstants::get().BrokerName),
                                custom_header->targetBrokerName());
    metadata->headers().addCopy(Http::LowerCaseString(RocketmqConstants::get().BrokerId),
                                custom_header->targetBrokerId());
  }
}

} // namespace RocketmqProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
