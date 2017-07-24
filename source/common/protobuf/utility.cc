#include "common/protobuf/utility.h"

#include "spdlog/spdlog.h"

namespace Envoy {

MissingFieldException::MissingFieldException(const std::string& field_name,
                                             const Protobuf::Message& message)
    : EnvoyException(
          fmt::format("Field '{}' is missing in: {}", field_name, message.DebugString())) {}

Protobuf::Message* MessageUtil::getMutableMessage(Protobuf::Message& message,
                                                  const std::string& field_name) {
  const auto* reflection = message.GetReflection();
  const auto* field = message.GetDescriptor()->FindFieldByName(field_name);
  return reflection->MutableMessage(&message, field);
}

const Protobuf::Message* MessageUtil::getMessage(const Protobuf::Message& message,
                                                 const std::string& field_name) {
  const auto* reflection = message.GetReflection();
  const auto* field = message.GetDescriptor()->FindFieldByName(field_name);
  if (reflection->HasField(message, field)) {
    return &reflection->GetMessage(message, field);
  }
  return nullptr;
}

uint32_t MessageUtil::getUInt32(const Protobuf::Message& message, const std::string& field_name,
                                uint32_t default_value) {
  const auto* sub_message = getMessage(message, field_name);
  if (sub_message != nullptr) {
    return dynamic_cast<const ProtobufWkt::UInt32Value&>(*sub_message).value();
  }
  return default_value;
}

uint32_t MessageUtil::getUInt32(const Protobuf::Message& message, const std::string& field_name) {
  const auto* sub_message = getMessage(message, field_name);
  if (sub_message != nullptr) {
    return dynamic_cast<const ProtobufWkt::UInt32Value&>(*sub_message).value();
  }
  throw MissingFieldException(field_name, message);
}

uint64_t MessageUtil::getMilliseconds(const Protobuf::Message& message,
                                      const std::string& field_name, uint64_t default_ms) {
  const auto* sub_message = getMessage(message, field_name);
  if (sub_message != nullptr) {
    return Protobuf::util::TimeUtil::DurationToMilliseconds(
        dynamic_cast<const ProtobufWkt::Duration&>(*sub_message));
  }
  return default_ms;
}

uint64_t MessageUtil::getMilliseconds(const Protobuf::Message& message,
                                      const std::string& field_name) {
  const auto* sub_message = getMessage(message, field_name);
  if (sub_message != nullptr) {
    return Protobuf::util::TimeUtil::DurationToMilliseconds(
        dynamic_cast<const ProtobufWkt::Duration&>(*sub_message));
  }
  throw MissingFieldException(field_name, message);
}

} // namespace Envoy
