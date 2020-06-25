#include "extensions/filters/network/rocketmq_proxy/codec.h"

#include <string>

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/common/logger.h"

#include "extensions/filters/network/rocketmq_proxy/protocol.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RocketmqProxy {

RemotingCommandPtr Decoder::decode(Buffer::Instance& buffer, bool& underflow, bool& has_error,
                                   int request_code) {
  // Verify there is at least some bits, which stores frame length and header length
  if (buffer.length() <= MIN_FRAME_SIZE) {
    underflow = true;
    return nullptr;
  }

  auto frame_length = buffer.peekBEInt<uint32_t>();

  if (frame_length > MAX_FRAME_SIZE) {
    has_error = true;
    return nullptr;
  }

  if (buffer.length() < frame_length) {
    underflow = true;
    return nullptr;
  }
  buffer.drain(FRAME_LENGTH_FIELD_SIZE);

  auto mark = buffer.peekBEInt<uint32_t>();
  uint32_t header_length = adjustHeaderLength(mark);
  ASSERT(frame_length > header_length);
  buffer.drain(FRAME_HEADER_LENGTH_FIELD_SIZE);

  uint32_t body_length = frame_length - 4 - header_length;

  ENVOY_LOG(debug,
            "Request/Response Frame Meta: Frame Length = {}, Header Length = {}, Body Length = {}",
            frame_length, header_length, body_length);

  Buffer::OwnedImpl header_buffer;
  header_buffer.move(buffer, header_length);
  std::string header_json = header_buffer.toString();
  ENVOY_LOG(trace, "Request/Response Header JSON: {}", header_json);

  int32_t code, version, opaque;
  uint32_t flag;
  if (isJsonHeader(mark)) {
    ProtobufWkt::Struct header_struct;

    // Parse header JSON text
    try {
      MessageUtil::loadFromJson(header_json, header_struct);
    } catch (std::exception& e) {
      has_error = true;
      ENVOY_LOG(error, "Failed to parse header JSON: {}. Error message: {}", header_json, e.what());
      return nullptr;
    }

    const auto& filed_value_pair = header_struct.fields();
    if (!filed_value_pair.contains("code")) {
      ENVOY_LOG(error, "Malformed frame: 'code' field is missing. Header JSON: {}", header_json);
      has_error = true;
      return nullptr;
    }
    code = filed_value_pair.at("code").number_value();
    if (!filed_value_pair.contains("version")) {
      ENVOY_LOG(error, "Malformed frame: 'version' field is missing. Header JSON: {}", header_json);
      has_error = true;
      return nullptr;
    }
    version = filed_value_pair.at("version").number_value();
    if (!filed_value_pair.contains("opaque")) {
      ENVOY_LOG(error, "Malformed frame: 'opaque' field is missing. Header JSON: {}", header_json);
      has_error = true;
      return nullptr;
    }
    opaque = filed_value_pair.at("opaque").number_value();
    if (!filed_value_pair.contains("flag")) {
      ENVOY_LOG(error, "Malformed frame: 'flag' field is missing. Header JSON: {}", header_json);
      has_error = true;
      return nullptr;
    }
    flag = filed_value_pair.at("flag").number_value();
    RemotingCommandPtr cmd = std::make_unique<RemotingCommand>(code, version, opaque);
    cmd->flag(flag);
    if (filed_value_pair.contains("language")) {
      cmd->language(filed_value_pair.at("language").string_value());
    }

    if (filed_value_pair.contains("serializeTypeCurrentRPC")) {
      cmd->serializeTypeCurrentRPC(filed_value_pair.at("serializeTypeCurrentRPC").string_value());
    }

    cmd->body_.move(buffer, body_length);

    if (RemotingCommand::isResponse(flag)) {
      if (filed_value_pair.contains("remark")) {
        cmd->remark(filed_value_pair.at("remark").string_value());
      }
      cmd->custom_header_ = decodeResponseExtHeader(static_cast<ResponseCode>(code), header_struct,
                                                    static_cast<RequestCode>(request_code));
    } else {
      cmd->custom_header_ = decodeExtHeader(static_cast<RequestCode>(code), header_struct);
    }
    return cmd;
  } else {
    ENVOY_LOG(warn, "Unsupported header serialization type");
    has_error = true;
    return nullptr;
  }
}

bool Decoder::isComplete(Buffer::Instance& buffer, int32_t cursor) {
  if (buffer.length() - cursor < 4) {
    // buffer is definitely incomplete.
    return false;
  }

  auto total_size = buffer.peekBEInt<int32_t>(cursor);
  return buffer.length() - cursor >= static_cast<uint32_t>(total_size);
}

std::string Decoder::decodeTopic(Buffer::Instance& buffer, int32_t cursor) {
  if (!isComplete(buffer, cursor)) {
    return EMPTY_STRING;
  }

  auto magic_code = buffer.peekBEInt<int32_t>(cursor + 4);

  MessageVersion message_version = V1;
  if (enumToSignedInt(MessageVersion::V1) == magic_code) {
    message_version = V1;
  } else if (enumToSignedInt(MessageVersion::V2) == magic_code) {
    message_version = V2;
  }

  int32_t offset = 4   /* total size */
                   + 4 /* magic code */
                   + 4 /* body CRC */
                   + 4 /* queue Id */
                   + 4 /* flag */
                   + 8 /* queue offset */
                   + 8 /* physical offset */
                   + 4 /* sys flag */
                   + 8 /* born timestamp */
                   + 4 /* born host */
                   + 4 /* born host port */
                   + 8 /* store timestamp */
                   + 4 /* store host */
                   + 4 /* store host port */
                   + 4 /* re-consume times */
                   + 8 /* transaction offset */
      ;
  auto body_size = buffer.peekBEInt<int32_t>(cursor + offset);
  offset += 4 /* body size */
            + body_size /* body */;
  int32_t topic_length;
  std::string topic;
  switch (message_version) {
  case V1: {
    topic_length = buffer.peekBEInt<int8_t>(cursor + offset);
    topic.reserve(topic_length);
    topic.resize(topic_length);
    buffer.copyOut(cursor + offset + sizeof(int8_t), topic_length, &topic[0]);
    break;
  }
  case V2: {
    topic_length = buffer.peekBEInt<int16_t>(cursor + offset);
    topic.reserve(topic_length);
    topic.resize(topic_length);
    buffer.copyOut(cursor + offset + sizeof(int16_t), topic_length, &topic[0]);
    break;
  }
  }
  return topic;
}

int32_t Decoder::decodeQueueId(Buffer::Instance& buffer, int32_t cursor) {
  if (!isComplete(buffer, cursor)) {
    return -1;
  }

  int32_t offset = 4   /* total size */
                   + 4 /* magic code */
                   + 4 /* body CRC */;

  return buffer.peekBEInt<int32_t>(cursor + offset);
}

int64_t Decoder::decodeQueueOffset(Buffer::Instance& buffer, int32_t cursor) {
  if (!isComplete(buffer, cursor)) {
    return -1;
  }

  int32_t offset = 4   /* total size */
                   + 4 /* magic code */
                   + 4 /* body CRC */
                   + 4 /* queue Id */
                   + 4 /* flag */;
  return buffer.peekBEInt<int64_t>(cursor + offset);
}

std::string Decoder::decodeMsgId(Buffer::Instance& buffer, int32_t cursor) {
  if (!isComplete(buffer, cursor)) {
    return EMPTY_STRING;
  }

  int32_t offset = 4   /* total size */
                   + 4 /* magic code */
                   + 4 /* body CRC */
                   + 4 /* queue Id */
                   + 4 /* flag */
                   + 8 /* queue offset */;
  auto physical_offset = buffer.peekBEInt<int64_t>(cursor + offset);
  offset += 8   /* physical offset */
            + 4 /* sys flag */
            + 8 /* born timestamp */
            + 4 /* born host */
            + 4 /* born host port */
            + 8 /* store timestamp */
      ;

  Buffer::OwnedImpl msg_id_buffer;
  msg_id_buffer.writeBEInt<int64_t>(buffer.peekBEInt<int64_t>(cursor + offset));
  msg_id_buffer.writeBEInt<int64_t>(physical_offset);
  std::string msg_id;
  msg_id.reserve(32);
  for (uint64_t i = 0; i < msg_id_buffer.length(); i++) {
    auto c = msg_id_buffer.peekBEInt<uint8_t>();
    msg_id.append(1, static_cast<char>(c >> 4U));
    msg_id.append(1, static_cast<char>(c & 0xFU));
  }
  return msg_id;
}

CommandCustomHeaderPtr Decoder::decodeExtHeader(RequestCode code,
                                                ProtobufWkt::Struct& header_struct) {
  const auto& filed_value_pair = header_struct.fields();
  switch (code) {
  case RequestCode::SendMessage: {
    ASSERT(filed_value_pair.contains("extFields"));
    const auto& ext_fields = filed_value_pair.at("extFields");
    auto send_msg_ext_header = new SendMessageRequestHeader();
    send_msg_ext_header->version_ = SendMessageRequestVersion::V1;
    send_msg_ext_header->decode(ext_fields);
    return send_msg_ext_header;
  }
  case RequestCode::SendMessageV2: {
    ASSERT(filed_value_pair.contains("extFields"));
    const auto& ext_fields = filed_value_pair.at("extFields");
    auto send_msg_ext_header = new SendMessageRequestHeader();
    send_msg_ext_header->version_ = SendMessageRequestVersion::V2;
    send_msg_ext_header->decode(ext_fields);
    return send_msg_ext_header;
  }

  case RequestCode::GetRouteInfoByTopic: {
    ASSERT(filed_value_pair.contains("extFields"));
    const auto& ext_fields = filed_value_pair.at("extFields");
    auto get_route_info_request_header = new GetRouteInfoRequestHeader();
    get_route_info_request_header->decode(ext_fields);
    return get_route_info_request_header;
  }

  case RequestCode::UnregisterClient: {
    ASSERT(filed_value_pair.contains("extFields"));
    const auto& ext_fields = filed_value_pair.at("extFields");
    auto unregister_client_request_header = new UnregisterClientRequestHeader();
    unregister_client_request_header->decode(ext_fields);
    return unregister_client_request_header;
  }

  case RequestCode::GetConsumerListByGroup: {
    ASSERT(filed_value_pair.contains("extFields"));
    const auto& ext_fields = filed_value_pair.at("extFields");
    auto get_consumer_list_by_group_request_header = new GetConsumerListByGroupRequestHeader();
    get_consumer_list_by_group_request_header->decode(ext_fields);
    return get_consumer_list_by_group_request_header;
  }

  case RequestCode::PopMessage: {
    ASSERT(filed_value_pair.contains("extFields"));
    const auto& ext_fields = filed_value_pair.at("extFields");
    auto pop_message_request_header = new PopMessageRequestHeader();
    pop_message_request_header->decode(ext_fields);
    return pop_message_request_header;
  }

  case RequestCode::AckMessage: {
    ASSERT(filed_value_pair.contains("extFields"));
    const auto& ext_fields = filed_value_pair.at("extFields");
    auto ack_message_request_header = new AckMessageRequestHeader();
    ack_message_request_header->decode(ext_fields);
    return ack_message_request_header;
  }

  case RequestCode::HeartBeat: {
    // Heartbeat does not have an extended header.
    return nullptr;
  }

  default:
    ENVOY_LOG(warn, "Unsupported request code: {}", static_cast<int>(code));
    return nullptr;
  }
}

CommandCustomHeaderPtr Decoder::decodeResponseExtHeader(ResponseCode response_code,
                                                        ProtobufWkt::Struct& header_struct,
                                                        RequestCode request_code) {
  // No need to decode a failed response.
  if (response_code != ResponseCode::Success &&
      response_code != ResponseCode::ReplicaNotAvailable) {
    return nullptr;
  }
  const auto& filed_value_pair = header_struct.fields();
  switch (request_code) {
  case RequestCode::SendMessage:
  case RequestCode::SendMessageV2: {
    auto send_message_response_header = new SendMessageResponseHeader();
    ASSERT(filed_value_pair.contains("extFields"));
    auto& ext_fields = filed_value_pair.at("extFields");
    send_message_response_header->decode(ext_fields);
    return send_message_response_header;
  }

  case RequestCode::PopMessage: {
    auto pop_message_response_header = new PopMessageResponseHeader();
    ASSERT(filed_value_pair.contains("extFields"));
    const auto& ext_fields = filed_value_pair.at("extFields");
    pop_message_response_header->decode(ext_fields);
    return pop_message_response_header;
  }
  default:
    return nullptr;
  }
}

void Encoder::encode(const RemotingCommandPtr& command, Buffer::Instance& data) {

  ProtobufWkt::Struct command_struct;
  auto* fields = command_struct.mutable_fields();

  ProtobufWkt::Value code_v;
  code_v.set_number_value(command->code_);
  (*fields)["code"] = code_v;

  ProtobufWkt::Value language_v;
  language_v.set_string_value(command->language());
  (*fields)["language"] = language_v;

  ProtobufWkt::Value version_v;
  version_v.set_number_value(command->version_);
  (*fields)["version"] = version_v;

  ProtobufWkt::Value opaque_v;
  opaque_v.set_number_value(command->opaque_);
  (*fields)["opaque"] = opaque_v;

  ProtobufWkt::Value flag_v;
  flag_v.set_number_value(command->flag_);
  (*fields)["flag"] = flag_v;

  if (!command->remark_.empty()) {
    ProtobufWkt::Value remark_v;
    remark_v.set_string_value(command->remark_);
    (*fields)["remark"] = remark_v;
  }

  ProtobufWkt::Value serialization_type_v;
  serialization_type_v.set_string_value(command->serializeTypeCurrentRPC());
  (*fields)["serializeTypeCurrentRPC"] = serialization_type_v;

  if (command->custom_header_) {
    ProtobufWkt::Value ext_fields_v;
    command->custom_header_->encode(ext_fields_v);
    (*fields)["extFields"] = ext_fields_v;
  }

  std::string json = MessageUtil::getJsonStringFromMessage(command_struct);

  int32_t frame_length = 4;
  int32_t header_length = json.size();
  frame_length += header_length;
  frame_length += command->bodyLength();

  data.writeBEInt<int32_t>(frame_length);
  data.writeBEInt<int32_t>(header_length);
  data.add(json);

  // add body
  if (command->bodyLength() > 0) {
    data.add(command->body());
  }
}

} // namespace RocketmqProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy