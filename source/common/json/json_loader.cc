#include "source/common/json/json_loader.h"

#include "source/common/json/json_internal.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Json {

ObjectSharedPtr Factory::loadFromString(const std::string& json) {
  return Nlohmann::Factory::loadFromString(json);
}

absl::StatusOr<ObjectSharedPtr> Factory::loadFromStringNoThrow(const std::string& json) {
  return Nlohmann::Factory::loadFromStringNoThrow(json);
}

ObjectSharedPtr Factory::loadFromProtobufStruct(const ProtobufWkt::Struct& protobuf_struct) {
  return Nlohmann::Factory::loadFromProtobufStruct(protobuf_struct);
}

} // namespace Json
} // namespace Envoy
