#include "common/protobuf/utility.h"

#include "common/filesystem/filesystem_impl.h"
#include "common/json/json_loader.h"

#include "spdlog/spdlog.h"

namespace Envoy {

MissingFieldException::MissingFieldException(const std::string& field_name,
                                             const Protobuf::Message& message)
    : EnvoyException(
          fmt::format("Field '{}' is missing in: {}", field_name, message.DebugString())) {}

void MessageUtil::loadFromFile(const std::string& path, Protobuf::Message& message) {
  ProtobufUtil::Status status;
  const std::string contents = Filesystem::fileReadToEnd(path);
  if (StringUtil::endsWith(path, ".yaml")) {
    const std::string json = Json::Factory::loadFromYamlString(contents)->asJsonString();
    status = Protobuf::util::JsonStringToMessage(json, &message);
  } else {
    status = Protobuf::util::JsonStringToMessage(contents, &message);
  }
  if (!status.ok()) {
    throw EnvoyException("Unable to parse JSON as proto: " + contents);
  }
}

} // namespace Envoy
