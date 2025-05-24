#include "source/common/json/json_loader.h"

#include "source/common/json/json_internal.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Json {

absl::StatusOr<ObjectSharedPtr> Factory::loadFromString(const std::string& json) {
  return Nlohmann::Factory::loadFromString(json);
}

ObjectSharedPtr Factory::loadFromProtobufStruct(const ProtobufWkt::Struct& protobuf_struct) {
  return Nlohmann::Factory::loadFromProtobufStruct(protobuf_struct);
}

std::vector<uint8_t> Factory::jsonToMsgpack(const std::string& json) {
  return Nlohmann::Factory::jsonToMsgpack(json);
}

const std::string Factory::listAsJsonString(const std::list<std::string>& items) {
  return Nlohmann::Factory::serialize(items);
}

} // namespace Json
} // namespace Envoy
