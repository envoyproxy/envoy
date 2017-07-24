#include "common/config/json_utility.h"

#include "common/protobuf/utility.h"

namespace Envoy {
namespace Config {

void JsonUtil::setUInt32Value(const Json::Object& json, Protobuf::Message& message,
                              const std::string& field_name) {
  if (json.hasObject(field_name)) {
    dynamic_cast<ProtobufWkt::UInt32Value*>(MessageUtil::getMutableMessage(message, field_name))
        ->set_value(json.getInteger(field_name));
  }
}

void JsonUtil::setDuration(const Json::Object& json, Protobuf::Message& message,
                           const std::string& field_name) {
  if (json.hasObject(field_name)) {
    dynamic_cast<Protobuf::Duration*>(MessageUtil::getMutableMessage(message, field_name))
        ->CopyFrom(
            Protobuf::util::TimeUtil::MillisecondsToDuration(json.getInteger(field_name + "_ms")));
  }
}

} // namespace Config
} // namespace Envoy
